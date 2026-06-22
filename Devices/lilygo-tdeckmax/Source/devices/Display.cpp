#include "Display.h"

#include <Cst3530Touch.h>
#include <Gdeq031t10Display.h>
#include <tactility/check.h>
#include <tactility/device.h>

// Pins and I2C address from Xinyuan-LilyGO/T-Deck-MAX's
// lib/TDeckMaxBoard/src/TDeckMaxBoard.h and docs/pinmap.md.
constexpr auto EPD_SPI_HOST = SPI2_HOST;
constexpr auto EPD_PIN_CS = GPIO_NUM_34;
constexpr auto EPD_PIN_DC = GPIO_NUM_35;
constexpr auto EPD_PIN_RST = GPIO_NUM_9;
constexpr auto EPD_PIN_BUSY = GPIO_NUM_37;
constexpr auto TOUCH_PIN_INT = GPIO_NUM_12;
constexpr uint16_t TOUCH_I2C_ADDRESS = 0x1A;

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto* i2c = device_find_by_name("i2c0");
    check(i2c);

    // The vendor reference driver leaves the touch reset pin undriven
    // (it's wired through the XL9555 expander, not a native GPIO).
    auto configuration = std::make_unique<Cst3530Touch::Configuration>(
        i2c,
        Gdeq031t10Display::WIDTH,
        Gdeq031t10Display::HEIGHT,
        false,
        false,
        false,
        GPIO_NUM_NC,
        TOUCH_PIN_INT,
        0,
        0,
        TOUCH_I2C_ADDRESS
    );

    return std::make_shared<Cst3530Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto configuration = std::make_unique<Gdeq031t10Display::Configuration>(
        EPD_SPI_HOST,
        EPD_PIN_CS,
        EPD_PIN_DC,
        EPD_PIN_RST,
        EPD_PIN_BUSY,
        createTouch(),
        10'000'000
    );

    return std::make_shared<Gdeq031t10Display>(std::move(configuration));
}
