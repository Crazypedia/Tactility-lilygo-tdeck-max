#include "Tactility/service/mesh/MeshCrypto.h"

#include <cstring>
#include <mbedtls/aes.h>

namespace tt::service::mesh {

// Expansion of the {0x01} PSK setting, from the Meshtastic firmware's
// CryptoEngine (defaultpsk).
const uint8_t DEFAULT_PSK[PSK_SIZE_AES128] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

uint8_t xorHash(const uint8_t* data, size_t length) {
    uint8_t hash = 0;
    for (size_t i = 0; i < length; i++) {
        hash ^= data[i];
    }
    return hash;
}

uint8_t channelHash(const std::string& name, const uint8_t* psk, size_t pskLength) {
    return xorHash(reinterpret_cast<const uint8_t*>(name.data()), name.size()) ^ xorHash(psk, pskLength);
}

bool cryptPayload(
    uint32_t fromNode,
    uint32_t packetId,
    const uint8_t* key,
    size_t keyLength,
    const uint8_t* input,
    uint8_t* output,
    size_t length
) {
    if (keyLength != PSK_SIZE_AES128 && keyLength != PSK_SIZE_AES256) {
        return false;
    }

    // Nonce layout (CryptoEngine::initNonce): packet id as 64-bit LE,
    // then sender node number as 32-bit LE, then 4 zero bytes that act
    // as the CTR block counter.
    uint8_t nonce[16] = {};
    for (int i = 0; i < 4; i++) {
        nonce[i] = static_cast<uint8_t>(packetId >> (8 * i));
        nonce[8 + i] = static_cast<uint8_t>(fromNode >> (8 * i));
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int result = mbedtls_aes_setkey_enc(&aes, key, keyLength * 8);
    if (result == 0) {
        size_t ncOff = 0;
        uint8_t streamBlock[16] = {};
        result = mbedtls_aes_crypt_ctr(&aes, length, &ncOff, nonce, streamBlock, input, output);
    }
    mbedtls_aes_free(&aes);
    return result == 0;
}

} // namespace tt::service::mesh
