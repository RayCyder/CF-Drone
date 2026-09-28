// Persistent bounded history: up to six recent stalls plus six ordinary events.
// When no stall exists, all twelve slots hold the newest ordinary events.
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>
#include "system_log.h"

static const uint32_t SYSTEM_LOG_MAGIC = 0x534C4F47;
static const int LOG_CAPACITY = 12;
static const int STALL_CAPACITY = 6;
static const char *STALL_TAG = "SLOW_LOOP";
static const char *STALL_DIAG_PREFIX = "ACTIVE LOOP";

struct PersistedSystemLog {
	uint32_t magic;
	uint32_t nextSequence;
	uint8_t eventCount;
	uint8_t normalCount;
	uint8_t normalNext;
	uint8_t stallCount;
	uint8_t stallNext;
	SystemLogEvent events[LOG_CAPACITY];
};

static PersistedSystemLog history = {};
static Preferences logStorage;
static bool storageReady = false;
static bool historyDirty = false;
static bool persistenceBusy = false;
static bool persistedWhileDisarmed = false;
static uint32_t systemLogBootId = 0;
static uint32_t armBlockedUntilMs = 0;
static uint32_t persistenceRetryAfterMs = 0;
static portMUX_TYPE systemLogMux = portMUX_INITIALIZER_UNLOCKED;

extern bool armed;
extern bool motorTestActive;

static void systemLogPersistenceTask(void *argument) {
	(void)argument;
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
		PersistedSystemLog snapshot;
		bool shouldPersist = false;
		portENTER_CRITICAL(&systemLogMux);
		const uint32_t now = millis();
		if (storageReady && historyDirty && !armed && !motorTestActive && !persistenceBusy &&
			!persistedWhileDisarmed && (int32_t)(now - persistenceRetryAfterMs) >= 0) {
			persistenceBusy = true;
			persistedWhileDisarmed = true;
			historyDirty = false;
			snapshot = history;
			shouldPersist = true;
		}
		portEXIT_CRITICAL(&systemLogMux);
		if (!shouldPersist) continue;

		const size_t written = logStorage.putBytes("SYS_LOG", &snapshot, sizeof(snapshot));
		portENTER_CRITICAL(&systemLogMux);
		persistenceBusy = false;
		if (written == sizeof(snapshot)) {
			armBlockedUntilMs = millis() + 10000UL;
		} else {
			historyDirty = true;
			persistedWhileDisarmed = false;
			persistenceRetryAfterMs = millis() + 10000UL;
		}
		portEXIT_CRITICAL(&systemLogMux);
	}
}

static bool isStallEvent(const char *tag, const char *message) {
	if (tag && strcmp(tag, STALL_TAG) == 0 && message) {
		const char *duration = strstr(message, "duration_us=");
		return duration && strtoul(duration + strlen("duration_us="), nullptr, 10) >= 10000UL;
	}
	return tag && strcmp(tag, "DIAG") == 0 && message &&
		strncmp(message, STALL_DIAG_PREFIX, strlen(STALL_DIAG_PREFIX)) == 0;
}

void initializeSystemLog() {
	storageReady = logStorage.begin("flix", false);
	PersistedSystemLog saved = {};
	if (storageReady && logStorage.getBytesLength("SYS_LOG") == sizeof(saved) &&
		logStorage.getBytes("SYS_LOG", &saved, sizeof(saved)) == sizeof(saved) &&
		saved.magic == SYSTEM_LOG_MAGIC && saved.nextSequence > 0 &&
		saved.eventCount <= LOG_CAPACITY && saved.normalCount <= LOG_CAPACITY &&
		saved.normalNext < LOG_CAPACITY && saved.stallCount <= STALL_CAPACITY && saved.stallNext < STALL_CAPACITY) {
		history = saved;
	}
	history.magic = SYSTEM_LOG_MAGIC;
	if (history.nextSequence == 0) history.nextSequence = 1;
	systemLogBootId = esp_random();
	if (systemLogBootId == 0) systemLogBootId = 1;
	if (storageReady && xTaskCreatePinnedToCore(systemLogPersistenceTask, "syslog_nvs", 4096,
		nullptr, 1, nullptr, 0) != pdPASS) {
		storageReady = false;
		Serial.println("SYSLOG_PERSIST state=DISABLED reason=task_create_failed");
	}
}

