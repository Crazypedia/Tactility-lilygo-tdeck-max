#include "devices/Display.h"

#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

// LoRa and SD card share the EPD's SPI bus but aren't wired up yet, so their
// chip-select lines are left floating. Deassert them before the EPD driver
// touches the bus, matching the vendor reference driver's own setup(), so a
// floating CS can't make either chip latch EPD command bytes meant for it.
constexpr auto LORA_PIN_CS = GPIO_NUM_3;
constexpr auto SD_PIN_CS = GPIO_NUM_48;

static bool initBoot() {
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << LORA_PIN_CS) | (1ULL << SD_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);
    gpio_set_level(LORA_PIN_CS, 1);
    gpio_set_level(SD_PIN_CS, 1);
    return true;
}

static DeviceVector createDevices() {
    return {
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
