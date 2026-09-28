#pragma once

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define OPEN_LOOP_MAX_STEPS 128
#define OPEN_LOOP_MAX_BODY 4096
#define OPEN_LOOP_MAX_TOTAL_MS 1800000UL
#define OPEN_LOOP_MAX_STEP_MS 600000UL
#define OPEN_LOOP_MIN_STEP_MS 100UL
#define OPEN_LOOP_ROLL_PITCH_YAW_SLEW_PER_SEC 1.0f
#define OPEN_LOOP_THROTTLE_SLEW_PER_SEC 0.2f
#define OPEN_LOOP_SCHEDULER_GAP_MS 100UL

struct OpenLoopPackedStep {
    uint32_t durationMs;
    uint16_t throttleCentipercent;
    int16_t rollCentipercent;
    int16_t pitchCentipercent;
    int16_t yawCentipercent;
};

static_assert(sizeof(OpenLoopPackedStep) == 12, "Open-loop route step must stay 12 bytes");

struct OpenLoopParseResult {
    bool ok;
    uint16_t count;
    uint32_t totalMs;
    const char *reason;
};

struct OpenLoopControls {
    float roll;
    float pitch;
    float yaw;
    float throttle;
};

enum OpenLoopRunState : uint8_t {
    OPEN_LOOP_STATE_EMPTY = 0,
    OPEN_LOOP_STATE_READY = 1,
    OPEN_LOOP_STATE_START_PENDING = 2,
    OPEN_LOOP_STATE_RUNNING = 3,
    OPEN_LOOP_STATE_LANDING = 4,
    OPEN_LOOP_STATE_COMPLETE = 5,
    OPEN_LOOP_STATE_ABORTED = 6
};

static inline const char *openLoopStateName(uint8_t state) {
    switch (state) {
        case OPEN_LOOP_STATE_READY: return "ready";
        case OPEN_LOOP_STATE_START_PENDING: return "start_pending";
        case OPEN_LOOP_STATE_RUNNING: return "running";
        case OPEN_LOOP_STATE_LANDING: return "landing";
        case OPEN_LOOP_STATE_COMPLETE: return "complete";
        case OPEN_LOOP_STATE_ABORTED: return "aborted";
        default: return "empty";
    }
}

static inline bool openLoopElapsed(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static inline float openLoopClampFloat(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static inline float openLoopApplyDeadzone(float norm, float deadzone) {
    if (fabsf(norm) < deadzone) return 0.0f;
    const float sign = norm > 0.0f ? 1.0f : -1.0f;
    return sign * (fabsf(norm) - deadzone) / (1.0f - deadzone);
}

static inline void openLoopMapStepToControls(const OpenLoopPackedStep &step,
                                             float stickDeadzone,
                                             float throttleDeadzone,
                                             float stickScale,
                                             float yawScale,
                                             float throttleScale,
                                             OpenLoopControls &out) {
    const float rollRaw = step.rollCentipercent / 10000.0f;
    const float pitchRaw = step.pitchCentipercent / 10000.0f;
    const float yawRaw = step.yawCentipercent / 10000.0f;
    float throttlePct = step.throttleCentipercent / 100.0f;
    if (throttlePct < throttleDeadzone * 100.0f) throttlePct = 0.0f;
    throttlePct = openLoopClampFloat(throttlePct * throttleScale, 0.0f, 100.0f);

    out.roll = openLoopClampFloat(openLoopApplyDeadzone(rollRaw, stickDeadzone) * stickScale, -1.0f, 1.0f);
    out.pitch = openLoopClampFloat(openLoopApplyDeadzone(pitchRaw, stickDeadzone) * stickScale, -1.0f, 1.0f);
    out.yaw = openLoopClampFloat(openLoopApplyDeadzone(yawRaw, stickDeadzone) * yawScale, -1.0f, 1.0f);
    out.throttle = throttlePct / 100.0f;
}

static inline float openLoopSlewOne(float current, float target, float maxDelta) {
    if (target > current + maxDelta) return current + maxDelta;
    if (target < current - maxDelta) return current - maxDelta;
    return target;
}

static inline void openLoopSlewControls(OpenLoopControls &current,
                                        const OpenLoopControls &target,
                                        uint32_t dtMs) {
    const float seconds = dtMs / 1000.0f;
    const float axisDelta = OPEN_LOOP_ROLL_PITCH_YAW_SLEW_PER_SEC * seconds;
    const float throttleDelta = OPEN_LOOP_THROTTLE_SLEW_PER_SEC * seconds;
    current.roll = openLoopSlewOne(current.roll, target.roll, axisDelta);
    current.pitch = openLoopSlewOne(current.pitch, target.pitch, axisDelta);
    current.yaw = openLoopSlewOne(current.yaw, target.yaw, axisDelta);
    current.throttle = openLoopSlewOne(current.throttle, target.throttle, throttleDelta);
}

static inline void openLoopSkipSeparators(char *&cursor) {
    while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',')) cursor++;
}

