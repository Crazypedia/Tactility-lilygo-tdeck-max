#include "Sx1262.h"

#include <tactility/log.h>

#include <cstring>

constexpr const char* TAG = "Sx1262";

template<typename T>
static constexpr Sx1262::ParameterStatus checkLimitsAndApply(T &target, const float value, const float lower, const float upper, const unsigned step = 0) {
    if ((value >= lower) && (value <= upper)) {
        if (step != 0) {
            int ivalue = static_cast<int>(value);
            if ((ivalue % step) != 0) {
                return Sx1262::ParameterStatus::ValueError;
            }
        }

        target = static_cast<T>(value);
        return Sx1262::ParameterStatus::Success;
    }
    return Sx1262::ParameterStatus::ValueError;
}

template<typename T>
static constexpr Sx1262::ParameterStatus checkValuesAndApply(T &target, const float value, std::initializer_list<float> valids) {
    for (float valid : valids) {
        if (value == valid) {
            target = static_cast<T>(value);
            return Sx1262::ParameterStatus::Success;
        }
    }
    return Sx1262::ParameterStatus::ValueError;
}

void IRAM_ATTR Sx1262::dio1Isr(void* ctx) {
    static_cast<Sx1262*>(ctx)->dio1Event();
}

Sx1262::ParameterStatus Sx1262::setBaseParameter(const Parameter parameter, const float value) {
    using enum Parameter;

    switch (parameter) {
        case Power:
            return checkLimitsAndApply(power, value, -9.0, 22.0);
        case BoostedGain:
            return checkLimitsAndApply(boostedGain, value, 0.0, 1.0, 1);
        default:
            return Sx1262::ParameterStatus::Unavailable;
    }
}

Sx1262::ParameterStatus Sx1262::setLoraParameter(const Parameter parameter, const float value) {
    using enum Parameter;

    switch (parameter) {
        case Frequency:
            return checkLimitsAndApply(frequency, value, 150.0, 960.0);
        case Bandwidth:
            return checkValuesAndApply(bandwidth, value, {
                7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125.0, 250.0, 500.0
            });
        case SpreadFactor:
            return checkLimitsAndApply(spreadFactor, value, 7.0, 12.0, 1);
        case CodingRate:
            return checkLimitsAndApply(codingRate, value, 5.0, 8.0, 1);
        case SyncWord:
            return checkLimitsAndApply(syncWord, value, 0.0, 255.0, 1);
        case PreambleLength:
            return checkLimitsAndApply(preambleLength, value, 0.0, 65535.0, 1);
        default:
            break;
    }

    LOG_W(TAG, "Tried to set unsupported LoRa parameter \"%s\" to %f", toString(parameter), value);
    return Sx1262::ParameterStatus::Unavailable;
}

Sx1262::ParameterStatus Sx1262::setFskParameter(const Parameter parameter, const float value) {
    using enum Parameter;

    switch (parameter) {
        case Frequency:
            return checkLimitsAndApply(frequency, value, 150.0, 960.0);
        case Bandwidth:
            return checkValuesAndApply(bandwidth, value, {
                4.8, 5.8, 7.3, 9.7, 11.7, 14.6, 19.5, 23.4, 29.3, 39.0, 46.9, 58.6, 78.2
            });
        case PreambleLength:
            return checkLimitsAndApply(preambleLength, value, 0.0, 65535.0, 1);
        case DataRate:
            return checkLimitsAndApply(bitRate, value, 0.6, 300.0);
        case FrequencyDeviation:
            return checkLimitsAndApply(frequencyDeviation, value, 0.0, 200.0);
        default:
            break;
    }

    LOG_W(TAG, "Tried to set unsupported FSK parameter \"%s\" to %f", toString(parameter), value);
    return Sx1262::ParameterStatus::Unavailable;
}

