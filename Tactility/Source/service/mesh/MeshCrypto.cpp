#include "Tactility/service/mesh/MeshCrypto.h"

#include <cstring>
#include <mbedtls/aes.h>
#include <mbedtls/ccm.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/sha256.h>

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

namespace {

mbedtls_ctr_drbg_context* getDrbg() {
    static mbedtls_entropy_context entropy;
    static mbedtls_ctr_drbg_context drbg;
    static bool seeded = false;
    static bool failed = false;
    if (!seeded && !failed) {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        const char* personalization = "tt-mesh-pkc";
        if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const unsigned char*>(personalization), strlen(personalization)) == 0) {
            seeded = true;
        } else {
            failed = true;
        }
    }
    return seeded ? &drbg : nullptr;
}

/** Nonce layout from CryptoEngine::initNonce: packet id as 64-bit LE with
 * bytes 4-7 overwritten by the extra nonce (the firmware memcpys extraNonce
 * to offset sizeof(uint32_t), clobbering the id's high half), then the
 * sender node number. CCM consumes the first 13 bytes.
 */
void buildPkcNonce(uint32_t fromNode, uint32_t packetId, uint32_t extraNonce, uint8_t nonce[13]) {
    memset(nonce, 0, 13);
    for (int i = 0; i < 4; i++) {
        nonce[i] = static_cast<uint8_t>(packetId >> (8 * i));
        nonce[4 + i] = static_cast<uint8_t>(extraNonce >> (8 * i));
        nonce[8 + i] = static_cast<uint8_t>(fromNode >> (8 * i));
    }
}

void clampPrivateKey(uint8_t key[PKC_KEY_SIZE]) {
    key[0] &= 248;
    key[31] &= 127;
    key[31] |= 64;
}

/** X25519(ownPrivate, remotePublic), rejecting all-zero results (low-order
 * points), matching the firmware's Curve25519::dh2 weak-key check.
 */
bool x25519SharedSecret(const uint8_t ownPrivate[PKC_KEY_SIZE], const uint8_t remotePublic[PKC_KEY_SIZE], uint8_t sharedOut[PKC_KEY_SIZE]) {
    auto* drbg = getDrbg();
    if (drbg == nullptr) {
        return false;
    }

    uint8_t clamped[PKC_KEY_SIZE];
    memcpy(clamped, ownPrivate, PKC_KEY_SIZE);
    clampPrivateKey(clamped);

    mbedtls_ecp_group group;
    mbedtls_ecp_point peerPoint;
    mbedtls_mpi privateScalar;
    mbedtls_mpi shared;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&peerPoint);
    mbedtls_mpi_init(&privateScalar);
    mbedtls_mpi_init(&shared);

    bool success = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_CURVE25519) == 0
        && mbedtls_mpi_read_binary_le(&privateScalar, clamped, PKC_KEY_SIZE) == 0
        && mbedtls_ecp_point_read_binary(&group, &peerPoint, remotePublic, PKC_KEY_SIZE) == 0
        && mbedtls_ecdh_compute_shared(&group, &shared, &peerPoint, &privateScalar, mbedtls_ctr_drbg_random, drbg) == 0
        && mbedtls_mpi_write_binary_le(&shared, sharedOut, PKC_KEY_SIZE) == 0;

    if (success) {
        uint8_t accumulator = 0;
        for (size_t i = 0; i < PKC_KEY_SIZE; i++) {
            accumulator |= sharedOut[i];
        }
        success = accumulator != 0;
    }

    mbedtls_mpi_free(&shared);
    mbedtls_mpi_free(&privateScalar);
    mbedtls_ecp_point_free(&peerPoint);
    mbedtls_ecp_group_free(&group);
    memset(clamped, 0, sizeof(clamped));
    return success;
}

/** PKC session key: SHA-256 of the X25519 shared secret. */
bool deriveSharedKey(const uint8_t ownPrivate[PKC_KEY_SIZE], const uint8_t remotePublic[PKC_KEY_SIZE], uint8_t keyOut[PKC_KEY_SIZE]) {
    uint8_t shared[PKC_KEY_SIZE];
    if (!x25519SharedSecret(ownPrivate, remotePublic, shared)) {
        return false;
    }
    const bool success = mbedtls_sha256(shared, PKC_KEY_SIZE, keyOut, 0) == 0;
    memset(shared, 0, sizeof(shared));
    return success;
}

} // namespace

