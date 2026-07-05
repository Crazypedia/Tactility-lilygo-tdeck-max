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
    receiver.setChannel("LongFast", otherKey, sizeof(otherKey));

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
