#pragma once

#include <Arduino.h>
#include <driver/gpio.h>
#include "board_config.h"

// The optional optical-flow module shares the IMU SPI wires. Even without
// its driver, its active-low CS must be held high before the first transfer.
// These are dedicated chip-select pins; leave I2C/XSHUT pins untouched.
inline void prepareImuSpiChipSelects() {
    // Arduino-ESP32 3.x ignores digitalWrite until pinMode registers GPIO
    // ownership. Preload the hardware latch directly before enabling output
    // so a hard reset cannot briefly select a slave during pin takeover.
    gpio_set_level(static_cast<gpio_num_t>(BOARD_SPI_CS), HIGH);
    pinMode(BOARD_SPI_CS, OUTPUT);
    digitalWrite(BOARD_SPI_CS, HIGH);
#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3) && \
    BOARD_PMW_CS_PIN >= 0 && BOARD_PMW_CS_PIN != BOARD_SPI_CS
    gpio_set_level(static_cast<gpio_num_t>(BOARD_PMW_CS_PIN), HIGH);
    pinMode(BOARD_PMW_CS_PIN, OUTPUT);
    digitalWrite(BOARD_PMW_CS_PIN, HIGH);
#endif
}
