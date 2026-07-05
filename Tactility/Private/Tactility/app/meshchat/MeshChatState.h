#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/mesh/MeshService.h>

namespace tt::app::meshchat {

/** A conversation is either a channel (broadcast) or a DM thread with one node. */
struct ConversationId {
    bool dm = false;
    size_t channelIndex = 0; // when !dm
    uint32_t nodeId = 0;     // when dm

    static ConversationId channel(size_t index) {
        return {.dm = false, .channelIndex = index, .nodeId = 0};
    }

    static ConversationId directMessage(uint32_t nodeId) {
        return {.dm = true, .channelIndex = 0, .nodeId = nodeId};
    }

    bool operator<(const ConversationId& other) const {
        if (dm != other.dm) return !dm;
        if (dm) return nodeId < other.nodeId;
        return channelIndex < other.channelIndex;
    }

    bool operator==(const ConversationId& other) const {
        return dm == other.dm && (dm ? nodeId == other.nodeId : channelIndex == other.channelIndex);
    }
};

/** The conversation a stored message belongs in: DMs to us thread under the
 * sender, own DMs under the recipient, everything else under its channel. */
ConversationId conversationOf(const service::mesh::MeshService::TextMessage& message, uint32_t ownNodeId);

/** UI state that must survive app relaunches. Message history lives in the
 * mesh service (getTextMessages()); this only tracks what the user was
 * looking at. Thread safety: all public methods are mutex-protected. */
class MeshChatState {

    mutable RecursiveMutex mutex;

    ConversationId current = ConversationId::channel(0);

public:

    void setCurrent(const ConversationId& id);
    ConversationId getCurrent() const;
};

/** The shared instance, kept alive across app open/close. */
MeshChatState& sharedState();

} // namespace tt::app::meshchat
