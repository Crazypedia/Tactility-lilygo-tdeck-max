#include "../../Tactility/Private/Tactility/service/mesh/MeshCodec.h"
#include "../../Tactility/Private/Tactility/service/mesh/MeshCrypto.h"
#include "../../Tactility/Private/Tactility/service/mesh/MeshProtocol.h"
#include "../../Tactility/Private/Tactility/service/mesh/PacketDedup.h"

#include "doctest.h"

#include <cstring>

using namespace tt::service::mesh;

TEST_CASE("parseHeader() should reject frames shorter than the header") {
    uint8_t data[PACKET_HEADER_SIZE - 1] = {};
    PacketHeader header;
    CHECK_EQ(parseHeader(data, sizeof(data), header), false);
}

TEST_CASE("parseHeader() should decode little-endian fields and flag bits") {
    // to=0xFFFFFFFF (broadcast), from=0x11223344, id=0xA1B2C3D4,
    // flags: hop_limit=3, want_ack, hop_start=7, channel hash 0x08
    const uint8_t data[PACKET_HEADER_SIZE] = {
        0xFF, 0xFF, 0xFF, 0xFF,
        0x44, 0x33, 0x22, 0x11,
        0xD4, 0xC3, 0xB2, 0xA1,
        0xEB, // hop_start 7 << 5 | want_ack | hop_limit 3
        0x08,
        0x42,
        0x24
    };

    PacketHeader header;
    REQUIRE(parseHeader(data, sizeof(data), header));
    CHECK_EQ(header.to, BROADCAST_ADDRESS);
    CHECK_EQ(header.from, 0x11223344U);
    CHECK_EQ(header.id, 0xA1B2C3D4U);
    CHECK_EQ(header.hopLimit, 3);
    CHECK_EQ(header.wantAck, true);
    CHECK_EQ(header.viaMqtt, false);
    CHECK_EQ(header.hopStart, 7);
    CHECK_EQ(header.channelHash, 0x08);
    CHECK_EQ(header.nextHop, 0x42);
    CHECK_EQ(header.relayNode, 0x24);
}

TEST_CASE("serializeHeader() and parseHeader() should round-trip") {
    PacketHeader original;
    original.to = 0xDEADBEEF;
    original.from = 0x0BADF00D;
    original.id = 12345678;
    original.hopLimit = 5;
    original.wantAck = true;
    original.viaMqtt = true;
    original.hopStart = 5;
    original.channelHash = 0x08;
    original.nextHop = 0xAB;
    original.relayNode = 0xCD;

    uint8_t wire[PACKET_HEADER_SIZE];
    serializeHeader(original, wire);

    PacketHeader parsed;
    REQUIRE(parseHeader(wire, sizeof(wire), parsed));
    CHECK_EQ(parsed.to, original.to);
    CHECK_EQ(parsed.from, original.from);
    CHECK_EQ(parsed.id, original.id);
    CHECK_EQ(parsed.hopLimit, original.hopLimit);
    CHECK_EQ(parsed.wantAck, original.wantAck);
    CHECK_EQ(parsed.viaMqtt, original.viaMqtt);
    CHECK_EQ(parsed.hopStart, original.hopStart);
    CHECK_EQ(parsed.channelHash, original.channelHash);
    CHECK_EQ(parsed.nextHop, original.nextHop);
    CHECK_EQ(parsed.relayNode, original.relayNode);
}

TEST_CASE("channelHash() should produce the well-known LongFast value") {
    // The default channel (name "LongFast", PSK setting {0x01}) hashes to 8.
    CHECK_EQ(channelHash("LongFast", DEFAULT_PSK, PSK_SIZE_AES128), 0x08);
}

TEST_CASE("cryptPayload() should round-trip and actually transform the data") {
    const char* plaintext = "hello mesh";
    const size_t length = strlen(plaintext);
    const uint32_t fromNode = 0x11223344;
    const uint32_t packetId = 0xA1B2C3D4;

    uint8_t encrypted[32] = {};
    REQUIRE(cryptPayload(fromNode, packetId, DEFAULT_PSK, PSK_SIZE_AES128, reinterpret_cast<const uint8_t*>(plaintext), encrypted, length));
    CHECK_NE(memcmp(encrypted, plaintext, length), 0);

    uint8_t decrypted[32] = {};
    REQUIRE(cryptPayload(fromNode, packetId, DEFAULT_PSK, PSK_SIZE_AES128, encrypted, decrypted, length));
    CHECK_EQ(memcmp(decrypted, plaintext, length), 0);
}

