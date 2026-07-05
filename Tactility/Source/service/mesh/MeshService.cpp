#include "Tactility/service/mesh/MeshService.h"

#include "Tactility/service/mesh/MeshCrypto.h"
#include "Tactility/service/mesh/MeshFrameBuilder.h"

#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>

#include <tactility/hal/Device.h>

#include <tactility/log.h>

#include <meshtastic/telemetry.pb.h>
#include <pb_decode.h>

#include <cstdio>
#include <random>

namespace tt::service::mesh {

constexpr auto TAG = "MeshService";

// LongFast US/915 primary slot. Becomes configurable via the settings app
// (Phase 3); until then these mirror the values proven in the Phase 1 soak.
constexpr float LONGFAST_US_FREQUENCY_MHZ = 906.875;
constexpr uint8_t DEFAULT_HOP_LIMIT = 3;

extern const ServiceManifest manifest;

bool MeshService::onStart(ServiceContext&) {
    // Node id and packet id must be unpredictable enough to not collide with
    // other nodes on air. Real Meshtastic derives the node id from the MAC;
    // that (plus persistence) lands with the settings app.
    std::random_device randomDevice;
    ownNodeId = randomDevice();
    nextPacketId = randomDevice();
    return true;
}

void MeshService::onStop(ServiceContext&) {
    disable();
}

bool MeshService::configureRadio() {
    using RadioDevice = hal::radio::RadioDevice;
    if (!radio->setModulation(RadioDevice::Modulation::LoRa)) {
        LOG_E(TAG, "setModulation(LoRa) failed");
        return false;
    }
    using P = RadioDevice::Parameter;
    radio->setParameter(P::Frequency, LONGFAST_US_FREQUENCY_MHZ);
    radio->setParameter(P::Bandwidth, 250.0);
    radio->setParameter(P::SpreadFactor, 11);
    radio->setParameter(P::CodingRate, 5);
    radio->setParameter(P::SyncWord, LORA_SYNC_WORD);
    radio->setParameter(P::PreambleLength, 16);
    radio->setParameter(P::BoostedGain, 1);
    return true;
}

bool MeshService::enable() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (enabled) {
        return true;
    }

    radio = hal::findFirstDevice<hal::radio::RadioDevice>(hal::Device::Type::Radio);
    if (radio == nullptr) {
        LOG_E(TAG, "No radio device found");
        return false;
    }

    rxSubscription = radio->subscribeRx([this](hal::Device::Id, const hal::radio::RxPacket& packet) {
        onRx(packet);
    });

    if (!configureRadio() || !radio->start()) {
        LOG_E(TAG, "Radio start failed");
        radio->unsubscribeRx(rxSubscription);
        radio = nullptr;
        return false;
    }

    enabled = true;
    LOG_I(TAG, "Enabled: LongFast %.3f MHz, node id !%08lx", LONGFAST_US_FREQUENCY_MHZ, static_cast<unsigned long>(ownNodeId));
    return true;
}

void MeshService::disable() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!enabled) {
        return;
    }

    radio->unsubscribeRx(rxSubscription);
    radio->stop();
    radio = nullptr;
    enabled = false;
    LOG_I(TAG, "Disabled");
}

bool MeshService::isEnabled() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return enabled;
}

MeshService::MessageSubscription MeshService::subscribeMessages(MessageCallback onMessage) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    const auto id = ++lastSubscriptionId;
    messageSubscriptions.push_back({id, std::move(onMessage)});
    return id;
}

void MeshService::unsubscribeMessages(MessageSubscription subscription) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    std::erase_if(messageSubscriptions, [subscription](auto& data) { return data.id == subscription; });
}

MeshService::NodeSubscription MeshService::subscribeNodeUpdates(NodeCallback onNodeUpdate) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    const auto id = ++lastSubscriptionId;
    nodeSubscriptions.push_back({id, std::move(onNodeUpdate)});
    return id;
}

void MeshService::unsubscribeNodeUpdates(NodeSubscription subscription) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    std::erase_if(nodeSubscriptions, [subscription](auto& data) { return data.id == subscription; });
}

std::vector<MeshService::NodeInfo> MeshService::getNodes() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    std::vector<NodeInfo> result;
    result.reserve(nodes.size());
    for (const auto& [id, node] : nodes) {
        result.push_back(node);
    }
    return result;
}

std::string MeshService::getNodeName(uint32_t nodeId) const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    const auto it = nodes.find(nodeId);
    if (it != nodes.end() && !it->second.shortName.empty()) {
        return it->second.shortName;
    }
    char fallback[12];
    snprintf(fallback, sizeof(fallback), "!%08lx", static_cast<unsigned long>(nodeId));
    return fallback;
}

uint32_t MeshService::getOwnNodeId() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return ownNodeId;
}

