#pragma once
#include "system_log.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

constexpr uint32_t SYSTEM_LOG_MAGIC = 0x534C4F47;
constexpr uint16_t SYSTEM_LOG_VERSION = 2;
constexpr int LOG_CAPACITY = 12;
constexpr int STALL_CAPACITY = 6;

// Exact previous on-device ABI; never extend this migration-only structure.
struct LegacySystemLogEvent {
    uint32_t sequence, uptimeMs;
    char tag[12], message[44];
};
struct LegacyPersistedSystemLog {
    uint32_t magic, nextSequence;
    uint8_t eventCount, normalCount, normalNext, stallCount, stallNext;
    LegacySystemLogEvent events[LOG_CAPACITY];
};
struct PersistedSystemLog {
    uint32_t magic;
    uint16_t version, recordBytes;
    uint32_t nextSequence;
    uint8_t eventCount, normalCount, normalNext, stallCount, stallNext;
    SystemLogEvent events[LOG_CAPACITY];
};
static_assert(sizeof(LegacySystemLogEvent) == 64 && sizeof(LegacyPersistedSystemLog) == 784,
    "Legacy NVS migration must preserve its original ESP32 ABI");
static_assert(sizeof(SystemLogEvent) == 68 && sizeof(PersistedSystemLog) == 836,
    "Versioned system-event history must remain bounded");

inline bool validSystemLogText(const char *text, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (!c) return true;
        // Prevent malformed NVS strings from injecting SSE frames or terminal controls.
        if (c < 0x20 || c == 0x7f) return false;
    }
    return false;
}
template<class History> bool validSystemLogHistory(const History &h) {
    if (h.magic != SYSTEM_LOG_MAGIC || !h.nextSequence || h.eventCount > LOG_CAPACITY ||
        h.stallCount > STALL_CAPACITY || h.stallNext >= STALL_CAPACITY ||
        h.normalCount > (h.stallCount ? STALL_CAPACITY : LOG_CAPACITY) ||
        h.normalNext >= (h.stallCount ? STALL_CAPACITY : LOG_CAPACITY) ||
        h.eventCount != h.normalCount + h.stallCount) return false;
    uint32_t sequences[LOG_CAPACITY] = {};
    int count = 0;
    // Only occupied slots are meaningful; unused slots may contain old overwritten data.
    for (int group = 0; group < 2; ++group) {
        const int capacity = group == 0 || h.stallCount ? STALL_CAPACITY : LOG_CAPACITY;
        const int entries = group == 0 ? h.stallCount : h.normalCount;
        const int next = group == 0 ? h.stallNext : h.normalNext;
        const int base = group == 1 && h.stallCount ? STALL_CAPACITY : 0;
        for (int i = 0; i < entries; ++i) {
            const auto &event = h.events[base + (next + capacity - entries + i) % capacity];
            if (!event.sequence || (int32_t)(h.nextSequence - event.sequence) <= 0 ||
                !validSystemLogText(event.tag, sizeof(event.tag)) ||
                !validSystemLogText(event.message, sizeof(event.message))) return false;
            for (int j = 0; j < count; ++j) if (sequences[j] == event.sequence) return false;
            sequences[count++] = event.sequence;
        }
    }
    return count == h.eventCount;
}
// Validates before publishing. A failed decode leaves destination untouched.
inline bool decodeSystemLogHistory(const void *bytes, size_t length,
    PersistedSystemLog &destination, bool &migrated) {
    migrated = false;
    if (!bytes) return false;
    if (length == sizeof(PersistedSystemLog)) {
        PersistedSystemLog saved;
        memcpy(&saved, bytes, sizeof(saved));
        if (saved.version != SYSTEM_LOG_VERSION || saved.recordBytes != sizeof(SystemLogEvent) ||
            !validSystemLogHistory(saved)) return false;
        destination = saved;
        return true;
    }
    if (length != sizeof(LegacyPersistedSystemLog)) return false;
    LegacyPersistedSystemLog old;
    memcpy(&old, bytes, sizeof(old));
    if (!validSystemLogHistory(old)) return false;
    PersistedSystemLog result = {};
    result.magic = SYSTEM_LOG_MAGIC; result.version = SYSTEM_LOG_VERSION;
    result.recordBytes = sizeof(SystemLogEvent); result.nextSequence = old.nextSequence;
    result.eventCount = old.eventCount; result.normalCount = old.normalCount;
    result.normalNext = old.normalNext; result.stallCount = old.stallCount; result.stallNext = old.stallNext;
    for (int i = 0; i < LOG_CAPACITY; ++i) {
        result.events[i].sequence = old.events[i].sequence;
        result.events[i].uptimeMs = old.events[i].uptimeMs;
        memcpy(result.events[i].tag, old.events[i].tag, sizeof(old.events[i].tag));
        memcpy(result.events[i].message, old.events[i].message, sizeof(old.events[i].message));
        // Original boot identity was never persisted. Do not assign the current boot.
        result.events[i].bootId = 0;
    }
    destination = result;
    migrated = true;
    return true;
}
inline SystemLogEvent makeSystemLogEvent(uint32_t bootId, uint32_t uptimeMs,
    const char *tag, const char *message) {
    SystemLogEvent event = {};
    event.bootId = bootId; event.uptimeMs = uptimeMs;
    const char *sources[2] = {tag ? tag : "SYSTEM", message ? message : ""};
    char *destinations[2] = {event.tag, event.message};
    const size_t sizes[2] = {sizeof(event.tag), sizeof(event.message)};
    for (int field = 0; field < 2; ++field) {
        size_t i = 0;
        for (; sources[field][i] && i + 1 < sizes[field]; ++i) {
            const unsigned char c = (unsigned char)sources[field][i];
            destinations[field][i] = c < 0x20 || c == 0x7f ? ' ' : (char)c;
        }
        destinations[field][i] = '\0';
    }
    return event;
}

