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
};
static_assert(sizeof(FlightLogRecord) == 80, "Flight log record must remain 80 bytes");
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
    static constexpr uint32_t CAPACITY = 400;
    static_assert(CAPACITY * sizeof(FlightLogRecord) == 32000, "Flight ring RAM budget");
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