TEST_CASE("cryptPayload() should bind the keystream to packet id and sender") {
    const uint8_t plaintext[8] = {};
    uint8_t a[8];
    uint8_t b[8];
    uint8_t c[8];

    REQUIRE(cryptPayload(1, 100, DEFAULT_PSK, PSK_SIZE_AES128, plaintext, a, sizeof(a)));
    REQUIRE(cryptPayload(1, 101, DEFAULT_PSK, PSK_SIZE_AES128, plaintext, b, sizeof(b)));
    REQUIRE(cryptPayload(2, 100, DEFAULT_PSK, PSK_SIZE_AES128, plaintext, c, sizeof(c)));

    CHECK_NE(memcmp(a, b, sizeof(a)), 0);
    CHECK_NE(memcmp(a, c, sizeof(a)), 0);
}

TEST_CASE("cryptPayload() should reject invalid key lengths") {
    uint8_t buffer[4] = {};
    CHECK_EQ(cryptPayload(1, 1, DEFAULT_PSK, 15, buffer, buffer, sizeof(buffer)), false);
}

TEST_CASE("encodeData() and decodeData() should round-trip a text message") {
    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    const char* text = "hello mesh";
    data.payload.size = strlen(text);
    memcpy(data.payload.bytes, text, data.payload.size);
    data.want_response = false;

    uint8_t buffer[MAX_ENCRYPTED_PAYLOAD];
    size_t encodedSize = 0;
    REQUIRE(encodeData(data, buffer, sizeof(buffer), encodedSize));
    CHECK_GT(encodedSize, 0);

    meshtastic_Data decoded = meshtastic_Data_init_zero;
    REQUIRE(decodeData(buffer, encodedSize, decoded));
    CHECK_EQ(decoded.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP);
    REQUIRE_EQ(decoded.payload.size, data.payload.size);
    CHECK_EQ(memcmp(decoded.payload.bytes, text, decoded.payload.size), 0);
}

TEST_CASE("encodeData() should fail when the buffer is too small") {
    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    data.payload.size = 100;
    memset(data.payload.bytes, 'x', data.payload.size);

    uint8_t buffer[8];
    size_t encodedSize = 0;
    CHECK_EQ(encodeData(data, buffer, sizeof(buffer), encodedSize), false);
}

TEST_CASE("PacketDedup should pass new packets and block duplicates") {
    PacketDedup dedup;
    CHECK_EQ(dedup.checkAndAdd(1, 100), true);
    CHECK_EQ(dedup.checkAndAdd(1, 100), false);
    CHECK_EQ(dedup.checkAndAdd(1, 101), true);
    CHECK_EQ(dedup.checkAndAdd(2, 100), true);
    CHECK_EQ(dedup.checkAndAdd(2, 100), false);
}

TEST_CASE("PacketDedup should evict oldest entries once full") {
    PacketDedup dedup;
    for (uint32_t i = 0; i < 64; i++) {
        CHECK_EQ(dedup.checkAndAdd(1, i), true);
    }
    // Entry 0 is the oldest; adding one more evicts it.
    CHECK_EQ(dedup.checkAndAdd(1, 64), true);
    CHECK_EQ(dedup.checkAndAdd(1, 0), true); // evicted, so treated as new again
    CHECK_EQ(dedup.checkAndAdd(1, 64), false); // still tracked
}
#include "../../Tactility/Private/Tactility/service/mesh/MeshReceiver.h"

// Build a complete on-air frame the way a transmitting node would:
// Data protobuf -> AES-CTR encrypt -> 16-byte header prepended.
static size_t buildFrame(uint8_t* frame, size_t frameSize, uint32_t from, uint32_t id, const char* text, uint8_t channelHashValue) {
    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    data.payload.size = strlen(text);
    memcpy(data.payload.bytes, text, data.payload.size);

    uint8_t encoded[MAX_ENCRYPTED_PAYLOAD];
    size_t encodedSize = 0;
    REQUIRE(encodeData(data, encoded, sizeof(encoded), encodedSize));
    REQUIRE(frameSize >= PACKET_HEADER_SIZE + encodedSize);

    PacketHeader header;
    header.to = BROADCAST_ADDRESS;
    header.from = from;
    header.id = id;
    header.hopLimit = 3;
    header.hopStart = 3;
    header.channelHash = channelHashValue;
    serializeHeader(header, frame);

    REQUIRE(cryptPayload(from, id, DEFAULT_PSK, PSK_SIZE_AES128, encoded, frame + PACKET_HEADER_SIZE, encodedSize));
    return PACKET_HEADER_SIZE + encodedSize;
}

