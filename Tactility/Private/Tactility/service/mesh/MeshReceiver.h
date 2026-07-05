#pragma once

#include "MeshCrypto.h"
#include "MeshProtocol.h"
#include "PacketDedup.h"

#include <meshtastic/mesh.pb.h>

#include <string>

namespace tt::service::mesh {

/**
 * RX pipeline: raw LoRa frame in, decoded packet out.
 * Header parse -> dedup -> channel-hash match -> decrypt -> protobuf decode.
 *
 * Radio-independent: the radio's RX callback feeds frames in, subscribers
 * consume ReceivedPacket. Phase 1 scope is a single configured channel.
 */
class MeshReceiver {

public:

    enum class Result {
        Ok,
        TooShort,      // frame smaller than header, or empty payload
        Duplicate,     // (from, id) already seen
        UnknownChannel,// channel hash doesn't match the configured channel
        DecodeFailed   // wrong key or corrupt payload (CTR has no MAC, so a
                       // bad key surfaces as a protobuf decode failure)
    };

    struct ReceivedPacket {
        PacketHeader header;
        meshtastic_Data data;
        float rssi = 0;
        float snr = 0;
    };

    /** Defaults to the LongFast channel with the well-known PSK. */
    MeshReceiver() {
        setChannel("LongFast", DEFAULT_PSK, PSK_SIZE_AES128);
    }

    void setChannel(const std::string& name, const uint8_t* psk, size_t pskLength);

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
    std::string channelName;
    uint8_t channelPsk[PSK_SIZE_AES256] = {};
    size_t channelPskLength = 0;
    uint8_t configuredChannelHash = 0;
};

} // namespace tt::service::mesh
