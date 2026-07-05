#include "Tactility/service/mesh/MeshService.h"

#include "Tactility/service/mesh/MeshCrypto.h"
#include "Tactility/service/mesh/MeshFrameBuilder.h"

#include <Tactility/Preferences.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>

#include <tactility/hal/Device.h>

#include <tactility/log.h>

#include <meshtastic/telemetry.pb.h>
#include <pb_decode.h>
#include <pb_encode.h>

#ifdef ESP_PLATFORM
#include <esp_mac.h>
#endif

#include <cstdio>
#include <random>

namespace tt::service::mesh {

constexpr auto TAG = "MeshService";

// LongFast US/915 primary slot. Becomes configurable via the settings app
// (Phase 3); until then these mirror the values proven in the Phase 1 soak.
constexpr float LONGFAST_US_FREQUENCY_MHZ = 906.875;
constexpr uint8_t DEFAULT_HOP_LIMIT = 3;

// In-memory chat history cap, shared across all conversations. Sized for
// UI scrollback, not archival - persistent storage is a Phase 3 item.
constexpr size_t MAX_TEXT_MESSAGES = 128;

extern const ServiceManifest manifest;

namespace {

std::string toHex(const uint8_t* data, size_t length) {
    std::string result(length * 2, '0');
    for (size_t i = 0; i < length; i++) {
        snprintf(result.data() + i * 2, 3, "%02x", data[i]);
    }
    return result;
}

bool fromHex(const std::string& hex, uint8_t* out, size_t length) {
    if (hex.size() != length * 2) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned value;
        if (sscanf(hex.c_str() + i * 2, "%02x", &value) != 1) {
            return false;
        }
        out[i] = static_cast<uint8_t>(value);
    }
    return true;
}

} // namespace

bool MeshService::onStart(ServiceContext&) {
    std::random_device randomDevice;
    nextPacketId = randomDevice();
    initIdentity();
    receiver.setPkc(ownNodeId, ownPrivateKey, [this](uint32_t nodeId, uint8_t* publicKeyOut) {
        auto lock = mutex.asScopedLock();
        lock.lock();
        const auto it = nodes.find(nodeId);
        if (it == nodes.end() || !it->second.hasPublicKey) {
            return false;
        }
        memcpy(publicKeyOut, it->second.publicKey, PKC_KEY_SIZE);
        return true;
    });
    return true;
}

void MeshService::initIdentity() {
    // Stable node id: PKC peers bind our public key to it, so it must
    // survive reboots. Meshtastic derives it from the MAC the same way.
#ifdef ESP_PLATFORM
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        ownNodeId = static_cast<uint32_t>(mac[2]) << 24 | static_cast<uint32_t>(mac[3]) << 16 | static_cast<uint32_t>(mac[4]) << 8 | mac[5];
    }
#else
    Preferences simPreferences("mesh");
    int32_t storedId = 0;
    if (simPreferences.optInt32("nodeId", storedId)) {
        ownNodeId = static_cast<uint32_t>(storedId);
    }
#endif
    while (ownNodeId < 4 || ownNodeId == BROADCAST_ADDRESS) {
        std::random_device randomDevice;
        ownNodeId = randomDevice();
#ifndef ESP_PLATFORM
        Preferences("mesh").putInt32("nodeId", static_cast<int32_t>(ownNodeId));
#endif
    }

    // X25519 identity keypair for PKC direct messages. Persisted because
    // peers cache the public key from our NodeInfo; regenerating it every
    // boot would break their ability to decrypt us ("key changed" warnings).
    Preferences preferences("mesh");
    std::string storedKey;
    if (preferences.optString("privateKey", storedKey) && fromHex(storedKey, ownPrivateKey, PKC_KEY_SIZE)) {
        hasIdentityKeys = derivePublicKey(ownPrivateKey, ownPublicKey);
    }
    if (!hasIdentityKeys) {
        hasIdentityKeys = generateKeyPair(ownPublicKey, ownPrivateKey);
        if (hasIdentityKeys) {
            preferences.putString("privateKey", toHex(ownPrivateKey, PKC_KEY_SIZE));
        } else {
            LOG_E(TAG, "Identity key generation failed; PKC DMs unavailable");
        }
    }
    if (hasIdentityKeys) {
        LOG_I(TAG, "Node !%08lx, public key %s", static_cast<unsigned long>(ownNodeId), toHex(ownPublicKey, 8).c_str());
    }
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

MeshService::TxStatusSubscription MeshService::subscribeTxStatus(TxStatusCallback onTxStatus) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    const auto id = ++lastSubscriptionId;
    txStatusSubscriptions.push_back({id, std::move(onTxStatus)});
    return id;
}

void MeshService::unsubscribeTxStatus(TxStatusSubscription subscription) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    std::erase_if(txStatusSubscriptions, [subscription](auto& data) { return data.id == subscription; });
}

