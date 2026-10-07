#include "Arduino.h"
#include <cassert>
#include <vector>

constexpr int HIGH = 1;
constexpr int OUTPUT = 3;
static bool owned[49] = {};
static unsigned latch[49] = {};
static std::vector<int> drivenLow;
static std::vector<int> latchWrites;

// Model Arduino-ESP32 3.x: digitalWrite cannot preload an unowned pin.
void digitalWrite(int pin, int level) {
    if (owned[pin]) latch[pin] = level;
}
void pinMode(int pin, int) {
    owned[pin] = true;
    if (!latch[pin]) drivenLow.push_back(pin);
}
int gpio_set_level(int pin, unsigned level) {
    latchWrites.push_back(pin);
    latch[pin] = level;
    return 0;
}

#include "../spi_bus_startup.h"

int main() {
    std::vector<int> expectedWrites = {BOARD_SPI_CS};
#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    expectedWrites.push_back(BOARD_PMW_CS_PIN);
#endif
    prepareImuSpiChipSelects();
    assert(latchWrites == expectedWrites);
    assert(drivenLow.empty()); // Neither slave may be selected during takeover.
    assert(owned[BOARD_SPI_CS] && latch[BOARD_SPI_CS] == HIGH);
#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    assert(owned[BOARD_PMW_CS_PIN] && latch[BOARD_PMW_CS_PIN] == HIGH);
#endif
    for (int pin = 0; pin < 49; ++pin) {
        if (pin == BOARD_SPI_CS) continue;
#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
        if (pin == BOARD_PMW_CS_PIN) continue;
#endif
        assert(!owned[pin]); // No ownership changes to data/I2C/motor pins.
    }
    prepareImuSpiChipSelects(); // Safe to repeat before bus startup.
    const auto firstWrites = expectedWrites;
    expectedWrites.insert(expectedWrites.end(), firstWrites.begin(), firstWrites.end());
    assert(latchWrites == expectedWrites);
    assert(drivenLow.empty());
}
