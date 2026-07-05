#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * Payload crypto for the Meshtastic LoRa protocol: AES-CTR with a
 * nonce derived from the packet id and sender node number, matching
 * the Meshtastic firmware's CryptoEngine.
 */
namespace tt::service::mesh {

constexpr size_t PSK_SIZE_AES128 = 16;
constexpr size_t PSK_SIZE_AES256 = 32;

/** The well-known key used by the default channel (PSK setting {0x01}). */
extern const uint8_t DEFAULT_PSK[PSK_SIZE_AES128];

/** XOR of all bytes, as used for channel hashing. */
uint8_t xorHash(const uint8_t* data, size_t length);

/** Channel hash carried in the packet header: xorHash(name) ^ xorHash(psk).
 * For a default channel the name is the modem preset string (e.g. "LongFast").
 */
uint8_t channelHash(const std::string& name, const uint8_t* psk, size_t pskLength);

/** Encrypt or decrypt a payload in place-compatible fashion (CTR is symmetric).
 * @param[in] fromNode sender node number (header 'from')
 * @param[in] packetId packet id (header 'id')
 * @param[in] key AES key, PSK_SIZE_AES128 or PSK_SIZE_AES256 bytes
 * @param[in] keyLength key length in bytes
 * @param[in] input payload bytes
 * @param[out] output transformed payload (may equal input)
 * @param[in] length payload length
 * @return false on unsupported key length or mbedtls failure
 */
bool cryptPayload(
    uint32_t fromNode,
    uint32_t packetId,
    const uint8_t* key,
    size_t keyLength,
    const uint8_t* input,
    uint8_t* output,
    size_t length
);

} // namespace tt::service::mesh
