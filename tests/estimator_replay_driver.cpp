#include "Arduino.h"
#include <cstdio>
#include <vector>
#include "vector.h"
#include "quaternion.h"

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

#ifndef ESTIMATOR_SOURCE
#error "ESTIMATOR_SOURCE must identify the estimator implementation to replay"
#endif
#include ESTIMATOR_SOURCE

struct Sample {
	uint32_t timeUs;
	Vector gyro;
	Vector acc;
};

int main(int argc, char **argv) {
	if (argc != 2 && argc != 5) return 2;
	if (argc == 5) {
		const Vector initialEuler(radians(atof(argv[2])), radians(atof(argv[3])), radians(atof(argv[4])));
		attitude = Quaternion::fromEuler(initialEuler);
	}
	FILE *file = fopen(argv[1], "r");
	if (!file) return 3;
	char line[512];
	if (!fgets(line, sizeof(line), file)) return 4;
	std::vector<Sample> samples;
	while (fgets(line, sizeof(line), file)) {
		unsigned long timeUs;
		float gx, gy, gz, ax, ay, az;
		if (sscanf(line, "%lu,%f,%f,%f,%f,%f,%f", &timeUs,
			&gx, &gy, &gz, &ax, &ay, &az) == 7) {
			samples.push_back({(uint32_t)timeUs, Vector(gx, gy, gz), Vector(ax, ay, az)});
		}
	}
	fclose(file);
	if (samples.size() < 500) return 5;

	levelWeight = 0.0f;
	levelBiasGain = 0.0f;
	levelGyroBias = Vector();
	accWeight = 0.003f;
	for (size_t i = 0; i < samples.size(); ++i) {
		dt = i ? (uint32_t)(samples[i].timeUs - samples[i - 1].timeUs) * 1e-6f : 0.001f;
		if (!(dt > 0.0f && dt < 0.01f)) dt = 0.001f;
		t += dt;
		gyro = samples[i].gyro;
		acc = samples[i].acc;
		// The first 450 samples are stationary pre-roll. Replay the motor window
		// as armed and motor-active so both estimators exercise airborne fusion.
		armed = i >= 450;
		motorOutputActive = i >= 450;
		estimate();
		if (i >= 350) {
			const Vector euler = attitude.toEuler();
			printf("%zu,%.8f,%.8f,%.8f\n", i,
				degrees(euler.x), degrees(euler.y), degrees(euler.z));
		}
	}
	return 0;
}
