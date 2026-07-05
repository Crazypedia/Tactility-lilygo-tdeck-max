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

    // PKC direct messages arrive with a zero channel hash (Router.cpp sets
    // p->channel = 0 for pki_encrypted packets). Try the sender's public key
    // first; a failure falls through to the PSK channels, since hash 0 can
    // also legitimately match a configured channel.
    if (lookupPublicKey != nullptr
        && out.header.channelHash == 0
        && out.header.to == ownNodeId
        && out.header.to != BROADCAST_ADDRESS
        && payloadLength > PKC_OVERHEAD) {
        if (tryPkcDecrypt(frame + PACKET_HEADER_SIZE, payloadLength, out)) {
            out.rssi = rssi;
            out.snr = snr;
            return Result::Ok;
        }
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

bool MeshReceiver::tryPkcDecrypt(const uint8_t* payload, size_t payloadLength, ReceivedPacket& out) {
    uint8_t senderPublicKey[PKC_KEY_SIZE];
    if (!lookupPublicKey(out.header.from, senderPublicKey)) {
        return false;
    }

    uint8_t decrypted[MAX_ENCRYPTED_PAYLOAD];
    size_t decryptedLength = 0;
    if (!pkcDecrypt(out.header.from, out.header.id, ownPrivateKey, senderPublicKey, payload, payloadLength, decrypted, sizeof(decrypted), decryptedLength)) {
        return false;
    }
    if (!decodeData(decrypted, decryptedLength, out.data)) {
        return false;
    }

    out.channelIndex = 0;
    out.pkiEncrypted = true;
    return true;
}

} // namespace tt::service::mesh
