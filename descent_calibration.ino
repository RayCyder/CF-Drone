// Pilot-operated descent calibration recorder. This module observes only; it never commands flight.
#include <Arduino.h>
#include <math.h>
#include "descent_calibration.h"
#include "control.h"
#include "diagnostics.h"

extern bool armed;
extern int mode;
extern float motThrMin;
extern float thrustTarget, batteryVoltage;
extern float controlRoll, controlPitch, controlYaw, controlThrottle;
extern Quaternion attitude;
extern bool isControlledLandingActive();
extern bool motorsActive();
extern uint32_t getActiveDiagnosticFaults();
extern ControlSource getCurrentControlSource();

static DescentCalibrationRecorder descentCalibration;
static portMUX_TYPE descentCalibrationMux = portMUX_INITIALIZER_UNLOCKED;

static int16_t scaledSigned(float value, float scale) {
	if (!isfinite(value)) return INT16_MAX;
	const float scaled = value * scale;
	if (scaled > INT16_MAX) return INT16_MAX;
	if (scaled < INT16_MIN) return INT16_MIN;
	return (int16_t)lroundf(scaled);
}

static uint16_t scaledUnit(float value) {
	if (!isfinite(value) || value <= 0.0f) return 0;
	return (uint16_t)lroundf(constrain(value, 0.0f, 1.0f) * 10000.0f);
}

bool startDescentCalibration() {
	if (armed || motorsActive() || mode != MODE_STAB || isControlledLandingActive() ||
		hasBlockingDiagnosticFault()) return false;
	portENTER_CRITICAL(&descentCalibrationMux);
	const bool started = descentCalibration.start(millis(), motThrMin, hoverThrustTarget());
	portEXIT_CRITICAL(&descentCalibrationMux);
	return started;
}

bool abortDescentCalibration() {
	portENTER_CRITICAL(&descentCalibrationMux);
	const bool wasActive = descentCalibration.active();
	if (wasActive) descentCalibration.abort("cancelled_by_pilot");
	portEXIT_CRITICAL(&descentCalibrationMux);
	return wasActive;
}

void clearDescentCalibration() {
	if (armed || motorsActive()) return;
	portENTER_CRITICAL(&descentCalibrationMux);
	descentCalibration.clear();
	portEXIT_CRITICAL(&descentCalibrationMux);
}

bool descentCalibrationStableMarkReady() {
	portENTER_CRITICAL(&descentCalibrationMux);
	const bool ready = descentCalibration.state() == DESCENT_CALIBRATION_HOVER_READY;
	portEXIT_CRITICAL(&descentCalibrationMux);
	return ready;
}

void recordDescentCalibrationSample() {
	const uint32_t now = millis();
	portENTER_CRITICAL(&descentCalibrationMux);
	const bool due = descentCalibration.sampleDue(now);
	portEXIT_CRITICAL(&descentCalibrationMux);
	if (!due) return;

	const Vector angles = attitude.toEuler();
	DescentCalibrationSample sample = {};
	sample.thrustTenThousand = scaledUnit(thrustTarget);
	sample.batteryMv = isfinite(batteryVoltage) && batteryVoltage > 0.0f
		? (uint16_t)constrain(lroundf(batteryVoltage * 1000.0f), 0L, 65535L) : 0;
	sample.rollCentiDeg = scaledSigned(angles.x, 5729.57795f);
	sample.pitchCentiDeg = scaledSigned(angles.y, 5729.57795f);
	sample.rcRollTenThousand = scaledSigned(controlRoll, 10000.0f);
	sample.rcPitchTenThousand = scaledSigned(controlPitch, 10000.0f);
	sample.rcYawTenThousand = scaledSigned(controlYaw, 10000.0f);
	sample.rcThrottleTenThousand = scaledUnit(controlThrottle);
	sample.faults = (uint16_t)getActiveDiagnosticFaults();
	sample.mode = (uint8_t)mode;
	sample.controlSource = (uint8_t)getCurrentControlSource();
	sample.landingGuardActive = isControlledLandingActive() ? 1 : 0;

	portENTER_CRITICAL(&descentCalibrationMux);
	if (descentCalibration.active()) {
		if (!armed && descentCalibration.state() == DESCENT_CALIBRATION_WAITING_TAKEOFF)
			descentCalibration.tick(now, sample);
		else if (!armed) descentCalibration.onDisarmed();
		else if (mode != MODE_STAB) descentCalibration.abort("flight_mode_changed");
		else if (isControlledLandingActive()) descentCalibration.abort("landing_guard_active");
		else if (sample.faults) descentCalibration.abort("flight_fault");
		else descentCalibration.tick(now, sample);
	}
	portEXIT_CRITICAL(&descentCalibrationMux);
}

DescentCalibrationSummary getDescentCalibrationSummary() {
	portENTER_CRITICAL(&descentCalibrationMux);
	const DescentCalibrationSummary result = descentCalibration.summary(millis());
	portEXIT_CRITICAL(&descentCalibrationMux);
	return result;
}

bool copyDescentCalibrationSample(uint16_t index, DescentCalibrationSample &sample) {
	portENTER_CRITICAL(&descentCalibrationMux);
	const bool copied = descentCalibration.copySample(index, sample);
	portEXIT_CRITICAL(&descentCalibrationMux);
	return copied;
}
