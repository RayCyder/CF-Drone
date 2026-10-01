#include "../level_rotation_transaction.h"

#include <cassert>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

struct FakeStore {
    std::map<std::string, std::vector<unsigned char>> values;
    int mutations = 0;
    int interruptBeforeMutation = 0;
    bool failNextPitchWrite = false;

    void beforeMutation() {
        ++mutations;
        if (mutations == interruptBeforeMutation) throw std::runtime_error("power cut");
    }
    bool isKey(const char *key) const { return values.count(key) != 0; }
    size_t putBytes(const char *key, const void *data, size_t size) {
        beforeMutation();
        const auto *first = static_cast<const unsigned char *>(data);
        values[key] = {first, first + size};
        return size;
    }
    size_t getBytesLength(const char *key) const {
        auto it = values.find(key);
        return it == values.end() ? 0 : it->second.size();
    }
    size_t getBytes(const char *key, void *data, size_t capacity) const {
        auto it = values.find(key);
        if (it == values.end() || it->second.size() > capacity) return 0;
        std::memcpy(data, it->second.data(), it->second.size());
        return it->second.size();
    }
    size_t putFloat(const char *key, float value) {
        beforeMutation();
        if (failNextPitchWrite && std::strcmp(key, "IMU_ROT_PITCH") == 0) {
            failNextPitchWrite = false;
            return 0;
        }
        const auto *first = reinterpret_cast<const unsigned char *>(&value);
        values[key] = {first, first + sizeof(value)};
        return sizeof(value);
    }
    float getFloat(const char *key, float fallback) const {
        auto it = values.find(key);
        if (it == values.end() || it->second.size() != sizeof(float)) return fallback;
        float value;
        std::memcpy(&value, it->second.data(), sizeof(value));
        return value;
    }
    bool remove(const char *key) {
        beforeMutation();
        return values.erase(key) == 1;
    }
};

static FakeStore initialStore() {
    FakeStore store;
    assert(store.putFloat("IMU_ROT_ROLL", 0.0f) == sizeof(float));
    assert(store.putFloat("IMU_ROT_PITCH", -0.030305f) == sizeof(float));
    store.mutations = 0;
    return store;
}

static void assertPair(const FakeStore &store, float roll, float pitch) {
    assert(store.getFloat("IMU_ROT_ROLL", NAN) == roll);
    assert(store.getFloat("IMU_ROT_PITCH", NAN) == pitch);
}

int main() {
    constexpr float oldRoll = 0.0f, oldPitch = -0.030305f;
    constexpr float newRoll = 0.004479f, newPitch = -0.037368f;

    for (int cut = 1; cut <= 4; ++cut) {
        FakeStore store = initialStore();
        store.interruptBeforeMutation = cut;
        try {
            (void)level_rotation::commit(store, oldRoll, oldPitch, newRoll, newPitch);
            assert(false && "power cut was not injected");
        } catch (const std::runtime_error &) {}
        store.interruptBeforeMutation = 0;
        assert(level_rotation::recover(store));
        assertPair(store, oldRoll, oldPitch);
        assert(!store.isKey(level_rotation::KEY));
    }

    // Recovery itself can lose power. The marker stays until both old values
    // have read back, so another boot can finish the rollback.
    for (int recoveryCut = 1; recoveryCut <= 3; ++recoveryCut) {
        FakeStore store = initialStore();
        store.interruptBeforeMutation = 4;
        try {
            (void)level_rotation::commit(store, oldRoll, oldPitch, newRoll, newPitch);
            assert(false && "power cut was not injected");
        } catch (const std::runtime_error &) {}
        store.mutations = 0;
        store.interruptBeforeMutation = recoveryCut;
        try {
            (void)level_rotation::recover(store);
            assert(false && "recovery cut was not injected");
        } catch (const std::runtime_error &) {}
        store.interruptBeforeMutation = 0;
        assert(level_rotation::recover(store));
        assertPair(store, oldRoll, oldPitch);
        assert(!store.isKey(level_rotation::KEY));
    }

    FakeStore success = initialStore();
    const auto result = level_rotation::commit(success, oldRoll, oldPitch, newRoll, newPitch);
    assert(result.success && !result.recoveryRequired);
    assertPair(success, newRoll, newPitch);
    assert(!success.isKey(level_rotation::KEY));

    FakeStore writeFailure = initialStore();
    writeFailure.failNextPitchWrite = true;
    const auto failed = level_rotation::commit(writeFailure, oldRoll, oldPitch, newRoll, newPitch);
    assert(!failed.success && !failed.recoveryRequired);
    assertPair(writeFailure, oldRoll, oldPitch);
    assert(!writeFailure.isKey(level_rotation::KEY));

    FakeStore corrupt = initialStore();
    const unsigned char badMarker[] = {1, 2, 3};
    assert(corrupt.putBytes(level_rotation::KEY, badMarker, sizeof(badMarker)) == sizeof(badMarker));
    assert(!level_rotation::recover(corrupt));
    assert(corrupt.isKey(level_rotation::KEY));
}
