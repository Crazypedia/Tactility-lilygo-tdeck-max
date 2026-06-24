#include "Gdeq031t10Display.h"

#include <Tactility/Logger.h>

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <themes/mono/lv_theme_mono.h>

static const auto LOGGER = tt::Logger("GDEQ031T10");

// UC8253-family commands, ported from Xinyuan-LilyGO/T-Deck-MAX's
// Display_EPD_W21.cpp reference driver.
namespace {
constexpr uint8_t CMD_PANEL_SETTING = 0x00;
constexpr uint8_t CMD_POWER_ON_OFF = 0x02; // shared opcode: power on when followed by 0x04, power off as standalone 0x02
constexpr uint8_t CMD_POWER_ON = 0x04;
constexpr uint8_t CMD_DEEP_SLEEP = 0x07;
constexpr uint8_t CMD_DATA_START_OLD = 0x10;
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;
constexpr uint8_t CMD_DATA_START_NEW = 0x13;
constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;
constexpr uint8_t CMD_PARTIAL_WINDOW = 0x90;
constexpr uint8_t CMD_PARTIAL_IN = 0x91;
constexpr uint8_t CMD_FAST_MODE_ENABLE = 0xE0;
constexpr uint8_t CMD_FAST_MODE_TIMING = 0xE5;

constexpr uint8_t DEEP_SLEEP_CHECK_CODE = 0xA5;
}

void Gdeq031t10Display::writeCommand(uint8_t command) {
    gpio_set_level(configuration->pinDc, 0);
    spi_transaction_t transaction = {};
    transaction.length = 8;
    transaction.tx_buffer = &command;
    spi_device_polling_transmit(spiDevice, &transaction);
}

void Gdeq031t10Display::writeData(const uint8_t* data, size_t length) {
    gpio_set_level(configuration->pinDc, 1);
    spi_transaction_t transaction = {};
    transaction.length = length * 8;
    transaction.tx_buffer = data;
    spi_device_polling_transmit(spiDevice, &transaction);
}

