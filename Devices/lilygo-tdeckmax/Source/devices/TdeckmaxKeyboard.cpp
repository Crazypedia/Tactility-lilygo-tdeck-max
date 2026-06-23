#include "TdeckmaxKeyboard.h"

#include <Tactility/Logger.h>

#include <tactility/drivers/i2c_controller.h>

static const auto LOGGER = tt::Logger("TdeckmaxKeyboard");

// BOARD_KEYBOARD_LED from the vendor TDeckMaxBoard.h
constexpr auto BACKLIGHT_PIN = GPIO_NUM_42;
constexpr auto BACKLIGHT_TIMER = LEDC_TIMER_0;
constexpr auto BACKLIGHT_CHANNEL = LEDC_CHANNEL_1;

constexpr int KB_ROWS = 4;
constexpr int KB_COLS = 10;

/* The vendor reference decodes the raw TCA8418 event with reversed columns
 * (col = (COLS-1) - (event % COLS)), whereas Tactility's Tca8418 driver decodes
 * columns straight (col = event % COLS). To keep the human-readable keymap below
 * matching the vendor source 1:1 while still producing the right character for
 * each physical key, we reverse the column index at lookup time.
 *
 * If keys come out mirrored left-to-right on hardware, flip this to false. */
constexpr bool COLUMNS_REVERSED = true;

// Modifier sentinels stored in the base layer (never emitted as characters).
constexpr char KEY_ALT = '\x01';
constexpr char KEY_SYM = '\x02';

