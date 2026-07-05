#include "Tactility/service/mesh/MeshReceiver.h"

#include "Tactility/service/mesh/MeshCodec.h"

namespace tt::service::mesh {

MeshReceiver::Result MeshReceiver::process(const uint8_t* frame, size_t length, float rssi, float snr, ReceivedPacket& out) {
    if (!parseHeader(frame, length, out.header)) {
        return Result::TooShort;
    }

    const size_t payloadLength = length - PACKET_HEADER_SIZE;
    if (payloadLength == 0 || payloadLength > MAX_ENCRYPTED_PAYLOAD) {
        return Result::TooShort;
    }

    if (!dedup.checkAndAdd(out.header.from, out.header.id)) {
        return Result::Duplicate;
    }

    bool hashMatched = false;
    for (size_t i = 0; i < channels.size(); i++) {
        const auto& channel = channels[i];
        if (channel.hash != out.header.channelHash) {
            continue;
        }
        hashMatched = true;

        uint8_t decrypted[MAX_ENCRYPTED_PAYLOAD];
        if (!cryptPayload(out.header.from, out.header.id, channel.psk, channel.pskLength, frame + PACKET_HEADER_SIZE, decrypted, payloadLength)) {
            continue;
        }
        if (decodeData(decrypted, payloadLength, out.data)) {
            out.channelIndex = i;
            out.rssi = rssi;
            out.snr = snr;
            return Result::Ok;
        }
    }

    return hashMatched ? Result::DecodeFailed : Result::UnknownChannel;
}

} // namespace tt::service::mesh
