// 板载LED灯控制
// Board's LED control

#include "board_config.h"
#include "diagnostics.h"
#include "led_alert_policy.h"

#if BOARD_LED_ENABLED

#define BLINK_PERIOD      500000  // 慢闪：500ms 半周期 → 1 Hz
#define BLINK_FAST_PERIOD  62500  // 快闪：62.5ms 半周期 → 8 Hz


// ---- 外部依赖声明 ----
extern bool armed;
extern int mode;
extern const int AUTO;
extern double t;
extern double controlTime;
extern float rcLossTimeout;
extern float thrustTarget;    // control.ino
extern float batteryVoltage;  // battery.ino
extern bool batteryAlertActiveForFlight(bool flying);
extern bool isInverted; // safety.ino
extern bool descentCalibrationStableMarkReady();
#if WEB_RC_ENABLED
extern bool webRCEnabled;
extern bool useWebRC;
bool isUsingWebRC();
#endif

void setupLED() {
	pinMode(BOARD_LED_PIN, OUTPUT);
	digitalWrite(BOARD_LED_PIN, BOARD_LED_INVERTED ? HIGH : LOW); // 初始熄灭
}

void setLED(bool on) {
	static bool state = false;
	if (on == state) {
		return; // don't call digitalWrite if the state is the same
	}
	digitalWrite(BOARD_LED_PIN, (on ^ BOARD_LED_INVERTED) ? HIGH : LOW);
	state = on;
}

void blinkLED() {
	setLED(micros() / BLINK_PERIOD % 2);
}

// 电池告警：按飞行状态选择阈值
// 飞行中（thrustTarget >= 0.15）→ L2（2.8V），L1 在飞行中不适用
// 未解锁 / 解锁怠速 → L1（3.4V）
bool batteryAlertActive() {
	bool flying = armed && thrustTarget >= 0.15f;
	return batteryAlertActiveForFlight(flying);
}

// 检测是否有任意告警（低电 / 遥控失联 / 倒置）
bool ledAlertActive() {
	if (getActiveDiagnosticFaults() != 0) return true;
	// 倒置检测
	if (isInverted) return true;

	// 遥控失联检测（SBUS RC，仅解锁后）
	if (mode != AUTO && controlTime != 0 && armed && (t - controlTime > rcLossTimeout)) return true;

#if WEB_RC_ENABLED
	// Web RC 失联检测：已激活但超时
	if (webRCEnabled && useWebRC && !isUsingWebRC()) return true;
#endif

	if (batteryAlertActive()) return true;

	return false;
}

bool ledFastBlinkActive() {
	const bool armedAlert = armed && ledAlertActive();
	const bool disarmedAlert = !armed && (batteryAlertActive() || hasBlockingDiagnosticFault());
	return ledFastBlinkRequested(armed, armedAlert, disarmedAlert);
}

// 主循环调用：根据飞行状态驱动 LED
void updateLED() {
	static bool calibrationPromptWasActive = false;
	static uint32_t calibrationPromptStartedMs = 0;
	const bool calibrationPromptActive = descentCalibrationStableMarkReady();
	if (!calibrationPromptActive) calibrationPromptWasActive = false;
	if (ledFastBlinkActive()) {
		setLED(micros() / BLINK_FAST_PERIOD % 2); // 告警：快闪 8Hz
	} else if (calibrationPromptActive) {
		// 5 x (100 ms on + 100 ms off), followed by 1 s off.
		// Fault indication above always has priority.
		if (!calibrationPromptWasActive) {
			calibrationPromptWasActive = true;
			calibrationPromptStartedMs = millis();
		}
		const uint32_t phaseMs = (millis() - calibrationPromptStartedMs) % 2000UL;
		setLED(phaseMs < 1000UL && (phaseMs % 200UL) < 100UL);
	} else if (!armed) {
		setLED(false); // 正常待机：常灭
	} else {
		setLED(micros() / BLINK_PERIOD % 2); // 正常飞行：慢闪 1Hz
	}
}

#else // BOARD_LED_ENABLED == 0（ESP32-C3 无板载 LED）

void setupLED() {}
void setLED(bool on) {}
void updateLED() {}
bool ledFastBlinkActive() { return false; }

#endif // BOARD_LED_ENABLED
