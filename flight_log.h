#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr int FLIGHT_LOG_LEGACY_COLUMNS = 35;
constexpr int FLIGHT_LOG_COLUMNS = 41;
constexpr uint32_t FLIGHT_LOG_LEGACY_ROW_BYTES = 140;
constexpr uint32_t FLIGHT_LOG_DISARM_REASON = 0x80000000UL;
// On a disarm trigger, bits 24..30 encode DisarmReason; bits 0..15 remain
// diagnostic fault flags. Zero means an unclassified disarm.
constexpr uint32_t FLIGHT_LOG_DISARM_CAUSE_SHIFT = 24;
enum FlightLogState : uint8_t { ROLLING, POST_TRIGGER, FROZEN };
struct FlightLogStatus {
    FlightLogState state;
    uint32_t generation, reasonMask, rowCount, missedSamples;
    uint64_t triggerUs;
};
FlightLogStatus getFlightLogStatus();
bool freezeFlightLog();
bool resumeFlightLog();
void triggerFlightLog(uint32_t faultMask);
bool copyFrozenLogRow(uint32_t generation, uint32_t row, float *destination, int capacity);
size_t readFrozenLogBytes(uint32_t generation, uint32_t ofs, uint8_t *destination, size_t maxLen);
int getLogColumnCount();
const char *getLogColumnName(int column);
bool copyLatestLogRow(float *destination, int capacity, uint32_t *sequence);
