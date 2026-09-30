#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "../descent_calibration.h"

static DescentCalibrationSample sample(float thrust, uint16_t faults = 0, int16_t roll = 0) {
	DescentCalibrationSample value{};
	value.thrustCenti = (uint16_t)lroundf(thrust * 100.0f);
	value.batteryMv = 3800;
	value.rollCentiDeg = roll;
	value.pitchCentiDeg = 0;
	value.rcThrottleCenti = 3000;
	value.faults = faults;
	value.mode = 2;
	value.controlSource = 2;
	return value;
}

static void steady_capture_and_summary() {
	DescentCalibrationRecorder recorder;
	recorder.start(1000);
	for (uint32_t ms = 1000; ms <= 2000; ms += 50) recorder.tick(ms, sample(.35f));
	recorder.stop(2000);
	const auto result = recorder.summary(2000);
	assert(result.state == DESCENT_CALIBRATION_COMPLETE);
	assert(result.usable && result.sampleCount == 21 && result.durationMs == 1000);
	assert(fabsf(result.medianThrust - .35f) < .011f);
	assert(fabsf(result.meanBatteryV - 3.8f) < .001f);
	assert(result.maxTiltDeg == 0 && result.faults == 0);
	DescentCalibrationSample copied{};
	assert(recorder.copySample(10, copied) && copied.elapsedMs == 500);
}

static void quality_and_lifecycle_checks() {
	DescentCalibrationRecorder recorder;
	recorder.start(0);
	for (uint32_t ms = 0; ms <= 1000; ms += 50) recorder.tick(ms, sample((ms < 500) ? .2f : .4f));
	recorder.stop(1000);
	assert(!recorder.summary(1000).usable);

	recorder.start(2000);
	for (uint32_t ms = 2000; ms <= 3000; ms += 50) recorder.tick(ms, sample(.3f, 4));
	recorder.stop(3000);
	assert(recorder.summary(3000).faults == 4 && !recorder.summary(3000).usable);

	recorder.start(UINT32_MAX - 500);
	for (uint32_t elapsed = 0; elapsed <= 1000; elapsed += 50)
		recorder.tick((uint32_t)(UINT32_MAX - 500 + elapsed), sample(.3f));
	recorder.stop((uint32_t)(UINT32_MAX - 500 + 1000));
	assert(recorder.summary(500).usable && recorder.summary(500).durationMs == 1000);

	recorder.clear();
	assert(recorder.summary(0).state == DESCENT_CALIBRATION_EMPTY);
	recorder.start(0);
	for (uint32_t ms = 0; ms <= DESCENT_CALIBRATION_MAX_MS; ms += 50) recorder.tick(ms, sample(.3f));
	assert(recorder.summary(DESCENT_CALIBRATION_MAX_MS).state == DESCENT_CALIBRATION_COMPLETE);
	assert(recorder.summary(DESCENT_CALIBRATION_MAX_MS).reason != nullptr);
}

int main() {
	static_assert(DescentCalibrationRecorder::CAPACITY == 600, "30 s at 20 Hz");
	steady_capture_and_summary();
	quality_and_lifecycle_checks();
	puts("descent calibration recorder regression: PASS");
}
