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
};

void setDiagnosticFault(DiagnosticFault fault, bool active);
bool hasBlockingDiagnosticFault();
uint32_t getActiveDiagnosticFaults();
void printDiagnostics();
void clearDiagnosticHistory();
void updateDiagnostics();
void recordLoopTiming(float dt);
void recordLoopStage(const char *stage, uint32_t durationUs);
void initializeDiagnostics();
