#include "Sdcard.h"

#include <tactility/device.h>

#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

using tt::hal::sdcard::SpiSdCardDevice;

// Pins from LilyGO T-Deck Max docs/pinmap.md
constexpr auto TDECKMAX_SDCARD_PIN_CS = GPIO_NUM_48;
constexpr auto TDECKMAX_EPD_PIN_CS = GPIO_NUM_34;
constexpr auto TDECKMAX_LORA_PIN_CS = GPIO_NUM_3;

std::shared_ptr<tt::hal::sdcard::SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        TDECKMAX_SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        tt::hal::sdcard::SdCardDevice::MountBehaviour::AtBoot,
        tt::lvgl::getSyncLock(),
        std::vector {
            TDECKMAX_LORA_PIN_CS,
            TDECKMAX_EPD_PIN_CS
        }
    );

    ::Device* spiController = device_find_by_name("spi0");
    if (spiController == nullptr) {
        return nullptr;
    }

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spiController
    );
}