void MeshService::updateNodeDb(const MeshReceiver::ReceivedPacket& packet) {
    // Caller holds the mutex.
    auto& node = nodes[packet.header.from];
    node.nodeId = packet.header.from;
    node.lastRssi = packet.rssi;
    node.lastSnr = packet.snr;
    node.packetsHeard++;

    switch (packet.data.portnum) {
        case meshtastic_PortNum_NODEINFO_APP: {
            meshtastic_User user = meshtastic_User_init_zero;
            pb_istream_t stream = pb_istream_from_buffer(packet.data.payload.bytes, packet.data.payload.size);
            if (pb_decode(&stream, meshtastic_User_fields, &user)) {
                node.shortName = user.short_name;
                node.longName = user.long_name;
            }
            break;
        }
        case meshtastic_PortNum_POSITION_APP: {
            meshtastic_Position position = meshtastic_Position_init_zero;
            pb_istream_t stream = pb_istream_from_buffer(packet.data.payload.bytes, packet.data.payload.size);
            if (pb_decode(&stream, meshtastic_Position_fields, &position) && position.latitude_i != 0) {
                node.hasPosition = true;
                node.latitude = position.latitude_i * 1e-7;
                node.longitude = position.longitude_i * 1e-7;
            }
            break;
        }
        case meshtastic_PortNum_TELEMETRY_APP: {
            meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
            pb_istream_t stream = pb_istream_from_buffer(packet.data.payload.bytes, packet.data.payload.size);
            if (pb_decode(&stream, meshtastic_Telemetry_fields, &telemetry) && telemetry.which_variant == meshtastic_Telemetry_device_metrics_tag) {
                node.hasBattery = true;
                node.batteryLevel = static_cast<uint8_t>(telemetry.variant.device_metrics.battery_level);
            }
            break;
        }
        default:
            break;
    }
}

void MeshService::onRx(const hal::radio::RxPacket& rxPacket) {
    MeshReceiver::ReceivedPacket packet;
    const auto result = receiver.process(rxPacket.data.data(), rxPacket.data.size(), rxPacket.rssi, rxPacket.snr, packet);

    if (result != MeshReceiver::Result::Ok) {
        if (result == MeshReceiver::Result::UnknownChannel || result == MeshReceiver::Result::DecodeFailed) {
            LOG_I(TAG, "Undecodable frame: %u bytes, ch=%02x, %.0f dBm", static_cast<unsigned>(rxPacket.data.size()), packet.header.channelHash, rxPacket.rssi);
        }
        return;
    }

    LOG_I(TAG, "RX !%08lx -> !%08lx port=%d %u bytes %.0f dBm", static_cast<unsigned long>(packet.header.from), static_cast<unsigned long>(packet.header.to), static_cast<int>(packet.data.portnum), static_cast<unsigned>(packet.data.payload.size), packet.rssi);

    // Copy the callback lists under the lock, invoke without it: subscribers
    // may grab other locks (e.g. LVGL) in their callbacks.
    std::vector<MessageCallback> messageCallbacks;
    std::vector<NodeCallback> nodeCallbacks;
    NodeInfo nodeSnapshot;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        updateNodeDb(packet);
        nodeSnapshot = nodes[packet.header.from];
        for (const auto& subscription : messageSubscriptions) {
            messageCallbacks.push_back(subscription.onMessage);
        }
        for (const auto& subscription : nodeSubscriptions) {
            nodeCallbacks.push_back(subscription.onNodeUpdate);
        }
    }

    for (const auto& callback : nodeCallbacks) {
        callback(nodeSnapshot);
    }
    for (const auto& callback : messageCallbacks) {
        callback(packet);
    }
}

bool MeshService::sendText(uint32_t destination, const std::string& text) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!enabled) {
        LOG_E(TAG, "sendText while disabled");
        return false;
    }

    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    data.payload.size = text.size() < sizeof(data.payload.bytes) ? text.size() : sizeof(data.payload.bytes);
    memcpy(data.payload.bytes, text.data(), data.payload.size);

    PacketHeader header;
    header.to = destination;
    header.from = ownNodeId;
    header.id = ++nextPacketId;
    header.hopLimit = DEFAULT_HOP_LIMIT;
    header.hopStart = DEFAULT_HOP_LIMIT;
    header.wantAck = destination != BROADCAST_ADDRESS;
    header.channelHash = channelHash("LongFast", DEFAULT_PSK, PSK_SIZE_AES128);

    uint8_t frame[MAX_LORA_PAYLOAD];
    size_t frameLength = 0;
    if (!buildDataFrame(header, data, DEFAULT_PSK, PSK_SIZE_AES128, frame, sizeof(frame), frameLength)) {
        LOG_E(TAG, "Failed to build frame");
        return false;
    }

    hal::radio::TxPacket packet {
        .data = std::vector<uint8_t>(frame, frame + frameLength),
        .address = 0
    };
    radio->transmit(packet, [](hal::radio::RadioDevice::TxId id, hal::radio::RadioDevice::TransmissionState state) {
        LOG_I(TAG, "TX %d state %d", id, static_cast<int>(state));
    });
    LOG_I(TAG, "Queued text to !%08lx, id=%lu, %u bytes", static_cast<unsigned long>(destination), static_cast<unsigned long>(header.id), static_cast<unsigned>(frameLength));
    return true;
}

std::shared_ptr<MeshService> findService() {
    return std::static_pointer_cast<MeshService>(
        findServiceById(manifest.id)
    );
}

extern const ServiceManifest manifest = {
    .id = "Mesh",
    .createService = create<MeshService>
};

} // namespace tt::service::mesh
