#include "tt_hal_radio.h"

#include <tactility/check.h>
#include <tactility/hal/Device.h>
#include "Tactility/hal/radio/RadioDevice.h"
#include <tactility/log.h>

auto constexpr TAG = "tt_hal_radio";

static RadioState fromCpp(tt::hal::radio::RadioDevice::State state);
static tt::hal::radio::RadioDevice::State toCpp(RadioState state);
static Modulation fromCpp(tt::hal::radio::RadioDevice::Modulation modulation);
static tt::hal::radio::RadioDevice::Modulation toCpp(Modulation modulation);
static RadioParameter fromCpp(tt::hal::radio::RadioDevice::Parameter parameter);
static tt::hal::radio::RadioDevice::Parameter toCpp(RadioParameter parameter);
static RadioParameterStatus fromCpp(tt::hal::radio::RadioDevice::ParameterStatus status);
static tt::hal::radio::RadioDevice::ParameterStatus toCpp(RadioParameterStatus status);
static RadioTxState fromCpp(tt::hal::radio::RadioDevice::TransmissionState state);
static tt::hal::radio::RadioDevice::TransmissionState toCpp(RadioTxState state);

struct DeviceWrapper {
    std::shared_ptr<tt::hal::radio::RadioDevice> device;
    std::string name;
    std::string description;
    DeviceWrapper(std::shared_ptr<tt::hal::radio::RadioDevice> device)
        : device(device)
        , name(device->getName())
        , description(device->getDescription()) {}
};

static std::shared_ptr<tt::hal::radio::RadioDevice> findValidRadioDevice(tt::hal::Device::Id id) {
    auto device = tt::hal::findDevice(id);
    if (device == nullptr || device->getType() != tt::hal::Device::Type::Radio) {
        return nullptr;
    }
    return std::reinterpret_pointer_cast<tt::hal::radio::RadioDevice>(device);
}

