#pragma once
#include "flight_log.h"
#include <math.h>
#include <string.h>
#include <limits.h>

// Internal RAM format. External binary export remains 35 little-endian float32s.
struct FlightLogRecord {
    uint32_t timeMs;
    uint16_t dtUs;
    int16_t vectors[18];
    uint16_t thrust, battery;
    int16_t rc[5];
    uint16_t rcAge, faults, motors[4], mixScale;
    int16_t integral[3];
    uint8_t mode, armed, source, quality;
    uint32_t routeRevision;
    uint16_t routeStep;
    uint8_t routeSchema, routeState, routeQuality, routeTerminationReason;
    int16_t routeTargetAltitude, routeActualAltitude, routeTargetYaw, routeActualYaw;
    int16_t routeFlowX, routeFlowY;
	int16_t magneticYaw, navigationYaw, magneticInnovation;
	uint16_t magnetometerFlags;
};
static_assert(sizeof(FlightLogRecord) == 112, "Flight log record layout changed unexpectedly");
static_assert(sizeof(float) == 4, "Legacy log transport requires float32");

namespace FlightLogCodec {
inline int16_t signedValue(float v, float scale, uint8_t &quality) {
    if (isnan(v)) { quality |= 1; return INT16_MIN; }
    const float scaled = v * scale;
    if (scaled > 32766) { quality |= 2; return INT16_MAX; }
    if (scaled < -32766) { quality |= 2; return -32767; }
    return (int16_t)(scaled + (scaled >= 0 ? 0.5f : -0.5f));
}
inline uint16_t unsignedValue(float v, float scale, uint8_t &quality) {
    if (isnan(v)) { quality |= 1; return UINT16_MAX; }
    const float scaled = v * scale;
    if (scaled > 65532) { quality |= 2; return 65534; }
    if (scaled < 0) { quality |= 2; return 65533; }
    return (uint16_t)(scaled + 0.5f);
}
inline float signedFloat(int16_t v, float scale) {
    return v == INT16_MIN ? NAN : v == INT16_MAX ? INFINITY : v == -32767 ? -INFINITY : v / scale;
}
inline float unsignedFloat(uint16_t v, float scale) {
    return v == 65535 ? NAN : v == 65534 ? INFINITY : v == 65533 ? -INFINITY : v / scale;
}
inline float vectorScale(int i) { return i >= 12 ? 8192.0f : i >= 3 && i < 6 ? 128.0f : 512.0f; }
inline FlightLogRecord encode(const float *v, uint64_t nowUs) {
    FlightLogRecord r = {};
    r.timeMs = (uint32_t)(nowUs / 1000);
    r.dtUs = unsignedValue(v[1], 1000000, r.quality);
    for (int i = 0; i < 18; ++i) r.vectors[i] = signedValue(v[i + 2], vectorScale(i), r.quality);
    r.thrust = unsignedValue(v[20], 32768, r.quality);
    r.battery = unsignedValue(v[21], 1000, r.quality);
    for (int i = 0; i < 5; ++i) r.rc[i] = signedValue(v[i + 22], 16384, r.quality);
    r.mode = isfinite(v[27]) && v[27] >= 0 && v[27] < 255 ? (uint8_t)v[27] : 255;
    r.rcAge = v[28] == -1 ? 65532 : unsignedValue(v[28], 1000, r.quality);
    if (v[28] != -1 && r.rcAge == 65532) { r.rcAge = 65534; r.quality |= 2; }
    r.armed = v[29] != 0;
    r.faults = isfinite(v[30]) && v[30] >= 0 && v[30] <= 65535 ? (uint16_t)v[30] : 65535;
    for (int i = 0; i < 4; ++i) r.motors[i] = unsignedValue(v[i + 31], 32768, r.quality);
    for (int i = 0; i < 3; ++i) r.integral[i] = signedValue(v[i + 35], 1024, r.quality);
    r.mixScale = unsignedValue(v[38], 32768, r.quality);
    r.source = isfinite(v[39]) && v[39] >= 0 && v[39] < 255 ? (uint8_t)v[39] : 255;
    float confidence = isfinite(v[40]) ? v[40] : 0.0f;
    if (confidence < 0.0f) confidence = 0.0f;
    if (confidence > 1.0f) confidence = 1.0f;
    r.quality = (uint8_t)((r.quality & 0x03) | ((uint8_t)(confidence * 63.0f + 0.5f) << 2));
    r.routeRevision = isfinite(v[41]) && v[41] >= 0.0f ? (uint32_t)v[41] : 0;
    r.routeSchema = isfinite(v[42]) && v[42] >= 0.0f && v[42] < 255.0f ? (uint8_t)v[42] : 0;
    r.routeStep = isfinite(v[43]) && v[43] >= 0.0f && v[43] <= 65535.0f ? (uint16_t)v[43] : 0;
    r.routeState = isfinite(v[44]) && v[44] >= 0.0f && v[44] < 255.0f ? (uint8_t)v[44] : 0;
    r.routeTargetAltitude = signedValue(v[45], 100.0f, r.quality);
    r.routeActualAltitude = signedValue(v[46], 100.0f, r.quality);
    r.routeTargetYaw = signedValue(v[47], 1000.0f, r.quality);
    r.routeActualYaw = signedValue(v[48], 1000.0f, r.quality);
    r.routeFlowX = signedValue(v[49], 100.0f, r.quality);
    r.routeFlowY = signedValue(v[50], 100.0f, r.quality);
    r.routeQuality = isfinite(v[51]) && v[51] >= 0.0f && v[51] < 255.0f ? (uint8_t)v[51] : 0;
    r.routeTerminationReason = isfinite(v[52]) && v[52] >= 0.0f && v[52] < 255.0f ? (uint8_t)v[52] : 0;
	r.magneticYaw = signedValue(v[53], 1000.0f, r.quality);
	r.navigationYaw = signedValue(v[54], 1000.0f, r.quality);
	r.magneticInnovation = signedValue(v[55], 1000.0f, r.quality);
	r.magnetometerFlags = isfinite(v[56]) && v[56] >= 0.0f && v[56] <= 65535.0f ? (uint16_t)v[56] : 65535;
    return r;
}
inline void decode(const FlightLogRecord &r, uint64_t anchorMs, float *v, int columns) {
    v[0] = (float)((double)(anchorMs - (uint32_t)((uint32_t)anchorMs - r.timeMs)) / 1000.0);
    v[1] = unsignedFloat(r.dtUs, 1000000);
    for (int i = 0; i < 18; ++i) v[i + 2] = signedFloat(r.vectors[i], vectorScale(i));
    v[20] = unsignedFloat(r.thrust, 32768); v[21] = unsignedFloat(r.battery, 1000);
    for (int i = 0; i < 5; ++i) v[i + 22] = signedFloat(r.rc[i], 16384);
    v[27] = r.mode; v[28] = r.rcAge == 65532 ? -1 : unsignedFloat(r.rcAge, 1000);
    v[29] = r.armed; v[30] = r.faults;
    for (int i = 0; i < 4; ++i) v[i + 31] = unsignedFloat(r.motors[i], 32768);
    if (columns >= 40) {
        for (int i = 0; i < 3; ++i) v[i + 35] = signedFloat(r.integral[i], 1024);
        v[38] = unsignedFloat(r.mixScale, 32768); v[39] = r.source;
    }
    if (columns >= FLIGHT_LOG_COLUMNS) {
        v[40] = ((r.quality >> 2) & 0x3f) / 63.0f;
        v[41] = (float)r.routeRevision; v[42] = r.routeSchema; v[43] = r.routeStep;
        v[44] = r.routeState;
        v[45] = signedFloat(r.routeTargetAltitude, 100.0f);
        v[46] = signedFloat(r.routeActualAltitude, 100.0f);
        v[47] = signedFloat(r.routeTargetYaw, 1000.0f);
        v[48] = signedFloat(r.routeActualYaw, 1000.0f);
        v[49] = signedFloat(r.routeFlowX, 100.0f);
        v[50] = signedFloat(r.routeFlowY, 100.0f);
        v[51] = r.routeQuality; v[52] = r.routeTerminationReason;
		v[53] = signedFloat(r.magneticYaw, 1000.0f);
		v[54] = signedFloat(r.navigationYaw, 1000.0f);
		v[55] = signedFloat(r.magneticInnovation, 1000.0f);
		v[56] = r.magnetometerFlags;
    }
}
inline void legacyBytes(const float *row, uint8_t *out) {
    for (int i = 0; i < FLIGHT_LOG_LEGACY_COLUMNS; ++i) {
        uint32_t bits; memcpy(&bits, &row[i], 4);
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = (uint8_t)(bits >> (j * 8));
    }
}
}

