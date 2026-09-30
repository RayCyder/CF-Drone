#pragma once

#include <stdint.h>

static inline bool motorTestDeadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
	return (int32_t)(nowMs - deadlineMs) >= 0;
}
