#include "Tactility/app/meshchat/MeshChatState.h"

namespace tt::app::meshchat {

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

void MeshChatState::addMessage(const ConversationId& id, const ChatMessage& message) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    auto& messages = conversations[id];
    messages.push_back(message);
    while (messages.size() > MAX_MESSAGES_PER_CONVERSATION) {
        messages.pop_front();
    }
}

bool MeshChatState::updateTxStatus(uint32_t packetId, service::mesh::MeshService::TxStatus status) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    for (auto& [id, messages] : conversations) {
        for (auto& message : messages) {
            if (message.isOwn && message.packetId == packetId) {
                message.txStatus = status;
                return true;
            }
        }
    }
    return false;
}

std::vector<ChatMessage> MeshChatState::getMessages(const ConversationId& id) const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    const auto it = conversations.find(id);
    if (it == conversations.end()) {
        return {};
    }
    return {it->second.begin(), it->second.end()};
}

std::vector<ConversationId> MeshChatState::getConversations() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    std::vector<ConversationId> result;
    result.reserve(conversations.size());
    for (const auto& [id, messages] : conversations) {
        result.push_back(id);
    }
    return result;
}

} // namespace tt::app::meshchat
