#pragma once

#include <stdint.h>

struct SystemLogEvent {
	uint32_t sequence;
	uint32_t uptimeMs;
	char tag[12];
	char message[44];
};

void recordSystemLogEvent(const char *tag, const char *message);
void initializeSystemLog();
bool tryArmWithSystemLog();
bool clearSystemLogHistory();
uint32_t getSystemLogBootId();
int copySystemLogEventsAfter(uint32_t sequence, SystemLogEvent *destination, int capacity);
