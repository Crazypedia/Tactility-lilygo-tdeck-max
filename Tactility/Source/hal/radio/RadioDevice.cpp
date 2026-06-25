#include "Tactility/hal/radio/RadioDevice.h"
#include <Tactility/CoreDefines.h>
#include <cstring>

namespace tt::hal::radio {

constexpr const char* TAG = "RadioDevice";

bool RadioDevice::setModulation(const RadioDevice::Modulation newModulation) {
    // A bool is chosen over an enum class because:
    //  - this is not tied to user input and
    //  - the programmer can infer why it didn't work using
    //    other methods such as getState() and canTransmit/Receive()
    const auto state = getState();
    if ((state == State::PendingOn) || (state == State::On)) {
        return false;
    } else if (!((newModulation == Modulation::None) || canTransmit(newModulation) || canReceive(newModulation))) {
        return false;
    } else {
        auto lock = mutex.asScopedLock();
        lock.lock();
        modulation = newModulation;
    }

    return true;
}

RadioDevice::Modulation RadioDevice::getModulation() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return modulation;
}

RadioDevice::State RadioDevice::getState() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return state; // Make copy because of thread safety
}

void RadioDevice::setState(State newState) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (state != newState) {
        statePubSub.publish(getId(), newState);
    }

    state = newState;
}

void RadioDevice::publishRx(const RxPacket& packet) {
    mutex.lock();
    rxPubSub.publish(getId(), packet);
    mutex.unlock();
}

const char* toString(RadioDevice::Modulation modulation) {
    using enum RadioDevice::Modulation;
    switch (modulation) {
        case None:
            return "none";
        case Fsk:
            return "FSK";
        case LoRa:
            return "LoRa";
        case LrFhss:
            return "LR-FHSS";
        default:
            return "Unkown";
    }
}

const char* toString(RadioDevice::Parameter parameter) {
    using enum RadioDevice::Parameter;
    switch (parameter) {
        case Power:
            return TT_STRINGIFY(Power);
        case Frequency:
            return TT_STRINGIFY(Frequency);
        case Bandwidth:
            return TT_STRINGIFY(Bandwidth);
        case SpreadFactor:
            return TT_STRINGIFY(SpreadFactor);
        case CodingRate:
            return TT_STRINGIFY(CodingRate);
        case SyncWord:
            return TT_STRINGIFY(SyncWord);
        case PreambleLength:
            return TT_STRINGIFY(PreambleLength);
        case FrequencyDeviation:
            return TT_STRINGIFY(FrequencyDeviation);
        case DataRate:
            return TT_STRINGIFY(DataRate);
        case AddressWidth:
            return TT_STRINGIFY(AddressWidth);
        case NarrowGrid:
            return TT_STRINGIFY(NarrowGrid);
        default:
            return "Unknown";
    }
}

} // namespace tt::hal::radio