void recordSystemLogEvent(const char *tag, const char *message) {
	SystemLogEvent event = {};
	event.uptimeMs = millis();
	snprintf(event.tag, sizeof(event.tag), "%s", tag ? tag : "SYSTEM");
	const char *source = message ? message : "";
	int out = 0;
	while (*source && out < (int)sizeof(event.message) - 1) {
		const unsigned char c = (unsigned char)*source++;
		event.message[out++] = c < 0x20 || c == 0x7f ? ' ' : (char)c;
	}
	event.message[out] = '\0';
	portENTER_CRITICAL(&systemLogMux);
	event.sequence = history.nextSequence++;
	if (isStallEvent(event.tag, event.message)) {
		if (history.stallCount == 0) {
			// Preserve the six newest ordinary events when the first stall arrives.
			SystemLogEvent latest[STALL_CAPACITY];
			const int count = history.normalCount < STALL_CAPACITY ? history.normalCount : STALL_CAPACITY;
			const int oldest = (history.normalNext + LOG_CAPACITY - history.normalCount) % LOG_CAPACITY;
			for (int i = 0; i < count; ++i)
				latest[i] = history.events[(oldest + history.normalCount - count + i) % LOG_CAPACITY];
			for (int i = 0; i < count; ++i) history.events[STALL_CAPACITY + i] = latest[i];
			history.normalCount = count;
			history.normalNext = count % STALL_CAPACITY;
			history.eventCount = count;
		}
		history.events[history.stallNext] = event;
		history.stallNext = (history.stallNext + 1) % STALL_CAPACITY;
		if (history.stallCount < STALL_CAPACITY) history.stallCount++;
		history.eventCount = history.stallCount + history.normalCount;
	} else if (history.stallCount == 0) {
		history.events[history.normalNext] = event;
		history.normalNext = (history.normalNext + 1) % LOG_CAPACITY;
		if (history.normalCount < LOG_CAPACITY) history.normalCount++;
		history.eventCount = history.normalCount;
	} else {
		history.events[STALL_CAPACITY + history.normalNext] = event;
		history.normalNext = (history.normalNext + 1) % STALL_CAPACITY;
		if (history.normalCount < STALL_CAPACITY) history.normalCount++;
		history.eventCount = history.stallCount + history.normalCount;
	}
	historyDirty = true;
	portEXIT_CRITICAL(&systemLogMux);
}

bool tryArmWithSystemLog() {
	portENTER_CRITICAL(&systemLogMux);
	const uint32_t now = millis();
	const bool allowed = !persistenceBusy && (int32_t)(now - armBlockedUntilMs) >= 0;
	if (allowed) {
		armed = true;
		persistedWhileDisarmed = false;
	}
	portEXIT_CRITICAL(&systemLogMux);
	return allowed;
}

bool clearSystemLogHistory() {
	portENTER_CRITICAL(&systemLogMux);
	if (armed || motorTestActive || persistenceBusy || !storageReady) {
		portEXIT_CRITICAL(&systemLogMux);
		return false;
	}
	history.eventCount = 0;
	history.normalCount = 0;
	history.normalNext = 0;
	history.stallCount = 0;
	history.stallNext = 0;
	memset(history.events, 0, sizeof(history.events));
	historyDirty = true;
	persistedWhileDisarmed = false;
	portEXIT_CRITICAL(&systemLogMux);
	return true;
}

uint32_t getSystemLogBootId() { return systemLogBootId; }

int copySystemLogEventsAfter(uint32_t sequence, SystemLogEvent *destination, int capacity) {
	if (!destination || capacity <= 0) return 0;
	PersistedSystemLog snapshot;
	portENTER_CRITICAL(&systemLogMux);
	snapshot = history;
	portEXIT_CRITICAL(&systemLogMux);
	SystemLogEvent selected[LOG_CAPACITY];
	int count = 0;
	if (snapshot.stallCount == 0) {
		const int oldest = (snapshot.normalNext + LOG_CAPACITY - snapshot.normalCount) % LOG_CAPACITY;
		for (int i = 0; i < snapshot.normalCount; ++i)
			selected[count++] = snapshot.events[(oldest + i) % LOG_CAPACITY];
	} else {
		const int stallOldest = (snapshot.stallNext + STALL_CAPACITY - snapshot.stallCount) % STALL_CAPACITY;
		for (int i = 0; i < snapshot.stallCount; ++i)
			selected[count++] = snapshot.events[(stallOldest + i) % STALL_CAPACITY];
		const int normalOldest = (snapshot.normalNext + STALL_CAPACITY - snapshot.normalCount) % STALL_CAPACITY;
		for (int i = 0; i < snapshot.normalCount; ++i)
			selected[count++] = snapshot.events[STALL_CAPACITY + (normalOldest + i) % STALL_CAPACITY];
	}
	for (int i = 1; i < count; ++i) {
		SystemLogEvent value = selected[i];
		int j = i;
		while (j > 0 && (int32_t)(selected[j - 1].sequence - value.sequence) > 0) {
			selected[j] = selected[j - 1];
			--j;
		}
		selected[j] = value;
	}
	int copied = 0;
	for (int i = 0; i < count && copied < capacity; ++i)
		if ((int32_t)(selected[i].sequence - sequence) > 0) destination[copied++] = selected[i];
	return copied;
}
