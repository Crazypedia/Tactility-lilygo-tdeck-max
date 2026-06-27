#include "Display.h"
#include "Cst66xxTouch.h"

#include <Gdeq031t10Display.h>
#include <Tactility/Logger.h>
#include <tactility/device.h>

static const auto LOGGER = tt::Logger("TdeckmaxDisplay");

// Pins and I2C address from Xinyuan-LilyGO/T-Deck-MAX's
// lib/TDeckMaxBoard/src/TDeckMaxBoard.h and docs/pinmap.md.
constexpr auto EPD_SPI_HOST = SPI2_HOST;
constexpr auto EPD_PIN_CS = GPIO_NUM_34;
constexpr auto EPD_PIN_DC = GPIO_NUM_35;
constexpr auto EPD_PIN_RST = GPIO_NUM_9;
constexpr auto EPD_PIN_BUSY = GPIO_NUM_37;
constexpr uint16_t TOUCH_I2C_ADDRESS = 0x1A;

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto* i2c = device_find_by_name("i2c0");
    if (i2c == nullptr) {
        LOGGER.error("i2c0 not found, booting without touch");
        return nullptr;
    }

    // The touch reset line is released earlier via the XL9555 expander (see
    // Configuration.cpp).
    const Cst66xxTouch::Configuration configuration = {
        .i2cController = i2c,
        .address = TOUCH_I2C_ADDRESS,
        .width = Gdeq031t10Display::WIDTH,
        .height = Gdeq031t10Display::HEIGHT,
        .swapXy = false,
        .mirrorX = false,
        .mirrorY = false,
    };

    return std::make_shared<Cst66xxTouch>(configuration);
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto configuration = std::make_unique<Gdeq031t10Display::Configuration>(
        EPD_SPI_HOST,
        EPD_PIN_CS,
        EPD_PIN_DC,
        EPD_PIN_RST,
        EPD_PIN_BUSY,
        createTouch(),
        10'000'000,
        // Default to a fast (~1s) refresh. A full (~3s) refresh holds the LVGL
        // lock long enough that GuiService's 5s lock timeout trips when several
        // refreshes queue up, leaving app content undrawn.
        Gdeq031t10Display::RefreshMode::Fast
    );

    return std::make_shared<Gdeq031t10Display>(std::move(configuration));
}
