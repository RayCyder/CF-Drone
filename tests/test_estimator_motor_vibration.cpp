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
}
