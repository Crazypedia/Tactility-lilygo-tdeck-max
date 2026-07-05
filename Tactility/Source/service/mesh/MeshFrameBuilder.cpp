#include "Tactility/service/mesh/MeshFrameBuilder.h"

#include "Tactility/service/mesh/MeshCodec.h"
#include "Tactility/service/mesh/MeshCrypto.h"

namespace tt::service::mesh {

bool buildDataFrame(
    const PacketHeader& header,
    const meshtastic_Data& data,
    const uint8_t* key,
    size_t keyLength,
    uint8_t* out,
    size_t outSize,
    size_t& frameLength
) {
    if (outSize < PACKET_HEADER_SIZE) {
        return false;
    }

    uint8_t encoded[MAX_ENCRYPTED_PAYLOAD];
    size_t encodedSize = 0;
    if (!encodeData(data, encoded, sizeof(encoded), encodedSize)) {
        return false;
    }

    if (outSize < PACKET_HEADER_SIZE + encodedSize) {
        return false;
    }

    serializeHeader(header, out);

    if (!cryptPayload(header.from, header.id, key, keyLength, encoded, out + PACKET_HEADER_SIZE, encodedSize)) {
        return false;
    }

    frameLength = PACKET_HEADER_SIZE + encodedSize;
    return true;
}

bool buildPkcDataFrame(
    const PacketHeader& header,
    const meshtastic_Data& data,
    const uint8_t* ownPrivateKey,
    const uint8_t* remotePublicKey,
    uint32_t extraNonce,
    uint8_t* out,
    size_t outSize,
    size_t& frameLength
) {
    uint8_t encoded[MAX_ENCRYPTED_PAYLOAD];
    size_t encodedSize = 0;
    if (!encodeData(data, encoded, sizeof(encoded), encodedSize)) {
        return false;
    }

    if (encodedSize + PKC_OVERHEAD > MAX_ENCRYPTED_PAYLOAD || outSize < PACKET_HEADER_SIZE + encodedSize + PKC_OVERHEAD) {
        return false;
    }

    serializeHeader(header, out);

    size_t sealedSize = 0;
    if (!pkcEncrypt(header.from, header.id, ownPrivateKey, remotePublicKey, extraNonce, encoded, encodedSize, out + PACKET_HEADER_SIZE, outSize - PACKET_HEADER_SIZE, sealedSize)) {
        return false;
    }

    frameLength = PACKET_HEADER_SIZE + sealedSize;
    return true;
}

} // namespace tt::service::mesh
