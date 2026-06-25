#pragma once

#include <tactility/hal/Device.h>
#include "PubSub.h"
#include "Unit.h"

#include <Tactility/RecursiveMutex.h>
#include <Tactility/Thread.h>

#include <deque>
#include <utility>

namespace tt::hal::radio {

struct RxPacket {
    std::vector<uint8_t> data;
    float rssi;
    float snr;
};

struct TxPacket {
    std::vector<uint8_t> data;
    uint32_t address; // FSK only
};

class RadioDevice : public Device {

public:
    enum class Modulation {
        None,
        Fsk,
        LoRa,
        LrFhss
    };

    enum class Parameter {
        Power,
        BoostedGain,
        Frequency,
        Bandwidth,
        SpreadFactor,
        CodingRate,
        SyncWord,
        PreambleLength,
        FrequencyDeviation,
        DataRate,
        AddressWidth,
        NarrowGrid,
    };

    enum class ParameterStatus {
        Unavailable,
        ValueError,
        Success
    };

    enum class State {
        PendingOn,
        On,
        Error,
        PendingOff,
        Off
    };

    enum class TransmissionState {
        Queued,
        PendingTransmit,
        Transmitted,
        Timeout,
        Error
    };

    typedef int TxId;

    using StatePubSub = PubSub<Device::Id, State>;
    using RxPubSub = PubSub<Device::Id, const RxPacket&>;
    using StateSubscriptionId = StatePubSub::SubscriptionId;
    using RxSubscriptionId = RxPubSub::SubscriptionId;

    using StateCallback = StatePubSub::Notifier;
    using RxCallback = RxPubSub::Notifier;
    using TxStateCallback = std::function<void(TxId id, TransmissionState state)>;

protected:
    struct TxItem {
        TxId id;
        TxPacket packet;
        TxStateCallback callback;
    };

private:
    State state;
    Modulation modulation;
    RecursiveMutex mutex;
    StatePubSub statePubSub;
    RxPubSub rxPubSub;
    std::deque<TxItem> txQueue;
    TxId lastTxId = 0;

protected:
    const RecursiveMutex &getMutex() const { return mutex; }
    void setState(State newState);

    virtual void txQueuedSignal() = 0;

    size_t getTxQueueSize() const {
        auto lock = mutex.asScopedLock();
        lock.lock();
        const auto size = txQueue.size();
        return size;
    }

    TxItem popNextQueuedTx() {
        auto lock = mutex.asScopedLock();
        lock.lock();

        auto tx = std::move(txQueue.front());
        txQueue.pop_front();

        return tx;
    }

    void publishRx(const RxPacket& packet);

public:
    explicit RadioDevice()
        : state(State::Off), modulation(Modulation::None) {}

    ~RadioDevice() override = default;

    Type getType() const override { return Type::Radio; }

    bool setModulation(const Modulation newModulation);
    Modulation getModulation() const;
    virtual ParameterStatus setParameter(const Parameter parameter, const float value) = 0;
    virtual ParameterStatus getParameter(const Parameter parameter, float &value) const = 0;
    virtual Unit getParameterUnit(const Parameter parameter) const = 0;
    virtual bool canTransmit(const Modulation modulation) = 0;
    virtual bool canReceive(const Modulation modulation) = 0;

    virtual bool start() = 0;
    virtual bool stop() = 0;

    TxId transmit(const TxPacket& packet, TxStateCallback callback) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        const auto txId = lastTxId;
        txQueue.push_back(TxItem{.id = txId, .packet = packet, .callback = callback});
        callback(txId, TransmissionState::Queued);
        lastTxId++;
        txQueuedSignal();
        return txId;
    }

    StateSubscriptionId subscribeStateChange(StateCallback onChange) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return statePubSub.subscribe(onChange);
    }

    void unsubscribeStateChange(StateSubscriptionId subscriptionId) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return rxPubSub.unsubscribe(subscriptionId);
    }

    RxSubscriptionId subscribeRx(const RxCallback& onData) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return rxPubSub.subscribe(onData);
    }

    void unsubscribeRx(RxSubscriptionId subscriptionId) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return rxPubSub.unsubscribe(subscriptionId);
    }

    State getState() const;
};

const char* toString(RadioDevice::Modulation modulation);
const char* toString(RadioDevice::Parameter parameter);
}
