#include "../range_reference.h"

#include <cassert>
#include <cmath>

int main() {
	RangeReferenceTracker tracker;
	for (int i = 0; i < 20; ++i) {
		const float ground = 0.007f + (i % 3 - 1) * 0.0005f;
		tracker.update(ground, true, false, 0.0f, true);
	}
	assert(tracker.state().source == RangeReferenceSource::GroundBaseline);
	assert(tracker.state().groundBaselineMeters > 0.006f);
	const auto &airborne = tracker.update(0.107f, true, true, 0.0f, true);
	assert(airborne.relativeValid);
	assert(std::fabs(airborne.relativeHeightMeters - 0.100f) < 0.002f);
	assert(rangeReferenceFusionUsable(airborne, true));
	assert(std::fabs(airborne.rawAglMeters - 0.107f) < 1e-6f);

	// A unit powered at 10 cm must not mistake that fixture height for ground.
	RangeReferenceTracker flightAttach;
	for (int i = 0; i < 10; ++i) flightAttach.update(0.007f, true, false, 0.0f, true);
	for (int i = 0; i < 30; ++i) flightAttach.update(0.017f, true, false, 0.0f, true);
	assert(flightAttach.state().source == RangeReferenceSource::None);
	assert(flightAttach.state().groundSampleCount == 0);
	for (int i = 0; i < 30; ++i) flightAttach.update(0.030f, true, false, 0.0f, true);
	assert(flightAttach.state().source == RangeReferenceSource::None);
	assert(flightAttach.state().groundSampleCount == 0);
	for (int i = 0; i < 30; ++i) flightAttach.update(0.100f, true, false, 0.0f, true);
	assert(flightAttach.state().source == RangeReferenceSource::None);
	assert(flightAttach.state().groundSampleCount == 0);
	const auto &attached = flightAttach.update(0.120f, true, true, 0.42f, true);
	assert(attached.source == RangeReferenceSource::FlightAligned);
	assert(std::fabs(attached.relativeHeightMeters - 0.42f) < 1e-6f);
	const auto &climbed = flightAttach.update(0.170f, true, true, 0.43f, true);
	assert(std::fabs(climbed.relativeHeightMeters - 0.47f) < 1e-6f);

	RangeReferenceTracker nearGroundAttach;
	const auto &nearGround = nearGroundAttach.update(0.008f, true, true, 0.0f, true);
	assert(nearGround.source == RangeReferenceSource::FlightAligned);
	assert(std::fabs(nearGround.relativeHeightMeters) < 1e-6f);
	assert(!rangeReferenceFusionUsable(nearGround, true));
	const auto &nearGroundClimbed = nearGroundAttach.update(0.108f, true, true, 0.02f, true);
	assert(std::fabs(nearGroundClimbed.relativeHeightMeters - 0.100f) < 1e-6f);
	assert(rangeReferenceFusionUsable(nearGroundClimbed, true));

	flightAttach.update(0.0f, false, true, 0.43f, true);
	assert(!flightAttach.state().relativeValid);
}
