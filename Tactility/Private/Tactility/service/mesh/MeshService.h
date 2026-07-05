#pragma once

#include "MeshReceiver.h"

#include <Tactility/RecursiveMutex.h>
#include <Tactility/hal/radio/RadioDevice.h>
#include <Tactility/service/Service.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tt::service::mesh {

/**
 * Long-running mesh transport service (Meshtastic wire protocol).
 *
 * Owns the radio, the RX pipeline, and the node database, so mesh
 * reception continues while apps come and go - apps only subscribe.
 * Placeholder name "mesh" pending project naming.
 *
 * Phase 2 scope: single channel (LongFast US defaults), RX always on
 * once enabled, TX only on explicit sendText() calls from a UI action.
 */
class MeshService final : public Service {

public:

    struct NodeInfo {
        uint32_t nodeId = 0;
        std::string shortName;
        std::string longName;
        float lastRssi = 0;
        float lastSnr = 0;
        uint32_t packetsHeard = 0;
        bool hasPosition = false;
        double latitude = 0;
        double longitude = 0;
        bool hasBattery = false;
        uint8_t batteryLevel = 0;
    };

    using MessageSubscription = int;
    using NodeSubscription = int;
    using MessageCallback = std::function<void(const MeshReceiver::ReceivedPacket&)>;
    using NodeCallback = std::function<void(const NodeInfo&)>;

    static constexpr MessageSubscription NO_SUBSCRIPTION = -1;

    // region Overrides

    bool onStart(ServiceContext& service) override;
    void onStop(ServiceContext& service) override;

    // endregion

    /** Acquire the radio, configure the channel PHY, and start continuous RX.
     * Safe to call when already enabled.
     * @return false when no radio device exists or it fails to start
     */
    bool enable();

    /** Stop RX and release the radio. */
    void disable();

    bool isEnabled() const;

    /** Subscribe to every successfully decoded packet (all ports). */
    MessageSubscription subscribeMessages(MessageCallback onMessage);
    void unsubscribeMessages(MessageSubscription subscription);

    /** Subscribe to node database updates (fired when a node is heard). */
    NodeSubscription subscribeNodeUpdates(NodeCallback onNodeUpdate);
    void unsubscribeNodeUpdates(NodeSubscription subscription);

    /** Snapshot of the node database. */
    std::vector<NodeInfo> getNodes() const;

    /** Look up a node's display name: short name when known, else !hex id. */
    std::string getNodeName(uint32_t nodeId) const;

    /** This node's id, as used in the header 'from' field for TX. */
    uint32_t getOwnNodeId() const;

    /** Queue a text message for transmission on the primary channel.
     * TX happens only through this call - the service never transmits on
     * its own (no ACKs, no beacons, no NodeInfo).
     * @param[in] destination node id, or BROADCAST_ADDRESS for the channel
     * @param[in] text UTF-8 message, truncated to the payload limit
     * @return false when disabled or the frame could not be built/queued
     */
    bool sendText(uint32_t destination, const std::string& text);

private:

    struct MessageSubscriptionData {
        MessageSubscription id;
        MessageCallback onMessage;
    };

    struct NodeSubscriptionData {
        NodeSubscription id;
        NodeCallback onNodeUpdate;
    };

    mutable RecursiveMutex mutex;
    std::shared_ptr<hal::radio::RadioDevice> radio;
    hal::radio::RadioDevice::RxSubscriptionId rxSubscription = 0;
    MeshReceiver receiver;
    std::map<uint32_t, NodeInfo> nodes;
    std::vector<MessageSubscriptionData> messageSubscriptions;
    std::vector<NodeSubscriptionData> nodeSubscriptions;
    int lastSubscriptionId = 0;
    bool enabled = false;
    uint32_t ownNodeId = 0;
    uint32_t nextPacketId = 0;

    bool configureRadio();
    void onRx(const hal::radio::RxPacket& rxPacket);
    void updateNodeDb(const MeshReceiver::ReceivedPacket& packet);
};

std::shared_ptr<MeshService> findService();

} // namespace tt::service::mesh