// Caller supplies synchronization; methods perform bounded work and no I/O.
class FlightLogStore {
public:
    static constexpr uint32_t CAPACITY = 334;
static_assert(CAPACITY * sizeof(FlightLogRecord) == 37408, "Flight ring RAM budget");
    FlightLogStatus status() const { return {state, generation, reason, count, missed, triggerUs}; }
    bool sampleDue(uint64_t now) {
        if (hasSchedule && now < nextSampleUs) return false;
        if (hasSchedule) {
            const uint64_t skipped = (now - nextSampleUs) / 10000;
            missed = skipped > UINT32_MAX - missed ? UINT32_MAX : missed + (uint32_t)skipped;
            nextSampleUs += (skipped + 1) * 10000;
        } else { nextSampleUs = now + 10000; hasSchedule = true; }
        return true;
    }
    void tick(uint64_t now) { if (state == POST_TRIGGER && now - triggerUs > 1000000) state = FROZEN; }
    void trigger(uint32_t mask, uint64_t now) {
        if (state == FROZEN) return;
        reason |= mask;
        if (state == ROLLING) { state = POST_TRIGGER; triggerUs = now; postCount = 0; }
    }
    void push(const FlightLogRecord &r, uint64_t now) {
        latest = r; latestAnchorMs = now / 1000; ++sequence; haveLatest = true;
        tick(now);
        if (state == FROZEN) return;
        // A gap spanning a full 32-bit millisecond epoch cannot be decoded
        // against one anchor. Drop stale prehistory rather than invent timestamps.
        if (count && now / 1000 - anchorMs > UINT32_MAX) count = next = 0;
        records[next] = r; anchorMs = now / 1000;
        next = (next + 1) % CAPACITY; if (count < CAPACITY) ++count;
        if (state == POST_TRIGGER && (++postCount >= 100 || now - triggerUs >= 1000000)) state = FROZEN;
    }
    bool freeze() { if (state == POST_TRIGGER) return false; state = FROZEN; return true; }
    void resume() {
        state = ROLLING; reason = count = next = postCount = missed = 0; triggerUs = 0;
        if (++generation == 0) ++generation;
    }
    bool copy(uint32_t wantedGeneration, uint32_t row, FlightLogRecord &r, uint64_t &anchor) const {
        if (state != FROZEN || wantedGeneration != generation || row >= count) return false;
        r = records[(next + CAPACITY - count + row) % CAPACITY]; anchor = anchorMs; return true;
    }
    bool copyLatest(FlightLogRecord &r, uint64_t &anchor, uint32_t &seq) const {
        if (!haveLatest) return false;
        r = latest; anchor = latestAnchorMs; seq = sequence; return true;
    }
private:
    FlightLogRecord records[CAPACITY] = {}, latest = {};
    FlightLogState state = ROLLING;
    uint32_t generation = 1, reason = 0, count = 0, next = 0, postCount = 0, missed = 0, sequence = 0;
    uint64_t triggerUs = 0, anchorMs = 0, latestAnchorMs = 0, nextSampleUs = 0;
    bool haveLatest = false, hasSchedule = false;
};
