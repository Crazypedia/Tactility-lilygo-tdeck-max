#include "Tactility/service/mesh/MeshCodec.h"

#include <pb_decode.h>
#include <pb_encode.h>

namespace tt::service::mesh {

bool encodeData(const meshtastic_Data& data, uint8_t* buffer, size_t bufferSize, size_t& encodedSize) {
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, bufferSize);
    if (!pb_encode(&stream, meshtastic_Data_fields, &data)) {
        return false;
    }
    encodedSize = stream.bytes_written;
    return true;
}

bool decodeData(const uint8_t* buffer, size_t length, meshtastic_Data& out) {
    pb_istream_t stream = pb_istream_from_buffer(buffer, length);
    return pb_decode(&stream, meshtastic_Data_fields, &out);
}

} // namespace tt::service::mesh
