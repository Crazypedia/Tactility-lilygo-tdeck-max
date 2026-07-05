#pragma once

#include "MeshProtocol.h"

#include <meshtastic/mesh.pb.h>

#include <cstddef>
#include <cstdint>

namespace tt::service::mesh {

/** Build a complete on-air frame: Data protobuf encoded, AES-CTR encrypted
 * with the given channel key, 16-byte header prepended. The header's from/id
 * fields also parameterize the crypto nonce, so they must be final before
 * calling.
 * @param[in] header packet header (channelHash must match the key)
 * @param[in] data payload message
 * @param[in] key channel PSK (16 or 32 bytes)
 * @param[in] keyLength key length in bytes
 * @param[out] out output buffer
 * @param[in] outSize output buffer capacity
 * @param[out] frameLength total frame length on success
 * @return false when encoding/encryption fails or the buffer is too small
 */
bool buildDataFrame(
    const PacketHeader& header,
    const meshtastic_Data& data,
    const uint8_t* key,
    size_t keyLength,
    uint8_t* out,
    size_t outSize,
    size_t& frameLength
);

/** Build a PKC direct-message frame: Data protobuf encoded, sealed with
 * X25519+AES-CCM for the recipient, 16-byte header prepended. The header's
 * channelHash must be 0 (PKC packets carry no channel hash on air) and
 * from/id parameterize the crypto nonce.
 * @param[in] header packet header (to = recipient node)
 * @param[in] data payload message
 * @param[in] ownPrivateKey our X25519 private key
 * @param[in] remotePublicKey recipient's X25519 public key
 * @param[in] extraNonce random per-packet value
 * @param[out] out output buffer
 * @param[in] outSize output buffer capacity
 * @param[out] frameLength total frame length on success
 * @return false when encoding/sealing fails or the buffer is too small
 */
bool buildPkcDataFrame(
    const PacketHeader& header,
    const meshtastic_Data& data,
    const uint8_t* ownPrivateKey,
    const uint8_t* remotePublicKey,
    uint32_t extraNonce,
    uint8_t* out,
    size_t outSize,
    size_t& frameLength
);

} // namespace tt::service::mesh
