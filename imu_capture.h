#pragma once

#include <stdint.h>
#include <math.h>
#include <stdlib.h>

// One second of body-frame IMU samples at the nominal 1 kHz loop rate.
// The fixed-size buffer avoids heap use in the flight loop.
static const uint16_t IMU_CAPTURE_CAPACITY = 1024;
static const size_t IMU_CAPTURE_MIN_FREE_HEAP = 48 * 1024;
static const size_t IMU_CAPTURE_RAW_MIN_FREE_HEAP = 64 * 1024;

struct ImuCaptureSample {
	uint32_t timeUs;
	int32_t gyroMicroRadPerSec[3];
	int16_t accCentiMetersPerSec2[3];
	int16_t temperatureCentiC;
};
static_assert(sizeof(ImuCaptureSample) * IMU_CAPTURE_CAPACITY <= 24 * 1024,
	"IMU capture RAM budget");

enum ImuCaptureState : uint8_t {
	IMU_CAPTURE_IDLE,
	IMU_CAPTURE_RUNNING,
	IMU_CAPTURE_READY
};

class ImuCaptureBuffer {
public:
	~ImuCaptureBuffer() { free(samples_); free(rawGyroMicroRadPerSec_); }
	ImuCaptureBuffer() = default;
	ImuCaptureBuffer(const ImuCaptureBuffer &) = delete;
	ImuCaptureBuffer &operator=(const ImuCaptureBuffer &) = delete;

	bool start(bool isArmed, bool areMotorsActive, size_t freeHeapBytes, bool captureRawGyro = false) {
		const size_t minimumFreeHeap = captureRawGyro
			? IMU_CAPTURE_RAW_MIN_FREE_HEAP : IMU_CAPTURE_MIN_FREE_HEAP;
		if (isArmed || areMotorsActive || freeHeapBytes < minimumFreeHeap) return false;
		if (!samples_) samples_ = (ImuCaptureSample *)malloc(sizeof(ImuCaptureSample) * IMU_CAPTURE_CAPACITY);
		if (!samples_) return false;
		if (captureRawGyro && !rawGyroMicroRadPerSec_) {
			rawGyroMicroRadPerSec_ = (int32_t *)malloc(sizeof(int32_t) * IMU_CAPTURE_CAPACITY * 3);
			if (!rawGyroMicroRadPerSec_) return false;
		}
		captureRawGyro_ = captureRawGyro;
		count_ = 0;
		state_ = IMU_CAPTURE_RUNNING;
		return true;
	}

	bool stop() {
		if (state_ != IMU_CAPTURE_RUNNING) return false;
		state_ = count_ ? IMU_CAPTURE_READY : IMU_CAPTURE_IDLE;
		return true;
	}

	void append(uint32_t timeUs, float gx, float gy, float gz,
		float ax, float ay, float az, float temperatureC,
		float rawGx = 0.0f, float rawGy = 0.0f, float rawGz = 0.0f) {
		if (state_ != IMU_CAPTURE_RUNNING) return;
		if (count_ >= IMU_CAPTURE_CAPACITY) {
			state_ = IMU_CAPTURE_READY;
			return;
		}
		ImuCaptureSample &sample = samples_[count_++];
		sample.timeUs = timeUs;
		sample.gyroMicroRadPerSec[0] = toInt32(gx * 1000000.0f);
		sample.gyroMicroRadPerSec[1] = toInt32(gy * 1000000.0f);
		sample.gyroMicroRadPerSec[2] = toInt32(gz * 1000000.0f);
		sample.accCentiMetersPerSec2[0] = toInt16(ax * 100.0f);
		sample.accCentiMetersPerSec2[1] = toInt16(ay * 100.0f);
		sample.accCentiMetersPerSec2[2] = toInt16(az * 100.0f);
		sample.temperatureCentiC = toInt16(temperatureC * 100.0f);
		if (captureRawGyro_ && rawGyroMicroRadPerSec_) {
			const size_t offset = (size_t)(count_ - 1) * 3;
			rawGyroMicroRadPerSec_[offset] = toInt32(rawGx * 1000000.0f);
			rawGyroMicroRadPerSec_[offset + 1] = toInt32(rawGy * 1000000.0f);
			rawGyroMicroRadPerSec_[offset + 2] = toInt32(rawGz * 1000000.0f);
		}
		if (count_ == IMU_CAPTURE_CAPACITY) state_ = IMU_CAPTURE_READY;
	}

	ImuCaptureState state() const { return state_; }
	uint16_t size() const { return count_; }
	bool capturesRawGyro() const { return captureRawGyro_ && rawGyroMicroRadPerSec_; }
	bool copy(uint16_t index, ImuCaptureSample &sample) const {
		if (!samples_ || index >= count_) return false;
		sample = samples_[index];
		return true;
	}
	bool copyRawGyro(uint16_t index, int32_t destination[3]) const {
		if (!capturesRawGyro() || index >= count_) return false;
		const size_t offset = (size_t)index * 3;
		for (size_t axis = 0; axis < 3; ++axis) destination[axis] = rawGyroMicroRadPerSec_[offset + axis];
		return true;
	}
	void release() {
		free(samples_);
		free(rawGyroMicroRadPerSec_);
		samples_ = nullptr;
		rawGyroMicroRadPerSec_ = nullptr;
		count_ = 0;
		state_ = IMU_CAPTURE_IDLE;
		captureRawGyro_ = false;
	}

private:
	static int32_t toInt32(float value) {
		if (!isfinite(value)) return 0;
		if (value > 2147483000.0f) return 2147483000;
		if (value < -2147483000.0f) return -2147483000;
		return (int32_t)roundf(value);
	}
	static int16_t toInt16(float value) {
		if (!isfinite(value)) return 0;
		if (value > 32767.0f) return 32767;
		if (value < -32768.0f) return -32768;
		return (int16_t)roundf(value);
	}

	ImuCaptureSample *samples_ = nullptr;
	int32_t *rawGyroMicroRadPerSec_ = nullptr;
	uint16_t count_ = 0;
	ImuCaptureState state_ = IMU_CAPTURE_IDLE;
	bool captureRawGyro_ = false;
};
