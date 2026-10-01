#pragma once

#include <stdint.h>
#include "loop_metrics.h"

struct SlowLoopCapture;

enum DiagnosticFault : uint32_t {
	DIAG_IMU_INIT       = 1UL << 0,
	DIAG_IMU_TIMEOUT    = 1UL << 1,
	DIAG_IMU_INVALID    = 1UL << 2,
	DIAG_MOTOR_INIT     = 1UL << 3,
	DIAG_RC_LOSS        = 1UL << 4,
	DIAG_WEB_RC_LOSS    = 1UL << 5,
	DIAG_BATTERY_LOW    = 1UL << 6,
	DIAG_LOOP_OVERRUN   = 1UL << 7,
	DIAG_PARAMETER      = 1UL << 8,
	DIAG_AUTO_TARGET_TIMEOUT = 1UL << 9,
	DIAG_INVERTED       = 1UL << 10,
};

void setDiagnosticFault(DiagnosticFault fault, bool active);
bool hasBlockingDiagnosticFault();
uint32_t getActiveDiagnosticFaults();
void printDiagnostics();
void clearDiagnosticHistory();
void updateDiagnostics();
void printDiagnosticsBrief();
void recordLoopTiming(float dt);
#if defined(CF_DRONE_CAPTURE_ARMED_LOOP_TRACE)
void freezeArmedLoopTraceForThrottleRelease();
#endif
void setLoopTimingSequence(uint32_t loopSequence);
enum LoopStageId : uint8_t {
	LOOP_STAGE_IMU,
	LOOP_STAGE_IMU_WAIT,
	LOOP_STAGE_IMU_PROCESS,
	LOOP_STAGE_RC_WEB,
	LOOP_STAGE_ESTIMATE,
	LOOP_STAGE_BATTERY_ADC,
	LOOP_STAGE_CONTROL_LAW,
	LOOP_STAGE_MOTOR_OUT,
	LOOP_STAGE_CONTROL,
	LOOP_STAGE_SERIAL_INPUT,
	LOOP_STAGE_MAVLINK,
	LOOP_STAGE_FLIGHT_LOG,
	LOOP_STAGE_PARAM_SYNC,
	LOOP_STAGE_LED,
	LOOP_STAGE_DIAGNOSTICS,
	LOOP_STAGE_WIFI_SERVICE,
	LOOP_STAGE_LOOP_GAP,
	LOOP_STAGE_MAINTENANCE,
	LOOP_STAGE_WHOLE_LOOP,
	LOOP_STAGE_COUNT
};
enum LoopTraceStageId : uint8_t {
	LOOP_TRACE_IMU_WAIT,
	LOOP_TRACE_IMU_PROCESS,
	LOOP_TRACE_RC_WEB,
	LOOP_TRACE_ESTIMATE,
	LOOP_TRACE_BATTERY_ADC,
	LOOP_TRACE_CONTROL_LAW,
	LOOP_TRACE_MOTOR_OUT,
	LOOP_TRACE_SERIAL_INPUT,
	LOOP_TRACE_MAVLINK,
	LOOP_TRACE_FLIGHT_LOG,
	LOOP_TRACE_PARAM_SYNC,
	LOOP_TRACE_LED,
	LOOP_TRACE_DIAGNOSTICS,
	LOOP_TRACE_WIFI_SERVICE,
	LOOP_TRACE_LOOP_GAP,
	LOOP_TRACE_UNACCOUNTED
};
void recordLoopStage(LoopStageId stage, uint32_t durationUs);
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
void recordImuWaitTrace(const ImuWaitTrace &trace);
#endif
void beginLoopTraceCycle();
void finishLoopTraceCycle();
uint8_t getLoopTraceCount();
uint32_t getLoopTraceOverwrittenCount();
uint32_t getLoopTraceOldestSequence();
uint32_t getLoopTraceNextSequence();
void getLoopTraceRange(uint32_t &oldest, uint32_t &next, uint32_t &overwritten);
bool copyLoopTrace(uint32_t sequence, LoopOverrunTrace &destination);
bool copyWorstLoopTrace(LoopOverrunTrace &destination);
void initializeSlowLoopRetention(uint32_t resetReason);
uint8_t retainedSlowLoopCount();
uint32_t retainedSlowLoopOverwritten();
bool retainedSlowLoopIntegrity();
bool copyRetainedSlowLoop(uint8_t index, SlowLoopCapture &destination);
const char *getLoopTraceStageName(uint8_t stage);
void resetLoopTraceState();
void initializeDiagnostics();
