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

} // namespace tt::service::mesh
