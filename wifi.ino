// Wi-Fi station connection and browser-based provisioning portal.

#if WIFI_ENABLED

#include <WiFi.h>
#include <WiFiAP.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include "Preferences.h"
#include "system_log.h"

extern Preferences storage;

const int W_DISABLED = 0, W_STA = 1, W_AP = 2;
int wifiMode = W_STA;
int udpLocalPort = 14550;
int udpRemotePort = 14550;
IPAddress udpRemoteIP = "255.255.255.255";

WiFiUDP udp;
static WiFiServer telemetryServer(81);
static DNSServer wifiDnsServer;
static bool configPortalActive = false;
static bool wifiWasConnected = false;
static bool wifiRestartScheduled = false;
static uint32_t wifiConnectStartedMs = 0;
static uint32_t wifiRestartAtMs = 0;
static const uint32_t TELEMETRY_SAMPLE_INTERVAL_MS = 500; // 遥测样本 2Hz，降低网络任务占用

extern int getLogColumnCount();
extern const char* getLogColumnName(int column);
extern bool copyLatestLogRow(float *destination, int capacity, uint32_t *sequence);

static void telemetryStreamTask(void *argument) {
	(void)argument;
	const int columns = getLogColumnCount();
	float row[40];
	char frame[900];
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
					(unsigned long)getSystemLogBootId(), (unsigned long)event.sequence,
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

static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static const uint32_t WIFI_RESTART_DELAY_MS = 1500;

static void startWiFiConfigPortal(bool keepStation) {
	if (configPortalActive) return;
	WiFi.mode(keepStation ? WIFI_AP_STA : WIFI_AP);
	String ssid = storage.getString("WIFI_AP_SSID", "Drone_WiFi");
	String password = storage.getString("WIFI_AP_PASS", "");
	if (ssid.isEmpty()) ssid = "Drone_WiFi";
	const char *passphrase = password.isEmpty() ? nullptr : password.c_str();
	if (!WiFi.softAP(ssid.c_str(), passphrase)) {
		print("WIFI_CONFIG_AP result=FAIL ssid=%s\n", ssid.c_str());
		recordSystemLogEvent("WIFI_AP", "result=FAIL");
		return;
	}
	configPortalActive = true;
	wifiDnsServer.start(53, "*", WiFi.softAPIP());
	print("WIFI_CONFIG_AP result=READY ssid=%s security=%s ip=%s\n",
		ssid.c_str(), password.isEmpty() ? "OPEN" : "WPA2", WiFi.softAPIP().toString().c_str());
	print("Wi-Fi配网页面: http://%s/wifi\n", WiFi.softAPIP().toString().c_str());
	String eventMessage = "result=READY ssid=" + ssid + " ip=" + WiFi.softAPIP().toString();
	recordSystemLogEvent("WIFI_AP", eventMessage.c_str());
}

void setupWiFi() {
	print("Setup Wi-Fi mode=%d (0=disabled,1=STA,2=AP)\n", wifiMode);
	WiFi.setSleep(false);
	const bool staCredentialsPresent = !storage.getString("WIFI_STA_SSID", "").isEmpty();
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
		startWiFiConfigPortal(false);
	} else {
		String ssid = storage.getString("WIFI_STA_SSID", "");
		String password = storage.getString("WIFI_STA_PASS", "");
		if (ssid.isEmpty()) {
			print("WIFI_STATE state=NO_CREDENTIALS action=START_CONFIG_AP\n");
			recordSystemLogEvent("WIFI", "state=NO_CREDENTIALS");
			startWiFiConfigPortal(false);
		} else {
			WiFi.mode(WIFI_STA);
			WiFi.setHostname("CF-Drone");
			WiFi.begin(ssid.c_str(), password.c_str());
			wifiConnectStartedMs = millis();
			print("WIFI_STATE state=CONNECTING ssid=%s timeout_ms=%lu\n",
				ssid.c_str(), (unsigned long)WIFI_CONNECT_TIMEOUT_MS);
			String eventMessage = "state=CONNECTING ssid=" + ssid;
			recordSystemLogEvent("WIFI", eventMessage.c_str());
		}
	}
	udp.begin(udpLocalPort);
	telemetryServer.begin();
	telemetryServer.setNoDelay(true);
	if (xTaskCreate(telemetryStreamTask, "telemetry_sse", 4096, nullptr, 1, nullptr) != pdPASS) {
		print("TELEMETRY_SSE state=DISABLED reason=task_create_failed\n");
	} else {
		print("TELEMETRY_SSE state=READY port=81 rate_hz=2\n");
		recordSystemLogEvent("SSE", "state=READY port=81 rate_hz=2");
	}
}

