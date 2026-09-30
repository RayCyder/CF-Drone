// Pilot-operated descent calibration recorder. This module observes only; it never commands flight.
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "descent_calibration.h"
#include "control.h"
#include "diagnostics.h"

extern bool armed;
extern int mode;
extern float thrustTarget, batteryVoltage, controlThrottle;
extern Quaternion attitude;
extern bool isControlledLandingActive();
extern bool motorsActive();
extern uint32_t getActiveDiagnosticFaults();
extern ControlSource getCurrentControlSource();

static DescentCalibrationRecorder descentCalibration;
static portMUX_TYPE descentCalibrationMux = portMUX_INITIALIZER_UNLOCKED;

static int16_t centiDegrees(float value) {
	if (!isfinite(value)) return INT16_MAX;
	const float scaled = value * 5729.57795f;
	if (scaled > INT16_MAX) return INT16_MAX;
	if (scaled < INT16_MIN) return INT16_MIN;
	return (int16_t)lroundf(scaled);
}

bool startDescentCalibration() {
	if (!armed || mode != MODE_STAB || isControlledLandingActive()) return false;
	portENTER_CRITICAL(&descentCalibrationMux);
	if (descentCalibration.state() == DESCENT_CALIBRATION_RECORDING) {
		portEXIT_CRITICAL(&descentCalibrationMux);
		return false;
	}
	descentCalibration.start(millis());
	portEXIT_CRITICAL(&descentCalibrationMux);
	return true;
}

bool stopDescentCalibration() {
	portENTER_CRITICAL(&descentCalibrationMux);
	const bool wasRecording = descentCalibration.state() == DESCENT_CALIBRATION_RECORDING;
	if (wasRecording) descentCalibration.stop(millis());
	portEXIT_CRITICAL(&descentCalibrationMux);
	return wasRecording;
}

void clearDescentCalibration() {
	if (armed || motorsActive()) return;
	portENTER_CRITICAL(&descentCalibrationMux);
	descentCalibration.clear();
	portEXIT_CRITICAL(&descentCalibrationMux);
}

void recordDescentCalibrationSample() {
	const uint32_t now = millis();
	portENTER_CRITICAL(&descentCalibrationMux);
	const bool due = descentCalibration.sampleDue(now);
	portEXIT_CRITICAL(&descentCalibrationMux);
	if (!due) return;
	const Vector angles = attitude.toEuler();
	DescentCalibrationSample sample = {};
	sample.thrustCenti = isfinite(thrustTarget) && thrustTarget > 0.0f
		? (uint16_t)lroundf(constrain(thrustTarget, 0.0f, 1.0f) * 100.0f) : 0;
	sample.batteryMv = isfinite(batteryVoltage) && batteryVoltage > 0.0f
		? (uint16_t)constrain(lroundf(batteryVoltage * 1000.0f), 0L, 65535L) : 0;
	sample.rollCentiDeg = centiDegrees(angles.x);
	sample.pitchCentiDeg = centiDegrees(angles.y);
	sample.rcThrottleCenti = isfinite(controlThrottle)
		? (uint16_t)lroundf(constrain(controlThrottle, 0.0f, 1.0f) * 10000.0f) : 0;
	sample.faults = (uint16_t)getActiveDiagnosticFaults();
	sample.mode = (uint8_t)mode;
	sample.controlSource = (uint8_t)getCurrentControlSource();
	portENTER_CRITICAL(&descentCalibrationMux);
	if (descentCalibration.state() == DESCENT_CALIBRATION_RECORDING) {
		if (!armed) descentCalibration.abort(now, "disarmed_before_stop");
		else if (mode != MODE_STAB || isControlledLandingActive()) descentCalibration.abort(now, "flight_mode_changed");
		else if (sample.faults) descentCalibration.abort(now, "flight_fault");
		else descentCalibration.tick(now, sample);
	}
	portEXIT_CRITICAL(&descentCalibrationMux);
}

DescentCalibrationSummary getDescentCalibrationSummary() {
	portENTER_CRITICAL(&descentCalibrationMux);
	DescentCalibrationSummary result = descentCalibration.summary(millis());
	if (result.state == DESCENT_CALIBRATION_COMPLETE && result.reason &&
		strcmp(result.reason, "complete") == 0 && !result.usable) {
		if (result.faults) result.reason = "flight_fault";
		else if (result.nonStabSamples) result.reason = "flight_mode_changed";
		else if (result.maxTiltDeg > 10.0f) result.reason = "excessive_tilt";
		else if (result.thrustP90MinusP10 > 0.05f) result.reason = "unstable_thrust";
		else result.reason = "too_few_samples";
	}
	portEXIT_CRITICAL(&descentCalibrationMux);
	return result;
}

bool copyDescentCalibrationSample(uint16_t index, DescentCalibrationSample &sample) {
	portENTER_CRITICAL(&descentCalibrationMux);
	const bool copied = descentCalibration.copySample(index, sample);
	portEXIT_CRITICAL(&descentCalibrationMux);
	return copied;
}
