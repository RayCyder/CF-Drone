#include "../navigation_heading.h"

#include <cassert>
#include <cmath>

int main() {
	NavigationHeadingEstimator estimator;
	estimator.predict(0.25f, 1000);
	assert(!estimator.state().trusted);
	assert(estimator.observe(1.0f, true, 2000, true));
	estimator.predict(0.25f, 2000);
	assert(estimator.state().trusted);
	assert(fabsf(estimator.state().fusedYawRadians - 1.0f) < 1e-5f);

	// A normal observation is rate limited instead of producing a jump.
	estimator.predict(0.26f, 22000);
	const float before = estimator.state().fusedYawRadians;
	assert(estimator.observe(1.1f, true, 22000, false));
	assert(fabsf(NavigationHeadingEstimator::wrap(estimator.state().fusedYawRadians - before)) < 0.02f);

	// A 180 degree magnetic discontinuity is rejected.
	estimator.predict(0.27f, 42000);
	assert(!estimator.observe(-2.0f, true, 42000, false));
	assert(!estimator.state().trusted);
	assert(estimator.state().rejectedCount == 1);
	estimator.predict(0.28f, 43000);
	assert(!estimator.state().trusted); // Rejection remains latched until a new sample is accepted.
	assert(fabsf(NavigationHeadingEstimator::wrap(estimator.state().fusedYawRadians - 1.03f)) < 0.02f);
	assert(estimator.observe(1.03f, true, 62000, false));
	estimator.predict(0.29f, 62000);
	assert(estimator.state().trusted);

	// Freshness uses wrap-safe unsigned time arithmetic.
	estimator.reset();
	estimator.predict(0.0f, 0xfffffff0u);
	assert(estimator.observe(0.0f, true, 0xfffffff0u, true));
	estimator.predict(0.0f, 0x20u);
	assert(estimator.state().trusted);
	estimator.predict(0.0f, 0x20000u);
	assert(!estimator.state().trusted);

	MagHeadingLossGuard guard(500);
	assert(!guard.update(true, false, 100)); // A compatibility route never used magnetic heading.
	assert(!guard.update(true, true, 200));
	assert(!guard.update(true, false, 300));
	assert(!guard.update(true, false, 799));
	assert(guard.update(true, false, 800));
	assert(!guard.update(false, false, 900));
	assert(!guard.update(true, false, 1500));
}