void Gdeq031t10Display::waitWhileBusy() const {
    // BUSY pin reads high when the controller is idle/ready.
    while (gpio_get_level(configuration->pinBusy) != 1) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void Gdeq031t10Display::reset() const {
    gpio_set_level(configuration->pinReset, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(configuration->pinReset, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void Gdeq031t10Display::initFull() {
    reset();
    writeCommand(CMD_PANEL_SETTING);
    writeData(configuration->mirror180 ? 0x13 : 0x1F);
    writeCommand(CMD_POWER_ON);
    waitWhileBusy();
}

void Gdeq031t10Display::initFast() {
    initFull();
    writeCommand(CMD_FAST_MODE_ENABLE);
    writeData(0x02);
    writeCommand(CMD_FAST_MODE_TIMING);
    writeData(0x5A); // ~1.0s
}

void Gdeq031t10Display::initSlow() {
    initFull();
    writeCommand(CMD_FAST_MODE_ENABLE);
    writeData(0x02);
    writeCommand(CMD_FAST_MODE_TIMING);
    writeData(0x6E); // ~1.5s
}

void Gdeq031t10Display::initPartial() {
    initFull();
    writeCommand(CMD_FAST_MODE_ENABLE);
    writeData(0x02);
    writeCommand(CMD_FAST_MODE_TIMING);
    writeData(0x79);
    writeCommand(CMD_VCOM_DATA_INTERVAL);
    writeData(0xD7);
}

void Gdeq031t10Display::powerOff() {
    writeCommand(CMD_POWER_ON_OFF); // 0x02 standalone = power off
    waitWhileBusy();
}

void Gdeq031t10Display::refresh() {
    switch (currentRefreshMode) {
        case RefreshMode::Full:
            initFull();
            break;
        case RefreshMode::Fast:
            initFast();
            break;
        case RefreshMode::Slow:
            initSlow();
            break;
        case RefreshMode::Partial:
            initPartial();
            break;
    }

    // The packed 1bpp bitmap LVGL rendered lives after the I1 palette header.
    const uint8_t* renderBitmap = renderFramebuffer.get() + LVGL_I1_PALETTE_SIZE;

    // LVGL's I1 bit polarity is inverted relative to the panel (the controller
    // treats 1 as black, LVGL packs 1 as the lighter colour), so invert before
    // sending. shadowFramebuffer keeps the panel-polarity copy of the last frame
    // for the controller's old/new differential refresh.
    writeCommand(CMD_DATA_START_OLD);
    writeData(shadowFramebuffer.get(), FRAMEBUFFER_SIZE);

    for (size_t i = 0; i < FRAMEBUFFER_SIZE; i++) {
        shadowFramebuffer[i] = static_cast<uint8_t>(~renderBitmap[i]);
    }
    writeCommand(CMD_DATA_START_NEW);
    writeData(shadowFramebuffer.get(), FRAMEBUFFER_SIZE);

    writeCommand(CMD_DISPLAY_REFRESH);
    vTaskDelay(pdMS_TO_TICKS(1)); // datasheet requires >=200us settle before polling BUSY
    waitWhileBusy();

    powerOff();
}

void Gdeq031t10Display::flushCallback(lv_display_t* display, const lv_area_t* area, uint8_t* pixelMap) {
    // pixelMap points into renderFramebuffer (it was passed directly to lv_display_set_buffers),
    // so the rendered frame is already in place; just trigger the panel update.
    auto* self = static_cast<Gdeq031t10Display*>(lv_display_get_user_data(display));

    if (lv_display_flush_is_last(display)) {
        self->refresh();
    }

    lv_display_flush_ready(display);
}

bool Gdeq031t10Display::start() {
    if (initialized) {
        return true;
    }

    gpio_config_t outputConfig = {
        .pin_bit_mask = (1ULL << configuration->pinDc) | (1ULL << configuration->pinReset),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&outputConfig);

    gpio_config_t busyConfig = {
        .pin_bit_mask = 1ULL << configuration->pinBusy,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&busyConfig);

    spi_device_interface_config_t deviceConfig = {
        .mode = 0,
        .clock_speed_hz = configuration->clockSpeedHz,
        .spics_io_num = configuration->pinCs,
        .queue_size = 1,
    };

    if (spi_bus_add_device(configuration->spiHost, &deviceConfig, &spiDevice) != ESP_OK) {
        LOGGER.error("Failed to add SPI device");
        return false;
    }

    shadowFramebuffer = std::make_unique<uint8_t[]>(FRAMEBUFFER_SIZE);
    // LVGL needs room for the 8-byte I1 palette ahead of the 1bpp bitmap.
    renderFramebuffer = std::make_unique<uint8_t[]>(LVGL_I1_PALETTE_SIZE + FRAMEBUFFER_SIZE);
    std::memset(shadowFramebuffer.get(), 0xFF, FRAMEBUFFER_SIZE);
    std::memset(renderFramebuffer.get(), 0xFF, LVGL_I1_PALETTE_SIZE + FRAMEBUFFER_SIZE);

    currentRefreshMode = configuration->defaultRefreshMode;
    initFull();
    powered = true;
    initialized = true;
    return true;
}

bool Gdeq031t10Display::stop() {
    if (!initialized) {
        return true;
    }

    stopLvgl();
    setPowerOn(false);

    spi_bus_remove_device(spiDevice);
    spiDevice = nullptr;
    shadowFramebuffer.reset();
    renderFramebuffer.reset();
    initialized = false;
    return true;
}

void Gdeq031t10Display::setPowerOn(bool turnOn) {
    if (turnOn == powered) {
        return;
    }

    if (turnOn) {
        initFull(); // toggling RST also wakes the panel from deep sleep
    } else {
        powerOff();
        vTaskDelay(pdMS_TO_TICKS(100));
        writeCommand(CMD_DEEP_SLEEP);
        writeData(DEEP_SLEEP_CHECK_CODE);
    }

    powered = turnOn;
}

void Gdeq031t10Display::requestFullRefresh() {
    currentRefreshMode = RefreshMode::Full;
}

bool Gdeq031t10Display::startLvgl() {
    if (lvglDisplay != nullptr) {
        return true;
    }

    lvglDisplay = lv_display_create(Gdeq031t10Display::WIDTH, Gdeq031t10Display::HEIGHT);
    if (lvglDisplay == nullptr) {
        return false;
    }

    lv_display_set_user_data(lvglDisplay, this);
    lv_display_set_color_format(lvglDisplay, LV_COLOR_FORMAT_I1);

    // The default colour theme renders accent-coloured text/icons that threshold
    // to near-invisible on a 1bpp panel. Apply LVGL's monochrome theme (light
    // background, dark foreground) so UI content shows as solid black on white.
    lv_theme_t* theme = lv_theme_mono_init(lvglDisplay, false, LV_FONT_DEFAULT);
    lv_display_set_theme(lvglDisplay, theme);
    lv_display_set_render_mode(lvglDisplay, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_buffers(lvglDisplay, renderFramebuffer.get(), nullptr, LVGL_I1_PALETTE_SIZE + FRAMEBUFFER_SIZE, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(lvglDisplay, flushCallback);

    return true;
}

bool Gdeq031t10Display::stopLvgl() {
    if (lvglDisplay == nullptr) {
        return true;
    }

    lv_display_delete(lvglDisplay);
    lvglDisplay = nullptr;
    return true;
}