bool secureRandom(uint8_t* out, size_t length) {
    auto* drbg = getDrbg();
    return drbg != nullptr && mbedtls_ctr_drbg_random(drbg, out, length) == 0;
}

bool derivePublicKey(const uint8_t privateKey[PKC_KEY_SIZE], uint8_t publicKey[PKC_KEY_SIZE]) {
    auto* drbg = getDrbg();
    if (drbg == nullptr) {
        return false;
    }

    uint8_t clamped[PKC_KEY_SIZE];
    memcpy(clamped, privateKey, PKC_KEY_SIZE);
    clampPrivateKey(clamped);

    mbedtls_ecp_group group;
    mbedtls_ecp_point publicPoint;
    mbedtls_mpi privateScalar;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&publicPoint);
    mbedtls_mpi_init(&privateScalar);

    size_t written = 0;
    bool success = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_CURVE25519) == 0
        && mbedtls_mpi_read_binary_le(&privateScalar, clamped, PKC_KEY_SIZE) == 0
        && mbedtls_ecp_mul(&group, &publicPoint, &privateScalar, &group.G, mbedtls_ctr_drbg_random, drbg) == 0
        && mbedtls_ecp_point_write_binary(&group, &publicPoint, MBEDTLS_ECP_PF_UNCOMPRESSED, &written, publicKey, PKC_KEY_SIZE) == 0
        && written == PKC_KEY_SIZE;

    mbedtls_mpi_free(&privateScalar);
    mbedtls_ecp_point_free(&publicPoint);
    mbedtls_ecp_group_free(&group);
    memset(clamped, 0, sizeof(clamped));
    return success;
}

bool generateKeyPair(uint8_t publicKey[PKC_KEY_SIZE], uint8_t privateKey[PKC_KEY_SIZE]) {
    if (!secureRandom(privateKey, PKC_KEY_SIZE)) {
        return false;
    }
    clampPrivateKey(privateKey);
    return derivePublicKey(privateKey, publicKey);
}

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
) {
    if (outputSize < inputLength + PKC_OVERHEAD) {
        return false;
    }

    uint8_t key[PKC_KEY_SIZE];
    if (!deriveSharedKey(ownPrivateKey, remotePublicKey, key)) {
        return false;
    }

    uint8_t nonce[13];
    buildPkcNonce(fromNode, packetId, extraNonce, nonce);

    mbedtls_ccm_context ccm;
    mbedtls_ccm_init(&ccm);
    int result = mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, key, PKC_KEY_SIZE * 8);
    if (result == 0) {
        result = mbedtls_ccm_encrypt_and_tag(&ccm, inputLength, nonce, sizeof(nonce), nullptr, 0, input, output, output + inputLength, 8);
    }
    mbedtls_ccm_free(&ccm);
    memset(key, 0, sizeof(key));
    if (result != 0) {
        return false;
    }

    for (int i = 0; i < 4; i++) {
        output[inputLength + 8 + i] = static_cast<uint8_t>(extraNonce >> (8 * i));
    }
    outputLength = inputLength + PKC_OVERHEAD;
    return true;
}

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
) {
    if (inputLength <= PKC_OVERHEAD || outputSize < inputLength - PKC_OVERHEAD) {
        return false;
    }
    const size_t plainLength = inputLength - PKC_OVERHEAD;

    uint32_t extraNonce = 0;
    for (int i = 0; i < 4; i++) {
        extraNonce |= static_cast<uint32_t>(input[plainLength + 8 + i]) << (8 * i);
    }

    uint8_t key[PKC_KEY_SIZE];
    if (!deriveSharedKey(ownPrivateKey, remotePublicKey, key)) {
        return false;
    }

    uint8_t nonce[13];
    buildPkcNonce(fromNode, packetId, extraNonce, nonce);

    mbedtls_ccm_context ccm;
    mbedtls_ccm_init(&ccm);
    int result = mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, key, PKC_KEY_SIZE * 8);
    if (result == 0) {
        result = mbedtls_ccm_auth_decrypt(&ccm, plainLength, nonce, sizeof(nonce), nullptr, 0, input, output, input + plainLength, 8);
    }
    mbedtls_ccm_free(&ccm);
    memset(key, 0, sizeof(key));
    if (result != 0) {
        return false;
    }

    outputLength = plainLength;
    return true;
}

} // namespace tt::service::mesh