TEST_CASE("MeshReceiver should decode a LongFast text broadcast end-to-end") {
    MeshReceiver receiver; // defaults to LongFast
    const uint8_t longFastHash = channelHash("LongFast", DEFAULT_PSK, PSK_SIZE_AES128);

    uint8_t frame[MAX_LORA_PAYLOAD];
    size_t frameLength = buildFrame(frame, sizeof(frame), 0x11223344, 999, "hello mesh", longFastHash);

    MeshReceiver::ReceivedPacket packet;
    REQUIRE_EQ(receiver.process(frame, frameLength, -95.0f, 7.5f, packet), MeshReceiver::Result::Ok);
    CHECK_EQ(packet.header.from, 0x11223344U);
    CHECK_EQ(packet.header.to, BROADCAST_ADDRESS);
    CHECK_EQ(packet.data.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP);
    REQUIRE_EQ(packet.data.payload.size, strlen("hello mesh"));
    CHECK_EQ(memcmp(packet.data.payload.bytes, "hello mesh", packet.data.payload.size), 0);
    CHECK_EQ(packet.rssi, -95.0f);
    CHECK_EQ(packet.snr, 7.5f);

    SUBCASE("the same frame heard again is a duplicate") {
        CHECK_EQ(receiver.process(frame, frameLength, -80.0f, 3.0f, packet), MeshReceiver::Result::Duplicate);
    }
}

TEST_CASE("MeshReceiver should reject frames for other channels") {
    MeshReceiver receiver;
    uint8_t frame[MAX_LORA_PAYLOAD];
    size_t frameLength = buildFrame(frame, sizeof(frame), 0xAABBCCDD, 1000, "hi", 0x77);

    MeshReceiver::ReceivedPacket packet;
    CHECK_EQ(receiver.process(frame, frameLength, -90.0f, 5.0f, packet), MeshReceiver::Result::UnknownChannel);
}

TEST_CASE("MeshReceiver should fail decode on a wrong key") {
    MeshReceiver receiver;
    const uint8_t longFastHash = channelHash("LongFast", DEFAULT_PSK, PSK_SIZE_AES128);
    // Same hash, different key: hash collision scenario. Decrypting with the
    // wrong key must surface as DecodeFailed, not garbage data.
    const uint8_t otherKey[PSK_SIZE_AES128] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    receiver.setChannels({ChannelConfig("LongFast", otherKey, sizeof(otherKey))});

    uint8_t frame[MAX_LORA_PAYLOAD];
    size_t frameLength = buildFrame(frame, sizeof(frame), 0x22334455, 1001, "secret", longFastHash);
    // Force the configured hash to match so we get past the channel gate.
    frame[13] = channelHash("LongFast", otherKey, sizeof(otherKey));

    MeshReceiver::ReceivedPacket packet;
    CHECK_EQ(receiver.process(frame, frameLength, -90.0f, 5.0f, packet), MeshReceiver::Result::DecodeFailed);
}

TEST_CASE("MeshReceiver should reject header-only and oversized frames") {
    MeshReceiver receiver;
    uint8_t frame[PACKET_HEADER_SIZE] = {};
    MeshReceiver::ReceivedPacket packet;
    CHECK_EQ(receiver.process(frame, sizeof(frame), 0, 0, packet), MeshReceiver::Result::TooShort);
    CHECK_EQ(receiver.process(frame, 3, 0, 0, packet), MeshReceiver::Result::TooShort);
}

#include "../../Tactility/Private/Tactility/service/mesh/MeshFrameBuilder.h"

