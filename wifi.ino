// Wi-Fi station connection and browser-based provisioning portal.

#if WIFI_ENABLED

#include <WiFi.h>
#include <WiFiAP.h>
#include <WiFiUdp.h>
#include <NetworkEvents.h>
#include <DNSServer.h>
#include <stddef.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_partition.h>
#include "Preferences.h"
#include "persistent_write_policy.h"
#include "system_log.h"
#include "flight_log.h"
#include "wifi_profiles.h"
#include "wifi_recovery_policy.h"

extern Preferences storage;
extern bool armed;
extern bool motorsActive();
extern bool parameterPersistenceReady();
extern uint32_t mavlinkRxDroppedCount();
extern uint32_t mavlinkRxQueueDepth();

static constexpr size_t WIFI_PROFILE_SLOT_BYTES = 4096;
static constexpr size_t WIFI_PROFILE_RESERVED_BYTES = WIFI_PROFILE_SLOT_BYTES * 2;
struct __attribute__((packed)) LegacyWifiProfileStore {
	uint32_t magic;
	uint8_t version;
	uint8_t count;
	WifiProfileRecord profiles[WIFI_PROFILE_LIMIT];
};

static WifiProfileRecord activeWifiProfiles[WIFI_PROFILE_LIMIT] = {};
static uint8_t activeWifiProfileCount = 0;
static uint8_t wifiProfileAttempt = 0;
static char wifiSaveError[48] = "none";
static const esp_partition_t *wifiProfilePartition = nullptr;
static bool wifiProfileStorageReady = false;
static int8_t wifiProfileActiveSlot = -1;
static uint32_t wifiProfileGeneration = 0;
static bool wifiProfileBackupReady = false;

static bool ensureWifiProfileStorage() {
	if (wifiProfileStorageReady) return true;
	// The project does not mount SPIFFS. Reserve two sectors in its partition for A/B records.
	wifiProfilePartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
		ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
	if (!wifiProfilePartition || wifiProfilePartition->size < WIFI_PROFILE_RESERVED_BYTES) {
		strncpy(wifiSaveError, "profile_partition_missing", sizeof(wifiSaveError));
		return false;
	}
	wifiProfileStorageReady = true;
	return true;
}

static size_t wifiProfileStoreLength(uint8_t count) {
	return offsetof(WifiProfileStore, profiles) + (size_t)count * sizeof(WifiProfileRecord);
}

static bool validWifiProfileStore(const WifiProfileStore &store, size_t length) {
	if (store.magic != WIFI_PROFILE_MAGIC || store.version != WIFI_PROFILE_VERSION ||
		store.count > WIFI_PROFILE_LIMIT || length != wifiProfileStoreLength(store.count)) return false;
	for (uint8_t i = 0; i < store.count; ++i) {
		const WifiProfileRecord &profile = store.profiles[i];
		if (!profile.ssidLength || profile.ssidLength > sizeof(profile.ssid) ||
			profile.passwordLength > sizeof(profile.password) ||
			(profile.passwordLength > 0 && profile.passwordLength < 8)) return false;
	}
	return true;
}

static bool readWifiProfileSlot(int slot, WifiProfileStore &store) {
	if (!ensureWifiProfileStorage() || slot < 0 || slot > 1) return false;
	memset(&store, 0, sizeof(store));
	if (esp_partition_read(wifiProfilePartition, slot * WIFI_PROFILE_SLOT_BYTES,
		&store, sizeof(store)) != ESP_OK) return false;
	return validWifiProfileStore(store, wifiProfileStoreLength(store.count));
}

static bool readWifiProfileStore(WifiProfileStore &store, bool &hasStore) {
	WifiProfileStore first = {}, second = {};
	const bool firstOK = readWifiProfileSlot(0, first);
	const bool secondOK = readWifiProfileSlot(1, second);
	hasStore = firstOK || secondOK;
	if (!hasStore) {
		wifiProfileActiveSlot = -1;
		wifiProfileGeneration = 0;
		memset(&store, 0, sizeof(store));
		return true;
	}
	const bool useSecond = secondOK && (!firstOK || (int32_t)(second.generation - first.generation) > 0);
	store = useSecond ? second : first;
	wifiProfileActiveSlot = useSecond ? 1 : 0;
	wifiProfileGeneration = store.generation;
	return true;
}

static uint8_t loadWifiProfiles(WifiProfileRecord *destination) {
	WifiProfileStore store;
	bool hasStore = false;
	if (!readWifiProfileStore(store, hasStore)) return 0;
	if (hasStore) {
		memcpy(destination, store.profiles, sizeof(store.profiles));
		return store.count;
	}
	// Migrate a previously saved NVS profile blob, if an earlier firmware ever committed one.
	const size_t oldLength = storage.getBytesLength("WIFI_NETS");
	LegacyWifiProfileStore legacy = {};
	if (oldLength >= offsetof(LegacyWifiProfileStore, profiles) && oldLength <= sizeof(legacy) &&
		storage.getBytes("WIFI_NETS", &legacy, oldLength) == oldLength && legacy.magic == WIFI_PROFILE_MAGIC &&
		legacy.version == 1 && legacy.count <= WIFI_PROFILE_LIMIT &&
		oldLength == offsetof(LegacyWifiProfileStore, profiles) + legacy.count * sizeof(WifiProfileRecord)) {
		memcpy(destination, legacy.profiles, sizeof(legacy.profiles));
		storeWifiProfiles(destination, legacy.count); // Backup remains until loadActiveWifiProfiles verifies flash.
		return legacy.count;
	}
	const String legacySsid = storage.getString("WIFI_STA_SSID", "");
	const String legacyPassword = storage.getString("WIFI_STA_PASS", "");
	if (legacySsid.isEmpty() || legacySsid.length() > 32 || legacyPassword.length() > 63) return 0;
	memset(destination, 0, sizeof(WifiProfileRecord) * WIFI_PROFILE_LIMIT);
	WifiProfileRecord &profile = destination[0];
	profile.ssidLength = (uint8_t)legacySsid.length();
	profile.passwordLength = (uint8_t)legacyPassword.length();
	memcpy(profile.ssid, legacySsid.c_str(), profile.ssidLength);
	memcpy(profile.password, legacyPassword.c_str(), profile.passwordLength);
	storeWifiProfiles(destination, 1); // Preserve the legacy NVS keys until the verified file is committed.
	return 1;
}

