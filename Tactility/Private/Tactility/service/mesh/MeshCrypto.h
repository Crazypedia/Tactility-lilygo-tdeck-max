#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * Payload crypto for the Meshtastic LoRa protocol, matching the
 * Meshtastic firmware's CryptoEngine:
 * - Channel messages: AES-CTR with a nonce derived from the packet id
 *   and sender node number.
 * - PKC direct messages (firmware 2.5+): X25519 ECDH, shared secret
 *   hashed with SHA-256, payload sealed with AES-256-CCM (13-byte
 *   nonce, 8-byte tag) plus a 4-byte random extra nonce on the wire.
 */
namespace tt::service::mesh {

constexpr size_t PSK_SIZE_AES128 = 16;
constexpr size_t PSK_SIZE_AES256 = 32;

constexpr size_t PKC_KEY_SIZE = 32;
/** Bytes appended to the plaintext on the wire: 8-byte CCM tag + 4-byte extra nonce. */
constexpr size_t PKC_OVERHEAD = 12;

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

/** Fill a buffer from the crypto-grade RNG (hardware entropy on ESP32).
 * @return false when the RNG fails to seed
 */
bool secureRandom(uint8_t* out, size_t length);

/** Generate an X25519 identity keypair for PKC direct messages.
 * The private key is clamped per RFC 7748.
 * @return false on RNG or curve failure
 */
bool generateKeyPair(uint8_t publicKey[PKC_KEY_SIZE], uint8_t privateKey[PKC_KEY_SIZE]);

/** Recompute the public key for a stored private key.
 * @return false when the private key is invalid (e.g. all zeroes)
 */
bool derivePublicKey(const uint8_t privateKey[PKC_KEY_SIZE], uint8_t publicKey[PKC_KEY_SIZE]);

/** Seal a payload for a PKC direct message.
 * Output layout: ciphertext + 8-byte tag + 4-byte little-endian extraNonce,
 * so outputSize must be at least inputLength + PKC_OVERHEAD.
 * @param[in] fromNode sender node number (header 'from')
 * @param[in] packetId packet id (header 'id')
 * @param[in] ownPrivateKey our X25519 private key
 * @param[in] remotePublicKey recipient's X25519 public key (from their NodeInfo)
 * @param[in] extraNonce random per-packet value (see secureRandom)
 * @return false on curve failure or a too-small output buffer
 */
bool pkcEncrypt(
    uint32_t fromNode,
    uint32_t packetId,
    const uint8_t ownPrivateKey[PKC_KEY_SIZE],
    const uint8_t remotePublicKey[PKC_KEY_SIZE],
    uint32_t extraNonce,
    const uint8_t* input,
    size_t inputLength,
    uint8_t* output,
    size_t outputSize,
    size_t& outputLength
);

/** Open a PKC direct message payload (inverse of pkcEncrypt).
 * @param[in] remotePublicKey sender's X25519 public key
 * @return false on authentication failure (wrong key, tampered payload)
 */
bool pkcDecrypt(
    uint32_t fromNode,
    uint32_t packetId,
    const uint8_t ownPrivateKey[PKC_KEY_SIZE],
    const uint8_t remotePublicKey[PKC_KEY_SIZE],
    const uint8_t* input,
    size_t inputLength,
    uint8_t* output,
    size_t outputSize,
    size_t& outputLength
);

} // namespace tt::service::mesh