Sx1262::ParameterStatus Sx1262::setLrFhssParameter(const Parameter parameter, const float value) {
    using enum Parameter;

    switch (parameter) {
        case Power:
            return checkLimitsAndApply(power, value, -9.0, 22.0);
        case Bandwidth:
            return checkValuesAndApply(bandwidth, value, {
                39.06, 85.94, 136.72, 183.59, 335.94, 386.72, 722.66, 773.44, 1523.4, 1574.2
            });
        case CodingRate:
            return checkValuesAndApply(codingRate, value, {
                RADIOLIB_SX126X_LR_FHSS_CR_5_6,
                RADIOLIB_SX126X_LR_FHSS_CR_2_3,
                RADIOLIB_SX126X_LR_FHSS_CR_1_2,
                RADIOLIB_SX126X_LR_FHSS_CR_1_3
            });
        case NarrowGrid:
            return checkLimitsAndApply(narrowGrid, value, 0.0, 1.0, 1);
        default:
            break;
    }

    LOG_W(TAG, "Tried to set unsupported LR-FHSS parameter \"%s\" to %f", toString(parameter), value);
    return Sx1262::ParameterStatus::Unavailable;
}


Sx1262::ParameterStatus Sx1262::setParameter(const Parameter parameter, const float value) {
    const auto currentModulation = getModulation();

    auto base_return = setBaseParameter(parameter, value);
    if (base_return != Sx1262::ParameterStatus::Unavailable) {
        return base_return;
    }

    switch (currentModulation) {
        case Modulation::LoRa:
            return setLoraParameter(parameter, value);
        case Modulation::Fsk:
            return setFskParameter(parameter, value);
        case Modulation::LrFhss:
            return setLrFhssParameter(parameter, value);
        default:
            break;
    }

    // Shouldn't be reachable, return failsafe value
    return Sx1262::ParameterStatus::Unavailable;
}


Sx1262::ParameterStatus Sx1262::getBaseParameter(const Parameter parameter, float &value) const {
    using enum Parameter;

    switch (parameter) {
        case Power:
            value = power;
            return Sx1262::ParameterStatus::Success;
        case BoostedGain:
            value = boostedGain;
            return Sx1262::ParameterStatus::Success;
        default:
            return Sx1262::ParameterStatus::Unavailable;
    }

}

Sx1262::ParameterStatus Sx1262::getLoraParameter(const Parameter parameter, float &value) const {
    using enum Parameter;

    switch (parameter) {
        case Frequency:
            value = frequency;
            return Sx1262::ParameterStatus::Success;
        case Bandwidth:
            value = bandwidth;
            return Sx1262::ParameterStatus::Success;
        case SpreadFactor:
            value = spreadFactor;
            return Sx1262::ParameterStatus::Success;
        case CodingRate:
            value = codingRate;
            return Sx1262::ParameterStatus::Success;
        case SyncWord:
            value = syncWord;
            return Sx1262::ParameterStatus::Success;
        case PreambleLength:
            value = preambleLength;
            return Sx1262::ParameterStatus::Success;
        default:
            break;
    }

    return Sx1262::ParameterStatus::Unavailable;
}

Sx1262::ParameterStatus Sx1262::getFskParameter(const Parameter parameter, float &value) const {
    using enum Parameter;

    switch (parameter) {
        case Frequency:
            value = frequency;
            return Sx1262::ParameterStatus::Success;
        case Bandwidth:
            value = bandwidth;
            return Sx1262::ParameterStatus::Success;
        case DataRate:
            value = bitRate;
            return Sx1262::ParameterStatus::Success;
        case FrequencyDeviation:
            value = frequencyDeviation;
            return Sx1262::ParameterStatus::Success;
        default:
            break;
    }

    return Sx1262::ParameterStatus::Unavailable;
}

Sx1262::ParameterStatus Sx1262::getLrFhssParameter(const Parameter parameter, float &value) const {
    using enum Parameter;

    switch (parameter) {
        case Bandwidth:
            value = bandwidth;
            return Sx1262::ParameterStatus::Success;
        case CodingRate:
            value = codingRate;
            return Sx1262::ParameterStatus::Success;
        case NarrowGrid:
            value = narrowGrid;
            return Sx1262::ParameterStatus::Success;
        default:
            break;
    }

    return Sx1262::ParameterStatus::Unavailable;
}


