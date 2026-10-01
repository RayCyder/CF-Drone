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

#define EST_RAW_ACCEL_NORM_TOLERANCE 0.05f
#include "../estimate.ino"

struct ImuSample { Vector gyro; Vector acc; };

static std::vector<ImuSample> readVibration(const std::string &path) {
	FILE *capture = fopen(path.c_str(), "r");
	assert(capture != nullptr);
	char line[256];
	assert(fgets(line, sizeof(line), capture) != nullptr);
	std::vector<ImuSample> samples;
	Vector gyroMean, accMean;
	int row = 0;
	while (fgets(line, sizeof(line), capture)) {
		unsigned timeUs;
		float gx, gy, gz, ax, ay, az;
		if (sscanf(line, "%u,%f,%f,%f,%f,%f,%f", &timeUs, &gx, &gy, &gz,
			&ax, &ay, &az) != 7) continue;
		if (row++ < 450) continue;
		ImuSample sample{Vector(gx, gy, gz), Vector(ax, ay, az)};
		samples.push_back(sample);
		gyroMean += sample.gyro;
		accMean += sample.acc;
	}
	fclose(capture);
	assert(samples.size() > 500);
	gyroMean = gyroMean / static_cast<float>(samples.size());
	accMean = accMean / static_cast<float>(samples.size());
	for (ImuSample &sample : samples) {
		sample.gyro -= gyroMean;
		sample.acc -= accMean;
	}
	return samples;
}

static void verifyDynamicReplay(const std::string &path) {
	const std::vector<ImuSample> vibration = readVibration(path);
	const float initialRoll = radians(-1.76f);
	const float initialPitch = radians(-0.09f);
	attitude = Quaternion::fromEuler(Vector(initialRoll, initialPitch, 0.0f));
	accWeight = 0.0005f;
	levelWeight = 0.0f;
	levelBiasGain = 0.0f;
	levelGyroBias = Vector();
	accelerationFusionFilter.reset();
	ratesFilter.reset();
	armed = false;
	motorOutputActive = false;
	float squaredError[3] = {};
	int measuredSamples = 0;
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
		gyro = Vector(rollRate - yawRate * sinf(truePitch),
			pitchRate * cosf(trueRoll) + yawRate * sinf(trueRoll) * cosf(truePitch),
			-pitchRate * sinf(trueRoll) + yawRate * cosf(trueRoll) * cosf(truePitch));
		const Quaternion truth = Quaternion::fromEuler(Vector(trueRoll, truePitch, trueYaw));
		acc = Quaternion::rotateVector(Vector(0.0f, 0.0f, ONE_G), truth);
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
			float error[3] = {
				static_cast<float>(degrees(estimated.x - trueRoll)),
				static_cast<float>(degrees(estimated.y - truePitch)),
				static_cast<float>(degrees(estimated.z - trueYaw))};
			while (error[2] > 180.0f) error[2] -= 360.0f;
			while (error[2] < -180.0f) error[2] += 360.0f;
			for (int axis = 0; axis < 3; ++axis) squaredError[axis] += error[axis] * error[axis];
			++measuredSamples;
		}
	}
	const float rollRmse = sqrtf(squaredError[0] / measuredSamples);
	const float pitchRmse = sqrtf(squaredError[1] / measuredSamples);
	const float yawRmse = sqrtf(squaredError[2] / measuredSamples);
	printf("candidate %s RMSE (deg): roll %.3f pitch %.3f yaw %.3f\n",
		path.substr(path.find_last_of('/') + 1).c_str(), rollRmse, pitchRmse, yawRmse);
	assert(rollRmse < 0.25f);
	assert(pitchRmse < 0.25f);
	assert(yawRmse < 0.60f);
}

int main() {
	const std::string data = __FILE__;
	const std::string root = data.substr(0, data.find_last_of('/')) + "/../data/attitude/";
	for (const char *capture : {
		"motor-fr-20261001-001156.csv", "motor-fr-20261001-001909.csv",
		"motor-fr-20261001-001918.csv", "motor-fl-20261001-001301.csv",
		"motor-rl-20261001-001310.csv", "motor-rr-20261001-081759.csv"}) {
		verifyDynamicReplay(root + capture);
	}
}
