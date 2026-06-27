#pragma once

#include <Tactility/hal/touch/TouchDevice.h>
#include <tactility/device.h>

// Minimal port of the CST66xx touch protocol for LVGL pointer input.
class Cst66xxTouch final : public tt::hal::touch::TouchDevice {

public:

    struct Configuration {
        ::Device* i2cController;
        uint8_t address;
        uint16_t width;
        uint16_t height;
        bool swapXy;
        bool mirrorX;
        bool mirrorY;
    };

    explicit Cst66xxTouch(const Configuration& configuration) : configuration(configuration) {}

    std::string getName() const override { return "CST66xx"; }
    std::string getDescription() const override { return "CST66xx I2C capacitive touch"; }

    bool start() override;
    bool stop() override;

    bool supportsLvgl() const override { return true; }
    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;
    lv_indev_t* getLvglIndev() override { return indev; }

    bool supportsTouchDriver() override { return false; }
    std::shared_ptr<tt::hal::touch::TouchDriver> getTouchDriver() override { return nullptr; }

private:

    Configuration configuration;
    lv_indev_t* indev = nullptr;
    int16_t lastX = 0;
    int16_t lastY = 0;

    bool readPoint(int16_t& x, int16_t& y);
    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);
};