// Keep an NVS copy so changing the partition layout can move the profile data
// before the old SPIFFS offset is reassigned to a larger OTA application slot.
static bool mirrorWifiProfilesToNvs(const WifiProfileRecord *profiles, uint8_t count, bool batchHeld = false) {
	if (!parameterPersistenceReady() || count > WIFI_PROFILE_LIMIT) return false;
	if (!count && !(wifiProfilePartition && wifiProfilePartition->address == 0x3D0000 &&
		storage.isKey("WIFI_NETS"))) return true;
	// The compact layout places SPIFFS at 0x3D0000; the profile records are
	// already safe there, so do not keep consuming NVS after migration.
	if (wifiProfilePartition && wifiProfilePartition->address == 0x3D0000) {
		if (!storage.isKey("WIFI_NETS")) return true;
		WifiProfileStore stored = {};
		bool hasStore = false;
		return readWifiProfileStore(stored, hasStore) && hasStore && stored.count == count &&
			memcmp(stored.profiles, profiles, count * sizeof(WifiProfileRecord)) == 0;
	}
	const bool ownsBatch = !batchHeld;
	if (ownsBatch && !beginPersistentWriteBatch()) return false;
	LegacyWifiProfileStore backup = {};
	backup.magic = WIFI_PROFILE_MAGIC;
	backup.version = 1;
	backup.count = count;
	if (count) memcpy(backup.profiles, profiles, count * sizeof(WifiProfileRecord));
	const size_t length = offsetof(LegacyWifiProfileStore, profiles) +
		(size_t)count * sizeof(WifiProfileRecord);
	LegacyWifiProfileStore current = {};
	const size_t currentLength = storage.getBytesLength("WIFI_NETS");
	if (currentLength == length && storage.getBytes("WIFI_NETS", &current, length) == length &&
		memcmp(&backup, &current, length) == 0) {
		if (ownsBatch) finishPersistentWriteBatch(false);
		return true;
	}
	const bool written = storage.putBytes("WIFI_NETS", &backup, length) == length;
	memset(&current, 0, sizeof(current));
	const bool verified = written && storage.getBytes("WIFI_NETS", &current, length) == length &&
		memcmp(&backup, &current, length) == 0;
	if (ownsBatch) finishPersistentWriteBatch(verified);
	return verified;
}

static void loadActiveWifiProfiles() {
	memset(activeWifiProfiles, 0, sizeof(activeWifiProfiles));
	activeWifiProfileCount = loadWifiProfiles(activeWifiProfiles);
	wifiProfileAttempt = 0;
	if (wifiProfilePartition && wifiProfilePartition->address == 0x3D0000) {
		if (!storage.isKey("WIFI_NETS")) {
			wifiProfileBackupReady = true;
			return;
		}
		if (!beginPersistentWriteBatch()) {
			wifiProfileBackupReady = false;
			return;
		}
		WifiProfileStore restored = {};
		bool restoredExists = false;
		const bool verified = readWifiProfileStore(restored, restoredExists) && restoredExists &&
			restored.count == activeWifiProfileCount &&
			memcmp(restored.profiles, activeWifiProfiles,
				activeWifiProfileCount * sizeof(WifiProfileRecord)) == 0;
		const bool removed = verified && storage.remove("WIFI_NETS");
		finishPersistentWriteBatch(removed);
		wifiProfileBackupReady = removed;
		if (!removed) recordSystemLogEvent("WIFI_PROFILE", verified ?
			"migration_backup_cleanup_failed" : "migration_restore_unverified");
	}
}

static void wifiProfileStrings(const WifiProfileRecord &profile, char *ssid, size_t ssidCapacity,
	char *password, size_t passwordCapacity) {
	if (ssidCapacity) {
		const size_t length = profile.ssidLength < ssidCapacity - 1 ? profile.ssidLength : ssidCapacity - 1;
		memcpy(ssid, profile.ssid, length);
		ssid[length] = '\0';
	}
	if (passwordCapacity) {
		const size_t length = profile.passwordLength < passwordCapacity - 1 ? profile.passwordLength : passwordCapacity - 1;
		memcpy(password, profile.password, length);
		password[length] = '\0';
	}
}

static bool storeWifiProfiles(const WifiProfileRecord *profiles, uint8_t count) {
	if (count > WIFI_PROFILE_LIMIT || !ensureWifiProfileStorage()) return false;
	WifiProfileStore store = {};
	store.magic = WIFI_PROFILE_MAGIC;
	store.version = WIFI_PROFILE_VERSION;
	store.count = count;
	store.generation = wifiProfileGeneration + 1;
	if (count) memcpy(store.profiles, profiles, count * sizeof(WifiProfileRecord));
	const size_t length = wifiProfileStoreLength(count);
	const int8_t targetSlot = wifiProfileActiveSlot == 0 ? 1 : 0;
	const size_t writeLength = (length + 3U) & ~3U;
	uint8_t padded[sizeof(store) + 3] = {};
	memcpy(padded, &store, length);
	const size_t offset = (size_t)targetSlot * WIFI_PROFILE_SLOT_BYTES;
	if (esp_partition_erase_range(wifiProfilePartition, offset, WIFI_PROFILE_SLOT_BYTES) != ESP_OK) {
		strncpy(wifiSaveError, "profile_flash_erase_failed", sizeof(wifiSaveError));
		return false;
	}
	if (esp_partition_write(wifiProfilePartition, offset, padded, writeLength) != ESP_OK) {
		strncpy(wifiSaveError, "profile_flash_write_failed", sizeof(wifiSaveError));
		return false;
	}
	WifiProfileStore committed;
	if (!readWifiProfileSlot(targetSlot, committed) || memcmp(&store, &committed, length) != 0) {
		strncpy(wifiSaveError, "profile_flash_verify_failed", sizeof(wifiSaveError));
		return false;
	}
	wifiProfileActiveSlot = targetSlot;
	wifiProfileGeneration = store.generation;
	return true;
}

