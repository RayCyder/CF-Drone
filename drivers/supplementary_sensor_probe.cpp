#include "external_sensors.h"
#include "board_config.h"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

namespace {
constexpr uint8_t PMW3901_PRODUCT_ID = 0x49;
constexpr uint8_t PMW3901_INVERSE_ID = 0xB6;
constexpr uint32_t PMW3901_SAMPLE_INTERVAL_US = 10000;
constexpr float PMW3901_RADIANS_PER_PIXEL = 0.002443389f;
constexpr uint8_t VL53L1X_ADDRESS = 0x29;
constexpr uint16_t VL53L1X_MODEL_ID = 0xEACC;
constexpr uint32_t VL53L1X_BOOT_TIMEOUT_MS = 100;
constexpr uint32_t VL53L1X_CALIBRATION_TIMEOUT_MS = 1000;

struct SupplementaryRuntimeState {
    OpticalFlowSample flowSample;
    DownwardRangeSample rangeSample;
    uint32_t flowSampleCount = 0;
    uint32_t flowFailureCount = 0;
    uint32_t rangeSampleCount = 0;
    uint32_t rangeFailureCount = 0;
    uint32_t lastFlowReadUs = 0;
    SemaphoreHandle_t i2cMutex = nullptr;
    bool flowDetected = false;
    bool flowReady = false;
    bool rangeDetected = false;
    bool rangeReady = false;
    bool rangeGpioPolarity = false;
};

SupplementaryRuntimeState supplementary;
portMUX_TYPE supplementarySampleMux = portMUX_INITIALIZER_UNLOCKED;

bool takeI2c() {
    return !supplementary.i2cMutex ||
        xSemaphoreTake(supplementary.i2cMutex, pdMS_TO_TICKS(20)) == pdTRUE;
}

void giveI2c() {
    if (supplementary.i2cMutex) xSemaphoreGive(supplementary.i2cMutex);
}

bool i2cRead(uint16_t reg, uint8_t *data, size_t length) {
    if (!takeI2c()) return false;
    Wire.beginTransmission(VL53L1X_ADDRESS);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)reg);
    if (Wire.endTransmission(false) != 0) {
        giveI2c();
        return false;
    }
    const size_t received = Wire.requestFrom((int)VL53L1X_ADDRESS, (int)length, (int)true);
    if (received != length) {
        while (Wire.available()) Wire.read();
        giveI2c();
        return false;
    }
    for (size_t i = 0; i < length; ++i) data[i] = (uint8_t)Wire.read();
    giveI2c();
    return true;
}

bool i2cWrite(uint16_t reg, const uint8_t *data, size_t length) {
    if (!takeI2c()) return false;
    Wire.beginTransmission(VL53L1X_ADDRESS);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)reg);
    const size_t written = Wire.write(data, length);
    const bool ok = written == length && Wire.endTransmission() == 0;
    giveI2c();
    return ok;
}

bool i2cWrite8(uint16_t reg, uint8_t value) {
    return i2cWrite(reg, &value, 1);
}

bool i2cWrite16(uint16_t reg, uint16_t value) {
    const uint8_t data[] = {(uint8_t)(value >> 8), (uint8_t)value};
    return i2cWrite(reg, data, sizeof(data));
}

bool i2cWrite32(uint16_t reg, uint32_t value) {
    const uint8_t data[] = {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8), (uint8_t)value
    };
    return i2cWrite(reg, data, sizeof(data));
}

bool i2cRead8(uint16_t reg, uint8_t &value) {
    return i2cRead(reg, &value, 1);
}

bool i2cRead16(uint16_t reg, uint16_t &value) {
    uint8_t data[2];
    if (!i2cRead(reg, data, sizeof(data))) return false;
    value = ((uint16_t)data[0] << 8) | data[1];
    return true;
}

#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
uint8_t pmwReadRegister(uint8_t reg) {
    digitalWrite(BOARD_SPI_CS, HIGH);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE3));
    digitalWrite(BOARD_PMW_CS_PIN, LOW);
    delayMicroseconds(50);
    SPI.transfer(reg & 0x7F);
    delayMicroseconds(50);
    const uint8_t value = SPI.transfer(0);
    delayMicroseconds(100);
    digitalWrite(BOARD_PMW_CS_PIN, HIGH);
    SPI.endTransaction();
    return value;
}

