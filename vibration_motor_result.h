#pragma once

#include <stdint.h>
#include <math.h>

struct VibrationMotorResult {
    float gyroRms;
    float accelRms;
    uint16_t samples;
};

static constexpr uint16_t VIBRATION_RESPONSE_MIN_MOTOR_SAMPLES = 50;
static constexpr uint16_t VIBRATION_RESPONSE_MIN_BASELINE_SAMPLES = 100;

static inline bool vibrationMotorResponseDetected(const VibrationMotorResult &result,
                                                   const VibrationMotorResult &baseline) {
    if (result.samples < VIBRATION_RESPONSE_MIN_MOTOR_SAMPLES ||
        baseline.samples < VIBRATION_RESPONSE_MIN_BASELINE_SAMPLES) return false;
    const bool accelResponse = result.accelRms >= fmaxf(0.06f, baseline.accelRms * 2.0f) &&
        result.accelRms - baseline.accelRms >= 0.04f;
    const bool gyroResponse = result.gyroRms >= fmaxf(0.006f, baseline.gyroRms * 2.0f) &&
        result.gyroRms - baseline.gyroRms >= 0.003f;
    return accelResponse || gyroResponse;
}