std::vector<MeshService::TextMessage> MeshService::getTextMessages() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return {textMessages.begin(), textMessages.end()};
}

void MeshService::recordTextMessage(TextMessage message) {
    textMessages.push_back(std::move(message));
    while (textMessages.size() > MAX_TEXT_MESSAGES) {
        textMessages.pop_front();
    }
}

void MeshService::updateTxStatus(uint32_t packetId, TxStatus status) {
    // Copy the callback list under the lock, invoke without it: subscribers
    // may grab other locks (e.g. LVGL) in their callbacks.
    std::vector<TxStatusCallback> callbacks;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        for (auto& message : textMessages) {
            if (message.isOwn && message.packetId == packetId) {
                message.txStatus = status;
                break;
            }
        }
        for (const auto& subscription : txStatusSubscriptions) {
            callbacks.push_back(subscription.onTxStatus);
        }
    }
    for (const auto& callback : callbacks) {
        callback(packetId, status);
    }
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

bool MeshService::getOwnPublicKey(uint8_t publicKeyOut[PKC_KEY_SIZE]) const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    if (!hasIdentityKeys) {
        return false;
    }
    memcpy(publicKeyOut, ownPublicKey, PKC_KEY_SIZE);
    return true;
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
                if (user.public_key.size == PKC_KEY_SIZE) {
                    if (node.hasPublicKey && memcmp(node.publicKey, user.public_key.bytes, PKC_KEY_SIZE) != 0) {
                        // A changed key is either a reinstall or an impersonation
                        // attempt; keep the first-seen key like the firmware does
                        // until a trust decision UI exists.
                        LOG_W(TAG, "Node !%08lx advertised a different public key; keeping the first-seen key", static_cast<unsigned long>(node.nodeId));
                    } else {
                        memcpy(node.publicKey, user.public_key.bytes, PKC_KEY_SIZE);
                        node.hasPublicKey = true;
                    }
                }
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
        if (packet.data.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
            TextMessage message;
            message.packetId = packet.header.id;
            message.from = packet.header.from;
            message.to = packet.header.to;
            message.channelIndex = packet.channelIndex;
            message.text = std::string(reinterpret_cast<const char*>(packet.data.payload.bytes), packet.data.payload.size);
            recordTextMessage(std::move(message));
        }
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

void MeshService::setChannels(std::vector<ChannelConfig> channels) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    receiver.setChannels(std::move(channels));
}

std::vector<ChannelConfig> MeshService::getChannels() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return receiver.getChannels();
}

void MeshService::maybeSendNodeInfo(uint32_t destination, const ChannelConfig& channel) {
    // Caller holds the mutex and has verified the service is enabled.
    // Directed (not broadcast), PSK-encrypted like the firmware does for
    // NODEINFO_APP (Router.cpp excludes it from PKC so new peers can read
    // it), and only once per peer per boot to keep airtime minimal.
    if (!hasIdentityKeys || nodeInfoSentTo.contains(destination)) {
        return;
    }

    meshtastic_User user = meshtastic_User_init_zero;
    snprintf(user.id, sizeof(user.id), "!%08lx", static_cast<unsigned long>(ownNodeId));
    snprintf(user.long_name, sizeof(user.long_name), "Tactility %04x", static_cast<unsigned>(ownNodeId & 0xFFFF));
    snprintf(user.short_name, sizeof(user.short_name), "%04x", static_cast<unsigned>(ownNodeId & 0xFFFF));
    user.hw_model = meshtastic_HardwareModel_PRIVATE_HW;
    user.public_key.size = PKC_KEY_SIZE;
    memcpy(user.public_key.bytes, ownPublicKey, PKC_KEY_SIZE);

    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_NODEINFO_APP;
    pb_ostream_t stream = pb_ostream_from_buffer(data.payload.bytes, sizeof(data.payload.bytes));
    if (!pb_encode(&stream, meshtastic_User_fields, &user)) {
        LOG_E(TAG, "NodeInfo encode failed");
        return;
    }
    data.payload.size = stream.bytes_written;
    data.has_bitfield = true;
    data.bitfield = 0;

    PacketHeader header;
    header.to = destination;
    header.from = ownNodeId;
    header.id = ++nextPacketId;
    header.hopLimit = DEFAULT_HOP_LIMIT;
    header.hopStart = DEFAULT_HOP_LIMIT;
    header.wantAck = false;
    header.channelHash = channel.hash;

    uint8_t frame[MAX_LORA_PAYLOAD];
    size_t frameLength = 0;
    if (!buildDataFrame(header, data, channel.psk, channel.pskLength, frame, sizeof(frame), frameLength)) {
        LOG_E(TAG, "Failed to build NodeInfo frame");
        return;
    }

    nodeInfoSentTo.insert(destination);
    hal::radio::TxPacket packet {
        .data = std::vector<uint8_t>(frame, frame + frameLength),
        .address = 0
    };
    // transmit() invokes the callback unconditionally — an empty std::function aborts
    radio->transmit(packet, [](hal::radio::RadioDevice::TxId, hal::radio::RadioDevice::TransmissionState) {});
    LOG_I(TAG, "Queued directed NodeInfo (public key advertisement) to !%08lx", static_cast<unsigned long>(destination));
}

