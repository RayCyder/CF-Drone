#include <cassert>
#include <cstdio>

#include "../navigation_origin.h"

int main() {
	NavigationOriginState state;
	state.xMeters = 3.0f;
	state.yMeters = -4.0f;
	state.vxMps = 0.5f;
	state.vyMps = -0.2f;
	state.reset();
	assert(state.xMeters == 0.0f && state.yMeters == 0.0f);
	assert(state.vxMps == 0.0f && state.vyMps == 0.0f);
	state.xMeters = -7.0f;
	state.yMeters = 8.0f;
	state.reset();
	assert(state.xMeters == 0.0f && state.yMeters == 0.0f);
	puts("navigation origin reset regression: PASS");
}