const int W_DISABLED = 0, W_STA = 1, W_AP = 2;
int wifiMode = W_STA;
int udpLocalPort = 14550;
int udpRemotePort = 14550;
IPAddress udpRemoteIP = "255.255.255.255";

WiFiUDP udp;
static WiFiUDP udpTx;
static constexpr size_t WIFI_TX_PACKET_CAPACITY = 320;
struct WifiTxPacket {
	uint16_t length;
	uint8_t data[WIFI_TX_PACKET_CAPACITY];
};
static QueueHandle_t wifiTxQueue = nullptr;
static portMUX_TYPE udpRemoteMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t wifiTxDropped = 0;
static WiFiServer telemetryServer(81);
static DNSServer wifiDnsServer;
static bool configPortalActive = false;
static bool configPortalStarting = false;
static String configPortalSSID;
static uint32_t wifiAPHealthCheckAtMs = 0;
static uint32_t wifiAPStartAttemptMs = 0;
static uint32_t wifiAPActiveSinceMs = 0;
static bool wifiWasConnected = false;
static uint32_t wifiDisconnectCount = 0;
static uint32_t wifiLastDisconnectMs = 0;
static bool wifiRestartScheduled = false;
static uint32_t wifiConnectStartedMs = 0;
static uint32_t wifiRestartAtMs = 0;
static uint32_t wifiAPRetryAtMs = 0;
static uint32_t wifiSTAReconnectAtMs = 0;
static bool wifiSTARetryResetPending = false;
static bool wifiAPEventStarted = false;
static const uint32_t TELEMETRY_SAMPLE_INTERVAL_MS = 500; // 遥测样本 2Hz，降低网络任务占用
static const int TELEMETRY_LOG_COLUMNS_CAPACITY = 41;
static const int TELEMETRY_FRAME_CAPACITY = 1024;
static_assert(TELEMETRY_LOG_COLUMNS_CAPACITY >= FLIGHT_LOG_COLUMNS, "SSE telemetry capacity must cover all flight log columns");
static_assert(TELEMETRY_FRAME_CAPACITY >= 1024, "SSE telemetry frame buffer must fit the flight-log CSV row");

static void wifiTransmitTask(void *argument) {
	(void)argument;
	udpTx.begin(0);
	WifiTxPacket packet;
	for (;;) {
		if (xQueueReceive(wifiTxQueue, &packet, portMAX_DELAY) != pdTRUE) continue;
		if (WiFi.softAPgetStationNum() == 0 && !WiFi.isConnected()) continue;
		IPAddress destination;
		int destinationPort;
		portENTER_CRITICAL(&udpRemoteMux);
		destination = udpRemoteIP;
		destinationPort = udpRemotePort;
		portEXIT_CRITICAL(&udpRemoteMux);
		if (!udpTx.beginPacket(destination, destinationPort)) continue;
		udpTx.write(packet.data, packet.length);
		udpTx.endPacket();
		vTaskDelay(1);
	}
}

extern int getLogColumnCount();
extern const char* getLogColumnName(int column);
extern bool copyLatestLogRow(float *destination, int capacity, uint32_t *sequence);

static void telemetryStreamTask(void *argument) {
	(void)argument;
	const int columns = getLogColumnCount();
	float row[TELEMETRY_LOG_COLUMNS_CAPACITY];
	char frame[TELEMETRY_FRAME_CAPACITY];
	for (;;) {
		WiFiClient client = telemetryServer.accept();
		if (!client) {
			vTaskDelay(pdMS_TO_TICKS(25));
			continue;
		}
		client.setNoDelay(true);
		client.setTimeout(100);
		client.print("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\n\r\n");

		int used = snprintf(frame, sizeof(frame), "event: schema\ndata: ");
		for (int i = 0; i < columns && used > 0 && used < (int)sizeof(frame); ++i) {
			used += snprintf(frame + used, sizeof(frame) - used, "%s%s", i ? "," : "", getLogColumnName(i));
		}
		if (used > 0 && used + 2 < (int)sizeof(frame)) {
			frame[used++] = '\n';
			frame[used++] = '\n';
			if (client.write((const uint8_t *)frame, used) != (size_t)used) {
				client.stop();
				continue;
			}
		}

		uint32_t lastSequence = UINT32_MAX;
		uint32_t lastSystemSequence = 0;
		uint32_t lastSampleMs = millis();
		while (client.connected()) {
			vTaskDelay(pdMS_TO_TICKS(100)); // 每100ms检查事件，样本单独限频
			SystemLogEvent events[4];
			const int eventCount = copySystemLogEventsAfter(lastSystemSequence, events, 4);
			for (int i = 0; i < eventCount; ++i) {
				const SystemLogEvent &event = events[i];
				used = snprintf(frame, sizeof(frame), "id: %08lx-%lu\nevent: system-log\ndata: %lu|%s|%s\n\n",
					(unsigned long)event.bootId, (unsigned long)event.sequence,
					(unsigned long)event.uptimeMs, event.tag, event.message);
				if (used <= 0 || used >= (int)sizeof(frame) ||
					client.write((const uint8_t *)frame, used) != (size_t)used) {
					client.stop();
					break;
				}
				lastSystemSequence = event.sequence;
			}
			if (!client.connected()) break;
			const uint32_t now = millis();
			if ((uint32_t)(now - lastSampleMs) < TELEMETRY_SAMPLE_INTERVAL_MS) continue;
			lastSampleMs = now;
			uint32_t sequence;
			if (!copyLatestLogRow(row, columns, &sequence) || sequence == lastSequence) continue;
			used = snprintf(frame, sizeof(frame), "id: %lu\nevent: sample\ndata: ", (unsigned long)sequence);
			for (int i = 0; i < columns && used > 0 && used < (int)sizeof(frame) - 16; ++i) {
				used += snprintf(frame + used, sizeof(frame) - used, "%s%.7g", i ? "," : "", row[i]);
			}
			if (used <= 0 || used + 2 >= (int)sizeof(frame)) break;
			frame[used++] = '\n';
			frame[used++] = '\n';
			if (client.write((const uint8_t *)frame, used) != (size_t)used) break;
			lastSequence = sequence;
		}
		client.stop();
	}
}

