#include "Tactility/service/mesh/MeshReceiver.h"

#include "Tactility/service/mesh/MeshCodec.h"

#include <cstring>

namespace tt::service::mesh {

void MeshReceiver::setChannel(const std::string& name, const uint8_t* psk, size_t pskLength) {
    channelName = name;
    channelPskLength = pskLength <= sizeof(channelPsk) ? pskLength : sizeof(channelPsk);
    std::memcpy(channelPsk, psk, channelPskLength);
    configuredChannelHash = channelHash(channelName, channelPsk, channelPskLength);
}

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

    if (out.header.channelHash != configuredChannelHash) {
        return Result::UnknownChannel;
    }

    uint8_t decrypted[MAX_ENCRYPTED_PAYLOAD];
    if (!cryptPayload(out.header.from, out.header.id, channelPsk, channelPskLength, frame + PACKET_HEADER_SIZE, decrypted, payloadLength)) {
        return Result::DecodeFailed;
    }

    if (!decodeData(decrypted, payloadLength, out.data)) {
        return Result::DecodeFailed;
    }

    out.rssi = rssi;
    out.snr = snr;
    return Result::Ok;
}

} // namespace tt::service::mesh