// Base layer — transcribed verbatim (in visual order) from the vendor
// examples/keypad/keypad.ino, with the printable placeholders replaced by the
// real LVGL key codes (DEL -> backspace, ENT -> enter, UP -> up).
static constexpr char keymapBase[KB_ROWS][KB_COLS] = {
    {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
    {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', LV_KEY_BACKSPACE},
    {KEY_ALT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', '$', LV_KEY_ENTER},
    {'\0', '\0', '\0', '\0', '\0', LV_KEY_UP, '0', ' ', KEY_SYM, LV_KEY_UP}
};

// ALT layer (uppercase) — DERIVED, verify on hardware.
static constexpr char keymapAlt[KB_ROWS][KB_COLS] = {
    {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'},
    {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', LV_KEY_BACKSPACE},
    {KEY_ALT, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '$', LV_KEY_ENTER},
    {'\0', '\0', '\0', '\0', '\0', LV_KEY_UP, '0', ' ', KEY_SYM, LV_KEY_UP}
};

// SYM layer (numbers/symbols) — DERIVED, verify on hardware.
static constexpr char keymapSym[KB_ROWS][KB_COLS] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'@', '#', '&', '*', '(', ')', '-', '+', '=', LV_KEY_BACKSPACE},
    {KEY_ALT, '_', '/', '\\', ':', ';', '"', '\'', '?', LV_KEY_ENTER},
    {'\0', '\0', '\0', '\0', '\0', LV_KEY_UP, '0', ' ', KEY_SYM, LV_KEY_UP}
};

static int visualCol(int col) {
    return COLUMNS_REVERSED ? (KB_COLS - 1 - col) : col;
}

void TdeckmaxKeyboard::readCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* keyboard = static_cast<TdeckmaxKeyboard*>(lv_indev_get_user_data(indev));
    char keypress = 0;

    if (xQueueReceive(keyboard->queue, &keypress, 0) == pdPASS) {
        data->key = keypress;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->key = 0;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void TdeckmaxKeyboard::processKeyboard() {
    if (!keypad->update()) {
        return;
    }

    // Determine modifier state from all currently-held keys (base layer).
    bool altHeld = false;
    bool symHeld = false;
    for (int i = 0; i < keypad->pressed_key_count; i++) {
        int row = keypad->pressed_list[i].row;
        int col = keypad->pressed_list[i].col;
        if (row < 0 || row >= KB_ROWS || col < 0 || col >= KB_COLS) {
            continue;
        }
        char base = keymapBase[row][visualCol(col)];
        if (base == KEY_ALT) altHeld = true;
        if (base == KEY_SYM) symHeld = true;
    }

    // Clear hold state for released keys.
    for (int i = 0; i < keypad->released_key_count; i++) {
        int row = keypad->released_list[i].row;
        int col = keypad->released_list[i].col;
        if (row >= 0 && row < KB_ROWS && col >= 0 && col < KB_COLS) {
            keyHeld[row][col] = false;
        }
    }

    // Emit characters for newly-pressed keys (each physical press fires once).
    bool anyPress = false;
    for (int i = 0; i < keypad->pressed_key_count; i++) {
        int row = keypad->pressed_list[i].row;
        int col = keypad->pressed_list[i].col;
        if (row < 0 || row >= KB_ROWS || col < 0 || col >= KB_COLS) {
            continue;
        }
        if (keyHeld[row][col]) {
            continue;
        }
        keyHeld[row][col] = true;
        anyPress = true;

        char chr = symHeld ? keymapSym[row][visualCol(col)]
                 : altHeld ? keymapAlt[row][visualCol(col)]
                           : keymapBase[row][visualCol(col)];

        if (chr == '\0' || chr == KEY_ALT || chr == KEY_SYM) {
            continue;
        }
        xQueueSend(queue, &chr, pdMS_TO_TICKS(50));
    }

    if (anyPress) {
        makeBacklightImpulse();
    }
}

bool TdeckmaxKeyboard::startLvgl(lv_display_t* display) {
    backlightOkay = initBacklight(BACKLIGHT_PIN, 30000);
    keypad->init(KB_ROWS, KB_COLS);

    assert(inputTimer == nullptr);
    inputTimer = std::make_unique<tt::Timer>(tt::Timer::Type::Periodic, tt::kernel::millisToTicks(20), [this] {
        processKeyboard();
    });

    assert(backlightImpulseTimer == nullptr);
    backlightImpulseTimer = std::make_unique<tt::Timer>(tt::Timer::Type::Periodic, tt::kernel::millisToTicks(50), [this] {
        processBacklightImpulse();
    });

    kbHandle = lv_indev_create();
    lv_indev_set_type(kbHandle, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kbHandle, &readCallback);
    lv_indev_set_display(kbHandle, display);
    lv_indev_set_user_data(kbHandle, this);

    inputTimer->start();
    backlightImpulseTimer->start();

    return true;
}

bool TdeckmaxKeyboard::stopLvgl() {
    assert(inputTimer);
    inputTimer->stop();
    inputTimer = nullptr;

    assert(backlightImpulseTimer);
    backlightImpulseTimer->stop();
    backlightImpulseTimer = nullptr;

    lv_indev_delete(kbHandle);
    kbHandle = nullptr;
    return true;
}

bool TdeckmaxKeyboard::isAttached() const {
    return i2c_controller_has_device_at_address(keypad->getController(), keypad->getAddress(), 100) == ERROR_NONE;
}

bool TdeckmaxKeyboard::initBacklight(gpio_num_t pin, uint32_t frequencyHz) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = BACKLIGHT_TIMER,
        .freq_hz = frequencyHz,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };

    if (ledc_timer_config(&ledc_timer) != ESP_OK) {
        LOGGER.error("Backlight timer config failed");
        return false;
    }

    ledc_channel_config_t ledc_channel = {
        .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BACKLIGHT_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = 0
        }
    };

    if (ledc_channel_config(&ledc_channel) != ESP_OK) {
        LOGGER.error("Backlight channel config failed");
        return false;
    }

    return true;
}

bool TdeckmaxKeyboard::setBacklightDuty(uint8_t duty) {
    if (!backlightOkay) {
        return false;
    }
    return (ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL, duty) == ESP_OK) &&
        (ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL) == ESP_OK);
}

void TdeckmaxKeyboard::makeBacklightImpulse() {
    backlightImpulseDuty = 255;
    setBacklightDuty(backlightImpulseDuty);
}

void TdeckmaxKeyboard::processBacklightImpulse() {
    if (backlightImpulseDuty > 64) {
        backlightImpulseDuty--;
        setBacklightDuty(backlightImpulseDuty);
    }
}
