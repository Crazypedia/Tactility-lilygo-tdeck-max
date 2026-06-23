#include "devices/Display.h"
#include "devices/TdeckmaxKeyboard.h"

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Tactility/Logger.h>
#include <Tactility/hal/Configuration.h>
#include <tactility/device.h>
#include <tactility/drivers/gpio_controller.h>

using namespace tt::hal;

static const auto LOGGER = tt::Logger("Tdeckmax");

// LoRa and SD card share the EPD's SPI bus but aren't wired up yet, so their
// chip-select lines are left floating. Deassert them before the EPD driver
// touches the bus, matching the vendor reference driver's own setup(), so a
// floating CS can't make either chip latch EPD command bytes meant for it.
constexpr auto LORA_PIN_CS = GPIO_NUM_3;
constexpr auto SD_PIN_CS = GPIO_NUM_48;

// Touch and keyboard reset lines hang off the XL9555 expander, not native GPIOs
// (BOARD_XL9555_07_TOUCH_RST / BOARD_XL9555_11_KEY_RST in the vendor header).
constexpr auto XL9555_PIN_TOUCH_RST = 7;
constexpr auto XL9555_PIN_KEY_RST = 9;

static void deassertSharedSpiChipSelects() {
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
}

// Pulse an XL9555 output low then high to reset+release the attached chip.
static void pulseExpanderReset(::Device* expander, uint32_t pin, const char* label) {
    auto* descriptor = gpio_descriptor_acquire(expander, pin, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOGGER.warn("Failed to acquire XL9555 pin {} ({})", pin, label);
        return;
    }
    gpio_descriptor_set_flags(descriptor, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_level(descriptor, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_descriptor_set_level(descriptor, true);
    gpio_descriptor_release(descriptor);
}

// Release the touch and keyboard controllers from reset. If the expander isn't
// available we still boot — the display works without it, and touch/keyboard
// will simply fail to respond rather than crashing the board.
static void releasePeripheralResets() {
    auto* expander = device_find_by_name("xl9555");
    if (expander == nullptr) {
        LOGGER.warn("xl9555 not found, skipping touch/keyboard reset release");
        return;
    }
    pulseExpanderReset(expander, XL9555_PIN_TOUCH_RST, "touch reset");
    pulseExpanderReset(expander, XL9555_PIN_KEY_RST, "keyboard reset");
    vTaskDelay(pdMS_TO_TICKS(10));
}

static bool initBoot() {
    deassertSharedSpiChipSelects();
    releasePeripheralResets();
    return true;
}

static DeviceVector createDevices() {
    DeviceVector devices = {
        createDisplay()
    };

    auto* i2c = device_find_by_name("i2c0");
    if (i2c != nullptr) {
        auto keypad = std::make_shared<Tca8418>(i2c);
        devices.push_back(std::make_shared<TdeckmaxKeyboard>(keypad));
    } else {
        LOGGER.error("i2c0 not found, booting without keyboard");
    }

    return devices;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
