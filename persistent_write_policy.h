#pragma once

// Runtime NVS writes are allowed only with the flight controller disarmed and
// all motor outputs stopped. Boot-time initialization is handled separately.
inline bool persistentWritesAllowed(bool armed, bool motorsActive) {
	return !armed && !motorsActive;
}
