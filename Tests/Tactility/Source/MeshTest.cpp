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