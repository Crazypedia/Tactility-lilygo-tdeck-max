#include "Tactility/service/meshcore/MeshProtocol.h"

namespace tt::service::meshcore {

constexpr uint8_t FLAG_HOP_LIMIT_MASK = 0x07;
constexpr uint8_t FLAG_WANT_ACK = 0x08;
constexpr uint8_t FLAG_VIA_MQTT = 0x10;
constexpr uint8_t FLAG_HOP_START_SHIFT = 5;

static uint32_t readU32Le(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16) |
        (static_cast<uint32_t>(data[3]) << 24);
}

static void writeU32Le(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
    data[2] = static_cast<uint8_t>(value >> 16);
    data[3] = static_cast<uint8_t>(value >> 24);
}

bool parseHeader(const uint8_t* data, size_t length, PacketHeader& out) {
    if (length < PACKET_HEADER_SIZE) {
        return false;
    }

    out.to = readU32Le(data);
    out.from = readU32Le(data + 4);
    out.id = readU32Le(data + 8);

    const uint8_t flags = data[12];
    out.hopLimit = flags & FLAG_HOP_LIMIT_MASK;
    out.wantAck = (flags & FLAG_WANT_ACK) != 0;
    out.viaMqtt = (flags & FLAG_VIA_MQTT) != 0;
    out.hopStart = (flags >> FLAG_HOP_START_SHIFT) & FLAG_HOP_LIMIT_MASK;

    out.channelHash = data[13];
    out.nextHop = data[14];
    out.relayNode = data[15];
    return true;
}

void serializeHeader(const PacketHeader& header, uint8_t out[PACKET_HEADER_SIZE]) {
    writeU32Le(out, header.to);
    writeU32Le(out + 4, header.from);
    writeU32Le(out + 8, header.id);

    uint8_t flags = header.hopLimit & FLAG_HOP_LIMIT_MASK;
    if (header.wantAck) flags |= FLAG_WANT_ACK;
    if (header.viaMqtt) flags |= FLAG_VIA_MQTT;
    flags |= (header.hopStart & FLAG_HOP_LIMIT_MASK) << FLAG_HOP_START_SHIFT;
    out[12] = flags;

    out[13] = header.channelHash;
    out[14] = header.nextHop;
    out[15] = header.relayNode;
}

} // namespace tt::service::meshcore
