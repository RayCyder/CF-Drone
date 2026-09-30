#pragma once

#include "Arduino.h"
#include "Wire.h"
#include "SPI.h"
#include "logger.h"
#include "../imu_wait_trace.h"

#ifdef ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// IMU driver interface
class IMUInterface {
public:
	enum DLPF {
		DLPF_OFF,
		DLPF_MAX,
		DLPF_100HZ_APPROX,
		DLPF_50HZ_APPROX,
		DLPF_5HZ_APPROX,
		DLPF_MIN
	};
	enum AccelRange {
		ACCEL_RANGE_MIN,
		ACCEL_RANGE_2G,
		ACCEL_RANGE_4G,
		ACCEL_RANGE_8G,
		ACCEL_RANGE_16G,
		ACCEL_RANGE_MAX
	};
	enum GyroRange {
		GYRO_RANGE_MIN,
		GYRO_RANGE_250DPS,
		GYRO_RANGE_500DPS,
		GYRO_RANGE_1000DPS,
		GYRO_RANGE_2000DPS,
		GYRO_RANGE_MAX
	};
	enum Rate {
		RATE_MIN,
		RATE_50HZ_APPROX,
		RATE_1KHZ_APPROX,
		RATE_8KHZ_APPROX,
		RATE_MAX
	};
	virtual bool begin() = 0;
	virtual void reset() = 0;
	virtual int status() const = 0; // 0 - success, otherwise error
	virtual uint8_t whoAmI() = 0;
	virtual bool read() = 0;
	virtual bool waitForData(uint32_t timeoutMs = 10) = 0;
	virtual void getAccel(float& x, float& y, float& z) const = 0;
	virtual void getGyro(float& x, float& y, float& z) const = 0;
	virtual void getMag(float& x, float& y, float& z) const = 0;
	virtual float getTemp() = 0;
	virtual bool setRate(const Rate rate) = 0;
	virtual float getRate() = 0;
	virtual bool setAccelRange(const AccelRange range) = 0;
	virtual bool setGyroRange(const GyroRange range) = 0;
	virtual bool setDLPF(const DLPF dlpf) = 0;
	virtual const char* getModel() const = 0;
	virtual bool setupInterrupt() = 0;
};

// Base for all IMU drivers
class IMUBase : public IMUInterface, public Logger {
private:
	bool usingInterrupt = false;
	int interruptPin = -1;

#ifdef ESP32
	SemaphoreHandle_t interruptSemaphore;
	hw_timer_t *timer = NULL;

#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
	volatile uint32_t diagnosticInterruptCount_ = 0;
	volatile uint32_t diagnosticLastInterruptUs_ = 0;
	ImuWaitTrace diagnosticLastWait_ = {};

	void ARDUINO_ISR_ATTR recordDiagnosticInterrupt() {
		__atomic_add_fetch(&diagnosticInterruptCount_, 1, __ATOMIC_RELAXED);
		__atomic_store_n(&diagnosticLastInterruptUs_, (uint32_t)micros(), __ATOMIC_RELEASE);
	}

	bool readForDiagnosticWait() {
		++diagnosticLastWait_.readAttempts;
		const uint32_t startedUs = (uint32_t)micros();
		const bool ready = this->read();
		const uint32_t elapsedUs = (uint32_t)micros() - startedUs;
		if (UINT16_MAX - diagnosticLastWait_.readTotalUs < elapsedUs)
			diagnosticLastWait_.readTotalUs = UINT16_MAX;
		else
			diagnosticLastWait_.readTotalUs += (uint16_t)elapsedUs;
		if (elapsedUs > diagnosticLastWait_.readMaxUs)
			diagnosticLastWait_.readMaxUs = elapsedUs > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsedUs;
		if (ready) ++diagnosticLastWait_.readyReads;
		return ready;
	}

	void finishDiagnosticWait(bool result, uint32_t interruptCountAtStart) {
		diagnosticLastWait_.waitEndedUs = (uint32_t)micros();
		const uint32_t interruptDelta = __atomic_load_n(&diagnosticInterruptCount_, __ATOMIC_ACQUIRE) - interruptCountAtStart;
		diagnosticLastWait_.interruptCount = interruptDelta > UINT16_MAX ? UINT16_MAX : (uint16_t)interruptDelta;
		diagnosticLastWait_.lastInterruptUs = __atomic_load_n(&diagnosticLastInterruptUs_, __ATOMIC_ACQUIRE);
		diagnosticLastWait_.result = result ? 1 : 0;
	}