TEST_CASE("buildDataFrame() output should decode through MeshReceiver") {
    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    const char* text = "tx path check";
    data.payload.size = strlen(text);
    memcpy(data.payload.bytes, text, data.payload.size);

    PacketHeader header;
    header.to = BROADCAST_ADDRESS;
    header.from = 0x0BADF00D;
    header.id = 4242;
    header.hopLimit = 3;
    header.hopStart = 3;
    header.channelHash = channelHash("LongFast", DEFAULT_PSK, PSK_SIZE_AES128);

    uint8_t frame[MAX_LORA_PAYLOAD];
    size_t frameLength = 0;
    REQUIRE(buildDataFrame(header, data, DEFAULT_PSK, PSK_SIZE_AES128, frame, sizeof(frame), frameLength));
    CHECK_GT(frameLength, PACKET_HEADER_SIZE);

    MeshReceiver receiver;
    MeshReceiver::ReceivedPacket packet;
    REQUIRE_EQ(receiver.process(frame, frameLength, -70.0f, 9.0f, packet), MeshReceiver::Result::Ok);
    CHECK_EQ(packet.header.from, 0x0BADF00DU);
    CHECK_EQ(packet.data.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP);
    REQUIRE_EQ(packet.data.payload.size, strlen(text));
    CHECK_EQ(memcmp(packet.data.payload.bytes, text, packet.data.payload.size), 0);
}

TEST_CASE("MeshReceiver should decode secondary-channel traffic with its own key") {
    const uint8_t customKey[PSK_SIZE_AES128] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6};
    const ChannelConfig secondary("Private", customKey, sizeof(customKey));

    MeshReceiver receiver;
    receiver.setChannels({ChannelConfig::longFast(), secondary});

    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    const char* text = "keyed channel";
    data.payload.size = strlen(text);
    memcpy(data.payload.bytes, text, data.payload.size);

    PacketHeader header;
    header.to = BROADCAST_ADDRESS;
    header.from = 0x31415926;
    header.id = 2718;
    header.channelHash = secondary.hash;

    uint8_t frame[MAX_LORA_PAYLOAD];
    size_t frameLength = 0;
    REQUIRE(buildDataFrame(header, data, secondary.psk, secondary.pskLength, frame, sizeof(frame), frameLength));

    MeshReceiver::ReceivedPacket packet;
    REQUIRE_EQ(receiver.process(frame, frameLength, -88.0f, 4.0f, packet), MeshReceiver::Result::Ok);
    CHECK_EQ(packet.channelIndex, 1);
    REQUIRE_EQ(packet.data.payload.size, strlen(text));
    CHECK_EQ(memcmp(packet.data.payload.bytes, text, packet.data.payload.size), 0);

    SUBCASE("LongFast traffic still decodes as channel 0") {
        uint8_t lfFrame[MAX_LORA_PAYLOAD];
        const uint8_t longFastHash = channelHash("LongFast", DEFAULT_PSK, PSK_SIZE_AES128);
        size_t lfLength = buildFrame(lfFrame, sizeof(lfFrame), 0x27182818, 314, "public", longFastHash);
        REQUIRE_EQ(receiver.process(lfFrame, lfLength, -95.0f, 6.0f, packet), MeshReceiver::Result::Ok);
        CHECK_EQ(packet.channelIndex, 0);
    }
}

TEST_CASE("buildDataFrame() should fail when the buffer is too small") {
    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    data.payload.size = 50;
    memset(data.payload.bytes, 'y', data.payload.size);

    PacketHeader header;
    header.from = 1;
    header.id = 1;

    uint8_t frame[24];
    size_t frameLength = 0;
    CHECK_EQ(buildDataFrame(header, data, DEFAULT_PSK, PSK_SIZE_AES128, frame, sizeof(frame), frameLength), false);
}

// region PKC (Curve25519 direct messages, firmware 2.5+)

