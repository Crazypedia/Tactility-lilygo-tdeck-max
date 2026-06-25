#include "RadiolibTactilityHal.h"

#include <Tactility/kernel/Kernel.h>
#include <tactility/log.h>

#include "hal/gpio_hal.h"
#include "esp_timer.h"

constexpr const char* TAG = "RadiolibTactilityHal";

void RadiolibTactilityHal::init() {
    spiBegin();
}

void RadiolibTactilityHal::term() {
    spiEnd();
}

void RadiolibTactilityHal::pinMode(uint32_t pin, uint32_t mode) {
    if(pin == RADIOLIB_NC) {
        return;
    }

    gpio_hal_context_t gpiohal;
    gpiohal.dev = GPIO_LL_GET_HW(GPIO_PORT_0);

    gpio_config_t conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = (gpio_mode_t)mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = (gpio_int_type_t)gpiohal.dev->pin[pin].int_type,
    };
    gpio_config(&conf);
}

void RadiolibTactilityHal::digitalWrite(uint32_t pin, uint32_t value) {
    if(pin == RADIOLIB_NC) {
        return;
    }

    gpio_set_level((gpio_num_t)pin, value);
}

uint32_t RadiolibTactilityHal::digitalRead(uint32_t pin) {
    if(pin == RADIOLIB_NC) {
        return 0;
    }

    return gpio_get_level((gpio_num_t)pin);
}

void RadiolibTactilityHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) {
    LOG_E(TAG, "Interrupt registration via RadioLib is not supported!");
}

void RadiolibTactilityHal::detachInterrupt(uint32_t interruptNum) {
    LOG_E(TAG, "Interrupt registration via RadioLib is not supported!");
}

void RadiolibTactilityHal::delay(unsigned long ms) {
    tt::kernel::delayMillis(ms);
}

void RadiolibTactilityHal::delayMicroseconds(unsigned long us) {
    tt::kernel::delayMicros(us);
}

unsigned long RadiolibTactilityHal::millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

unsigned long RadiolibTactilityHal::micros() {
    return (unsigned long)(esp_timer_get_time());
}

long RadiolibTactilityHal::pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) {
    if(pin == RADIOLIB_NC) {
        return(0);
    }

    this->pinMode(pin, GPIO_MODE_INPUT);
    uint32_t start = this->micros();
    uint32_t curtick = this->micros();

    while(this->digitalRead(pin) == state) {
        if((this->micros() - curtick) > timeout) {
            return 0;
        }
    }

    return (this->micros() - start);
}

void RadiolibTactilityHal::spiBegin() {
    if (!spiInitialized) {
        spi_device_interface_config_t devcfg = {};
        devcfg.clock_speed_hz = spiFrequency;
        devcfg.mode = 0;
        // CS is set to unused, as RadioLib sets it manually
        devcfg.spics_io_num = -1;
        devcfg.queue_size = 1;
        esp_err_t ret = spi_bus_add_device(spiHostDevice, &devcfg, &spiDeviceHandle);
        if (ret != ESP_OK) {
            LOG_E(TAG, "Failed to add SPI device, error %s", esp_err_to_name(ret));
        }
        spiInitialized = true;
    }
}

void RadiolibTactilityHal::spiBeginTransaction() {
    getLock()->lock();
    spi_device_acquire_bus(spiDeviceHandle, portMAX_DELAY);
}

void RadiolibTactilityHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = out;
    t.rx_buffer = in;
    spi_device_polling_transmit(spiDeviceHandle, &t);
}

void RadiolibTactilityHal::spiEndTransaction() {
    spi_device_release_bus(spiDeviceHandle);
    getLock()->unlock();
}

void RadiolibTactilityHal::spiEnd() {
    if (spiInitialized) {
        spi_bus_remove_device(spiDeviceHandle);
        spiInitialized = false;
    }
}
