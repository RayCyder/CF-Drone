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
    int16_t altitudeCentimeters;
    int16_t headingDecidegrees;
};

static_assert(sizeof(OpenLoopPackedStep) == 16, "Open-loop route step must stay 16 bytes");

#define OPEN_LOOP_ALTITUDE_SENTINEL_CM INT16_MIN
#define OPEN_LOOP_HEADING_SENTINEL_DDEG INT16_MIN
#define OPEN_LOOP_MIN_ALTITUDE_M -20.0f
#define OPEN_LOOP_MAX_ALTITUDE_M 20.0f
#define OPEN_LOOP_MIN_HEADING_DEG -360.0f
#define OPEN_LOOP_MAX_HEADING_DEG 360.0f

struct OpenLoopParseResult {
    bool ok;
    uint16_t count;
    uint32_t totalMs;
    const char *reason;
    uint8_t schemaVersion = 0;
    uint8_t sourceKind = 0;
    uint8_t controlPolicy = 0;
};

enum OpenLoopSourceKind : uint8_t {
    OPEN_LOOP_SOURCE_AUTHORED = 1,
    OPEN_LOOP_SOURCE_RECORDED = 2,
};

enum OpenLoopControlPolicy : uint8_t {
    OPEN_LOOP_POLICY_SLEW = 1,
    OPEN_LOOP_POLICY_DIRECT = 2,
};

struct OpenLoopControls {
    float roll;
    float pitch;
    float yaw;
    float throttle;
    bool hasAltitude;
    float altitudeMeters;
    bool hasHeading;
    float headingDegrees;
};

static inline void openLoopResetStartOffsets(float &altitudeMeters, float &headingRadians) {
    altitudeMeters = 0.0f;
    headingRadians = 0.0f;
}

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

static inline bool openLoopStepHasAltitude(const OpenLoopPackedStep &step) {
    return step.altitudeCentimeters != OPEN_LOOP_ALTITUDE_SENTINEL_CM;
}

static inline bool openLoopStepHasHeading(const OpenLoopPackedStep &step) {
    return step.headingDecidegrees != OPEN_LOOP_HEADING_SENTINEL_DDEG;
}

static inline float openLoopStepAltitudeMeters(const OpenLoopPackedStep &step) {
    return openLoopStepHasAltitude(step) ? step.altitudeCentimeters / 100.0f : NAN;
}

