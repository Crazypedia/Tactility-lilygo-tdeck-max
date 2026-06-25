#pragma once

#include <Tactility/EventGroup.h>
#include <Tactility/Lock.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <memory>

#include <RadioLib.h>
#include "RadiolibTactilityHal.h"
#include "RadiolibThreadedDevice.h"
#include "LevelInterruptHandler.h"

#include <utility>

class Sx1262 final : public RadiolibThreadedDevice {

public:
    struct Configuration {
        spi_host_device_t spiHostDevice;
        int spiFrequency;
        gpio_num_t csPin;
        gpio_num_t resetPin;
        gpio_num_t busyPin;
        gpio_num_t irqPin;
        float tcxoVoltage;
        bool useRegulatorLdo;
        bool dio2RfSwitch;
        std::shared_ptr<tt::Lock> spiLock;
    };

private:
    static constexpr auto SX1262_DEFAULT_NAME = "SX1262";
    static constexpr auto SX1262_COOLDOWN_MILLIS = 100;
    static constexpr auto SX1262_RX_TIMEOUT_MILLIS = 10'000;
    static constexpr auto SX1262_TX_TIMEOUT_MILLIS = 2000;
    static constexpr auto SX1262_INTERRUPT_BIT = BIT0;
    static constexpr auto SX1262_DIO1_EVENT_BIT = BIT1;
    static constexpr auto SX1262_QUEUED_TX_BIT = BIT2;
    static constexpr auto SX1262_IRQ_FLAGS = (RADIOLIB_IRQ_RX_DEFAULT_FLAGS);

    const Configuration configuration;
    LevelInterruptHandler interrupt;
    std::string name;
    tt::EventGroup events;
    RadiolibTactilityHal hal;
    Module radioModule;
    SX1262 radio;
    TxItem currentTx;

    int8_t power = -9.0;
    float frequency = 150;
    float bandwidth = 0.0;
    uint8_t spreadFactor = 0.0;
    uint8_t codingRate = 0;
    uint8_t syncWord = 0;
    uint16_t preambleLength = 0;
    float bitRate = 0.0;
    float frequencyDeviation = 0.0;
    bool narrowGrid = false;
    bool boostedGain = false;

    void registerDio1Isr();
    void unregisterDio1Isr();

    ParameterStatus setBaseParameter(const Parameter parameter, const float value);
    ParameterStatus setLoraParameter(const Parameter parameter, const float value);
    ParameterStatus setFskParameter(const Parameter parameter, const float value);
    ParameterStatus setLrFhssParameter(const Parameter parameter, const float value);
    ParameterStatus getBaseParameter(const Parameter parameter, float &value) const;
    ParameterStatus getLoraParameter(const Parameter parameter, float &value) const;
    ParameterStatus getFskParameter(const Parameter parameter, float &value) const;
    ParameterStatus getLrFhssParameter(const Parameter parameter, float &value) const;


protected:
    static void dio1Isr(void* ctx);

    virtual void txQueuedSignal() override;
    virtual void interruptSignal() override;

    virtual int doBegin(const Modulation modulation) override;
    virtual void doEnd() override;
    virtual void doTransmit() override;
    virtual bool doListen() override;
    virtual void doReceive() override;

public:

    explicit Sx1262(const Configuration& configuration, const std::string& name = SX1262_DEFAULT_NAME)
        : RadiolibThreadedDevice(name, 4096)
        , configuration(configuration)
        , interrupt(configuration.irqPin, LevelInterruptHandler::Type::PositiveEdge, dio1Isr, this)
        , name(name)
        , hal(configuration.spiHostDevice, configuration.spiFrequency, configuration.spiLock)
        , radioModule(&hal, configuration.csPin, RADIOLIB_NC, configuration.resetPin, configuration.busyPin)
        , radio(&radioModule)
        {}

    ~Sx1262() override = default;

    std::string getName() const override { return name; }
    std::string getDescription() const override { return "Semtech SX1262 LoRa and (G)FSK capable radio"; }

    ParameterStatus setParameter(const Parameter parameter, const float value) override;
    ParameterStatus getParameter(const Parameter parameter, float &value) const override;
    tt::hal::radio::Unit getParameterUnit(const Parameter parameter) const override;

    bool canTransmit(const Modulation modulation) override {
        return (modulation == Modulation::Fsk) ||
               (modulation == Modulation::LoRa) ||
               (modulation == Modulation::LrFhss);
    }

    bool canReceive(const Modulation modulation) override {
        return (modulation == Modulation::Fsk) || (modulation == Modulation::LoRa);
    }

    void dio1Event();
};