static inline bool openLoopParseFloat(char *&cursor, float &value) {
    openLoopSkipSeparators(cursor);
    if (!*cursor) return false;
    char *end = nullptr;
    value = strtof(cursor, &end);
    if (end == cursor || isnan(value) || isinf(value)) return false;
    cursor = end;
    if (*cursor && !isspace((unsigned char)*cursor) && *cursor != ',') return false;
    return true;
}

static inline int32_t openLoopRoundToInt(float value) {
    return (int32_t)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

static inline bool openLoopParseLine(char *line, OpenLoopPackedStep &step, uint32_t &durationMs) {
    char *cursor = line;
    openLoopSkipSeparators(cursor);
    if (!*cursor || *cursor == '#') return false;

    float values[5];
    for (int i = 0; i < 5; ++i) {
        if (!openLoopParseFloat(cursor, values[i])) return false;
    }
    openLoopSkipSeparators(cursor);
    if (*cursor) return false;

    if (values[0] < 0.1f || values[0] > 600.0f ||
        values[1] < 0.0f || values[1] > 100.0f ||
        fabsf(values[2]) > 100.0f || fabsf(values[3]) > 100.0f || fabsf(values[4]) > 100.0f) {
        return false;
    }

    durationMs = (uint32_t)openLoopRoundToInt(values[0] * 1000.0f);
    if (durationMs < OPEN_LOOP_MIN_STEP_MS || durationMs > OPEN_LOOP_MAX_STEP_MS) return false;
    step.durationMs = durationMs;
    step.throttleCentipercent = (uint16_t)openLoopRoundToInt(values[1] * 100.0f);
    step.rollCentipercent = (int16_t)openLoopRoundToInt(values[2] * 100.0f);
    step.pitchCentipercent = (int16_t)openLoopRoundToInt(values[3] * 100.0f);
    step.yawCentipercent = (int16_t)openLoopRoundToInt(values[4] * 100.0f);
    return true;
}

static inline OpenLoopParseResult parseOpenLoopSequenceText(const char *text,
                                                            size_t length,
                                                            OpenLoopPackedStep *out,
                                                            uint16_t capacity) {
    OpenLoopParseResult result = {false, 0, 0, "invalid"};
    if (!text || !out || capacity == 0 || length == 0) {
        result.reason = "empty";
        return result;
    }
    if (length > OPEN_LOOP_MAX_BODY) {
        result.reason = "body_too_large";
        return result;
    }

    char line[128];
    size_t lineLength = 0;
    for (size_t i = 0; i <= length; ++i) {
        const char c = (i == length) ? '\n' : text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            line[lineLength] = '\0';
            char *trim = line;
            while (isspace((unsigned char)*trim)) trim++;
            if (*trim && *trim != '#') {
                if (result.count >= capacity || result.count >= OPEN_LOOP_MAX_STEPS) {
                    result.reason = "too_many_steps";
                    return result;
                }
                uint32_t durationMs = 0;
                if (!openLoopParseLine(trim, out[result.count], durationMs)) {
                    result.reason = "invalid_row";
                    return result;
                }
                if (result.totalMs > OPEN_LOOP_MAX_TOTAL_MS - durationMs) {
                    result.reason = "total_duration";
                    return result;
                }
                result.totalMs += durationMs;
                result.count++;
            }
            lineLength = 0;
        } else {
            if (lineLength >= sizeof(line) - 1) {
                result.reason = "line_too_long";
                return result;
            }
            line[lineLength++] = c;
        }
    }
    if (result.count == 0) {
        result.reason = "empty";
        return result;
    }
    result.ok = true;
    result.reason = "ok";
    return result;
}