static inline float openLoopStepHeadingDegrees(const OpenLoopPackedStep &step) {
    return openLoopStepHasHeading(step) ? step.headingDecidegrees / 10.0f : NAN;
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
    out.hasAltitude = openLoopStepHasAltitude(step);
    out.altitudeMeters = openLoopStepAltitudeMeters(step);
    out.hasHeading = openLoopStepHasHeading(step);
    out.headingDegrees = openLoopStepHeadingDegrees(step);
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
    current.hasAltitude = target.hasAltitude;
    current.altitudeMeters = target.altitudeMeters;
    current.hasHeading = target.hasHeading;
    current.headingDegrees = target.headingDegrees;
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

static inline bool openLoopParseLine(char *line, OpenLoopPackedStep &step, uint32_t &durationMs,
                                     uint8_t &columnCount) {
    char *cursor = line;
    openLoopSkipSeparators(cursor);
    if (!*cursor || *cursor == '#') return false;

    float values[7];
    int valueCount = 0;
    for (; valueCount < 7; ++valueCount) {
        if (!openLoopParseFloat(cursor, values[valueCount])) return false;
        char *lookahead = cursor;
        openLoopSkipSeparators(lookahead);
        if (!*lookahead) {
            ++valueCount;
            break;
        }
        cursor = lookahead;
    }
    openLoopSkipSeparators(cursor);
    if (*cursor) return false;
    if (valueCount != 5 && valueCount != 7) return false;
    columnCount = (uint8_t)valueCount;

    if (values[0] < 0.1f || values[0] > 600.0f ||
        values[1] < 0.0f || values[1] > 100.0f ||
        fabsf(values[2]) > 100.0f || fabsf(values[3]) > 100.0f || fabsf(values[4]) > 100.0f) {
        return false;
    }
    if (valueCount == 7 &&
        (values[5] < OPEN_LOOP_MIN_ALTITUDE_M || values[5] > OPEN_LOOP_MAX_ALTITUDE_M ||
         values[6] < OPEN_LOOP_MIN_HEADING_DEG || values[6] > OPEN_LOOP_MAX_HEADING_DEG)) {
        return false;
    }

    durationMs = (uint32_t)openLoopRoundToInt(values[0] * 1000.0f);
    if (durationMs < OPEN_LOOP_MIN_STEP_MS || durationMs > OPEN_LOOP_MAX_STEP_MS) return false;
    step.durationMs = durationMs;
    step.throttleCentipercent = (uint16_t)openLoopRoundToInt(values[1] * 100.0f);
    step.rollCentipercent = (int16_t)openLoopRoundToInt(values[2] * 100.0f);
    step.pitchCentipercent = (int16_t)openLoopRoundToInt(values[3] * 100.0f);
    step.yawCentipercent = (int16_t)openLoopRoundToInt(values[4] * 100.0f);
    step.altitudeCentimeters = valueCount == 7
        ? (int16_t)openLoopRoundToInt(values[5] * 100.0f)
        : OPEN_LOOP_ALTITUDE_SENTINEL_CM;
    step.headingDecidegrees = valueCount == 7
        ? (int16_t)openLoopRoundToInt(values[6] * 10.0f)
        : OPEN_LOOP_HEADING_SENTINEL_DDEG;
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
    uint8_t expectedColumns = 0;
    bool metadataSeen = false;
    bool contentSeen = false;
    bool nonEmptyLineSeen = false;
    size_t lineNumber = 0;
    for (size_t i = 0; i <= length; ++i) {
        const char c = (i == length) ? '\n' : text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            line[lineLength] = '\0';
            char *trim = line;
            while (isspace((unsigned char)*trim)) trim++;
            if (*trim == '#') {
                const bool legacyV1 = strcmp(trim, "# WEB_RC_RECORDED_V1") == 0;
                const bool legacyV2 = strcmp(trim, "# WEB_RC_RECORDED_V2") == 0;
                const bool authoredV2 = strcmp(trim,
                    "# CF_ROUTE_META schema=2 source=authored policy=slew") == 0;
                const bool routeMetadata = strncmp(trim, "# CF_ROUTE_META", 15) == 0 ||
                    strncmp(trim, "# WEB_RC_RECORDED_", 18) == 0;
                if (legacyV1 || legacyV2 || authoredV2) {
                    if (lineNumber != 0 || metadataSeen || contentSeen || nonEmptyLineSeen) {
                        result.reason = "schema_mismatch";
                        return result;
                    }
                    metadataSeen = true;
                    if (legacyV1) {
                        result.schemaVersion = 1;
                        result.sourceKind = OPEN_LOOP_SOURCE_RECORDED;
                        result.controlPolicy = OPEN_LOOP_POLICY_DIRECT;
                        expectedColumns = 5;
                    } else if (legacyV2) {
                        result.schemaVersion = 2;
                        result.sourceKind = OPEN_LOOP_SOURCE_RECORDED;
                        result.controlPolicy = OPEN_LOOP_POLICY_DIRECT;
                        expectedColumns = 7;
                    } else {
                        result.schemaVersion = 2;
                        result.sourceKind = OPEN_LOOP_SOURCE_AUTHORED;
                        result.controlPolicy = OPEN_LOOP_POLICY_SLEW;
                        expectedColumns = 7;
                    }
                } else if (routeMetadata) {
                    result.reason = "schema_mismatch";
                    return result;
                }
            } else if (*trim) {
                contentSeen = true;
                if (result.count >= capacity || result.count >= OPEN_LOOP_MAX_STEPS) {
                    result.reason = "too_many_steps";
                    return result;
                }
                uint32_t durationMs = 0;
                uint8_t columnCount = 0;
                if (!openLoopParseLine(trim, out[result.count], durationMs, columnCount)) {
                    result.reason = "invalid_row";
                    return result;
                }
                if (!metadataSeen) {
                    if (columnCount != 5) {
                        result.reason = "schema_mismatch";
                        return result;
                    }
                    result.schemaVersion = 1;
                    result.sourceKind = OPEN_LOOP_SOURCE_AUTHORED;
                    result.controlPolicy = OPEN_LOOP_POLICY_SLEW;
                    expectedColumns = 5;
                }
                if (columnCount != expectedColumns) {
                    result.reason = "schema_mismatch";
                    return result;
                }
                if (result.totalMs > OPEN_LOOP_MAX_TOTAL_MS - durationMs) {
                    result.reason = "total_duration";
                    return result;
                }
                result.totalMs += durationMs;
                result.count++;
            }
            if (*trim) nonEmptyLineSeen = true;
            ++lineNumber;
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