void pmwWriteRegister(uint8_t reg, uint8_t value) {
    digitalWrite(BOARD_SPI_CS, HIGH);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE3));
    digitalWrite(BOARD_PMW_CS_PIN, LOW);
    delayMicroseconds(50);
    SPI.transfer(reg | 0x80);
    SPI.transfer(value);
    delayMicroseconds(50);
    digitalWrite(BOARD_PMW_CS_PIN, HIGH);
    SPI.endTransaction();
    delayMicroseconds(200);
}

// Register sequence from the MIT-licensed Bitcraze PMW3901 Arduino driver.
void configurePmw3901() {
    static const uint8_t settings[][2] = {
        {0x7F,0x00},{0x61,0xAD},{0x7F,0x03},{0x40,0x00},{0x7F,0x05},
        {0x41,0xB3},{0x43,0xF1},{0x45,0x14},{0x5B,0x32},{0x5F,0x34},
        {0x7B,0x08},{0x7F,0x06},{0x44,0x1B},{0x40,0xBF},{0x4E,0x3F},
        {0x7F,0x08},{0x65,0x20},{0x6A,0x18},{0x7F,0x09},{0x4F,0xAF},
        {0x5F,0x40},{0x48,0x80},{0x49,0x80},{0x57,0x77},{0x60,0x78},
        {0x61,0x78},{0x62,0x08},{0x63,0x50},{0x7F,0x0A},{0x45,0x60},
        {0x7F,0x00},{0x4D,0x11},{0x55,0x80},{0x74,0x1F},{0x75,0x1F},
        {0x4A,0x78},{0x4B,0x78},{0x44,0x08},{0x45,0x50},{0x64,0xFF},
        {0x65,0x1F},{0x7F,0x14},{0x65,0x60},{0x66,0x08},{0x63,0x78},
        {0x7F,0x15},{0x48,0x58},{0x7F,0x07},{0x41,0x0D},{0x43,0x14},
        {0x4B,0x0E},{0x45,0x0F},{0x44,0x42},{0x4C,0x80},{0x7F,0x10},
        {0x5B,0x02},{0x7F,0x07},{0x40,0x41},{0x70,0x00}
    };
    for (const auto &setting : settings) pmwWriteRegister(setting[0], setting[1]);
    delay(100);
    static const uint8_t finalSettings[][2] = {
        {0x32,0x44},{0x7F,0x07},{0x40,0x40},{0x7F,0x06},{0x62,0xF0},
        {0x63,0x00},{0x7F,0x0D},{0x48,0xC0},{0x6F,0xD5},{0x7F,0x00},
        {0x5B,0xA0},{0x4E,0xA8},{0x5A,0x50},{0x40,0x80}
    };
    for (const auto &setting : finalSettings) pmwWriteRegister(setting[0], setting[1]);
}

bool beginPmw3901(uint8_t &id, uint8_t &inverse) {
    digitalWrite(BOARD_PMW_CS_PIN, HIGH);
    pinMode(BOARD_PMW_CS_PIN, OUTPUT);
    delay(1);
    digitalWrite(BOARD_PMW_CS_PIN, LOW);
    delay(1);
    digitalWrite(BOARD_PMW_CS_PIN, HIGH);
    delay(1);
    id = pmwReadRegister(0x00);
    inverse = pmwReadRegister(0x5F);
    supplementary.flowDetected = id == PMW3901_PRODUCT_ID && inverse == PMW3901_INVERSE_ID;
    if (!supplementary.flowDetected) return false;
    pmwWriteRegister(0x3A, 0x5A);
    delay(5);
    for (uint8_t reg = 0x02; reg <= 0x06; ++reg) pmwReadRegister(reg);
    delay(1);
    configurePmw3901();
    return true;
}