uint32_t MeshService::sendText(size_t channelIndex, uint32_t destination, const std::string& text) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!enabled) {
        LOG_E(TAG, "sendText while disabled");
        return 0;
    }

    const auto& channels = receiver.getChannels();
    if (channelIndex >= channels.size()) {
        LOG_E(TAG, "sendText: invalid channel index %u", static_cast<unsigned>(channelIndex));
        return 0;
    }
    const auto& channel = channels[channelIndex];

    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    data.payload.size = text.size() < sizeof(data.payload.bytes) ? text.size() : sizeof(data.payload.bytes);
    memcpy(data.payload.bytes, text.data(), data.payload.size);
    // 2.5+ senders always carry the bitfield byte; bit 0 clear = not OK to
    // uplink to MQTT, matching the stock firmware default.
    data.has_bitfield = true;
    data.bitfield = 0;

    // DMs to peers that advertised an X25519 key must be PKC-sealed:
    // 2.5+ firmware drops PSK-encrypted DMs from PKC-capable senders.
    const NodeInfo* destinationNode = nullptr;
    if (destination != BROADCAST_ADDRESS) {
        const auto it = nodes.find(destination);
        if (it != nodes.end() && it->second.hasPublicKey) {
            destinationNode = &it->second;
        }
    }
    const bool usePkc = destinationNode != nullptr && hasIdentityKeys;

    if (usePkc) {
        // The peer can only decrypt (and reply) once it has our public key,
        // which is only ever advertised as part of this user-initiated send.
        maybeSendNodeInfo(destination, channel);
    }

    PacketHeader header;
    header.to = destination;
    header.from = ownNodeId;
    header.id = ++nextPacketId;
    header.hopLimit = DEFAULT_HOP_LIMIT;
    header.hopStart = DEFAULT_HOP_LIMIT;
    header.wantAck = destination != BROADCAST_ADDRESS;
    header.channelHash = usePkc ? 0 : channel.hash; // PKC packets carry no channel hash

    uint8_t frame[MAX_LORA_PAYLOAD];
    size_t frameLength = 0;
    bool built;
    if (usePkc) {
        uint8_t extraNonceBytes[4];
        if (!secureRandom(extraNonceBytes, sizeof(extraNonceBytes))) {
            LOG_E(TAG, "RNG failure");
            return 0;
        }
        uint32_t extraNonce;
        memcpy(&extraNonce, extraNonceBytes, sizeof(extraNonce));
        built = buildPkcDataFrame(header, data, ownPrivateKey, destinationNode->publicKey, extraNonce, frame, sizeof(frame), frameLength);
    } else {
        built = buildDataFrame(header, data, channel.psk, channel.pskLength, frame, sizeof(frame), frameLength);
    }
    if (!built) {
        LOG_E(TAG, "Failed to build frame");
        return 0;
    }

    const uint32_t packetId = header.id;

    TextMessage message;
    message.packetId = packetId;
    message.from = ownNodeId;
    message.to = destination;
    message.channelIndex = channelIndex;
    message.isOwn = true;
    message.txStatus = TxStatus::Queued;
    message.text = std::string(reinterpret_cast<const char*>(data.payload.bytes), data.payload.size);
    recordTextMessage(std::move(message));

    hal::radio::TxPacket packet {
        .data = std::vector<uint8_t>(frame, frame + frameLength),
        .address = 0
    };
    // The lambda captures only the service; it outlives any app, so a
    // pending TX can't end up invoking a destroyed subscriber directly.
    radio->transmit(packet, [this, packetId](hal::radio::RadioDevice::TxId id, hal::radio::RadioDevice::TransmissionState state) {
        using TransmissionState = hal::radio::RadioDevice::TransmissionState;
        TxStatus status;
        switch (state) {
            case TransmissionState::Queued: status = TxStatus::Queued; break;
            case TransmissionState::PendingTransmit: status = TxStatus::Sending; break;
            case TransmissionState::Transmitted: status = TxStatus::Sent; break;
            default: status = TxStatus::Failed; break;
        }
        LOG_I(TAG, "TX %d state %d", id, static_cast<int>(state));
        updateTxStatus(packetId, status);
    });
    LOG_I(TAG, "Queued %s on ch%u to !%08lx, id=%lu, %u bytes", usePkc ? "PKC text" : "text", static_cast<unsigned>(channelIndex), static_cast<unsigned long>(destination), static_cast<unsigned long>(packetId), static_cast<unsigned>(frameLength));
    return packetId;
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
