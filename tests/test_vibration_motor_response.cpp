#include <cassert>
#include "../vibration_motor_result.h"

int main() {
    const VibrationMotorResult baseline = {0.001f, 0.02f, 190};
    assert(!vibrationMotorResponseDetected({0.002f, 0.03f, 98}, baseline));
    assert(vibrationMotorResponseDetected({0.002f, 0.08f, 98}, baseline));
    assert(vibrationMotorResponseDetected({0.008f, 0.03f, 98}, baseline));
    assert(!vibrationMotorResponseDetected({0.008f, 0.08f, 49}, baseline));
    assert(!vibrationMotorResponseDetected({0.008f, 0.08f, 98}, {0.001f, 0.02f, 99}));
}