bool readPmw3901(OpticalFlowSample &sample) {
    uint8_t burst[12] = {};
    digitalWrite(BOARD_SPI_CS, HIGH);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE3));
    digitalWrite(BOARD_PMW_CS_PIN, LOW);
    delayMicroseconds(50);
    SPI.transfer(0x16);
    delayMicroseconds(50);
    for (uint8_t &byte : burst) byte = SPI.transfer(0);
    delayMicroseconds(50);
    digitalWrite(BOARD_PMW_CS_PIN, HIGH);
    SPI.endTransaction();
    delayMicroseconds(50);

    sample.deltaX = (int16_t)(((uint16_t)burst[3] << 8) | burst[2]);
    sample.deltaY = (int16_t)(((uint16_t)burst[5] << 8) | burst[4]);
    sample.deltaXAngularRadians = sample.deltaX * PMW3901_RADIANS_PER_PIXEL;
    sample.deltaYAngularRadians = sample.deltaY * PMW3901_RADIANS_PER_PIXEL;
    sample.quality = burst[6];
    sample.motionDetected = (burst[0] & 0x80) != 0;
    sample.timestampUs = micros();
    const bool plausible = sample.deltaX >= -240 && sample.deltaX <= 240 &&
        sample.deltaY >= -240 && sample.deltaY <= 240;
    sample.valid = plausible && sample.quality > 0;
    return plausible;
}
#endif

// ST ULD default register block, also used by the dual BSD/GPL Linux driver.
constexpr uint8_t VL53L1X_DEFAULT_CONFIG[] = {
    0x00,0x00,0x00,0x01,0x02,0x00,0x02,0x08,0x00,0x08,0x10,0x01,0x01,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x0F,0x00,0x00,0x00,0x00,0x00,0x20,0x0B,0x00,0x00,0x02,0x0A,0x21,
    0x00,0x00,0x05,0x00,0x00,0x00,0x00,0xC8,0x00,0x00,0x38,0xFF,0x01,0x00,0x08,0x00,
    0x00,0x01,0xCC,0x0F,0x01,0xF1,0x0D,0x01,0x68,0x00,0x80,0x08,0xB8,0x00,0x00,0x00,
    0x00,0x0F,0x89,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x0F,0x0D,0x0E,0x0E,0x00,
    0x00,0x02,0xC7,0xFF,0x9B,0x00,0x00,0x00,0x01,0x00,0x00
};

bool writeVl53DefaultConfig() {
    constexpr size_t CHUNK = 24;
    for (size_t offset = 0; offset < sizeof(VL53L1X_DEFAULT_CONFIG); offset += CHUNK) {
        const size_t remaining = sizeof(VL53L1X_DEFAULT_CONFIG) - offset;
        const size_t length = remaining < CHUNK ? remaining : CHUNK;
        if (!i2cWrite(0x002D + offset, VL53L1X_DEFAULT_CONFIG + offset, length)) return false;
    }
    return true;
}

bool vl53DataReady(bool &ready) {
    uint8_t gpio = 0;
    if (!i2cRead8(0x0031, gpio)) return false;
    ready = ((gpio & 0x01) != 0) != supplementary.rangeGpioPolarity;
    return true;
}

bool waitVl53DataReady(uint32_t timeoutMs) {
    const uint32_t started = millis();
    do {
        bool ready = false;
        if (vl53DataReady(ready) && ready) return true;
        delay(1);
    } while ((uint32_t)(millis() - started) < timeoutMs);
    return false;
}