// Interop vectors generated independently with python-cryptography
// (X25519 + SHA256 + AES-256-CCM, tag 8, Meshtastic nonce layout).
namespace pkcvectors {
const uint8_t PRIV_A[32] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f };
const uint8_t PUB_A[32] = { 0xd8, 0x9e, 0x3b, 0xad, 0x79, 0x43, 0x7d, 0xbe, 0xd9, 0xf8, 0x43, 0x41, 0x83, 0x04, 0xf4, 0x60, 0xff, 0x05, 0xc7, 0xfe, 0x81, 0xfe, 0x4a, 0x95, 0x77, 0xa8, 0x04, 0xcb, 0x93, 0x67, 0xff, 0x66 };
const uint8_t PRIV_B[32] = { 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f };
const uint8_t PUB_B[32] = { 0x49, 0x3e, 0x82, 0xfc, 0x74, 0x46, 0x4a, 0x59, 0x26, 0x88, 0x17, 0x62, 0x3d, 0x20, 0x53, 0xc5, 0xeb, 0x8e, 0x2c, 0xc4, 0xa9, 0x88, 0xb4, 0xfe, 0xe1, 0x79, 0xec, 0x6b, 0x01, 0x0d, 0x53, 0x1d };
constexpr uint32_t PACKET_ID = 0x1A2B3C4D;
constexpr uint32_t FROM_NODE = 0x0DECAF01;
constexpr uint32_t EXTRA_NONCE = 0xC0FFEE42;
const uint8_t PLAINTEXT[18] = { 0x50, 0x4b, 0x43, 0x20, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74, 0x20, 0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65 };
const uint8_t WIRE[30] = { 0x21, 0x09, 0x82, 0xe4, 0x97, 0xa5, 0x7e, 0x4f, 0x92, 0xb6, 0x48, 0x7d, 0x82, 0xbe, 0xfe, 0x9c, 0xa8, 0x5b, 0x7f, 0x69, 0xc2, 0x63, 0x64, 0x4d, 0x44, 0x85, 0x42, 0xee, 0xff, 0xc0 };
} // namespace pkcvectors

TEST_CASE("derivePublicKey() should match the reference X25519 public keys") {
    uint8_t publicKey[PKC_KEY_SIZE];
    REQUIRE(derivePublicKey(pkcvectors::PRIV_A, publicKey));
    CHECK_EQ(memcmp(publicKey, pkcvectors::PUB_A, PKC_KEY_SIZE), 0);
    REQUIRE(derivePublicKey(pkcvectors::PRIV_B, publicKey));
    CHECK_EQ(memcmp(publicKey, pkcvectors::PUB_B, PKC_KEY_SIZE), 0);
}

TEST_CASE("pkcEncrypt() should reproduce the reference wire payload") {
    using namespace pkcvectors;
    uint8_t out[sizeof(WIRE)];
    size_t outLength = 0;
    REQUIRE(pkcEncrypt(FROM_NODE, PACKET_ID, PRIV_A, PUB_B, EXTRA_NONCE, PLAINTEXT, sizeof(PLAINTEXT), out, sizeof(out), outLength));
    REQUIRE_EQ(outLength, sizeof(WIRE));
    CHECK_EQ(memcmp(out, WIRE, sizeof(WIRE)), 0);
}

TEST_CASE("pkcDecrypt() should open the reference wire payload") {
    using namespace pkcvectors;
    uint8_t out[sizeof(WIRE)];
    size_t outLength = 0;
    // B receives from A: B's private key, A's public key.
    REQUIRE(pkcDecrypt(FROM_NODE, PACKET_ID, PRIV_B, PUB_A, WIRE, sizeof(WIRE), out, sizeof(out), outLength));
    REQUIRE_EQ(outLength, sizeof(PLAINTEXT));
    CHECK_EQ(memcmp(out, PLAINTEXT, sizeof(PLAINTEXT)), 0);
}

TEST_CASE("pkcDecrypt() should reject tampered payloads and wrong keys") {
    using namespace pkcvectors;
    uint8_t out[sizeof(WIRE)];
    size_t outLength = 0;

    uint8_t tampered[sizeof(WIRE)];
    memcpy(tampered, WIRE, sizeof(WIRE));
    tampered[3] ^= 0x01;
    CHECK_EQ(pkcDecrypt(FROM_NODE, PACKET_ID, PRIV_B, PUB_A, tampered, sizeof(tampered), out, sizeof(out), outLength), false);

    // Wrong sender key: CCM authentication must fail.
    CHECK_EQ(pkcDecrypt(FROM_NODE, PACKET_ID, PRIV_B, PUB_B, WIRE, sizeof(WIRE), out, sizeof(out), outLength), false);

    // Truncated to overhead-only.
    CHECK_EQ(pkcDecrypt(FROM_NODE, PACKET_ID, PRIV_B, PUB_A, WIRE, PKC_OVERHEAD, out, sizeof(out), outLength), false);
}

