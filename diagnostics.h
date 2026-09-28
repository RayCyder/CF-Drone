#pragma once

#include <stdint.h>

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
};

void setDiagnosticFault(DiagnosticFault fault, bool active);
bool hasBlockingDiagnosticFault();
uint32_t getActiveDiagnosticFaults();
void printDiagnostics();
void clearDiagnosticHistory();
void updateDiagnostics();
void recordLoopTiming(float dt);
enum LoopStageId : uint8_t { LOOP_STAGE_IMU, LOOP_STAGE_IMU_WAIT, LOOP_STAGE_IMU_PROCESS, LOOP_STAGE_RC_WEB, LOOP_STAGE_ESTIMATE, LOOP_STAGE_BATTERY_ADC, LOOP_STAGE_CONTROL_LAW, LOOP_STAGE_MOTOR_OUT, LOOP_STAGE_CONTROL, LOOP_STAGE_SERIAL_INPUT, LOOP_STAGE_MAVLINK, LOOP_STAGE_FLIGHT_LOG, LOOP_STAGE_PARAM_SYNC, LOOP_STAGE_LED, LOOP_STAGE_DIAGNOSTICS, LOOP_STAGE_WIFI_SERVICE, LOOP_STAGE_LOOP_GAP, LOOP_STAGE_MAINTENANCE, LOOP_STAGE_WHOLE_LOOP, LOOP_STAGE_COUNT };
void recordLoopStage(LoopStageId stage, uint32_t durationUs);
void initializeDiagnostics();
