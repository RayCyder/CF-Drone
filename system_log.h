#pragma once

#include <stdint.h>

struct SystemLogEvent {
	uint32_t sequence;
	uint32_t uptimeMs;
	char tag[12];
	char message[44];
	uint32_t bootId; // 0 means an event migrated from a legacy boot of unknown identity.
};

void recordSystemLogEvent(const char *tag, const char *message);
void initializeSystemLog();
void startPersistentWriteTaskBeforeLoop();
bool tryArmWithSystemLog();
bool systemLogArmingBlocked();
bool beginPersistentWriteBatch();
bool systemLogPersistencePending();
bool persistSystemLogInBatch();
void finishPersistentWriteBatch(bool wroteAny);
bool clearSystemLogHistory();
uint32_t getSystemLogBootId();
int copySystemLogEventsAfter(uint32_t sequence, SystemLogEvent *destination, int capacity);