extern "C" {

    RadioHandle tt_hal_radio_alloc(DeviceId radioId) {
        auto radio = findValidRadioDevice(radioId);
        if (radio != nullptr) {
            return new DeviceWrapper(radio);
        }
        return nullptr;
    }

    void tt_hal_radio_free(RadioHandle handle) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        delete wrapper;
    }

    DeviceId tt_hal_radio_get_device_id(RadioHandle handle) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return wrapper->device->getId();
    }

    const char* tt_hal_radio_get_name(RadioHandle handle) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return wrapper->name.c_str();
    }

    const char* tt_hal_radio_get_desc(RadioHandle handle) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return wrapper->description.c_str();
    }

    RadioState tt_hal_radio_get_state(RadioHandle handle) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return fromCpp(wrapper->device->getState());
    }

    bool tt_hal_radio_set_modulation(RadioHandle handle, Modulation modulation) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return wrapper->device->setModulation(toCpp(modulation));
    }

    Modulation tt_hal_radio_get_modulation(RadioHandle handle) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return fromCpp(wrapper->device->getModulation());
    }

    RadioParameterStatus tt_hal_radio_set_parameter(RadioHandle handle, RadioParameter parameter, float value) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return fromCpp(wrapper->device->setParameter(toCpp(parameter), value));
    }

    RadioParameterStatus tt_hal_radio_get_parameter(RadioHandle handle, RadioParameter parameter, float *value) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        // This is a programming error not an input error, thus assert.
        // TODO: Does TactC even know assert? Maybe putting a crash is the optimal solution.
        assert(value);
        return fromCpp(wrapper->device->getParameter(toCpp(parameter), *value));
    }

    void tt_hal_radio_get_parameter_unit_str(RadioHandle handle, RadioParameter parameter, char str[], unsigned maxSize) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        assert(str);
        std::string unitString = wrapper->device->getParameterUnit(toCpp(parameter)).toString();
        size_t i = 0;
        for (; i < (maxSize - 1); ++i) {
            str[i] = unitString[i];
        }
        str[i] = '\0';
    }

    bool tt_hal_radio_can_transmit(RadioHandle handle, Modulation modulation) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return wrapper->device->canTransmit(toCpp(modulation));
    }

    bool tt_hal_radio_can_receive(RadioHandle handle, Modulation modulation) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return wrapper->device->canReceive(toCpp(modulation));
    }

    bool tt_hal_radio_start(RadioHandle handle) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return wrapper->device->start();
    }

    bool tt_hal_radio_stop(RadioHandle handle) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return wrapper->device->stop();
    }

    RadioTxId tt_hal_radio_transmit(RadioHandle handle, RadioTxPacket packet, RadioTxStateCallback callback, void* ctx) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        auto ttPacket = tt::hal::radio::TxPacket{
            .data = std::vector<uint8_t>(packet.data, packet.data + packet.size),
            .address = packet.address
        };
        return wrapper->device->transmit(ttPacket, [callback, ctx](tt::hal::radio::RadioDevice::TxId id, tt::hal::radio::RadioDevice::TransmissionState state) {
            if (callback) {
                callback(id, fromCpp(state), ctx);
            }
        });
    }

    RadioStateSubscriptionId tt_hal_radio_subscribe_state(RadioHandle handle, RadioStateCallback callback, void* ctx) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return wrapper->device->subscribeStateChange([callback, ctx](tt::hal::Device::Id id, tt::hal::radio::RadioDevice::State state) {
            if (callback) {
                callback(id, fromCpp(state), ctx);
            }
        });
    }

    RadioRxSubscriptionId tt_hal_radio_subscribe_receive(RadioHandle handle, RadioOnReceiveCallback callback, void* ctx) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        return wrapper->device->subscribeRx([callback, ctx](tt::hal::Device::Id id, const tt::hal::radio::RxPacket& ttPacket) {
            if (callback) {
                auto ttcPacket = RadioRxPacket{
                    .data = ttPacket.data.data(),
                    .size = ttPacket.data.size(),
                    .rssi = ttPacket.rssi,
                    .snr = ttPacket.snr
                };
                callback(id, &ttcPacket, ctx);
            }
        });
    }

    void tt_hal_radio_unsubscribe_receive(RadioHandle handle, RadioRxSubscriptionId id) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        wrapper->device->unsubscribeRx(id);
    }

    void tt_hal_radio_unsubscribe_state(RadioHandle handle, RadioStateSubscriptionId id) {
        auto wrapper = static_cast<DeviceWrapper*>(handle);
        wrapper->device->unsubscribeStateChange(id);
    }
}

static RadioState fromCpp(tt::hal::radio::RadioDevice::State state) {
    switch (state) {
        case tt::hal::radio::RadioDevice::State::PendingOn:
            return RADIO_PENDING_ON;
        case tt::hal::radio::RadioDevice::State::On:
            return RADIO_ON;
        case tt::hal::radio::RadioDevice::State::Error:
            return RADIO_ERROR;
        case tt::hal::radio::RadioDevice::State::PendingOff:
            return RADIO_PENDING_OFF;
        case tt::hal::radio::RadioDevice::State::Off:
            return RADIO_OFF;
        default:
            LOG_W(TAG, "Unknown enum \"%d\" passed!", state);
            check(false, "Radio state not supported");
    }
}

static tt::hal::radio::RadioDevice::State toCpp(RadioState state) {
    switch (state) {
        case RADIO_PENDING_ON:
            return tt::hal::radio::RadioDevice::State::PendingOn;
        case RADIO_ON:
            return tt::hal::radio::RadioDevice::State::On;
        case RADIO_ERROR:
            return tt::hal::radio::RadioDevice::State::Error;
        case RADIO_PENDING_OFF:
            return tt::hal::radio::RadioDevice::State::PendingOff;
        case RADIO_OFF:
            return tt::hal::radio::RadioDevice::State::Off;
        default:
            LOG_W(TAG, "Unknown enum \"%d\" passed!", state);
            check(false, "Radio state not supported");
    }
}

