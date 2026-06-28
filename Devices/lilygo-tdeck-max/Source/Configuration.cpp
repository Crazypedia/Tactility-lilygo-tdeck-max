#include "devices/Display.h"
#include "devices/Radio.h"
#include "devices/TdeckmaxKeyboard.h"
#include "devices/TdeckmaxPower.h"

#include <Bq27220.h>
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>

using namespace tt::hal;

// LoRa and SD card share the EPD's SPI bus but aren't wired up yet, so their
// chip-select lines are left floating. Deassert them before the EPD driver
// touches the bus, matching the vendor reference driver's own setup(), so a
// floating CS can't make either chip latch EPD command bytes meant for it.
constexpr auto LORA_PIN_CS = GPIO_NUM_3;
constexpr auto SD_PIN_CS = GPIO_NUM_48;

// Reset lines routed through the XL9555 IO expander (P-numbers from the vendor
// lib/TDeckMaxBoard/src/TDeckMaxBoard.h). Both are active low.
constexpr auto XL9555_PIN_TOUCH_RST = 7; // P07
constexpr auto XL9555_PIN_KEY_RST = 9;   // P11

// LoRa power and antenna control, also via the XL9555 (vendor TDeckMaxBoard.h).
// LORA_EN must be HIGH to power the SX1262; LORA_SEL HIGH selects the internal
// antenna (LOW = external). The XL9555 latches the output level, so the level
// persists after the descriptor is released.
constexpr auto XL9555_PIN_LORA_EN = 1;  // P01: HIGH enables SX1262 power
constexpr auto XL9555_PIN_LORA_SEL = 4; // P04: HIGH = internal antenna

// Release the touch and keyboard reset lines held by the XL9555. Without this
// the touch controller may stay in a half-powered state and the keyboard's
// TCA8418 is held in reset, so neither responds. Mirrors the vendor factory
// firmware's XL9555 init (examples/factory/factory.ino).
static void initIoExpander() {
    auto* xl9555 = device_find_by_name("xl9555");
    if (xl9555 == nullptr) {
        // Boot anyway; display works without the expander, but touch/keyboard won't.
        return;
    }

    auto* touch_rst = gpio_descriptor_acquire(xl9555, XL9555_PIN_TOUCH_RST, GPIO_OWNER_GPIO);
    auto* key_rst = gpio_descriptor_acquire(xl9555, XL9555_PIN_KEY_RST, GPIO_OWNER_GPIO);

    if (touch_rst != nullptr) {
        gpio_descriptor_set_flags(touch_rst, GPIO_FLAG_DIRECTION_OUTPUT);
        gpio_descriptor_set_level(touch_rst, false);
    }
    if (key_rst != nullptr) {
        gpio_descriptor_set_flags(key_rst, GPIO_FLAG_DIRECTION_OUTPUT);
        gpio_descriptor_set_level(key_rst, false);
    }

    delay_millis(20);

    // Release both resets and give the controllers time to boot before they're probed.
    if (touch_rst != nullptr) {
        gpio_descriptor_set_level(touch_rst, true);
        gpio_descriptor_release(touch_rst);
    }
    if (key_rst != nullptr) {
        gpio_descriptor_set_level(key_rst, true);
        gpio_descriptor_release(key_rst);
    }

    // Power up the SX1262 and select the internal antenna. Without LORA_EN high
    // the radio is unpowered and won't enumerate on the SPI bus.
    auto* lora_en = gpio_descriptor_acquire(xl9555, XL9555_PIN_LORA_EN, GPIO_OWNER_GPIO);
    auto* lora_sel = gpio_descriptor_acquire(xl9555, XL9555_PIN_LORA_SEL, GPIO_OWNER_GPIO);
    if (lora_en != nullptr) {
        gpio_descriptor_set_flags(lora_en, GPIO_FLAG_DIRECTION_OUTPUT);
        gpio_descriptor_set_level(lora_en, true);
        gpio_descriptor_release(lora_en);
    }
    if (lora_sel != nullptr) {
        gpio_descriptor_set_flags(lora_sel, GPIO_FLAG_DIRECTION_OUTPUT);
        gpio_descriptor_set_level(lora_sel, true);
        gpio_descriptor_release(lora_sel);
    }

    delay_millis(60);
}

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

    // The SX1262 DIO1 line uses a per-pin GPIO ISR (LevelInterruptHandler), which
    // needs the shared ISR service. INVALID_STATE means it's already installed.
    esp_err_t isr_result = gpio_install_isr_service(0);
    if (isr_result != ESP_OK && isr_result != ESP_ERR_INVALID_STATE) {
        LOG_W("tdeckmax", "Failed to install GPIO ISR service: %s", esp_err_to_name(isr_result));
    }

    initIoExpander();
    return true;
}

static DeviceVector createDevices() {
    auto* i2c = device_find_by_name("i2c0");

    // SD card is no longer created here: it's an espressif,esp32-sdspi node in
    // the devicetree (sdcard@1 under spi0), started by Hal::init's mountAll().
    DeviceVector devices = {
        createDisplay(),
        createRadio()
    };

    if (i2c != nullptr) {
        auto keypad = std::make_shared<Tca8418>(i2c);
        devices.push_back(keypad);
        devices.push_back(std::make_shared<TdeckmaxKeyboard>(keypad));

        // Battery metrics from the BQ27220 fuel gauge (0x55); power-off via the
        // SY6970 charger (0x6A). Enables the launcher's power-off button.
        auto gauge = std::make_shared<Bq27220>(i2c);
        devices.push_back(gauge);
        devices.push_back(std::make_shared<TdeckmaxPower>(gauge, i2c));
    }

    return devices;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