// Snapshot stays with the caller; sorting only twelve slot indices avoids a
// second 816-byte event array on the telemetry task's small stack.
inline int copySystemLogSnapshotAfter(const PersistedSystemLog &snapshot, uint32_t sequence,
    SystemLogEvent *destination, int capacity) {
    if (!destination || capacity <= 0) return 0;
    uint8_t selected[LOG_CAPACITY];
    int count = 0;
    if (snapshot.stallCount == 0) {
        const int oldest = (snapshot.normalNext + LOG_CAPACITY - snapshot.normalCount) % LOG_CAPACITY;
        for (int i = 0; i < snapshot.normalCount; ++i)
            selected[count++] = (oldest + i) % LOG_CAPACITY;
    } else {
        const int stallOldest = (snapshot.stallNext + STALL_CAPACITY - snapshot.stallCount) % STALL_CAPACITY;
        for (int i = 0; i < snapshot.stallCount; ++i)
            selected[count++] = (stallOldest + i) % STALL_CAPACITY;
        const int normalOldest = (snapshot.normalNext + STALL_CAPACITY - snapshot.normalCount) % STALL_CAPACITY;
        for (int i = 0; i < snapshot.normalCount; ++i)
            selected[count++] = STALL_CAPACITY + (normalOldest + i) % STALL_CAPACITY;
    }
    for (int i = 1; i < count; ++i) {
        const uint8_t index = selected[i];
        int j = i;
        while (j > 0 && (int32_t)(snapshot.events[selected[j - 1]].sequence - snapshot.events[index].sequence) > 0) {
            selected[j] = selected[j - 1];
            --j;
        }
        selected[j] = index;
    }
    int copied = 0;
    for (int i = 0; i < count && copied < capacity; ++i) {
        const SystemLogEvent &event = snapshot.events[selected[i]];
        if ((int32_t)(event.sequence - sequence) > 0) destination[copied++] = event;
    }
    return copied;
}