void serviceWiFi() {
	if (configPortalActive) wifiDnsServer.processNextRequest();
	if (wifiRestartScheduled && (int32_t)(millis() - wifiRestartAtMs) >= 0) {
		print("WIFI_STATE state=RESTARTING reason=credentials_saved\n");
		recordSystemLogEvent("WIFI", "state=RESTARTING reason=credentials_saved");
		ESP.restart();
	}
	if (wifiMode != W_STA) return;

	if (WiFi.isConnected()) {
		if (!wifiWasConnected) {
			wifiWasConnected = true;
			print("WIFI_STATE state=CONNECTED ssid=%s ip=%s rssi=%d\n",
				WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
			String eventMessage = "state=CONNECTED ssid=" + WiFi.SSID() + " ip=" + WiFi.localIP().toString() +
				" rssi=" + String(WiFi.RSSI());
			recordSystemLogEvent("WIFI", eventMessage.c_str());
			if (configPortalActive) {
				wifiDnsServer.stop();
				WiFi.softAPdisconnect(false);
				WiFi.mode(WIFI_STA);
				configPortalActive = false;
				print("WIFI_CONFIG_AP state=CLOSED reason=station_connected\n");
			}
		}
		return;
	}

	if (wifiWasConnected) {
		wifiWasConnected = false;
		wifiConnectStartedMs = millis();
			print("WIFI_STATE state=DISCONNECTED reconnect=enabled\n");
			recordSystemLogEvent("WIFI", "state=DISCONNECTED reconnect=enabled");
	}
	if (!configPortalActive && (uint32_t)(millis() - wifiConnectStartedMs) >= WIFI_CONNECT_TIMEOUT_MS) {
		print("WIFI_STATE state=CONNECT_TIMEOUT action=START_CONFIG_AP_STA\n");
		recordSystemLogEvent("WIFI", "state=CONNECT_TIMEOUT action=START_CONFIG_AP_STA");
		startWiFiConfigPortal(true);
	}
}

void scheduleWiFiRestart() {
	wifiRestartScheduled = true;
	wifiRestartAtMs = millis() + WIFI_RESTART_DELAY_MS;
	recordSystemLogEvent("WIFI", "state=RESTART_SCHEDULED delay_ms=1500");
}

bool isWiFiConfigPortalActive() {
	return configPortalActive;
}

void sendWiFi(const uint8_t *buf, int len) {
	if (WiFi.softAPgetStationNum() == 0 && !WiFi.isConnected()) return;
	udp.beginPacket(udpRemoteIP, udpRemotePort);
	udp.write(buf, len);
	udp.endPacket();
}

int receiveWiFi(uint8_t *buf, int len) {
	udp.parsePacket();
	if (udp.remoteIP()) udpRemoteIP = udp.remoteIP();
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
}

bool configWiFi(bool ap, const char *ssid, const char *password) {
	extern bool armed;
	extern bool motorTestActive;
	if (armed || motorTestActive) {
		recordSystemLogEvent("WIFI_SAVE", "result=FAIL reason=motors_active");
		return false;
	}
	if (!ssid || !password || !*ssid) {
		recordSystemLogEvent("WIFI_SAVE", "result=FAIL reason=invalid_input");
		return false;
	}
	const char *ssidKey = ap ? "WIFI_AP_SSID" : "WIFI_STA_SSID";
	const char *passwordKey = ap ? "WIFI_AP_PASS" : "WIFI_STA_PASS";
	const int requestedMode = ap ? W_AP : W_STA;
	const size_t ssidLength = strlen(ssid);
	const size_t passwordLength = strlen(password);
	const size_t ssidWritten = storage.putString(ssidKey, ssid);
	const size_t passwordWritten = storage.putString(passwordKey, password);
	const size_t modeWritten = storage.putFloat("WIFI_MODE", (float)requestedMode);
	const bool versionOK = storage.getUChar("WIFI_MODE_VER", 0) == 1;
	const bool writeResultsOK = ssidWritten == ssidLength &&
		(passwordLength == 0 || passwordWritten == passwordLength) && modeWritten == sizeof(float) && versionOK;
	const bool readbackOK = storage.getString(ssidKey, "__readback_failed__") == ssid &&
		storage.getString(passwordKey, "__readback_failed__") == password &&
		storage.getFloat("WIFI_MODE", -1.0f) == (float)requestedMode;
	if (!writeResultsOK || !readbackOK) {
		print("WIFI_CONFIG_SAVE result=FAIL ssid_write=%u password_write=%u mode_write=%u version=%u readback=%u\n",
			(unsigned)ssidWritten, (unsigned)passwordWritten, (unsigned)modeWritten, versionOK ? 1 : 0, readbackOK ? 1 : 0);
		recordSystemLogEvent("WIFI_SAVE", "result=FAIL reason=NVS_write_or_readback");
		return false;
	}
	wifiMode = requestedMode;
	print("WIFI_CONFIG_SAVE result=OK mode=%d ssid_length=%u password=not_logged\n",
		wifiMode, (unsigned)ssidLength);
	recordSystemLogEvent("WIFI_SAVE", ap ? "result=OK mode=AP" : "result=OK mode=STA");
	return true;
}

#endif
