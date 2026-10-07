#pragma once

#include <math.h>
#include <stdint.h>

enum class RangeReferenceSource : uint8_t {
	None = 0,
	GroundBaseline = 1,
	FlightAligned = 2,
};

struct RangeReferenceState {
	float rawAglMeters = 0.0f;
	float groundBaselineMeters = 0.0f;
	float relativeHeightMeters = 0.0f;
	float alignmentOffsetMeters = 0.0f;
	uint16_t groundSampleCount = 0;
	RangeReferenceSource source = RangeReferenceSource::None;
	bool rawValid = false;
	bool relativeValid = false;
};

inline bool rangeReferenceFusionUsable(const RangeReferenceState &state, bool armed,
	float minimumRelativeHeightMeters = 0.020f) {
	return armed && state.relativeValid && isfinite(state.relativeHeightMeters) &&
		state.relativeHeightMeters >= minimumRelativeHeightMeters;
}

class RangeReferenceTracker {
public:
	static constexpr float GROUND_CAPTURE_MIN_METERS = 0.002f;
	// The installed VL53L1X reports roughly 6-9 mm when the airframe is resting.
	// Keep the automatic ground window narrow so a short false return while the
	// aircraft is visibly elevated cannot be stored as the ground plane.
	static constexpr float GROUND_CAPTURE_MAX_METERS = 0.020f;
	static constexpr float GROUND_CAPTURE_MAX_SPREAD_METERS = 0.010f;
	static constexpr uint16_t GROUND_CAPTURE_SAMPLES = 20;

	const RangeReferenceState &update(float rawAglMeters, bool rawValid, bool armed,
		float currentRelativeHeightMeters, bool currentHeightValid) {
		state_.rawValid = rawValid && isfinite(rawAglMeters) && rawAglMeters > 0.0f;
		if (!state_.rawValid) {
			if (!armed && !groundBaselineValid_) resetGroundCollection();
			state_.relativeValid = false;
			return state_;
		}
		state_.rawAglMeters = rawAglMeters;
		if (!armed) {
			flightAlignmentValid_ = false;
			if (rawAglMeters >= GROUND_CAPTURE_MIN_METERS &&
				rawAglMeters <= GROUND_CAPTURE_MAX_METERS) collectGround(rawAglMeters);
			else if (!groundBaselineValid_) resetGroundCollection();
		}
		if (groundBaselineValid_) {
			state_.source = RangeReferenceSource::GroundBaseline;
			state_.groundBaselineMeters = groundBaselineMeters_;
			state_.relativeHeightMeters = fmaxf(0.0f, rawAglMeters - groundBaselineMeters_);
			state_.alignmentOffsetMeters = groundBaselineMeters_;
			state_.relativeValid = true;
			return state_;
		}
		if (armed && !flightAlignmentValid_ && currentHeightValid &&
			isfinite(currentRelativeHeightMeters)) {
			flightAlignmentOffsetMeters_ = rawAglMeters - currentRelativeHeightMeters;
			flightAlignmentValid_ = true;
		}
		if (flightAlignmentValid_) {
			state_.source = RangeReferenceSource::FlightAligned;
			state_.alignmentOffsetMeters = flightAlignmentOffsetMeters_;
			state_.relativeHeightMeters = rawAglMeters - flightAlignmentOffsetMeters_;
			state_.relativeValid = isfinite(state_.relativeHeightMeters);
		} else {
			state_.source = RangeReferenceSource::None;
			state_.relativeValid = false;
		}
		return state_;
	}

	const RangeReferenceState &state() const { return state_; }

	void reset() { *this = RangeReferenceTracker(); }

private:
	void resetGroundCollection() {
		groundCollecting_ = false;
		groundSamples_ = 0;
		groundSumMeters_ = 0.0f;
		state_.groundSampleCount = 0;
	}

	void collectGround(float value) {
		if (!groundCollecting_) {
			groundMinimumMeters_ = groundMaximumMeters_ = groundSumMeters_ = value;
			groundSamples_ = 1;
			groundCollecting_ = true;
		} else {
			groundSumMeters_ += value;
			++groundSamples_;
			if (value < groundMinimumMeters_) groundMinimumMeters_ = value;
			if (value > groundMaximumMeters_) groundMaximumMeters_ = value;
		}
		state_.groundSampleCount = groundSamples_;
		if (groundMaximumMeters_ - groundMinimumMeters_ > GROUND_CAPTURE_MAX_SPREAD_METERS) {
			resetGroundCollection();
			return;
		}
		if (groundSamples_ >= GROUND_CAPTURE_SAMPLES) {
			groundBaselineMeters_ = groundSumMeters_ / (float)groundSamples_;
			groundBaselineValid_ = true;
			state_.groundBaselineMeters = groundBaselineMeters_;
		}
	}

	RangeReferenceState state_;
	float groundSumMeters_ = 0.0f;
	float groundMinimumMeters_ = 0.0f;
	float groundMaximumMeters_ = 0.0f;
	float groundBaselineMeters_ = 0.0f;
	float flightAlignmentOffsetMeters_ = 0.0f;
	uint16_t groundSamples_ = 0;
	bool groundCollecting_ = false;
	bool groundBaselineValid_ = false;
	bool flightAlignmentValid_ = false;
};
