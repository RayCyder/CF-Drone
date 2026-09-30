#include <cassert>
#include <cstdint>
#include "../motor_test_timer.h"

int main() {
	assert(!motorTestDeadlineReached(1999, 2000));
	assert(motorTestDeadlineReached(2000, 2000));
	assert(motorTestDeadlineReached(2001, 2000));
	assert(!motorTestDeadlineReached(UINT32_MAX - 10u, 20u));
	assert(!motorTestDeadlineReached(19u, 20u));
	assert(motorTestDeadlineReached(20u, 20u));
}
