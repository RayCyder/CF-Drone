#include "../system_log_storage.h"
#include <cassert>
#include <initializer_list>
#include <cstdio>
#include <cstring>

static LegacyPersistedSystemLog legacyFixture(bool stalls) {
    LegacyPersistedSystemLog old = {};
    old.magic = SYSTEM_LOG_MAGIC; old.nextSequence = 101;
    old.normalCount = stalls ? 6 : 12; old.normalNext = stalls ? 2 : 3;
    old.stallCount = stalls ? 6 : 0; old.stallNext = stalls ? 4 : 0;
    old.eventCount = old.normalCount + old.stallCount;
    for (int i = 0; i < 12; ++i) {
        old.events[i].sequence = 89 + i; old.events[i].uptimeMs = 1000 + i;
        snprintf(old.events[i].tag, sizeof(old.events[i].tag), "event-%d", i);
        snprintf(old.events[i].message, sizeof(old.events[i].message), "message %d", i);
    }
    return old;
}
static void migrationTests() {
    for (bool stalls : {false, true}) {
        auto old = legacyFixture(stalls);
        PersistedSystemLog current = {}; bool migrated = false;
        assert(decodeSystemLogHistory(&old, sizeof(old), current, migrated) && migrated);
        assert(current.version == 2 && current.recordBytes == sizeof(SystemLogEvent));
        assert(current.normalNext == old.normalNext && current.stallNext == old.stallNext);
        assert(current.eventCount == old.eventCount && current.nextSequence == 101);
        for (int i = 0; i < 12; ++i) {
            assert(current.events[i].bootId == 0);
            assert(!memcmp(&current.events[i], &old.events[i], sizeof(LegacySystemLogEvent)));
        }
        // Re-saving and restoring cannot reassign legacy events to the new boot.
        current.events[0] = makeSystemLogEvent(0x12345678, 42, "BOOT", "new boot");
        current.events[0].sequence = 100; current.events[11].sequence = 88;
        assert(validSystemLogHistory(current));
        PersistedSystemLog restored = {};
        assert(decodeSystemLogHistory(&current, sizeof(current), restored, migrated) && !migrated);
        assert(restored.events[0].bootId == 0x12345678 && restored.events[1].bootId == 0);
        assert(restored.events[0].uptimeMs == 42 && restored.events[1].uptimeMs == 1001);
    }
    // Decoding deliberately unaligned NVS bytes is supported through memcpy.
    auto old = legacyFixture(false); unsigned char raw[sizeof(old) + 1];
    memcpy(raw + 1, &old, sizeof(old)); PersistedSystemLog destination = {}; bool migrated;
    assert(decodeSystemLogHistory(raw + 1, sizeof(old), destination, migrated));
    LegacyPersistedSystemLog empty = {}; empty.magic = SYSTEM_LOG_MAGIC; empty.nextSequence = 1;
    assert(decodeSystemLogHistory(&empty, sizeof(empty), destination, migrated));
    assert(destination.eventCount == 0);
}
static void malformedTests() {
    const auto original = legacyFixture(true);
    PersistedSystemLog destination = {}; destination.nextSequence = 999;
    auto reject = [&](const LegacyPersistedSystemLog &old) {
        bool migrated = true;
        assert(!decodeSystemLogHistory(&old, sizeof(old), destination, migrated));
        assert(!migrated && destination.nextSequence == 999);
    };
    auto old = original; old.magic = 0; reject(old);
    old = original; old.nextSequence = 0; reject(old);
    old = original; old.eventCount = 13; reject(old);
    old = original; old.eventCount = 11; reject(old);
    old = original; old.normalCount = 7; reject(old);
    old = original; old.normalNext = 6; reject(old);
    old = original; old.stallCount = 7; reject(old);
    old = original; old.stallNext = 6; reject(old);
    old = original; memset(old.events[0].tag, 'x', sizeof(old.events[0].tag)); reject(old);
    old = original; memset(old.events[11].message, 'x', sizeof(old.events[11].message)); reject(old);
    old = original; old.events[1].message[0] = '\n'; reject(old);
    old = original; old.events[2].tag[0] = 0x7f; reject(old);
    old = original; old.events[0].sequence = 0; reject(old);
    old = original; old.events[0].sequence = old.nextSequence; reject(old);
    old = original; old.events[0].sequence = old.events[1].sequence; reject(old);
    bool migrated;
    assert(!decodeSystemLogHistory(&old, sizeof(old) - 1, destination, migrated));
    assert(!decodeSystemLogHistory(nullptr, sizeof(old), destination, migrated));
    PersistedSystemLog current = {};
    assert(decodeSystemLogHistory(&original, sizeof(original), current, migrated));
    current.version = 3; assert(!decodeSystemLogHistory(&current, sizeof(current), destination, migrated));
    current.version = 2; current.recordBytes = 64;
    assert(!decodeSystemLogHistory(&current, sizeof(current), destination, migrated));
    current.recordBytes = sizeof(SystemLogEvent); current.events[0].tag[0] = '\r';
    assert(!decodeSystemLogHistory(&current, sizeof(current), destination, migrated));
    // An unused slot is never exposed; stale bytes there must not discard valid live history.
    old = {}; old.magic = SYSTEM_LOG_MAGIC; old.nextSequence = 2; old.normalCount = old.eventCount = 1;
    old.normalNext = 1; old.events[0].sequence = 1; memset(old.events[7].tag, 'x', sizeof(old.events[7].tag));
    assert(decodeSystemLogHistory(&old, sizeof(old), destination, migrated));
}
static void creationTests() {
    const auto event = makeSystemLogEvent(0xaabbccdd, 321, "TAG\ninject\r", "first\nsecond\t\x7f");
    assert(event.bootId == 0xaabbccdd && event.uptimeMs == 321 && event.sequence == 0);
    assert(!strcmp(event.tag, "TAG inject "));
    assert(!strcmp(event.message, "first second  "));
    assert(validSystemLogText(event.tag, sizeof(event.tag)) && validSystemLogText(event.message, sizeof(event.message)));
    const auto fallback = makeSystemLogEvent(1, 0, nullptr, nullptr);
    assert(!strcmp(fallback.tag, "SYSTEM") && !strcmp(fallback.message, ""));
    char longText[100]; memset(longText, 'x', 99);longText[99] = 0;
    const auto truncated = makeSystemLogEvent(2, 3, longText, longText);
    assert(strlen(truncated.tag) == 11 && strlen(truncated.message) == 43);
}
static void snapshotCopyTests() {
    PersistedSystemLog snapshot = {}; SystemLogEvent out[12] = {};
    assert(copySystemLogSnapshotAfter(snapshot, 0, out, 12) == 0);
    assert(copySystemLogSnapshotAfter(snapshot, 0, nullptr, 12) == 0);
    assert(copySystemLogSnapshotAfter(snapshot, 0, out, 0) == 0);
    for (bool stalls : {false, true}) {
        const auto old = legacyFixture(stalls); bool migrated;
        assert(decodeSystemLogHistory(&old, sizeof(old), snapshot, migrated));
        for (int i = 0; i < 12; ++i) snapshot.events[i].bootId = 1000 + i;
        // Ring positions differ from chronological sequence order in both layouts.
        assert(copySystemLogSnapshotAfter(snapshot, 0, out, 12) == 12);
        for (int i = 0; i < 12; ++i) {
            assert(out[i].sequence == (uint32_t)(89 + i));
            assert(out[i].bootId == (uint32_t)(1000 + i));
            assert(!memcmp(&out[i], &snapshot.events[i], sizeof(SystemLogEvent)));
        }
        uint32_t cursor = 92;
        assert(copySystemLogSnapshotAfter(snapshot, cursor, out, 3) == 3);
        assert(out[0].sequence == 93 && out[2].sequence == 95);
        cursor = out[2].sequence;
        assert(copySystemLogSnapshotAfter(snapshot, cursor, out, 3) == 3);
        assert(out[0].sequence == 96 && out[2].sequence == 98);
        assert(copySystemLogSnapshotAfter(snapshot, 100, out, 12) == 0);
    }
    // Partially filled split rings: poison unused slots so selection errors show up.
    snapshot = {}; snapshot.stallCount = 2; snapshot.stallNext = 1;
    snapshot.normalCount = 2; snapshot.normalNext = 0; snapshot.eventCount = 4;
    for (auto &event : snapshot.events) event.sequence = 12345;
    snapshot.events[5].sequence = 10; snapshot.events[0].sequence = 12;
    snapshot.events[10].sequence = 11; snapshot.events[11].sequence = 13;
    assert(copySystemLogSnapshotAfter(snapshot, 0, out, 12) == 4);
    for (int i = 0; i < 4; ++i) assert(out[i].sequence == (uint32_t)(10 + i));
    // Preserve the existing modular ordering and exclusive cursor across uint32 wrap.
    snapshot = {}; snapshot.normalCount = 5; snapshot.normalNext = 2;
    const uint32_t wrapped[] = {UINT32_MAX - 1, UINT32_MAX, 0, 1, 2};
    const int slots[] = {9, 10, 11, 0, 1};
    for (int i = 0; i < 5; ++i) snapshot.events[slots[i]].sequence = wrapped[i];
    assert(copySystemLogSnapshotAfter(snapshot, UINT32_MAX - 2, out, 12) == 5);
    for (int i = 0; i < 5; ++i) assert(out[i].sequence == wrapped[i]);
    assert(copySystemLogSnapshotAfter(snapshot, UINT32_MAX, out, 12) == 3);
    assert(out[0].sequence == 0 && out[2].sequence == 2);
}
int main(){ migrationTests(); malformedTests(); creationTests(); snapshotCopyTests(); puts("system event persistence migration/copy: PASS"); }
