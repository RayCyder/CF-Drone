// 电池电压检测
// Battery voltage monitoring

#include "board_config.h"
#include "battery_adc.h"

#define VBAT_ADC_PIN       BOARD_VBAT_ADC_PIN     // 电池电压采样引脚
#define VBAT_ADC_SAMPLES   BOARD_VBAT_ADC_SAMPLES  // 每个均值周期累计的 ADC 样本数
#define VBAT_DIVIDER       (43.0f / 33.0f)  // 分压比：上桥10kΩ+下桥33kΩ，公式 Vbat = Vadc × 43/33
#define VBAT_WARN_THRESHOLD     3.5f  // L1：空载/怠速预警门限（V）
#define VBAT_LOW_THRESHOLD      3.3f  // L2：飞行中低电告警门限（V）
#define VBAT_CRITICAL_THRESHOLD 3.0f  // L3：飞行中危急门限，自动降落（V）
#define VBAT_ABSENT_THRESHOLD   0.5f  // 低于此值视为未接电池，忽略所有告警

#define BATTERY_FLYING_THRUST_MIN    0.15f  // 推力≥此值视为飞行中，L1不适用
#define BATTERY_ACTION_DEBOUNCE_TIME  0.9f  // 低压连续判定防抖时间（秒）

extern double t;
float batteryVoltage = 0.0f;  // 全局最新电压，由 updateBatteryVoltage() 定期刷新
static BatteryAdcAccumulator batteryAdcAccumulator;

bool batteryBlocksArming() {
	return batteryVoltage > VBAT_ABSENT_THRESHOLD && batteryVoltage < VBAT_WARN_THRESHOLD;
}

bool batteryAlertActiveForFlight(bool flying) {
	if (batteryVoltage <= VBAT_ABSENT_THRESHOLD || !isfinite(batteryVoltage)) return false;
	return flying ? batteryVoltage < VBAT_LOW_THRESHOLD : batteryVoltage < VBAT_WARN_THRESHOLD;
}

void updateBatteryVoltage() {
	static double lastCheck = 0;
	if (t - lastCheck < 0.5f) return;  // 每 0.5 秒启动一次分帧采样
	float sampledVoltage = batteryVoltage;
	if (batteryAdcAccumulator.add(analogReadMilliVolts(VBAT_ADC_PIN), VBAT_ADC_SAMPLES,
			VBAT_DIVIDER, sampledVoltage)) {
		batteryVoltage = sampledVoltage;
		lastCheck = t;
	}
}
