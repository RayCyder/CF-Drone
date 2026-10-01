#include <cassert>
#include <cmath>
#include <cstdio>
#include "Arduino.h"
#include "../vector.h"
#include "../lpf.h"

int main() {
	LowPassFilter<float> nominal(0.2f);
	LowPassFilter<float> jittered(0.2f);
	assert(nominal.update(0.0f) == 0.0f);
	assert(jittered.update(0.0f) == 0.0f);
	assert(fabsf(nominal.update(1.0f, 0.001f, 0.001f) - 0.2f) < 1e-6f);
	for (int i = 1; i < 10; ++i) nominal.update(1.0f, 0.001f, 0.001f);
	for (int i = 0; i < 20; ++i) jittered.update(1.0f, 0.0005f, 0.001f);
	assert(fabsf(nominal.output - jittered.output) < 0.002f);
	assert(fabsf(nominal.alpha - 0.2f) < 1e-7f); // configured nominal coefficient is stable

	LowPassFilter<Vector> vectorFilter(0.2f);
	vectorFilter.update(Vector(0, 0, 0));
	const Vector first = vectorFilter.update(Vector(1, -1, 0.5f), 0.001f, 0.001f);
	assert(fabsf(first.x - 0.2f) < 1e-6f && fabsf(first.y + 0.2f) < 1e-6f);

	LowPassFilter<float> stalled(0.2f);
	assert(stalled.update(0.0f) == 0.0f);
	const float afterStall = stalled.update(1.0f, 0.050f, 0.001f);
	assert(fabsf(afterStall - 1.0f) < 1e-6f); // long dt consumes the newest sample without overshoot
	puts("dt-aware filter regression: PASS");
}
