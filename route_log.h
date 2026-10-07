#pragma once

#include <stdint.h>

struct RouteLogSnapshot {
	uint32_t revision = 0;
	uint16_t step = 0;
	uint8_t schema = 0;
	uint8_t state = 0;
	uint8_t quality = 0;
	uint8_t terminationReason = 0;
	float targetAltitudeMeters = 0.0f;
	float actualAltitudeMeters = 0.0f;
	float targetYawRadians = 0.0f;
	float actualYawRadians = 0.0f;
	float flowXMeters = 0.0f;
	float flowYMeters = 0.0f;
};

bool getRouteLogSnapshot(RouteLogSnapshot &snapshot);
