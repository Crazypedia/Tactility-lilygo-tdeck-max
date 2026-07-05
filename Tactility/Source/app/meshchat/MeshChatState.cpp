#include "Tactility/app/meshchat/MeshChatState.h"

namespace tt::app::meshchat {

ConversationId conversationOf(const service::mesh::MeshService::TextMessage& message, uint32_t ownNodeId) {
    if (message.isOwn) {
        return message.to == service::mesh::BROADCAST_ADDRESS
            ? ConversationId::channel(message.channelIndex)
            : ConversationId::directMessage(message.to);
    }
    return message.to == ownNodeId
        ? ConversationId::directMessage(message.from)
        : ConversationId::channel(message.channelIndex);
}

void MeshChatState::setCurrent(const ConversationId& id) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    current = id;
}

ConversationId MeshChatState::getCurrent() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return current;
}

MeshChatState& sharedState() {
    static MeshChatState state;
    return state;
}

} // namespace tt::app::meshchat