Sx1262::ParameterStatus Sx1262::getParameter(const Parameter parameter, float &value) const {
    const auto currentModulation = getModulation();

    // No warnings are emitted to be able to discover parameters by return status
    auto base_return = getBaseParameter(parameter, value);
    if (base_return != Sx1262::ParameterStatus::Unavailable) {
        return base_return;
    }

    switch (currentModulation) {
        case Modulation::LoRa:
            return getLoraParameter(parameter, value);
        case Modulation::Fsk:
            return getFskParameter(parameter, value);
        case Modulation::LrFhss:
            return getLrFhssParameter(parameter, value);
        default:
            // Shouldn't be reachable, return failsafe value
            return Sx1262::ParameterStatus::Unavailable;
    }
}

tt::hal::radio::Unit Sx1262::getParameterUnit(const Parameter parameter) const {
    using enum Parameter;
    using Unit = tt::hal::radio::Unit;

    switch (parameter) {
        case Power:
            return Unit(Unit::Name::DecibelMilliwatts);
        case Frequency:
            return Unit(Unit::Prefix::Mega, Unit::Name::Herz);
        case Bandwidth:
            return Unit(Unit::Prefix::Kilo, Unit::Name::Herz);
        case SpreadFactor:
        case CodingRate:        // no break
        case SyncWord:          // no break
        case PreambleLength:    // no break
            return Unit(Unit::Name::None);
        case DataRate:
            return Unit(Unit::Prefix::Kilo, Unit::Name::BitsPerSecond);
        case FrequencyDeviation:
            return Unit(Unit::Prefix::Kilo, Unit::Name::Herz);
        case NarrowGrid:
            return Unit(Unit::Name::None);
        default:
            LOG_W(TAG, "Tried to get unit for unsupported parameter \"%s\"", toString(parameter));
            return Unit(Unit::Name::None);
    }
}

void Sx1262::registerDio1Isr() {
    gpio_config_t conf = {
        .pin_bit_mask = (1ULL << configuration.irqPin),
        .mode = (gpio_mode_t)GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&conf);

    interrupt.install();
    interrupt.arm();
}

void Sx1262::unregisterDio1Isr() {
    interrupt.disarm();
}

void IRAM_ATTR Sx1262::dio1Event() {
    static const auto DRAM_ATTR bit = SX1262_DIO1_EVENT_BIT;
    events.set(bit);
}

void Sx1262::txQueuedSignal() {
    events.set(SX1262_QUEUED_TX_BIT);
}

void Sx1262::interruptSignal() {
    events.set(SX1262_INTERRUPT_BIT);
}

int Sx1262::doBegin(const Modulation modulation) {
    uint16_t rc = RADIOLIB_ERR_NONE;

    if (modulation == Modulation::LoRa) {
        rc = radio.begin(
            frequency,
            bandwidth,
            spreadFactor,
            codingRate,
            syncWord,
            power,
            preambleLength,
            configuration.tcxoVoltage,
            configuration.useRegulatorLdo
        );
    } else if (modulation == Modulation::Fsk) {
        rc = radio.beginFSK(
            frequency,
            bitRate,
            frequencyDeviation,
            bandwidth,
            power,
            preambleLength,
            configuration.tcxoVoltage,
            configuration.useRegulatorLdo
        );
    } else if (modulation == Modulation::LrFhss) {
        rc = radio.beginLRFHSS(
            bandwidth,
            codingRate,
            narrowGrid,
            configuration.tcxoVoltage,
            configuration.useRegulatorLdo
        );
    } else {
        LOG_E(TAG, "SX1262 not capable of modulation \"%s\"", toString(modulation));
        setState(State::Error);
        return -1;
    }

    if (rc != RADIOLIB_ERR_NONE) {
        LOG_E(TAG, "Radiolib initialization failed with code %hi", rc);
        setState(State::Error);
        return -1;
    }

    // Modules that wire the antenna TX/RX switch to DIO2 (e.g. LilyGO T-Deck Max)
    // must enable this or the RF path stays disconnected and no TX/RX gets through.
    if (configuration.dio2RfSwitch) {
        rc = radio.setDio2AsRfSwitch(true);
        if (rc != RADIOLIB_ERR_NONE) {
            LOG_E(TAG, "Setting DIO2 as RF switch failed with code %hi", rc);
            setState(State::Error);
            return -1;
        }
    }

    rc = radio.setRxBoostedGainMode(boostedGain, true);
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_E(TAG, "Setting RX boosted gain to %s failed with code %hi", boostedGain ? "true" : "false", rc);
        setState(State::Error);
        return -1;
    }

    registerDio1Isr();
    return 0;
}