TEST_CASE("generateKeyPair() should produce working, distinct identities") {
    uint8_t pubA[PKC_KEY_SIZE], privA[PKC_KEY_SIZE];
    uint8_t pubB[PKC_KEY_SIZE], privB[PKC_KEY_SIZE];
    REQUIRE(generateKeyPair(pubA, privA));
    REQUIRE(generateKeyPair(pubB, privB));
    CHECK_NE(memcmp(pubA, pubB, PKC_KEY_SIZE), 0);

    uint8_t derived[PKC_KEY_SIZE];
    REQUIRE(derivePublicKey(privA, derived));
    CHECK_EQ(memcmp(derived, pubA, PKC_KEY_SIZE), 0);

    // Fresh keys round-trip a payload in both directions.
    const uint8_t message[] = "keypair roundtrip";
    uint8_t sealed[sizeof(message) + PKC_OVERHEAD];
    uint8_t opened[sizeof(message)];
    size_t sealedLength = 0, openedLength = 0;
    REQUIRE(pkcEncrypt(1, 2, privA, pubB, 0x12345678, message, sizeof(message), sealed, sizeof(sealed), sealedLength));
    REQUIRE(pkcDecrypt(1, 2, privB, pubA, sealed, sealedLength, opened, sizeof(opened), openedLength));
    REQUIRE_EQ(openedLength, sizeof(message));
    CHECK_EQ(memcmp(opened, message, sizeof(message)), 0);
}

TEST_CASE("buildPkcDataFrame() output should decode through MeshReceiver PKC path") {
    using namespace pkcvectors;
    constexpr uint32_t receiverNodeId = 0x0B0B0B0B;

    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    const char* text = "pkc dm";
    data.payload.size = strlen(text);
    memcpy(data.payload.bytes, text, data.payload.size);
    data.has_bitfield = true;
    data.bitfield = 0;

    PacketHeader header;
    header.to = receiverNodeId;
    header.from = FROM_NODE;
    header.id = 7777;
    header.hopLimit = 3;
    header.hopStart = 3;
    header.wantAck = true;
    header.channelHash = 0; // PKC packets carry no channel hash

    uint8_t frame[MAX_LORA_PAYLOAD];
    size_t frameLength = 0;
    REQUIRE(buildPkcDataFrame(header, data, PRIV_A, PUB_B, 0xA5A5A5A5, frame, sizeof(frame), frameLength));

    MeshReceiver receiver; // LongFast configured; PKC tried before PSKs
    receiver.setPkc(receiverNodeId, PRIV_B, [](uint32_t nodeId, uint8_t* publicKeyOut) {
        if (nodeId != FROM_NODE) {
            return false;
        }
        memcpy(publicKeyOut, PUB_A, PKC_KEY_SIZE);
        return true;
    });

    MeshReceiver::ReceivedPacket packet;
    REQUIRE_EQ(receiver.process(frame, frameLength, -60.0f, 10.0f, packet), MeshReceiver::Result::Ok);
    CHECK_EQ(packet.pkiEncrypted, true);
    CHECK_EQ(packet.data.portnum, meshtastic_PortNum_TEXT_MESSAGE_APP);
    REQUIRE_EQ(packet.data.payload.size, strlen(text));
    CHECK_EQ(memcmp(packet.data.payload.bytes, text, packet.data.payload.size), 0);

    SUBCASE("the same DM is rejected when the sender's key is unknown") {
        MeshReceiver noKeyReceiver;
        noKeyReceiver.setPkc(receiverNodeId, PRIV_B, [](uint32_t, uint8_t*) { return false; });
        MeshReceiver::ReceivedPacket rejected;
        CHECK_EQ(noKeyReceiver.process(frame, frameLength, -60.0f, 10.0f, rejected), MeshReceiver::Result::UnknownChannel);
    }

    SUBCASE("a DM addressed to someone else is not decrypted") {
        MeshReceiver otherReceiver;
        otherReceiver.setPkc(0x0C0C0C0C, PRIV_B, [](uint32_t, uint8_t* publicKeyOut) {
            memcpy(publicKeyOut, PUB_A, PKC_KEY_SIZE);
            return true;
        });
        MeshReceiver::ReceivedPacket rejected;
        CHECK_EQ(otherReceiver.process(frame, frameLength, -60.0f, 10.0f, rejected), MeshReceiver::Result::UnknownChannel);
    }
}

// endregion
