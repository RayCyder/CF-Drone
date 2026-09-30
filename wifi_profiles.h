#pragma once

#include <stdint.h>

static constexpr uint8_t WIFI_PROFILE_LIMIT = 4;
static constexpr uint32_t WIFI_PROFILE_MAGIC = 0x57494649UL;
static constexpr uint8_t WIFI_PROFILE_VERSION = 2;

struct __attribute__((packed)) WifiProfileRecord {
	uint8_t ssidLength;
	uint8_t passwordLength;
	char ssid[32];
	char password[63];
};

struct __attribute__((packed)) WifiProfileStore {
	uint32_t magic;
	uint8_t version;
	uint8_t count;
	uint32_t generation;
	WifiProfileRecord profiles[WIFI_PROFILE_LIMIT];
};
