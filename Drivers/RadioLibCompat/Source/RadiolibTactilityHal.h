#pragma once

#include <Tactility/Lock.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <RadioLib.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <memory>

class RadiolibTactilityHal : public RadioLibHal {
private:
    spi_host_device_t spiHostDevice;
    int spiFrequency;
    spi_device_handle_t spiDeviceHandle;
    std::shared_ptr<tt::Lock> lock;
    bool spiInitialized;

public:
    explicit RadiolibTactilityHal(spi_host_device_t spiHostDevice, int spiFrequency, std::shared_ptr<tt::Lock> lock)
    : RadioLibHal(
        GPIO_MODE_INPUT,
        GPIO_MODE_OUTPUT,
        0, // LOW
        1, // HIGH
        GPIO_INTR_POSEDGE,
        GPIO_INTR_NEGEDGE)
    , spiHostDevice(spiHostDevice)
    , spiFrequency(spiFrequency)
    , lock(std::move(lock))
    , spiInitialized(false) {}

    void init() override;
    void term() override;

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;

    void delay(unsigned long ms) override;
    void delayMicroseconds(unsigned long us) override;
    unsigned long millis() override;
    unsigned long micros() override;
    long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override;

    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd();

    std::shared_ptr<tt::Lock> getLock() const { return lock; }
};