void serviceWiFi();

static void wifiServiceTask(void *argument) {
	(void)argument;
	uint32_t backupRetryAtMs = 0;
	for (;;) {
		serviceWiFi();
		if (!wifiProfileBackupReady && activeWifiProfileCount && !armed &&
			(int32_t)(millis() - backupRetryAtMs) >= 0) {
			wifiProfileBackupReady = mirrorWifiProfilesToNvs(activeWifiProfiles, activeWifiProfileCount);
			backupRetryAtMs = millis() + 1000;
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;
static const uint32_t WIFI_RESTART_DELAY_MS = 1500;
static const uint32_t WIFI_AP_RETRY_DELAY_MS = 5000;

static void stopWiFiConfigPortalForRetry(const char *reason) {
	wifiDnsServer.stop();
	WiFi.softAPdisconnect(false);
	__atomic_store_n(&wifiAPEventStarted, false, __ATOMIC_RELEASE);
	configPortalActive = false;
	configPortalStarting = false;
	configPortalSSID = "";
	wifiAPRetryAtMs = millis() + WIFI_AP_RETRY_DELAY_MS;
	if (wifiMode == W_STA) {
		WiFi.mode(activeWifiProfileCount ? WIFI_STA : WIFI_AP);
		wifiSTAReconnectAtMs = millis();
		wifiSTARetryResetPending = false;
	} else {
		WiFi.mode(WIFI_OFF);
	}
	char eventMessage[64];
	snprintf(eventMessage, sizeof(eventMessage), "state=RETRY reason=%s", reason);
	recordSystemLogEvent("WIFI_AP", eventMessage);
}

static void startWiFiConfigPortal() {
	if (configPortalActive || configPortalStarting ||
		!WifiRecoveryPolicy::portalStartAllowed(armed, motorsActive())) return;
	__atomic_store_n(&wifiAPEventStarted, false, __ATOMIC_RELEASE);
	const bool modeReady = WiFi.mode(WIFI_AP);
	String ssid = storage.getString("WIFI_AP_SSID", "Drone_WiFi");
	String password = storage.getString("WIFI_AP_PASS", "");
	if (ssid.isEmpty()) ssid = "Drone_WiFi";
	const bool credentialsValid = ssid.length() <= 32 &&
		(password.isEmpty() || (password.length() >= 8 && password.length() <= 63));
	bool started = false;
	configPortalStarting = true;
	wifiAPStartAttemptMs = millis();
	if (credentialsValid) {
		const char *passphrase = password.isEmpty() ? nullptr : password.c_str();
		started = WiFi.softAP(ssid.c_str(), passphrase);
	}
	if (!started) {
		// Invalid/stale AP credentials or a failed custom configuration must not
		// strand the user without a provisioning network. Fall back to the known
		// open default SSID; never print or persist a password in diagnostics.
		ssid = "Drone_WiFi";
		password = "";
		__atomic_store_n(&wifiAPEventStarted, false, __ATOMIC_RELEASE);
		wifiAPStartAttemptMs = millis();
		started = WiFi.softAP(ssid.c_str(), nullptr);
		if (started) {
			print("WIFI_CONFIG_AP fallback=DEFAULT reason=%s mode_ready=%d\n",
				credentialsValid ? "configured_start_failed" : "invalid_credentials",
				modeReady ? 1 : 0);
			recordSystemLogEvent("WIFI_AP", "fallback=DEFAULT result=READY");
		}
	}
	if (!started) {
		configPortalStarting = false;
		wifiAPRetryAtMs = millis() + WIFI_AP_RETRY_DELAY_MS;
		print("WIFI_CONFIG_AP result=FAIL ssid=%s mode_ready=%d mode=%d retry_ms=%lu credentials_valid=%d\n",
			ssid.c_str(), modeReady ? 1 : 0, (int)WiFi.getMode(),
			(unsigned long)WIFI_AP_RETRY_DELAY_MS, credentialsValid ? 1 : 0);
		recordSystemLogEvent("WIFI_AP", "result=FAIL retry_ms=5000");
		return;
	}
	configPortalSSID = ssid;
	wifiAPRetryAtMs = 0;
	print("WIFI_CONFIG_AP result=STARTING ssid=%s security=%s mode_ready=%d\n",
		ssid.c_str(), password.isEmpty() ? "OPEN" : "WPA2", modeReady ? 1 : 0);
	String eventMessage = "result=STARTING ssid=" + ssid;
	recordSystemLogEvent("WIFI_AP", eventMessage.c_str());
}

void setupWiFi() {
	print("Setup Wi-Fi mode=%d (0=disabled,1=STA,2=AP)\n", wifiMode);
	// Credentials live in our A/B flash slots. Keep the Arduino Wi-Fi driver's
	// transient STA/AP configuration in RAM so a full shared NVS cannot block
	// esp_wifi_set_config() or consume more NVS entries.
	WiFi.persistent(false);
	WiFi.setSleep(false);
	WiFi.onEvent([](WiFiEvent_t event) {
		if (event == ARDUINO_EVENT_WIFI_AP_START)
			__atomic_store_n(&wifiAPEventStarted, true, __ATOMIC_RELEASE);
		else if (event == ARDUINO_EVENT_WIFI_AP_STOP)
			__atomic_store_n(&wifiAPEventStarted, false, __ATOMIC_RELEASE);
	});
	loadActiveWifiProfiles();
	wifiProfileBackupReady = mirrorWifiProfilesToNvs(activeWifiProfiles, activeWifiProfileCount);
	print("WIFI_PROFILE_BACKUP state=%s profiles=%u\n", wifiProfileBackupReady ? "READY" : "PENDING",
		(unsigned)activeWifiProfileCount);
	const bool staCredentialsPresent = activeWifiProfileCount > 0;
	const uint8_t modeVersion = storage.getUChar("WIFI_MODE_VER", 0);
	print("WIFI_BOOT mode=%d sta_credentials=%d mode_version=%u\n", wifiMode,
		staCredentialsPresent ? 1 : 0, (unsigned)modeVersion);
	char bootEvent[48];
	snprintf(bootEvent, sizeof(bootEvent), "mode=%d sta=%d ver=%u", wifiMode,
		staCredentialsPresent ? 1 : 0, (unsigned)modeVersion);
	recordSystemLogEvent("WIFI_BOOT", bootEvent);

	if (wifiMode == W_DISABLED) {
		WiFi.mode(WIFI_OFF);
		print("WIFI_STATE state=DISABLED\n");
		recordSystemLogEvent("WIFI", "state=DISABLED");
		return;
	}

	if (wifiMode == W_AP) {
		startWiFiConfigPortal();
	} else {
		if (!activeWifiProfileCount) {
			startWiFiConfigPortal();
			print("WIFI_STATE state=AP_CONTROL_ONLY reason=NO_STA_CREDENTIALS\n");
			recordSystemLogEvent("WIFI", "state=AP_CONTROL_ONLY");
		} else {
			char ssid[33], password[64];
			wifiProfileStrings(activeWifiProfiles[0], ssid, sizeof(ssid), password, sizeof(password));
			WiFi.mode(WIFI_STA);
			WiFi.setHostname("CF-Drone");
			WiFi.setAutoReconnect(true);
			WiFi.begin(ssid, password);
			wifiConnectStartedMs = millis();
			wifiSTAReconnectAtMs = wifiConnectStartedMs + WifiRecoveryPolicy::STA_RETRY_INTERVAL_MS;
			print("WIFI_STATE state=CONNECTING profile=1/%u ssid=%s timeout_ms=%lu\n",
				(unsigned)activeWifiProfileCount, ssid, (unsigned long)WIFI_CONNECT_TIMEOUT_MS);
			String eventMessage = "state=CONNECTING profile=1/" + String(activeWifiProfileCount) + " ssid=" + ssid;
			recordSystemLogEvent("WIFI", eventMessage.c_str());
		}
	}
#if CF_DRONE_ENABLE_MAVLINK
	udp.begin(udpLocalPort);
	wifiTxQueue = xQueueCreate(8, sizeof(WifiTxPacket));
	if (!wifiTxQueue || xTaskCreatePinnedToCore(wifiTransmitTask, "wifi_udp_tx", 4096,
		nullptr, 1, nullptr, 0) != pdPASS) {
		print("MAVLINK_TX state=DISABLED reason=task_create_failed\n");
		if (wifiTxQueue) { vQueueDelete(wifiTxQueue); wifiTxQueue = nullptr; }
	} else {
		print("MAVLINK_TX state=READY queue=8 core=0\n");
	}
#endif
	telemetryServer.begin();
	telemetryServer.setNoDelay(true);
	if (xTaskCreatePinnedToCore(telemetryStreamTask, "telemetry_sse", 4096, nullptr, 1,
		nullptr, 0) != pdPASS) {
		print("TELEMETRY_SSE state=DISABLED reason=task_create_failed\n");
	} else {
		print("TELEMETRY_SSE state=READY port=81 rate_hz=2 core=0 priority=1\n");
		recordSystemLogEvent("SSE", "state=READY port=81 rate_hz=2");
	}
	if (xTaskCreatePinnedToCore(wifiServiceTask, "wifi_service", 4096, nullptr, 1,
		nullptr, 0) != pdPASS) {
		print("WIFI_SERVICE state=DISABLED reason=task_create_failed\n");
		recordSystemLogEvent("WIFI", "service_task_create_failed");
	} else {
		print("WIFI_SERVICE state=READY core=0 priority=1 period_ms=20\n");
	}
}

void serviceWiFi() {
	const uint32_t now = millis();
	// Captive-DNS servicing can block for tens of milliseconds on a slow UDP
	// request. The AP and HTTP/Web RC remain available while armed; DNS portal
	// work can safely wait until disarm instead of stalling the control loop.
	if (configPortalActive && !armed) wifiDnsServer.processNextRequest();
	const bool restartScheduled = __atomic_load_n(&wifiRestartScheduled, __ATOMIC_ACQUIRE);
	const uint32_t restartAtMs = __atomic_load_n(&wifiRestartAtMs, __ATOMIC_RELAXED);
	if (WifiRecoveryPolicy::restartReady(restartScheduled,
		(int32_t)(now - restartAtMs) >= 0, armed, motorsActive())) {
		print("WIFI_STATE state=RESTARTING reason=credentials_saved\n");
		recordSystemLogEvent("WIFI", "state=RESTARTING reason=credentials_saved");
		ESP.restart();
	}
	if (configPortalStarting) {
		const String activeSSID = WiFi.softAPSSID();
		const IPAddress activeAPIP = WiFi.softAPIP();
		const bool eventStarted = __atomic_load_n(&wifiAPEventStarted, __ATOMIC_ACQUIRE);
		if (WifiRecoveryPolicy::apReady(eventStarted, activeSSID == configPortalSSID,
			activeAPIP != IPAddress(0, 0, 0, 0))) {
			configPortalStarting = false;
			configPortalActive = true;
			wifiAPActiveSinceMs = now;
			wifiAPHealthCheckAtMs = now + 1000;
			wifiDnsServer.start(53, "*", activeAPIP);
			print("WIFI_CONFIG_AP result=READY ssid=%s ip=%s event=AP_START\n",
				configPortalSSID.c_str(), activeAPIP.toString().c_str());
			print("Wi-Fi配网页面: http://%s/wifi\n", activeAPIP.toString().c_str());
			String eventMessage = "result=READY ssid=" + configPortalSSID + " ip=" + activeAPIP.toString();
			recordSystemLogEvent("WIFI_AP", eventMessage.c_str());
		} else if (WifiRecoveryPolicy::apStartTimedOut(true, now, wifiAPStartAttemptMs)) {
			print("WIFI_CONFIG_AP result=TIMEOUT event_started=%d ssid=%s ip=%s action=RETRY\n",
				eventStarted ? 1 : 0, activeSSID.c_str(), activeAPIP.toString().c_str());
			stopWiFiConfigPortalForRetry("start_timeout");
		}
	}
	if (configPortalActive && (int32_t)(millis() - wifiAPHealthCheckAtMs) >= 0) {
		wifiAPHealthCheckAtMs = millis() + 1000;
		const String activeSSID = WiFi.softAPSSID();
		const IPAddress activeAPIP = WiFi.softAPIP();
		const bool eventStarted = __atomic_load_n(&wifiAPEventStarted, __ATOMIC_ACQUIRE);
		if (!WifiRecoveryPolicy::apReady(eventStarted, activeSSID == configPortalSSID,
			activeAPIP != IPAddress(0, 0, 0, 0))) {
			print("WIFI_CONFIG_AP state=LOST expected_ssid=%s actual_ssid=%s ip=%s action=RESTART\n",
				configPortalSSID.c_str(), activeSSID.c_str(), activeAPIP.toString().c_str());
			stopWiFiConfigPortalForRetry("health_lost");
		}
	}
	const bool apControlMode = wifiMode == W_AP ||
		(wifiMode == W_STA && activeWifiProfileCount == 0);
	if (apControlMode && !configPortalActive && !configPortalStarting &&
		(!wifiAPRetryAtMs || WifiRecoveryPolicy::deadlineReached(now, wifiAPRetryAtMs))) {
		startWiFiConfigPortal();
	}
	if (wifiMode != W_STA) return;

	if (WiFi.isConnected()) {
		wifiSTARetryResetPending = false;
		if (!wifiWasConnected) {
			wifiWasConnected = true;
			// Avoid String allocation and network-stack queries in the 1 kHz
			// control loop. Detailed link information remains available via `wifi`.
			recordSystemLogEvent("WIFI", "state=CONNECTED");
		}
		return;
	}

	if (wifiWasConnected) {
		wifiWasConnected = false;
		const uint32_t disconnectCount = __atomic_load_n(&wifiDisconnectCount, __ATOMIC_RELAXED);
		if (disconnectCount < UINT32_MAX)
			__atomic_store_n(&wifiDisconnectCount, disconnectCount + 1, __ATOMIC_RELAXED);
		__atomic_store_n(&wifiLastDisconnectMs, now, __ATOMIC_RELAXED);
		wifiConnectStartedMs = millis();
		wifiProfileAttempt = 0;
		wifiSTARetryResetPending = false;
		wifiSTAReconnectAtMs = now + WifiRecoveryPolicy::STA_RETRY_INTERVAL_MS;
		if (activeWifiProfileCount) {
			char ssid[33], password[64];
			wifiProfileStrings(activeWifiProfiles[0], ssid, sizeof(ssid), password, sizeof(password));
			WiFi.setAutoReconnect(true);
			WiFi.disconnect(false, false);
			WiFi.begin(ssid, password);
		}
		print("WIFI_STATE state=DISCONNECTED reconnect=profile_1\n");
		recordSystemLogEvent("WIFI", "state=DISCONNECTED reconnect=profile_1");
	}
	const bool portalOpen = configPortalActive || configPortalStarting;
	const WifiRecoveryPolicy::StaRetryAction retryAction = WifiRecoveryPolicy::staRetryAction(
		activeWifiProfileCount > 0, false, portalOpen, wifiSTARetryResetPending,
		now, wifiSTAReconnectAtMs);
	if (retryAction == WifiRecoveryPolicy::STA_RETRY_RESET) {
		// ESP-IDF rejects a new station configuration while an earlier connection
		// attempt is still active. Stop that attempt first and let the Wi-Fi task
		// observe the disconnect before applying the next saved profile.
		WiFi.setAutoReconnect(false);
		WiFi.disconnect(false, false);
		wifiSTARetryResetPending = true;
		wifiSTAReconnectAtMs = now + WifiRecoveryPolicy::STA_RETRY_RESET_DELAY_MS;
		print("WIFI_STATE state=STA_RETRY_RESET\n");
		recordSystemLogEvent("WIFI", "state=STA_RETRY_RESET");
	} else if (retryAction == WifiRecoveryPolicy::STA_RETRY_BEGIN) {
		wifiProfileAttempt = (uint8_t)((wifiProfileAttempt + 1) % activeWifiProfileCount);
		char ssid[33], password[64];
		wifiProfileStrings(activeWifiProfiles[wifiProfileAttempt], ssid, sizeof(ssid), password, sizeof(password));
		WiFi.mode(WIFI_STA);
		WiFi.setAutoReconnect(true);
		WiFi.begin(ssid, password);
		wifiSTARetryResetPending = false;
		wifiSTAReconnectAtMs = now + WifiRecoveryPolicy::STA_RETRY_INTERVAL_MS;
		print("WIFI_STATE state=STA_RETRY_BEGIN profile=%u/%u ssid=%s\n",
			(unsigned)(wifiProfileAttempt + 1), (unsigned)activeWifiProfileCount, ssid);
		recordSystemLogEvent("WIFI", "state=STA_RETRY_BEGIN");
	}
	if (!configPortalActive && (uint32_t)(millis() - wifiConnectStartedMs) >= WIFI_CONNECT_TIMEOUT_MS &&
		!configPortalStarting && WifiRecoveryPolicy::portalStartAllowed(armed, motorsActive()) &&
		(!wifiAPRetryAtMs || (int32_t)(millis() - wifiAPRetryAtMs) >= 0)) {
		if ((uint8_t)(wifiProfileAttempt + 1) < activeWifiProfileCount) {
			++wifiProfileAttempt;
			char ssid[33], password[64];
			wifiProfileStrings(activeWifiProfiles[wifiProfileAttempt], ssid, sizeof(ssid), password, sizeof(password));
			WiFi.setAutoReconnect(false);
			WiFi.disconnect(false, false);
			WiFi.begin(ssid, password);
			wifiConnectStartedMs = millis();
			print("WIFI_STATE state=TRY_NEXT_PROFILE profile=%u/%u ssid=%s\n",
				(unsigned)(wifiProfileAttempt + 1), (unsigned)activeWifiProfileCount, ssid);
			String eventMessage = "state=TRY_NEXT_PROFILE profile=" + String(wifiProfileAttempt + 1) + "/" + String(activeWifiProfileCount);
			recordSystemLogEvent("WIFI", eventMessage.c_str());
			return;
		}
		print("WIFI_STATE state=CONNECT_TIMEOUT action=KEEP_STA_RETRY\n");
		recordSystemLogEvent("WIFI", "state=CONNECT_TIMEOUT action=KEEP_STA_RETRY");
		wifiConnectStartedMs = millis();
		wifiSTAReconnectAtMs = millis();
		wifiSTARetryResetPending = false;
	}
}

uint32_t getWiFiDisconnectCount() {
	return __atomic_load_n(&wifiDisconnectCount, __ATOMIC_RELAXED);
}

uint32_t getWiFiLastDisconnectMs() {
	return __atomic_load_n(&wifiLastDisconnectMs, __ATOMIC_RELAXED);
}

void scheduleWiFiRestart() {
	__atomic_store_n(&wifiRestartAtMs, millis() + WIFI_RESTART_DELAY_MS, __ATOMIC_RELAXED);
	__atomic_store_n(&wifiRestartScheduled, true, __ATOMIC_RELEASE);
	recordSystemLogEvent("WIFI", "state=RESTART_SCHEDULED delay_ms=1500");
}

bool wifiRestartPending() {
	return __atomic_load_n(&wifiRestartScheduled, __ATOMIC_ACQUIRE);
}

bool isWiFiConfigPortalActive() {
	return configPortalActive || configPortalStarting;
}

void sendWiFi(const uint8_t *buf, int len) {
	if (!wifiTxQueue || !buf || len <= 0 || len > (int)WIFI_TX_PACKET_CAPACITY) return;
	WifiTxPacket packet;
	packet.length = (uint16_t)len;
	memcpy(packet.data, buf, packet.length);
	if (xQueueSend(wifiTxQueue, &packet, 0) != pdTRUE && wifiTxDropped < UINT32_MAX)
		++wifiTxDropped;
}

int receiveWiFi(uint8_t *buf, int len) {
	udp.parsePacket();
	IPAddress remote = udp.remoteIP();
	if (remote) {
		portENTER_CRITICAL(&udpRemoteMux);
		udpRemoteIP = remote;
		portEXIT_CRITICAL(&udpRemoteMux);
	}
	return udp.read(buf, len);
}

void printWiFiInfo() {
	const wifi_mode_t mode = WiFi.getMode();
	if (mode == WIFI_MODE_NULL) {
		print("Mode: Disabled\n");
		return;
	}
	if (mode & WIFI_MODE_AP) {
		print("Mode: Access Point (AP)%s\n", configPortalActive ? " [configuration portal]" : "");
		print("AP SSID: %s\n", WiFi.softAPSSID().c_str());
		print("AP clients: %d\n", WiFi.softAPgetStationNum());
		print("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
	}
	if (mode & WIFI_MODE_STA) {
		print("Mode: Client (STA)\n");
		print("Connected: %d\n", WiFi.isConnected());
		print("MAC: %s\n", WiFi.macAddress().c_str());
		print("SSID: %s\n", WiFi.SSID().c_str());
		print("IP: %s\n", WiFi.localIP().toString().c_str());
		if (WiFi.isConnected()) print("RSSI: %d dBm\n", WiFi.RSSI());
	}
	print("Remote IP: %s\n", udpRemoteIP.toString().c_str());
	print("MAVLink connected: %d\n", mavlinkConnected);
	print("MAVLink RX queue=%lu/8 dropped=%lu UDP TX dropped=%lu\n",
		(unsigned long)mavlinkRxQueueDepth(), (unsigned long)mavlinkRxDroppedCount(),
		(unsigned long)__atomic_load_n(&wifiTxDropped, __ATOMIC_RELAXED));
}

bool configWiFi(bool ap, const char *ssid, const char *password) {
	extern bool armed;
	extern bool motorTestActive;
	extern bool motorsActive();
	strncpy(wifiSaveError, "none", sizeof(wifiSaveError));
	if (!persistentWritesAllowed(armed, motorsActive()) || motorTestActive) {
		strncpy(wifiSaveError, "motors_active", sizeof(wifiSaveError));
		recordSystemLogEvent("WIFI_SAVE", "result=FAIL reason=motors_active");
		return false;
	}
	if (!ssid || !password || !*ssid) {
		strncpy(wifiSaveError, "invalid_input", sizeof(wifiSaveError));
		recordSystemLogEvent("WIFI_SAVE", "result=FAIL reason=invalid_input");
		return false;
	}
	const int requestedMode = ap ? W_AP : W_STA;
	const size_t ssidLength = strlen(ssid);
	const size_t passwordLength = strlen(password);
	if (ssidLength > 32 || passwordLength > 63 || (passwordLength && passwordLength < 8)) {
		strncpy(wifiSaveError, "invalid_input", sizeof(wifiSaveError));
		return false;
	}
	if (!parameterPersistenceReady()) {
		strncpy(wifiSaveError, "nvs_unavailable", sizeof(wifiSaveError));
		return false;
	}
	if (!beginPersistentWriteBatch()) {
		strncpy(wifiSaveError, "write_busy_or_unsafe", sizeof(wifiSaveError));
		return false;
	}
	const size_t freeBefore = storage.freeEntries();
	bool writeOK = false;
	bool readbackOK = false;
	if (ap) {
		const size_t ssidWritten = storage.putString("WIFI_AP_SSID", ssid);
		const size_t passwordWritten = storage.putString("WIFI_AP_PASS", password);
		writeOK = ssidWritten == ssidLength &&
			(passwordLength == 0 || passwordWritten == passwordLength);
		if (writeOK) {
			readbackOK = storage.getString("WIFI_AP_SSID", "__readback_failed__") == ssid &&
				storage.getString("WIFI_AP_PASS", "__readback_failed__") == password;
		}
	} else {
		WifiProfileRecord profiles[WIFI_PROFILE_LIMIT] = {};
		uint8_t count = loadWifiProfiles(profiles);
		{
			int existingIndex = -1;
			for (uint8_t i = 0; i < count; ++i) {
				if (profiles[i].ssidLength == ssidLength &&
					memcmp(profiles[i].ssid, ssid, ssidLength) == 0) {
					existingIndex = i;
					break;
				}
			}
			if (existingIndex < 0 && count >= WIFI_PROFILE_LIMIT) {
				strncpy(wifiSaveError, "profile_limit", sizeof(wifiSaveError));
			} else {
				const uint8_t newCount = existingIndex < 0 ? count + 1 : count;
				if (existingIndex >= 0) {
					for (int i = existingIndex; i > 0; --i) profiles[i] = profiles[i - 1];
				} else {
					for (int i = newCount - 1; i > 0; --i) profiles[i] = profiles[i - 1];
				}
				WifiProfileRecord &profile = profiles[0];
				memset(&profile, 0, sizeof(profile));
				profile.ssidLength = (uint8_t)ssidLength;
				profile.passwordLength = (uint8_t)passwordLength;
				memcpy(profile.ssid, ssid, ssidLength);
				memcpy(profile.password, password, passwordLength);
				count = newCount;
				writeOK = storeWifiProfiles(profiles, count);
				WifiProfileStore verify = {};
				bool verifyHasStore = false;
				readbackOK = writeOK && readWifiProfileStore(verify, verifyHasStore) && verifyHasStore &&
					verify.count == count && memcmp(profiles, verify.profiles, count * sizeof(WifiProfileRecord)) == 0;
				writeOK = writeOK && readbackOK;
				if (writeOK) {
					wifiProfileBackupReady = mirrorWifiProfilesToNvs(profiles, count, true);
				}
			}
		}
	}
	const bool modeAlreadySet = storage.getFloat("WIFI_MODE", -1.0f) == (float)requestedMode;
	const size_t modeWritten = writeOK ? (modeAlreadySet ? sizeof(float) : storage.putFloat("WIFI_MODE", (float)requestedMode)) : 0;
	const bool modeVerified = modeWritten == sizeof(float) && storage.getFloat("WIFI_MODE", -1.0f) == (float)requestedMode;
	writeOK = writeOK && modeVerified;
	if (!writeOK && strcmp(wifiSaveError, "none") == 0) {
		strncpy(wifiSaveError, readbackOK ? "nvs_mode_write_failed" : "profile_flash_write_failed", sizeof(wifiSaveError));
	}
	finishPersistentWriteBatch(writeOK);
	if (!writeOK) {
		print("WIFI_CONFIG_SAVE result=FAIL reason=%s free_before=%u free_after=%u ssid_bytes=%u password_bytes=%u mode_write=%u readback=%u\n",
			wifiSaveError, (unsigned)freeBefore, (unsigned)storage.freeEntries(),
			(unsigned)ssidLength, (unsigned)passwordLength, (unsigned)modeWritten, readbackOK ? 1 : 0);
		String eventMessage = String("result=FAIL reason=") + wifiSaveError;
		recordSystemLogEvent("WIFI_SAVE", eventMessage.c_str());
		return false;
	}
	wifiMode = requestedMode;
	if (!ap) loadActiveWifiProfiles();
	print("WIFI_CONFIG_SAVE result=OK mode=%d ssid_length=%u profiles=%u password=not_logged\n",
		wifiMode, (unsigned)ssidLength, (unsigned)(ap ? 0 : activeWifiProfileCount));
	recordSystemLogEvent("WIFI_SAVE", ap ? "result=OK mode=AP" : "result=OK mode=STA");
	return true;
}

const char *wifiConfigLastError() { return wifiSaveError; }

int getWiFiProfileCount() {
	WifiProfileRecord profiles[WIFI_PROFILE_LIMIT] = {};
	return loadWifiProfiles(profiles);
}

size_t getWiFiProfileStorageUsedBytes() {
	return wifiProfileActiveSlot < 0 ? 0 : wifiProfileStoreLength(activeWifiProfileCount);
}
size_t getWiFiProfileStorageTotalBytes() { return WIFI_PROFILE_RESERVED_BYTES; }

bool getWiFiProfileSsid(int index, char *destination, size_t capacity) {
	if (!destination || !capacity || index < 0 || index >= WIFI_PROFILE_LIMIT) return false;
	WifiProfileRecord profiles[WIFI_PROFILE_LIMIT] = {};
	const uint8_t count = loadWifiProfiles(profiles);
	if (index >= count) return false;
	wifiProfileStrings(profiles[index], destination, capacity, nullptr, 0);
	return true;
}

bool removeWiFiProfile(const char *ssid) {
	extern bool armed;
	extern bool motorTestActive;
	extern bool motorsActive();
	strncpy(wifiSaveError, "none", sizeof(wifiSaveError));
	if (!ssid || !*ssid) { strncpy(wifiSaveError, "invalid_input", sizeof(wifiSaveError)); return false; }
	if (!persistentWritesAllowed(armed, motorsActive()) || motorTestActive || !parameterPersistenceReady() ||
		!beginPersistentWriteBatch()) {
		strncpy(wifiSaveError, "write_busy_or_unsafe", sizeof(wifiSaveError));
		return false;
	}
	WifiProfileRecord profiles[WIFI_PROFILE_LIMIT] = {};
	uint8_t count = loadWifiProfiles(profiles);
	int found = -1;
	for (uint8_t i = 0; i < count; ++i) {
		char savedSsid[33];
		wifiProfileStrings(profiles[i], savedSsid, sizeof(savedSsid), nullptr, 0);
		if (strcmp(savedSsid, ssid) == 0) { found = i; break; }
	}
	bool ok = found >= 0;
	if (ok) {
		for (uint8_t i = found; i + 1 < count; ++i) profiles[i] = profiles[i + 1];
		--count;
		ok = storeWifiProfiles(profiles, count);
		if (ok) {
			wifiProfileBackupReady = mirrorWifiProfilesToNvs(profiles, count, true);
		}
	}
	if (ok) {
		storage.remove("WIFI_STA_SSID");
		storage.remove("WIFI_STA_PASS");
		loadActiveWifiProfiles();
	} else {
		strncpy(wifiSaveError, found < 0 ? "profile_not_found" : "nvs_remove_failed", sizeof(wifiSaveError));
	}
	finishPersistentWriteBatch(ok);
	if (!ok) recordSystemLogEvent("WIFI_SAVE", "result=FAIL reason=remove_failed");
	return ok;
}

#endif
