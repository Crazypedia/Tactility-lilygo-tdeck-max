#pragma once

#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/mesh/MeshService.h>

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace tt::app::meshchat {

constexpr size_t MAX_MESSAGES_PER_CONVERSATION = 50;

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

struct ChatMessage {
    uint32_t packetId = 0;
    std::string senderName;
    std::string text;
    bool isOwn = false;
    service::mesh::MeshService::TxStatus txStatus = service::mesh::MeshService::TxStatus::Sent;
};

/** Thread safety: all public methods are mutex-protected.
 *  LVGL sync lock must be held separately when updating UI. */
class MeshChatState {

    mutable RecursiveMutex mutex;

    std::map<ConversationId, std::deque<ChatMessage>> conversations;
    ConversationId current = ConversationId::channel(0);

public:

    void setCurrent(const ConversationId& id);
    ConversationId getCurrent() const;

    void addMessage(const ConversationId& id, const ChatMessage& message);

    /** @return true when the message was found and updated */
    bool updateTxStatus(uint32_t packetId, service::mesh::MeshService::TxStatus status);

    std::vector<ChatMessage> getMessages(const ConversationId& id) const;

    /** Conversations that have message history (channels without traffic are
     * listed by the view from the service's channel table instead). */
    std::vector<ConversationId> getConversations() const;
};

} // namespace tt::app::meshchat
