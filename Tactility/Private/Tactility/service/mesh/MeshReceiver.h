#pragma once

#include "MeshCrypto.h"
#include "MeshProtocol.h"
#include "PacketDedup.h"

#include <meshtastic/mesh.pb.h>

#include <cstring>
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
        size_t channelIndex = 0; // index into getChannels() when Result::Ok
        float rssi = 0;
        float snr = 0;
    };

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
};

} // namespace tt::service::mesh
