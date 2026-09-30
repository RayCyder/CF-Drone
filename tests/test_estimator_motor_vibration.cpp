#include "Arduino.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../vector.h"
#include "../quaternion.h"

double t = 0.0;
float dt = 0.001f;
float controlRoll = 0.0f, controlPitch = 0.0f;
Vector rates, gyro, acc;
Quaternion attitude;
bool landed = false;
bool armed = false;
static bool motorOutputActive = false;
bool motorsActive() { return motorOutputActive; }
void applyGyro();
void applyAcc();
void applyLevel();

#include "../estimate.ino"

struct ImuSample {
	Vector gyro;
	Vector acc;
};

int main() {
	std::string testPath = __FILE__;
	const size_t slash = testPath.find_last_of('/');
	assert(slash != std::string::npos);
	const std::string capturePath = testPath.substr(0, slash) +
		"/../data/attitude/motor-fr-vqf-repeat-20260930.csv";
	FILE *capture = fopen(capturePath.c_str(), "r");
	assert(capture != nullptr);
	char line[256];
	assert(fgets(line, sizeof(line), capture) != nullptr); // CSV header
	std::vector<ImuSample> vibration;
	Vector gyroMean, accMean;
	int row = 0;
	while (fgets(line, sizeof(line), capture)) {
		unsigned timeUs;
		float gx, gy, gz, ax, ay, az;
		if (sscanf(line, "%u,%f,%f,%f,%f,%f,%f", &timeUs, &gx, &gy, &gz, &ax, &ay, &az) != 7) continue;
		if (row++ < 450) continue; // exclude the pre-motor static portion
		vibration.push_back({Vector(gx, gy, gz), Vector(ax, ay, az)});
		gyroMean += Vector(gx, gy, gz);
		accMean += Vector(ax, ay, az);
	}
	fclose(capture);
	assert(vibration.size() > 500);
	gyroMean = gyroMean / (float)vibration.size();
	accMean = accMean / (float)vibration.size();
	for (ImuSample &sample : vibration) {
		sample.gyro -= gyroMean;
		sample.acc -= accMean;
	}

	const float initialRoll = radians(-1.76f);
	const float initialPitch = radians(-0.09f);
	attitude = Quaternion::fromEuler(Vector(initialRoll, initialPitch, 0.0f));
	const Vector gravityBody = Quaternion::rotateVector(Vector(0.0f, 0.0f, ONE_G), attitude);
	levelWeight = 0.0f;
	levelBiasGain = 0.0f;
	levelGyroBias = Vector();
	accelerationFusionFilter.reset();
	ratesFilter.reset();
	armed = false;
	motorOutputActive = false;

	float rollSum = 0.0f, pitchSum = 0.0f;
	int measuredSamples = 0;
	for (int i = 0; i < 10000; ++i) {
		dt = 0.001f;
		if (i < 400) {
			gyro = Vector();
			acc = gravityBody;
		} else {
			armed = true;
			motorOutputActive = true;
			const ImuSample &sample = vibration[(i - 400) % vibration.size()];
			gyro = sample.gyro;
			acc = gravityBody + sample.acc;
		}
		estimate();
		if (i >= 7000) {
			const Vector euler = attitude.toEuler();
			rollSum += degrees(euler.x);
			pitchSum += degrees(euler.y);
			++measuredSamples;
		}
	}
	const float rollMean = rollSum / measuredSamples;
	const float pitchMean = pitchSum / measuredSamples;
	assert(fabsf(rollMean - (-1.76f)) < 0.5f);
	assert(fabsf(pitchMean - (-0.09f)) < 0.2f);

	// Replay measured FR vibration over a known dynamic trajectory. This bounds
	// tracking error as well as static drift, so extra smoothing alone cannot pass.
	attitude = Quaternion::fromEuler(Vector(initialRoll, initialPitch, 0.0f));
	levelGyroBias = Vector();
	levelWeight = 0.0f;
	levelBiasGain = 0.0f;
	accelerationFusionFilter.reset();
	ratesFilter.reset();
	armed = false;
	motorOutputActive = false;
	float squaredError[3] = {0.0f, 0.0f, 0.0f};
	int dynamicSamples = 0;
	for (int i = 0; i < 10000; ++i) {
		const float seconds = i * 0.001f;
		const float motionTime = fmaxf(0.0f, seconds - 1.0f);
		const float rollPhase = 2.0f * PI * 0.35f * motionTime;
		const float pitchPhase = 2.0f * PI * 0.23f * motionTime;
		const float yawPhase = 2.0f * PI * 0.17f * motionTime;
		const float trueRoll = initialRoll + radians(8.0f) * (1.0f - cosf(rollPhase));
		const float truePitch = initialPitch + radians(5.0f) * (1.0f - cosf(pitchPhase));
		const float trueYaw = radians(12.0f) * (1.0f - cosf(yawPhase));
		const float rollRate = radians(8.0f) * 2.0f * PI * 0.35f * sinf(rollPhase);
		const float pitchRate = radians(5.0f) * 2.0f * PI * 0.23f * sinf(pitchPhase);
		const float yawRate = radians(12.0f) * 2.0f * PI * 0.17f * sinf(yawPhase);
		const Vector bodyRate(
			rollRate - yawRate * sinf(truePitch),
			pitchRate * cosf(trueRoll) + yawRate * sinf(trueRoll) * cosf(truePitch),
			-pitchRate * sinf(trueRoll) + yawRate * cosf(trueRoll) * cosf(truePitch));
		const Quaternion trueAttitude = Quaternion::fromEuler(Vector(trueRoll, truePitch, trueYaw));
		gyro = bodyRate;
		acc = Quaternion::rotateVector(Vector(0.0f, 0.0f, ONE_G), trueAttitude);
		dt = 0.001f;
		if (i >= 1000) {
			armed = true;
			motorOutputActive = true;
			const ImuSample &sample = vibration[(i - 1000) % vibration.size()];
			gyro += sample.gyro;
			acc += sample.acc;
		}
		estimate();
		if (i >= 2000) {
			const Vector estimated = attitude.toEuler();
			const float error[3] = {
				static_cast<float>(degrees(estimated.x - trueRoll)),
				static_cast<float>(degrees(estimated.y - truePitch)),
				static_cast<float>(degrees(estimated.z - trueYaw))};
			for (int axis = 0; axis < 3; ++axis) squaredError[axis] += error[axis] * error[axis];
			++dynamicSamples;
		}
	}
	const float rollRmse = sqrtf(squaredError[0] / dynamicSamples);
	const float pitchRmse = sqrtf(squaredError[1] / dynamicSamples);
	const float yawRmse = sqrtf(squaredError[2] / dynamicSamples);
	printf("FR vibration dynamic RMSE (deg): roll %.3f pitch %.3f yaw %.3f\n",
		rollRmse, pitchRmse, yawRmse);
	assert(rollRmse < 0.5f);
	assert(pitchRmse < 0.8f);
	assert(yawRmse < 0.5f);
}
