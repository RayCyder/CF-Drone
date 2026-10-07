#pragma once

struct NavigationOriginState {
	float xMeters = 0.0f;
	float yMeters = 0.0f;
	float vxMps = 0.0f;
	float vyMps = 0.0f;

	void reset() {
		xMeters = 0.0f;
		yMeters = 0.0f;
		vxMps = 0.0f;
		vyMps = 0.0f;
	}
};
