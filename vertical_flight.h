#pragma once

#include <stdint.h>

struct VerticalFlightState {
	float altitudeMeters = 0.0f;
	float verticalSpeedMps = 0.0f;
	float verticalAccelerationMps2 = 0.0f;
	float rangeAglMeters = 0.0f;
	float flowPositionXMeters = 0.0f;
	float flowPositionYMeters = 0.0f;
	float flowVelocityXMps = 0.0f;
	float flowVelocityYMps = 0.0f;
	float altitudeTargetMeters = 0.0f;
	float verticalSpeedTargetMps = 0.0f;
	float thrustCommand = 0.0f;
	uint32_t timestampUs = 0;
	uint32_t barometerAgeMs = UINT32_MAX;
	uint32_t rangeAgeMs = UINT32_MAX;
	uint32_t flowAgeMs = UINT32_MAX;
	uint8_t heightSource = 0;
	uint8_t flowQuality = 0;
	bool healthy = false;
	bool degraded = false;
	bool rangeValid = false;
	bool flowValid = false;
	bool controlActive = false;
};

void updateVerticalFlightState();
bool getVerticalFlightState(VerticalFlightState &state);
bool verticalFlightHealthy();
bool enterAltitudeHold(float currentThrust);
void leaveAltitudeHold();
bool applyAltitudeHoldControl(float throttleInput, float hoverThrottleInput,
	float hoverThrust, float &thrust);
void setRouteNavigationTarget(float altitudeMeters, float headingRadians, bool valid);
void clearRouteNavigationTarget();
bool routeNavigationTarget(float &altitudeMeters, float &headingRadians);
bool applyRouteAltitudeControl(float hoverThrust, float &thrust);
bool resetNavigationOrigin(bool taskActive);
