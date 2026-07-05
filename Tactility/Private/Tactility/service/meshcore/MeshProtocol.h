#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Wire-level definitions for the Meshtastic LoRa protocol.
 *
 * Byte layout and constants follow the Meshtastic firmware
 * (RadioInterface.h, PacketHeader) as of protocol 2.6. All multi-byte
 * fields are little-endian on the wire.
 */
namespace tt::service::meshcore {

/** LoRa sync word shared by all Meshtastic networks. */
constexpr uint8_t LORA_SYNC_WORD = 0x2B;

/** Destination address for channel broadcasts. */
constexpr uint32_t BROADCAST_ADDRESS = 0xFFFFFFFFU;

constexpr size_t PACKET_HEADER_SIZE = 16;
constexpr size_t MAX_LORA_PAYLOAD = 255;
constexpr size_t MAX_ENCRYPTED_PAYLOAD = MAX_LORA_PAYLOAD - PACKET_HEADER_SIZE;

/** Decoded form of the 16-byte packet header that precedes the encrypted payload. */
struct PacketHeader {
    uint32_t to = 0;
    uint32_t from = 0;
    uint32_t id = 0;
    uint8_t hopLimit = 0;   // flags bits 0-2
    bool wantAck = false;   // flags bit 3
    bool viaMqtt = false;   // flags bit 4
    uint8_t hopStart = 0;   // flags bits 5-7
    uint8_t channelHash = 0;
    uint8_t nextHop = 0;
    uint8_t relayNode = 0;
};

/** Parse the first 16 bytes of a received LoRa frame.
 * @param[in] data raw frame bytes
 * @param[in] length total frame length
 * @param[out] out parsed header
 * @return false when length < PACKET_HEADER_SIZE
 */
bool parseHeader(const uint8_t* data, size_t length, PacketHeader& out);

/** Serialize a header into its 16-byte wire format. */
void serializeHeader(const PacketHeader& header, uint8_t out[PACKET_HEADER_SIZE]);

} // namespace tt::service::meshcore