static Modulation fromCpp(tt::hal::radio::RadioDevice::Modulation modulation) {
    switch (modulation) {
        case tt::hal::radio::RadioDevice::Modulation::None:
            return MODULATION_NONE;
        case tt::hal::radio::RadioDevice::Modulation::LoRa:
            return MODULATION_LORA;
        case tt::hal::radio::RadioDevice::Modulation::Fsk:
            return MODULATION_FSK;
        case tt::hal::radio::RadioDevice::Modulation::LrFhss:
            return MODULATION_LRFHSS;
        default:
            LOG_W(TAG, "Unknown enum \"%d\" passed!", modulation);
            check(false, "Modulation not supported");
    }
}

static tt::hal::radio::RadioDevice::Modulation toCpp(Modulation modulation) {
    switch (modulation) {
        case MODULATION_NONE:
            return tt::hal::radio::RadioDevice::Modulation::None;
        case MODULATION_LORA:
            return tt::hal::radio::RadioDevice::Modulation::LoRa;
        case MODULATION_FSK:
            return tt::hal::radio::RadioDevice::Modulation::Fsk;
        case MODULATION_LRFHSS:
            return tt::hal::radio::RadioDevice::Modulation::LrFhss;
        default:
            LOG_W(TAG, "Unknown enum \"%d\" passed!", modulation);
            check(false, "Modulation not supported");
    }
}

static RadioParameter fromCpp(tt::hal::radio::RadioDevice::Parameter parameter) {
    switch (parameter) {
        case tt::hal::radio::RadioDevice::Parameter::Power:
            return RADIO_POWER;
        case tt::hal::radio::RadioDevice::Parameter::BoostedGain:
            return RADIO_BOOSTEDGAIN;
        case tt::hal::radio::RadioDevice::Parameter::Frequency:
            return RADIO_FREQUENCY;
        case tt::hal::radio::RadioDevice::Parameter::Bandwidth:
            return RADIO_BANDWIDTH;
        case tt::hal::radio::RadioDevice::Parameter::SpreadFactor:
            return RADIO_SPREADFACTOR;
        case tt::hal::radio::RadioDevice::Parameter::CodingRate:
            return RADIO_CODINGRATE;
        case tt::hal::radio::RadioDevice::Parameter::SyncWord:
            return RADIO_SYNCWORD;
        case tt::hal::radio::RadioDevice::Parameter::PreambleLength:
            return RADIO_PREAMBLES;
        case tt::hal::radio::RadioDevice::Parameter::FrequencyDeviation:
            return RADIO_FREQDIV;
        case tt::hal::radio::RadioDevice::Parameter::DataRate:
            return RADIO_DATARATE;
        case tt::hal::radio::RadioDevice::Parameter::AddressWidth:
            return RADIO_ADDRWIDTH;
        case tt::hal::radio::RadioDevice::Parameter::NarrowGrid:
            return RADIO_NARROWGRID;
        default:
            LOG_W(TAG, "Unknown enum \"%d\" passed!", parameter);
            check(false, "Parameter not supported");
    }
}

static tt::hal::radio::RadioDevice::Parameter toCpp(RadioParameter parameter) {
    switch (parameter) {
        case RADIO_POWER:
            return tt::hal::radio::RadioDevice::Parameter::Power;
        case RADIO_BOOSTEDGAIN:
            return tt::hal::radio::RadioDevice::Parameter::BoostedGain;
        case RADIO_FREQUENCY:
            return tt::hal::radio::RadioDevice::Parameter::Frequency;
        case RADIO_BANDWIDTH:
            return tt::hal::radio::RadioDevice::Parameter::Bandwidth;
        case RADIO_SPREADFACTOR:
            return tt::hal::radio::RadioDevice::Parameter::SpreadFactor;
        case RADIO_CODINGRATE:
            return tt::hal::radio::RadioDevice::Parameter::CodingRate;
        case RADIO_SYNCWORD:
            return tt::hal::radio::RadioDevice::Parameter::SyncWord;
        case RADIO_PREAMBLES:
            return tt::hal::radio::RadioDevice::Parameter::PreambleLength;
        case RADIO_FREQDIV:
            return tt::hal::radio::RadioDevice::Parameter::FrequencyDeviation;
        case RADIO_DATARATE:
            return tt::hal::radio::RadioDevice::Parameter::DataRate;
        case RADIO_ADDRWIDTH:
            return tt::hal::radio::RadioDevice::Parameter::AddressWidth;
        case RADIO_NARROWGRID:
            return tt::hal::radio::RadioDevice::Parameter::NarrowGrid;
        default:
            LOG_W(TAG, "Unknown enum \"%d\" passed!", parameter);
            check(false, "Parameter not supported");
    }
}

