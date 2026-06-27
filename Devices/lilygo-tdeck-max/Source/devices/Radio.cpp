#include "Radio.h"

#include <Sx1262.h>
#include <Tactility/lvgl/LvglSync.h>

#include <driver/spi_master.h>

// SX1262 LoRa transceiver. Same part as the T-Lora Pager, but wired differently
// (pins + power) per the vendor docs/pinmap.md and examples/LoRa_sx1262.
//  - CS/RST/BUSY/DIO1 = GPIO 3/4/6/5
//  - Shares SPI2 (spi0) with the EPD and SD card, so it must use the LVGL sync
//    lock to serialise bus access. initBoot() deasserts the other CS lines.
//  - TCXO reference is 2.4 V (vendor radio.setTCXO(2.4)).
//  - DIO2 drives the antenna TX/RX switch (vendor radio.setDio2AsRfSwitch()).
//  - Module power (LORA_EN) and antenna select (LORA_SEL) are gated by the
//    XL9555 IO expander and enabled in Configuration.cpp initIoExpander().
constexpr auto TDECKMAX_LORA_PIN_CS = GPIO_NUM_3;
constexpr auto TDECKMAX_LORA_PIN_RST = GPIO_NUM_4;
constexpr auto TDECKMAX_LORA_PIN_DIO1 = GPIO_NUM_5;
constexpr auto TDECKMAX_LORA_PIN_BUSY = GPIO_NUM_6;

std::shared_ptr<tt::hal::radio::RadioDevice> createRadio() {
    return std::make_shared<Sx1262>(Sx1262::Configuration{
        .spiHostDevice = SPI2_HOST,
        .spiFrequency = 4'000'000,
        .csPin = TDECKMAX_LORA_PIN_CS,
        .resetPin = TDECKMAX_LORA_PIN_RST,
        .busyPin = TDECKMAX_LORA_PIN_BUSY,
        .irqPin = TDECKMAX_LORA_PIN_DIO1,
        .tcxoVoltage = 2.4,
        .useRegulatorLdo = false,
        .dio2RfSwitch = true,
        .spiLock = tt::lvgl::getSyncLock()
    });
}
