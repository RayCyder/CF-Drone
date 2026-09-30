#pragma once

#include <stdint.h>

enum ImuInterruptSource : uint8_t {
	IMU_INTERRUPT_NONE = 0,
	IMU_INTERRUPT_GPTIMER = 1,
	IMU_INTERRUPT_DRDY_PIN = 2
};

struct ImuWaitTrace {
	uint32_t waitStartedUs = 0;
	uint32_t waitEndedUs = 0;
	uint32_t lastInterruptUs = 0;
	uint16_t interruptCount = 0;
	uint16_t semaphoreTakes = 0;
	uint16_t semaphoreTimeouts = 0;
	uint16_t readAttempts = 0;
	uint16_t readyReads = 0;
	uint16_t readTotalUs = 0;
	uint16_t readMaxUs = 0;
	uint8_t interruptSource = IMU_INTERRUPT_NONE;
	uint8_t result = 0;
};
