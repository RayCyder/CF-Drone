#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Fixed memory and bounded work; excess console text is counted, never allocated.
template<size_t Capacity> class LogByteQueue {
    uint8_t bytes[Capacity] = {};
    size_t head = 0, used = 0;
public:
    uint32_t dropped = 0;
    size_t size() const { return used; }
    void clear() { head = used = 0; }
    void push(const char *text) {
        if (!text) return;
        while (*text) {
            if (used < Capacity) bytes[(head + used++) % Capacity] = (uint8_t)*text;
            else if (dropped != UINT32_MAX) ++dropped;
            ++text;
        }
    }
    size_t pop(uint8_t *out, size_t capacity) {
        const size_t count = used < capacity ? used : capacity;
        for (size_t i = 0; i < count; ++i) out[i] = bytes[(head + i) % Capacity];
        head = (head + count) % Capacity;
        used -= count;
        return count;
    }
};

struct LogTransferCursor {
    uint32_t generation = 0, offset = 0, remaining = 0;
    bool active = false;
    void start(uint32_t gen, uint32_t size, uint32_t ofs, uint32_t count) {
        generation = gen;
        offset = ofs;
        const uint32_t available = ofs < size ? size - ofs : 0;
        remaining = count < available ? count : available;
        active = true; // EOF/zero requests receive one empty response.
    }
    uint8_t nextCount() const { return remaining < 90 ? (uint8_t)remaining : 90; }
    void advance(uint8_t count) {
        if (count > remaining) { active = false; return; }
        remaining -= count;
        offset += count;
        if (!remaining || !count) active = false;
    }
};

struct LogOutputChunk {
    char data[1024] = {};
    size_t size = 0, offset = 0;
    bool empty() const { return offset >= size; }
    void clear() { size = offset = 0; }
    template<class Writer> void send(size_t available, size_t budget, Writer write) {
        size_t count = size - offset;
        if (count > available) count = available;
        if (count > budget) count = budget;
        if (count) {
            const size_t sent = write((const uint8_t *)data + offset, count);
            offset += sent < count ? sent : count;
        }
    }
};