void Sx1262::doEnd() {
    unregisterDio1Isr();
}

void Sx1262::doTransmit() {
    currentTx = popNextQueuedTx();
    uint16_t rc = RADIOLIB_ERR_NONE;
    rc = radio.standby();
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_W(TAG, "RadioLib returned %hi on TX standby", rc);
    }

    if (getModulation() == Modulation::Fsk) {
        rc = radio.startTransmit(currentTx.packet.data.data(), currentTx.packet.data.size(),
                                 currentTx.packet.address);
    } else {
        rc = radio.startTransmit(currentTx.packet.data.data(), currentTx.packet.data.size());
    }

    if (rc == RADIOLIB_ERR_NONE) {
        currentTx.callback(currentTx.id, TransmissionState::PendingTransmit);

        // outFlags stays 0 on timeout (wait returns false), which routes to the Timeout branch below
        uint32_t txEventFlags = 0;
        events.wait(SX1262_INTERRUPT_BIT | SX1262_DIO1_EVENT_BIT, false, true, &txEventFlags,
                    pdMS_TO_TICKS(SX1262_TX_TIMEOUT_MILLIS));

        // Clean up after transmission
        radio.finishTransmit();

        // Thread might've been interrupted in the meanwhile
        if (isThreadInterrupted()) {
            return;
        }

        // If the DIO1 bit is unset, this means the wait timed out
        if (txEventFlags & SX1262_DIO1_EVENT_BIT) {
            currentTx.callback(currentTx.id, TransmissionState::Transmitted);
        } else {
            currentTx.callback(currentTx.id, TransmissionState::Timeout);
        }

    } else {
        LOG_E(TAG, "Error transmitting id=%d, rc=%hi", currentTx.id, rc);
        currentTx.callback(currentTx.id, TransmissionState::Error);
    }
}

bool Sx1262::doListen() {
    uint16_t rc = RADIOLIB_ERR_NONE;

    if (getModulation() != Modulation::LrFhss) {
        rc = radio.startReceiveDutyCycleAuto(preambleLength, 0, SX1262_IRQ_FLAGS);
        if (rc == RADIOLIB_ERR_NONE) {
            uint32_t flags = 0;
            events.wait(SX1262_INTERRUPT_BIT | SX1262_DIO1_EVENT_BIT | SX1262_QUEUED_TX_BIT, false, true, &flags);
            return (flags & SX1262_DIO1_EVENT_BIT) != 0;
        } else {
            LOG_E(TAG, "Error setting dutycycle RX, RadioLib returned %hi", rc);
        }
        return false;
    } else {
        // LR-FHSS modem only supports TX
        events.wait(SX1262_INTERRUPT_BIT | SX1262_QUEUED_TX_BIT);
        return false;
    }
}

void Sx1262::doReceive() {
    uint16_t rc = RADIOLIB_ERR_NONE;

    // LR-FHSS modem only supports TX
    if (getModulation() == Modulation::LrFhss) return;
    /*
    rc = radio.standby();
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_W(TAG, "RadioLib returned %hi on TX standby", rc);
    }*/

    uint16_t rxSize = radio.getPacketLength(true);
    std::vector<uint8_t> data(rxSize);
    rc = radio.readData(data.data(), rxSize);
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_E(TAG, "Error receiving data, RadioLib returned %hi", rc);
    } else if(rxSize == 0) {
        // This can cause a flood of messages if there are ones emitted here,
        // as a warning here doesn't bring that much to the table it is skipped.
        // The body is kept empty intentionally.'
    } else {
        float rssi = radio.getRSSI();
        float snr = radio.getSNR();
        auto rxPacket = tt::hal::radio::RxPacket {
            .data = data,
            .rssi = rssi,
            .snr = snr
        };

        publishRx(rxPacket);
        radio.finishReceive();
    }

    // A delay before a new command improves reliability
    //vTaskDelay(pdMS_TO_TICKS(SX1262_COOLDOWN_MILLIS));
}

