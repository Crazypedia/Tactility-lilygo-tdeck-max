#pragma once

#include <Tactility/hal/keyboard/KeyboardDevice.h>
#include <Tactility/Timer.h>

#include <Tca8418.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/queue.h>

#include <memory>

/**
 * TCA8418 matrix keyboard for the LilyGO T-Deck Pro Max.
 *
 * The base keymap is transcribed verbatim from the vendor reference at
 * Xinyuan-LilyGO/T-Deck-MAX (examples/keypad/keypad.ino). The vendor's debug
 * example only defines the base layer; the ALT (uppercase) and SYM (numbers/
 * symbols) layers here are derived and should be verified/adjusted on hardware.
 */
class TdeckmaxKeyboard final : public tt::hal::keyboard::KeyboardDevice {

    lv_indev_t* kbHandle = nullptr;
    QueueHandle_t queue = nullptr;

    bool backlightOkay = false;
    int backlightImpulseDuty = 0;

    std::shared_ptr<Tca8418> keypad;
    std::unique_ptr<tt::Timer> inputTimer;
    std::unique_ptr<tt::Timer> backlightImpulseTimer;

    // Tracks which (row, col) are currently held so each physical press emits once.
    bool keyHeld[4][10] = {};

    bool initBacklight(gpio_num_t pin, uint32_t frequencyHz);
    void processKeyboard();
    void processBacklightImpulse();

    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

public:

    explicit TdeckmaxKeyboard(const std::shared_ptr<Tca8418>& tca) : keypad(tca) {
        queue = xQueueCreate(20, sizeof(char));
    }

    ~TdeckmaxKeyboard() override {
        vQueueDelete(queue);
    }

    std::string getName() const override { return "T-Deck Pro Max Keyboard"; }
    std::string getDescription() const override { return "TCA8418 I2C matrix keyboard"; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    bool isAttached() const override;
    lv_indev_t* getLvglIndev() override { return kbHandle; }

    bool setBacklightDuty(uint8_t duty);
    void makeBacklightImpulse();
};
