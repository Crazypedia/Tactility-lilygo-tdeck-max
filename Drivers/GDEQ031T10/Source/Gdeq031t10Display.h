#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/touch/TouchDevice.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <lvgl.h>
#include <memory>

/**
 * Driver for the GoodDisplay GDEQ031T10 (UC8253-family controller) 3.1" 320x240
 * SPI e-paper panel, as used on the LilyGO T-Deck Pro/Max.
 *
 * Command sequence and timings are ported from the vendor reference driver in
 * Xinyuan-LilyGO/T-Deck-MAX (examples/Elink_paper/GDEQ031T10_Arduino).
 */
class Gdeq031t10Display final : public tt::hal::display::DisplayDevice {

public:

    enum class RefreshMode {
        Full,    // ~3s, best quality
        Fast,    // ~1.0s
        Slow,    // ~1.5s, fast LUT with extra settling
        Partial  // ~0.5s, full-frame partial-mode refresh (more ghosting)
    };

    class Configuration {
    public:

        Configuration(
            spi_host_device_t spiHost,
            gpio_num_t pinCs,
            gpio_num_t pinDc,
            gpio_num_t pinReset,
            gpio_num_t pinBusy,
            std::shared_ptr<tt::hal::touch::TouchDevice> touch = nullptr,
            int clockSpeedHz = 4'000'000,
            RefreshMode defaultRefreshMode = RefreshMode::Full,
            bool mirror180 = false
        ) : spiHost(spiHost),
            pinCs(pinCs),
            pinDc(pinDc),
            pinReset(pinReset),
            pinBusy(pinBusy),
            touch(std::move(touch)),
            clockSpeedHz(clockSpeedHz),
            defaultRefreshMode(defaultRefreshMode),
            mirror180(mirror180)
        {}

        spi_host_device_t spiHost;
        gpio_num_t pinCs;
        gpio_num_t pinDc;
        gpio_num_t pinReset;
        gpio_num_t pinBusy;
        std::shared_ptr<tt::hal::touch::TouchDevice> touch;
        int clockSpeedHz;
        RefreshMode defaultRefreshMode;
        /** Panel is mounted upside down relative to the reference orientation */
        bool mirror180;
    };

    static constexpr uint16_t WIDTH = 240;
    static constexpr uint16_t HEIGHT = 320;
    static constexpr size_t FRAMEBUFFER_SIZE = (WIDTH * HEIGHT) / 8; // 1 bpp packed

private:

    std::unique_ptr<Configuration> configuration;
    spi_device_handle_t spiDevice = nullptr;
    lv_display_t* _Nullable lvglDisplay = nullptr;
    /** Mirrors what the panel currently holds, required by the controller's
     * "old data" + "new data" double-buffered refresh protocol. */
    std::unique_ptr<uint8_t[]> shadowFramebuffer;
    /** Render target for LVGL; copied to the panel on flush */
    std::unique_ptr<uint8_t[]> renderFramebuffer;
    bool initialized = false;
    bool powered = false;
    bool deepInitDone = false;
    RefreshMode currentRefreshMode = RefreshMode::Full;

    void writeCommand(uint8_t command);
    void writeData(const uint8_t* data, size_t length);
    void writeData(uint8_t data) { writeData(&data, 1); }
    void waitWhileBusy() const;
    void reset() const;

    void initFull();
    void initFast();
    void initSlow();
    void initPartial();
    void ensureInitialized(RefreshMode mode);

    void powerOff();
    void refresh();

    static void flushCallback(lv_display_t* display, const lv_area_t* area, uint8_t* pixelMap);

public:

    explicit Gdeq031t10Display(std::unique_ptr<Configuration> inConfiguration) : configuration(std::move(inConfiguration)) {
        assert(configuration != nullptr);
    }

    std::string getName() const override { return "GDEQ031T10"; }

    std::string getDescription() const override { return "GoodDisplay GDEQ031T10 e-paper display"; }

    bool start() override;
    bool stop() override;

    void setPowerOn(bool turnOn) override;
    bool isPoweredOn() const override { return powered; }
    bool supportsPowerControl() const override { return true; }

    void requestFullRefresh() override;

    std::shared_ptr<tt::hal::touch::TouchDevice> _Nullable getTouchDevice() override {
        return configuration->touch;
    }

    bool supportsLvgl() const override { return true; }
    bool startLvgl() override;
    bool stopLvgl() override;
    lv_display_t* _Nullable getLvglDisplay() const override { return lvglDisplay; }

    bool supportsDisplayDriver() const override { return false; }
    std::shared_ptr<tt::hal::display::DisplayDriver> _Nullable getDisplayDriver() override { return nullptr; }

    /** Force the next flush to use a specific refresh mode (one-shot) */
    void setRefreshMode(RefreshMode mode) { currentRefreshMode = mode; }
};
