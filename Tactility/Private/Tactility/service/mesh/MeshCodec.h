#pragma once

#include <cstddef>
#include <cstdint>

#include <meshtastic/mesh.pb.h>

/**
 * nanopb encode/decode helpers for the decrypted payload of a mesh
 * packet (a meshtastic.Data protobuf).
 */
namespace tt::service::mesh {

/** Encode a Data message.
 * @param[in] data message to encode
 * @param[out] buffer output buffer
 * @param[in] bufferSize output buffer capacity
 * @param[out] encodedSize bytes written on success
 * @return false when encoding fails or the buffer is too small
 */
bool encodeData(const meshtastic_Data& data, uint8_t* buffer, size_t bufferSize, size_t& encodedSize);

/** Decode a Data message.
 * @param[in] buffer encoded protobuf bytes (decrypted payload)
 * @param[in] length encoded length
 * @param[out] out decoded message on success
 * @return false when the bytes are not a valid Data message
 */
bool decodeData(const uint8_t* buffer, size_t length, meshtastic_Data& out);

} // namespace tt::service::mesh
