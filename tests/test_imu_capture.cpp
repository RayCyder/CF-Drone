#include <cassert>
#include <cmath>
#include <cstdio>
#include "../imu_capture.h"

int main() {
	ImuCaptureBuffer capture;
	assert(capture.state() == IMU_CAPTURE_IDLE);
	assert(!capture.start(true, false, 100000));
	assert(!capture.start(false, true, 100000));
	assert(!capture.start(false, false, IMU_CAPTURE_MIN_FREE_HEAP - 1));
	assert(capture.start(false, false, 100000));
	capture.append(1234, 0.012345f, -0.25f, 1.5f, 0.01f, -0.3f, 9.81f);
	assert(capture.size() == 1);
	ImuCaptureSample sample;
	assert(capture.copy(0, sample));
	assert(sample.timeUs == 1234);
	assert(sample.gyroMicroRadPerSec[0] == 12345);
	assert(sample.gyroMicroRadPerSec[1] == -250000);
	assert(sample.accCentiMetersPerSec2[1] == -30);
	assert(capture.stop());
	assert(capture.state() == IMU_CAPTURE_READY);
	capture.append(5678, 1, 2, 3, 4, 5, 6);
	assert(capture.size() == 1);
	assert(capture.start(false, false, 100000));
	for (uint16_t i = 0; i < IMU_CAPTURE_CAPACITY; ++i)
		capture.append(i * 1000, 0, 0, 0, 0, 0, 9.80665f);
	assert(capture.state() == IMU_CAPTURE_READY);
	assert(capture.size() == IMU_CAPTURE_CAPACITY);
	assert(capture.copy(IMU_CAPTURE_CAPACITY - 1, sample));
	assert(sample.timeUs == (IMU_CAPTURE_CAPACITY - 1) * 1000u);
	assert(!capture.copy(IMU_CAPTURE_CAPACITY, sample));
	capture.release();
	assert(capture.state() == IMU_CAPTURE_IDLE && capture.size() == 0);
	puts("bounded IMU capture regression: PASS");
}