bool beginVl53l1x(const char *&failureStage) {
    failureStage = "boot";
#if BOARD_VL53_XSHUT_PIN >= 0
    digitalWrite(BOARD_VL53_XSHUT_PIN, LOW);
    pinMode(BOARD_VL53_XSHUT_PIN, OUTPUT);
    delay(2);
    digitalWrite(BOARD_VL53_XSHUT_PIN, HIGH);
    delay(2);
#endif
    const uint32_t bootStarted = millis();
    uint8_t firmwareStatus = 0;
    while ((!i2cRead8(0x00E5, firmwareStatus) || !(firmwareStatus & 0x01)) &&
        (uint32_t)(millis() - bootStarted) < VL53L1X_BOOT_TIMEOUT_MS) delay(2);
    if (!(firmwareStatus & 0x01)) return false;

    failureStage = "identity";
    uint16_t modelId = 0;
    if (!i2cRead16(0x010F, modelId) || modelId != VL53L1X_MODEL_ID) return false;
    supplementary.rangeDetected = true;

    failureStage = "configuration";
    if (!writeVl53DefaultConfig()) return false;
    uint8_t mux = 0;
    if (!i2cRead8(0x0030, mux)) return false;
    supplementary.rangeGpioPolarity = (mux & 0x10) != 0;

    failureStage = "initial_calibration";
    if (!i2cWrite8(0x0086, 0x01) || !i2cWrite8(0x0087, 0x40) ||
        !waitVl53DataReady(VL53L1X_CALIBRATION_TIMEOUT_MS) ||
        !i2cWrite8(0x0086, 0x01) || !i2cWrite8(0x0087, 0x00) ||
        !i2cWrite8(0x0008, 0x09) || !i2cWrite8(0x000B, 0x00)) return false;

    failureStage = "ranging_configuration";
    if (!i2cWrite8(0x004B, 0x0A) || !i2cWrite8(0x0060, 0x0F) ||
        !i2cWrite8(0x0063, 0x0D) || !i2cWrite8(0x0069, 0xB8) ||
        !i2cWrite8(0x0078, 0x0F) || !i2cWrite8(0x0079, 0x0D) ||
        !i2cWrite8(0x007A, 0x0E) || !i2cWrite8(0x007B, 0x0E) ||
        !i2cWrite16(0x005E, 0x00AD) || !i2cWrite16(0x0061, 0x00C6)) return false;
    uint16_t oscillator = 0;
    if (!i2cRead16(0x00DE, oscillator)) return false;
    const uint32_t interMeasurement =
        ((uint32_t)(oscillator & 0x03FF) * 50U * 1075U) / 1000U;
    if (!i2cWrite32(0x006C, interMeasurement) ||
        !i2cWrite8(0x0086, 0x01) || !i2cWrite8(0x0087, 0x40)) return false;
    failureStage = "ready";
    return true;
}

uint8_t normalizeVl53RangeStatus(uint8_t rawStatus, uint8_t streamCount) {
    switch (rawStatus) {
        case 9: return streamCount == 0 ? 6 : 0; // valid, but first frame has no wrap check
        case 6: return 1;  // sigma failure
        case 4: return 2;  // signal failure
        case 8: return 3;  // valid, minimum range clipped
        case 5: return 4;  // phase/out of bounds
        case 17:
        case 2:
        case 1:
        case 3: return 5;  // hardware failure
        case 7: return 7;  // wrap target failure
        case 12: return 8; // crosstalk signal failure
        case 18: return 9; // synchronization interrupt
        case 13: return 10; // minimum range failure
        default: return 0xFF;
    }
}

bool updateVl53DynamicSpads(const uint8_t result[17]) {
    const uint16_t spadCount = ((uint16_t)result[3] << 8) | result[4];
    const uint16_t ambientRate = ((uint16_t)result[7] << 8) | result[8];
    const uint16_t peakRate = ((uint16_t)result[15] << 8) | result[16];
    uint16_t requiredSpads = 0x8000;
    if (spadCount != 0) {
        uint32_t totalRatePerSpad = (uint32_t)ambientRate + peakRate;
        if (totalRatePerSpad > 0xFFFF) totalRatePerSpad = 0xFFFF;
        totalRatePerSpad = (totalRatePerSpad << 16) / spadCount;
        if (totalRatePerSpad != 0) {
            uint32_t calculated = ((uint32_t)0x0A00 << 16) / totalRatePerSpad;
            if (calculated > 0xFFFF) calculated = 0xFFFF;
            requiredSpads = (uint16_t)calculated;
        }
    }
    return i2cWrite16(0x0054, requiredSpads);
}
} // namespace

void setupSupplementarySensors(bool i2cReady, SemaphoreHandle_t i2cMutex) {
    supplementary = SupplementaryRuntimeState();
    supplementary.i2cMutex = i2cMutex;
    uint8_t id = 0, inverse = 0;
#if BOARD_OPTICAL_FLOW_ENABLED && \
    !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3) && \
    BOARD_PMW_CS_PIN >= 0 && BOARD_PMW_CS_PIN != BOARD_SPI_CS
    supplementary.flowReady = beginPmw3901(id, inverse);
#endif
    Serial.printf("FLOW pmw3901 detected=%u ready=%u id=0x%02X inverse=0x%02X cs=%d\n",
        supplementary.flowDetected, supplementary.flowReady, id, inverse, BOARD_PMW_CS_PIN);

    const char *rangeFailureStage = i2cReady ? "disabled" : "i2c_unavailable";
#if BOARD_DOWNWARD_RANGE_ENABLED
    if (i2cReady) supplementary.rangeReady = beginVl53l1x(rangeFailureStage);