static RadioParameterStatus fromCpp(tt::hal::radio::RadioDevice::ParameterStatus status) {
    switch (status) {
        case tt::hal::radio::RadioDevice::ParameterStatus::Unavailable:
            return RADIO_PARAM_UNAVAILABLE;
        case tt::hal::radio::RadioDevice::ParameterStatus::ValueError:
            return RADIO_PARAM_VALERROR;
        case tt::hal::radio::RadioDevice::ParameterStatus::Success:
            return RADIO_PARAM_SUCCESS;
        default:
            LOG_W(TAG, "Unknown enum \"%d\" passed!", status);
            check(false, "Parameter status not supported");
    }
}

static tt::hal::radio::RadioDevice::ParameterStatus toCpp(RadioParameterStatus status) {
    switch (status) {
        case RADIO_PARAM_UNAVAILABLE:
            return tt::hal::radio::RadioDevice::ParameterStatus::Unavailable;
        case RADIO_PARAM_VALERROR:
            return tt::hal::radio::RadioDevice::ParameterStatus::ValueError;
        case RADIO_PARAM_SUCCESS:
            return tt::hal::radio::RadioDevice::ParameterStatus::Success;
        default:
            LOG_W(TAG, "Unknown enum \"%d\" passed!", status);
            check(false, "Parameter status not supported");
    }
}

static RadioTxState fromCpp(tt::hal::radio::RadioDevice::TransmissionState state) {
    switch (state) {
        case tt::hal::radio::RadioDevice::TransmissionState::Queued:
            return RADIO_TX_QUEUED;
        case tt::hal::radio::RadioDevice::TransmissionState::PendingTransmit:
            return RADIO_TX_PENDING_TRANSMIT;
        case tt::hal::radio::RadioDevice::TransmissionState::Transmitted:
            return RADIO_TX_TRANSMITTED;
        case tt::hal::radio::RadioDevice::TransmissionState::Timeout:
            return RADIO_TX_TIMEOUT;
        case tt::hal::radio::RadioDevice::TransmissionState::Error:
            return RADIO_TX_ERROR;
        default:
            LOG_W(TAG, "Unknown enum \"%d\" passed!", state);
            check(false, "Transmission state not supported");
    }
}

static tt::hal::radio::RadioDevice::TransmissionState toCpp(RadioTxState state) {
    switch (state) {
        case RADIO_TX_QUEUED:
            return tt::hal::radio::RadioDevice::TransmissionState::Queued;
        case RADIO_TX_PENDING_TRANSMIT:
            return tt::hal::radio::RadioDevice::TransmissionState::PendingTransmit;
        case RADIO_TX_TRANSMITTED:
            return tt::hal::radio::RadioDevice::TransmissionState::Transmitted;
        case RADIO_TX_TIMEOUT:
            return tt::hal::radio::RadioDevice::TransmissionState::Timeout;
        case RADIO_TX_ERROR:
            return tt::hal::radio::RadioDevice::TransmissionState::Error;
        default:
            LOG_W(TAG, "Unknown enum \"%d\" passed!", state);
            check(false, "Transmission state not supported");
    }
}