	bool waitForDataWithDiagnostics(uint32_t timeoutMs) {
		diagnosticLastWait_ = {};
		diagnosticLastWait_.waitStartedUs = (uint32_t)micros();
		diagnosticLastWait_.interruptSource = !usingInterrupt ? IMU_INTERRUPT_NONE :
			interruptPin == -1 ? IMU_INTERRUPT_SOFTWARE_TIMER : IMU_INTERRUPT_DRDY_PIN;
		const uint32_t interruptCountAtStart = __atomic_load_n(&diagnosticInterruptCount_, __ATOMIC_ACQUIRE);
		if (this->status() && interruptPin != -1) {
			finishDiagnosticWait(false, interruptCountAtStart);
			return false;
		}
		if (usingInterrupt) {
			const uint32_t startedUs = diagnosticLastWait_.waitStartedUs;
			const uint32_t timeoutUs = timeoutMs * 1000UL;
			do {
				const uint32_t elapsedUs = (uint32_t)(micros() - startedUs);
				if (elapsedUs >= timeoutUs) break;
				const uint32_t remaining = (timeoutUs - elapsedUs + 999UL) / 1000UL;
				++diagnosticLastWait_.semaphoreTakes;
				if (xSemaphoreTake(interruptSemaphore, pdMS_TO_TICKS(remaining)) != pdTRUE) {
					++diagnosticLastWait_.semaphoreTimeouts;
					break;
				}
				if (readForDiagnosticWait()) {
					finishDiagnosticWait(true, interruptCountAtStart);
					return true;
				}
				const uint32_t pollStartedUs = (uint32_t)micros();
				while ((uint32_t)((uint32_t)micros() - pollStartedUs) < 200UL &&
					(uint32_t)((uint32_t)micros() - startedUs) < timeoutUs) {
					delayMicroseconds(20);
					if (readForDiagnosticWait()) {
						finishDiagnosticWait(true, interruptCountAtStart);
						return true;
					}
				}
			} while ((uint32_t)((uint32_t)micros() - startedUs) < timeoutUs);
		} else {
			const uint32_t startedMs = millis();
			do {
				if (readForDiagnosticWait()) {
					finishDiagnosticWait(true, interruptCountAtStart);
					return true;
				}
				delay(0);
			} while ((uint32_t)(millis() - startedMs) < timeoutMs);
		}
		finishDiagnosticWait(false, interruptCountAtStart);
		return false;
	}
#endif

	static void ARDUINO_ISR_ATTR interruptHandler(void *interruptSemaphore) {
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
		IMUBase *instance = (IMUBase *)interruptSemaphore;
		instance->recordDiagnosticInterrupt();
		xSemaphoreGiveFromISR(instance->interruptSemaphore, &xHigherPriorityTaskWoken);
#else
		xSemaphoreGiveFromISR((SemaphoreHandle_t)interruptSemaphore, &xHigherPriorityTaskWoken);
#endif
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}

	bool setupInterruptTimer() {
		interruptSemaphore = xSemaphoreCreateBinary();

		float rate = getRate();
		uint32_t frequency = 1000000;
		// The software timer substitutes for an unwired DRDY pin. Make it wake
		// slightly after the nominal sensor period so phase drift cannot make the
		// loop poll a not-yet-ready sample and then wait through another full tick.
		constexpr uint64_t IMU_TIMER_PHASE_MARGIN_US = 10;
		uint64_t alarmValue = round(1000000 / rate) + IMU_TIMER_PHASE_MARGIN_US;

		if (timer != NULL) {
			timerEnd(timer);
		}

		timer = timerBegin(frequency);
		if (timer == NULL) {
			log("Failed to create timer");
			return false;
		}

		timerAttachInterruptArg(timer, IMUBase::interruptHandler,
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
			this
#else
			interruptSemaphore
#endif
		);
		timerAlarm(timer, alarmValue, true, 0);
		usingInterrupt = true;
		return true;
	}

	bool setupInterruptPin(uint8_t pin) {
		interruptSemaphore = xSemaphoreCreateBinary();
		pinMode(pin, INPUT_PULLUP);
		attachInterruptArg(digitalPinToInterrupt(pin), IMUBase::interruptHandler,
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
			this,
#else
			interruptSemaphore,
#endif
			FALLING);
		usingInterrupt = true;
		interruptPin = pin;
		return true;
	}
#else
	bool setupInterruptTimer() { return false; }
	bool setupInterruptPin(uint8_t pin) { return false; }
#endif

protected:
	bool setupInterrupt(int pin = -1) {
		if (usingInterrupt) return true; // already set

		if (pin == -1) {
			return setupInterruptTimer();
		} else {
			return setupInterruptPin(pin);
		}
	}

public:
	bool waitForData(uint32_t timeoutMs = 10) override {
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
		return waitForDataWithDiagnostics(timeoutMs);
#else
		if (this->status() && interruptPin != -1) return false;

		if (usingInterrupt) {
#ifdef ESP32
			const uint32_t startedUs = micros();
			const uint32_t timeoutUs = timeoutMs * 1000UL;
			do {
				const uint32_t elapsedUs = (uint32_t)(micros() - startedUs);
				if (elapsedUs >= timeoutUs) return false;
				const uint32_t remaining = (timeoutUs - elapsedUs + 999UL) / 1000UL;
				if (xSemaphoreTake(interruptSemaphore, pdMS_TO_TICKS(remaining)) != pdTRUE) return false;
				// The ESP32 board has no wired MPU data-ready pin, so the software
				// timer can fire just before the sensor sets RAW_DATA_RDY. Poll briefly
				// after the wake: waiting for the next 1 kHz timer tick here can stretch
				// an otherwise healthy control iteration by a full millisecond.
				if (this->read()) return true;
				const uint32_t pollStartedUs = micros();
				while ((uint32_t)(micros() - pollStartedUs) < 200UL &&
					(uint32_t)(micros() - startedUs) < timeoutUs) {
					delayMicroseconds(20);
					if (this->read()) return true;
				}
			} while ((uint32_t)(micros() - startedUs) < timeoutUs);
			return false;
#endif
		} else {
			const uint32_t started = millis();
			do {
				if (this->read()) return true;
				delay(0);
			} while ((uint32_t)(millis() - started) < timeoutMs);
		}
		return false;
#endif
	}

#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
	const ImuWaitTrace &lastWaitTrace() const { return diagnosticLastWait_; }
#endif
};
