#pragma once

#include <stdint.h>
#include "control.h"
#include "web_rc_fast_stop_policy.h"

WebRCFastStopAction consumeWebRCFastStop();

struct WebRCInputEvent {
	uint8_t type;
	float roll, pitch, yaw, throttle;
	int8_t buttonIndex;
	int8_t buttonState;
	uint32_t receivedAt;
};

struct OpenLoopStep {
	float duration, throttle, roll, pitch, yaw;
};
