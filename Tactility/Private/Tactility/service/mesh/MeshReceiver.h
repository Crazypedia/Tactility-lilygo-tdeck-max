#pragma once

#include "MeshCrypto.h"
#include "MeshProtocol.h"
#include "PacketDedup.h"

#include <meshtastic/mesh.pb.h>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace tt::service::mesh {

/** A channel the node participates in: display name plus PSK. The wire
 * channel hash is derived from both. Index 0 is the primary channel used
 * for TX by default. */
struct ChannelConfig {
    std::string name;
    uint8_t psk[PSK_SIZE_AES256] = {};
    size_t pskLength = 0;
    uint8_t hash = 0; // derived via updateHash()

    ChannelConfig() = default;

    ChannelConfig(const std::string& name, const uint8_t* pskData, size_t pskDataLength) : name(name) {
        pskLength = pskDataLength <= sizeof(psk) ? pskDataLength : sizeof(psk);
        std::memcpy(psk, pskData, pskLength);
        updateHash();
    }

    void updateHash() {
        hash = channelHash(name, psk, pskLength);
    }

    /** The default channel every Meshtastic node knows. */
    static ChannelConfig longFast() {
        return ChannelConfig("LongFast", DEFAULT_PSK, PSK_SIZE_AES128);
    }
};

/**
 * RX pipeline: raw LoRa frame in, decoded packet out.
 * Header parse -> dedup -> channel-hash match -> decrypt -> protobuf decode.
 *
 * Multiple channels are matched by hash; on hash collision every matching
 * channel's key is tried until one yields a valid protobuf.
 *
 * Radio-independent: the radio's RX callback feeds frames in, subscribers
 * consume ReceivedPacket. Not internally synchronized - callers serialize.
 */
class MeshReceiver {

public:

    enum class Result {
        Ok,
        TooShort,      // frame smaller than header, or empty payload
        Duplicate,     // (from, id) already seen
        UnknownChannel,// channel hash doesn't match any configured channel
        DecodeFailed   // wrong key or corrupt payload (CTR has no MAC, so a
                       // bad key surfaces as a protobuf decode failure)
    };

    struct ReceivedPacket {
        PacketHeader header;
        meshtastic_Data data;
        size_t channelIndex = 0;  // index into getChannels() when Result::Ok and !pkiEncrypted
        bool pkiEncrypted = false; // decrypted with our X25519 key, not a channel PSK
        float rssi = 0;
        float snr = 0;
    };

    /** Looks up a node's X25519 public key (32 bytes into publicKeyOut).
     * @return false when the node or its key is unknown
     */
    using PublicKeyLookup = std::function<bool(uint32_t nodeId, uint8_t* publicKeyOut)>;

    /** Defaults to the LongFast channel with the well-known PSK. */
    MeshReceiver() {
        channels.push_back(ChannelConfig::longFast());
    }

    /** Replace the channel table. An empty vector disables all decoding. */
    void setChannels(std::vector<ChannelConfig> newChannels) {
        channels = std::move(newChannels);
    }

    const std::vector<ChannelConfig>& getChannels() const {
        return channels;
    }

    /** Enable PKC direct-message decryption: frames addressed to ownNodeId
     * with a zero channel hash are tried against the sender's public key
     * before the channel PSKs. The lookup may be called from the radio
     * thread and must handle its own locking. */
    void setPkc(uint32_t newOwnNodeId, const uint8_t newOwnPrivateKey[PKC_KEY_SIZE], PublicKeyLookup lookup) {
        ownNodeId = newOwnNodeId;
        memcpy(ownPrivateKey, newOwnPrivateKey, PKC_KEY_SIZE);
        lookupPublicKey = std::move(lookup);
    }

    /** Process a received frame.
     * @param[in] frame raw LoRa frame (header + encrypted payload)
     * @param[in] length frame length
     * @param[in] rssi received signal strength (dBm)
     * @param[in] snr signal-to-noise ratio (dB)
     * @param[out] out decoded packet, valid only when Result::Ok
     */
    Result process(const uint8_t* frame, size_t length, float rssi, float snr, ReceivedPacket& out);

private:

    PacketDedup dedup;
    std::vector<ChannelConfig> channels;
    uint32_t ownNodeId = 0;
    uint8_t ownPrivateKey[PKC_KEY_SIZE] = {};
    PublicKeyLookup lookupPublicKey;

    bool tryPkcDecrypt(const uint8_t* payload, size_t payloadLength, ReceivedPacket& out);
};

} // namespace tt::service::mesh