#endif
    Serial.printf("RANGE vl53l1x detected=%u ready=%u address=0x%02X xshut=%d stage=%s\n",
        supplementary.rangeDetected, supplementary.rangeReady, VL53L1X_ADDRESS,
        BOARD_VL53_XSHUT_PIN, rangeFailureStage);
}

void updateSupplementarySensors() {
#if BOARD_OPTICAL_FLOW_ENABLED && \
    !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3) && \
    BOARD_PMW_CS_PIN >= 0 && BOARD_PMW_CS_PIN != BOARD_SPI_CS
    if (!supplementary.flowReady) return;
    const uint32_t now = micros();
    if ((uint32_t)(now - supplementary.lastFlowReadUs) < PMW3901_SAMPLE_INTERVAL_US) return;
    supplementary.lastFlowReadUs = now;
    OpticalFlowSample sample;
    const bool transferValid = readPmw3901(sample);
    portENTER_CRITICAL(&supplementarySampleMux);
    supplementary.flowSample = sample;
    ++supplementary.flowSampleCount;
    if (!transferValid) ++supplementary.flowFailureCount;
    portEXIT_CRITICAL(&supplementarySampleMux);
#endif
}

void pollDownwardRangeSensor() {
    if (!supplementary.rangeReady) return;
    bool ready = false;
    if (!vl53DataReady(ready)) {
        portENTER_CRITICAL(&supplementarySampleMux);
        ++supplementary.rangeFailureCount;
        portEXIT_CRITICAL(&supplementarySampleMux);
        return;
    }
    if (!ready) return;
    uint8_t result[17];
    DownwardRangeSample sample;
    if (!i2cRead(0x0089, result, sizeof(result))) {
        portENTER_CRITICAL(&supplementarySampleMux);
        ++supplementary.rangeFailureCount;
        portEXIT_CRITICAL(&supplementarySampleMux);
        return;
    }
    sample.rawRangeStatus = result[0] & 0x1F;
    sample.rangeStatus = normalizeVl53RangeStatus(sample.rawRangeStatus, result[2]);
    const uint16_t millimeters = ((uint16_t)result[13] << 8) | result[14];
    const uint16_t correctedMillimeters =
        (uint16_t)(((uint32_t)millimeters * 2011U + 0x0400U) / 0x0800U);
    sample.distanceMeters = correctedMillimeters * 0.001f;
    sample.quality = sample.rangeStatus == 0 ? 100 : 0;
    sample.timestampUs = micros();
    sample.valid = sample.rangeStatus == 0 && correctedMillimeters > 0;
    const bool maintained = updateVl53DynamicSpads(result);
    const bool cleared = i2cWrite8(0x0086, 0x01);
    portENTER_CRITICAL(&supplementarySampleMux);
    supplementary.rangeSample = sample;
    ++supplementary.rangeSampleCount;
    if (!maintained || !cleared) ++supplementary.rangeFailureCount;
    portEXIT_CRITICAL(&supplementarySampleMux);
}

bool opticalFlowDetected() { return supplementary.flowDetected; }
bool downwardRangeDetected() { return supplementary.rangeDetected; }
bool opticalFlowAvailable() { return supplementary.flowReady; }
bool downwardRangeAvailable() { return supplementary.rangeReady; }

void markDownwardRangeUnavailable() {
    supplementary.rangeReady = false;
}

bool getOpticalFlowSample(OpticalFlowSample &sample, uint32_t &sampleCount,
    uint32_t &failureCount) {
    portENTER_CRITICAL(&supplementarySampleMux);
    sample = supplementary.flowSample;
    sampleCount = supplementary.flowSampleCount;
    failureCount = supplementary.flowFailureCount;
    portEXIT_CRITICAL(&supplementarySampleMux);
    return sampleCount > 0;
}

bool getDownwardRangeSample(DownwardRangeSample &sample, uint32_t &sampleCount,
    uint32_t &failureCount) {
    portENTER_CRITICAL(&supplementarySampleMux);
    sample = supplementary.rangeSample;
    sampleCount = supplementary.rangeSampleCount;
    failureCount = supplementary.rangeFailureCount;
    portEXIT_CRITICAL(&supplementarySampleMux);
    return sampleCount > 0;
}
