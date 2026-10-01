#include "Arduino.h"
#include "../pwm_config.h"

#include <cassert>
#include <cstdio>

static void test_stepwise_mosfet_to_esc_configuration() {
	assert(motorPwmConfigurationValid(25000, 0, 0, -1));
	assert(motorPwmCandidateConfigurationValid("MOT_PWM_FREQ", 400, 25000, 0, 0, -1));
	assert(motorPwmCandidateConfigurationValid("MOT_PWM_STOP", 1000, 400, 0, 0, -1));
	assert(motorPwmCandidateConfigurationValid("MOT_PWM_MIN", 1000, 400, 1000, 0, -1));
	assert(motorPwmCandidateConfigurationValid("MOT_PWM_MAX", 2000, 400, 1000, 1000, -1));
	assert(motorPwmConfigurationValid(400, 1000, 1000, 2000));
}

static void test_rejects_esc_pulses_longer_than_period() {
	assert(!motorPwmCandidateConfigurationValid("MOT_PWM_MAX", 2000, 25000, 0, 0, -1));
	assert(!motorPwmCandidateConfigurationValid("MOT_PWM_FREQ", 25000, 400, 1000, 1000, 2000));
	assert(!motorPwmConfigurationValid(25000, 1000, 1000, 2000));
	assert(motorPwmConfigurationValid(500, 1000, 1000, 2000));
	assert(!motorPwmConfigurationValid(501, 1000, 1000, 2000));
	assert(!motorPwmConfigurationValid(600, 1000, 1000, 2000));
}

static void test_rejects_non_monotonic_esc_stop() {
	assert(motorPwmConfigurationValid(400, 1000, 1000, 2000));
	assert(motorPwmConfigurationValid(400, 900, 1000, 2000));
	assert(!motorPwmConfigurationValid(400, 1200, 1000, 2000));
	assert(!motorPwmCandidateConfigurationValid("MOT_PWM_STOP", 1200, 400, 1000, 1000, 2000));
	assert(!motorPwmCandidateConfigurationValid("MOT_PWM_MIN", 900, 400, 1000, 1000, 2000));
}

static void test_duty_output_is_clamped_to_ledc_range() {
	assert(pwmDutyFromPulseMicros(2000, 25000, 10) == 1023);
	assert(pwmDutyFromPulseMicros(-10, 400, 10) == 0);
	assert(pwmDutyFromPulseMicros(1000, 400, 10) == 409);
	assert(pwmDutyFromPulseMicros(2500, 400, 10) == 1023);
	assert(motorPwmDutyFromValue(0.5f, 400, 10, 1000, 1000, 2000) == 614);
	assert(motorPwmDutyFromValue(0.5f, 25000, 10, 1000, 1000, 2000) == 0);
	assert(motorPwmDutyFromValue(0.5f, 400, 10, 1200, 1000, 2000) == 0);
}

int main() {
	test_stepwise_mosfet_to_esc_configuration();
	test_rejects_esc_pulses_longer_than_period();
	test_rejects_non_monotonic_esc_stop();
	test_duty_output_is_clamped_to_ledc_range();
	puts("PWM parameter regression: PASS");
}
