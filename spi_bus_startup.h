#pragma once

#include <Arduino.h>
#include "board_config.h"

// The optional optical-flow module shares the IMU SPI wires. Even without
// its driver, its active-low CS must be held high before the first transfer.
// These are dedicated chip-select pins; leave I2C/XSHUT pins untouched.
inline void prepareImuSpiChipSelects() {
    digitalWrite(BOARD_SPI_CS, HIGH);
    pinMode(BOARD_SPI_CS, OUTPUT);
#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3) && \
    BOARD_PMW_CS_PIN >= 0 && BOARD_PMW_CS_PIN != BOARD_SPI_CS
    digitalWrite(BOARD_PMW_CS_PIN, HIGH);
    pinMode(BOARD_PMW_CS_PIN, OUTPUT);
#endif
}
