#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace level_rotation {

constexpr char KEY[] = "IMU_ROT_TXN";
constexpr uint32_t MAGIC = 0x49524F54UL;

struct Record {
    uint32_t magic;
    float oldRoll;
    float oldPitch;
};
static_assert(sizeof(Record) == 12, "IMU rotation transaction record layout changed");

inline bool validAngle(float value) {
    return isfinite(value) && value >= -3.141592654f && value <= 3.141592654f;
}

template <typename Store>
bool writeValue(Store &store, const char *key, float value) {
    return store.putFloat(key, value) == sizeof(float) && store.getFloat(key, NAN) == value;
}

template <typename Store>
bool clear(Store &store) {
    return store.remove(KEY) && !store.isKey(KEY);
}

template <typename Store>
bool recover(Store &store) {
    if (!store.isKey(KEY)) return true;
    Record record = {};
    if (store.getBytesLength(KEY) != sizeof(record) ||
        store.getBytes(KEY, &record, sizeof(record)) != sizeof(record) ||
        record.magic != MAGIC || !validAngle(record.oldRoll) || !validAngle(record.oldPitch)) return false;
    const bool rollRestored = writeValue(store, "IMU_ROT_ROLL", record.oldRoll);
    const bool pitchRestored = writeValue(store, "IMU_ROT_PITCH", record.oldPitch);
    return rollRestored && pitchRestored && clear(store);
}

struct CommitResult {
    bool success;
    bool recoveryRequired;
};

// A committed marker makes every interrupted two-key write recoverable to the
// old pair. Erasing it is the last operation, after both new keys read back.
template <typename Store>
CommitResult commit(Store &store, float oldRoll, float oldPitch, float newRoll, float newPitch) {
    if (!validAngle(oldRoll) || !validAngle(oldPitch) ||
        !validAngle(newRoll) || !validAngle(newPitch) || store.isKey(KEY))
        return {false, store.isKey(KEY)};
    const Record record = {MAGIC, oldRoll, oldPitch};
    Record readBack = {};
    const bool marked = store.putBytes(KEY, &record, sizeof(record)) == sizeof(record) &&
        store.getBytes(KEY, &readBack, sizeof(readBack)) == sizeof(readBack) &&
        memcmp(&record, &readBack, sizeof(record)) == 0;
    if (!marked) return {false, store.isKey(KEY)};

    const bool rollSaved = writeValue(store, "IMU_ROT_ROLL", newRoll);
    const bool pitchSaved = rollSaved && writeValue(store, "IMU_ROT_PITCH", newPitch);
    if (rollSaved && pitchSaved && clear(store)) return {true, false};

    const bool rollRestored = writeValue(store, "IMU_ROT_ROLL", oldRoll);
    const bool pitchRestored = writeValue(store, "IMU_ROT_PITCH", oldPitch);
    const bool rolledBack = rollRestored && pitchRestored && clear(store);
    return {false, !rolledBack};
}

} // namespace level_rotation
