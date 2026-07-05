#include "Tactility/app/meshchat/MeshChatState.h"

#include <Tactility/app/App.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/mesh/MeshService.h>

#include <tactility/log.h>

#include <lvgl.h>

#include <cstdio>
#include <cstring>
#include <string>

// Mesh text chat on the mesh service (Meshtastic wire protocol).
// Layering follows the built-in Chat app: state holds history, the view is
// LVGL-only, the app wires the service. The service owns the radio, so chat
// receives even while this app is closed (messages that arrive then are
// dropped from history for now - service-side history is a later step).
//
// TX only happens when the user presses Send. Channel PSKs beyond the
// default LongFast key are supported by the service (ChannelConfig); the
// entry UI for them lands with the settings app.
namespace tt::app::meshchat {

using service::mesh::MeshService;
using service::mesh::MeshReceiver;

constexpr auto TAG = "MeshChat";

class MeshChatApp final : public App {

    std::shared_ptr<MeshService> mesh;
    MeshService::MessageSubscription messageSubscription = MeshService::NO_SUBSCRIPTION;
    MeshChatState state;

    // View widgets (LVGL lock required for all access)
    lv_obj_t* toolbar = nullptr;
    lv_obj_t* msgList = nullptr;
    lv_obj_t* inputWrapper = nullptr;
    lv_obj_t* inputField = nullptr;
    lv_obj_t* conversationPanel = nullptr;

    std::string conversationTitle(const ConversationId& id) {
        if (id.dm) {
            return "DM " + mesh->getNodeName(id.nodeId);
        }
        const auto channels = mesh->getChannels();
        if (id.channelIndex < channels.size()) {
            return "#" + channels[id.channelIndex].name;
        }
        return "#?";
    }

    static const char* statusSuffix(MeshService::TxStatus status) {
        switch (status) {
            case MeshService::TxStatus::Queued:
            case MeshService::TxStatus::Sending:
                return " ...";
            case MeshService::TxStatus::Sent:
                return " " LV_SYMBOL_OK;
            case MeshService::TxStatus::Failed:
                return " " LV_SYMBOL_CLOSE;
        }
        return "";
    }

    void addMessageToList(const ChatMessage& message) {
        auto* label = lv_label_create(msgList);
        std::string line = (message.isOwn ? "me" : message.senderName) + ": " + message.text;
        if (message.isOwn) {
            line += statusSuffix(message.txStatus);
        }
        lv_label_set_text(label, line.c_str());
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_pad_all(label, 2, 0);
    }

    void refreshMessageList() {
        lv_obj_clean(msgList);
        for (const auto& message : state.getMessages(state.getCurrent())) {
            addMessageToList(message);
        }
        lv_obj_scroll_to_y(msgList, LV_COORD_MAX, LV_ANIM_OFF);
        lvgl::toolbar_set_title(toolbar, conversationTitle(state.getCurrent()));
    }

    void createInputBar(lv_obj_t* parent) {
        inputWrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(inputWrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(inputWrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(inputWrapper, 0, 0);
        lv_obj_set_style_pad_column(inputWrapper, 4, 0);
        lv_obj_set_style_border_opa(inputWrapper, 0, LV_STATE_DEFAULT);

        inputField = lv_textarea_create(inputWrapper);
        lv_obj_set_flex_grow(inputField, 1);
        lv_textarea_set_placeholder_text(inputField, "Message...");
        lv_textarea_set_one_line(inputField, true);
        lv_textarea_set_max_length(inputField, 200);

        auto* sendButton = lv_button_create(inputWrapper);
        lv_obj_set_style_margin_all(sendButton, 0, LV_STATE_DEFAULT);
        lv_obj_add_event_cb(sendButton, [](lv_event_t* e) {
            static_cast<MeshChatApp*>(lv_event_get_user_data(e))->onSendClicked();
        }, LV_EVENT_CLICKED, this);
        auto* sendLabel = lv_label_create(sendButton);
        lv_label_set_text(sendLabel, "Send");
        lv_obj_center(sendLabel);
    }

    // Conversation selector: channels from the service table, DM threads for
    // every node heard on air.
    void showConversationPanel() {
        if (conversationPanel != nullptr) {
            lv_obj_delete(conversationPanel);
        }
        conversationPanel = lv_list_create(lv_obj_get_parent(msgList));
        lv_obj_set_width(conversationPanel, LV_PCT(100));
        lv_obj_set_flex_grow(conversationPanel, 1);

        const auto channels = mesh->getChannels();
        for (size_t i = 0; i < channels.size(); i++) {
            addConversationButton(ConversationId::channel(i), "#" + channels[i].name);
        }
        for (const auto& node : mesh->getNodes()) {
            addConversationButton(ConversationId::directMessage(node.nodeId), "DM " + mesh->getNodeName(node.nodeId));
        }

        lv_obj_add_flag(msgList, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(inputWrapper, LV_OBJ_FLAG_HIDDEN);
    }

    void addConversationButton(const ConversationId& id, const std::string& title) {
        auto* button = lv_list_add_button(conversationPanel, nullptr, title.c_str());
        auto* idCopy = new ConversationId(id);
        lv_obj_add_event_cb(button, [](lv_event_t* e) {
            auto* self = static_cast<MeshChatApp*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
            auto* conversation = static_cast<ConversationId*>(lv_event_get_user_data(e));
            self->onConversationSelected(*conversation);
        }, LV_EVENT_CLICKED, idCopy);
        lv_obj_set_user_data(button, this);
        lv_obj_add_event_cb(button, [](lv_event_t* e) {
            delete static_cast<ConversationId*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, idCopy);
    }

    void hideConversationPanel() {
        if (conversationPanel != nullptr) {
            lv_obj_delete(conversationPanel);
            conversationPanel = nullptr;
        }
        lv_obj_remove_flag(msgList, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(inputWrapper, LV_OBJ_FLAG_HIDDEN);
    }

    void onConversationSelected(const ConversationId& id) {
        state.setCurrent(id);
        hideConversationPanel();
        refreshMessageList();
    }

    void onSendClicked() {
        const char* text = lv_textarea_get_text(inputField);
        if (text == nullptr || strlen(text) == 0) {
            return;
        }

        const auto conversation = state.getCurrent();
        // DMs go out on the primary channel with the node as destination.
        const size_t channelIndex = conversation.dm ? 0 : conversation.channelIndex;
        const uint32_t destination = conversation.dm ? conversation.nodeId : service::mesh::BROADCAST_ADDRESS;

        const uint32_t packetId = mesh->sendText(channelIndex, destination, text, [this](uint32_t id, MeshService::TxStatus status) {
            // Radio thread: update state, then redraw under the LVGL lock.
            if (state.updateTxStatus(id, status) && lvgl::lock(100 / portTICK_PERIOD_MS)) {
                refreshMessageList();
                lvgl::unlock();
            }
        });

        if (packetId == 0) {
            LOG_E(TAG, "Send failed");
            return;
        }

        ChatMessage message;
        message.packetId = packetId;
        message.text = text;
        message.isOwn = true;
        message.txStatus = MeshService::TxStatus::Queued;
        state.addMessage(conversation, message);

        lv_textarea_set_text(inputField, "");
        refreshMessageList();
    }

    void onMeshMessage(const MeshReceiver::ReceivedPacket& packet) {
        if (packet.data.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) {
            return;
        }

        ChatMessage message;
        message.senderName = mesh->getNodeName(packet.header.from);
        message.text = std::string(reinterpret_cast<const char*>(packet.data.payload.bytes), packet.data.payload.size);

        // DMs to this node thread under the sender; everything else under
        // the channel it arrived on.
        const auto conversation = packet.header.to == mesh->getOwnNodeId()
            ? ConversationId::directMessage(packet.header.from)
            : ConversationId::channel(packet.channelIndex);
        state.addMessage(conversation, message);

        if (lvgl::lock(100 / portTICK_PERIOD_MS)) {
            if (conversation == state.getCurrent() && conversationPanel == nullptr) {
                addMessageToList(message);
                lv_obj_scroll_to_y(msgList, LV_COORD_MAX, LV_ANIM_OFF);
            }
            lvgl::unlock();
        }
    }

public:

    void onShow(AppContext& context, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        toolbar = lvgl::toolbar_create(parent, context);
        lvgl::toolbar_add_text_button_action(toolbar, LV_SYMBOL_LIST, [](lv_event_t* e) {
            static_cast<MeshChatApp*>(lv_event_get_user_data(e))->showConversationPanel();
        }, this);

        msgList = lv_list_create(parent);
        lv_obj_set_flex_grow(msgList, 1);
        lv_obj_set_width(msgList, LV_PCT(100));
        lv_obj_set_style_border_width(msgList, 0, 0);
        lv_obj_set_style_pad_ver(msgList, 2, 0);
        lv_obj_set_style_pad_hor(msgList, 4, 0);

        createInputBar(parent);

        mesh = service::mesh::findService();
        if (mesh == nullptr || !mesh->enable()) {
            auto* label = lv_label_create(msgList);
            lv_label_set_text(label, "Mesh unavailable (no radio?)");
            return;
        }

        messageSubscription = mesh->subscribeMessages([this](const MeshReceiver::ReceivedPacket& packet) {
            onMeshMessage(packet);
        });

        refreshMessageList();
    }

    void onHide(AppContext& /*context*/) override {
        // Drop only the subscription: the service (and mesh RX) keeps running.
        if (mesh != nullptr) {
            mesh->unsubscribeMessages(messageSubscription);
            mesh = nullptr;
        }
    }
};

extern const AppManifest manifest = {
    .appId = "MeshChat",
    .appName = "Mesh Chat",
    .appCategory = Category::User,
    .createApp = create<MeshChatApp>
};

} // namespace tt::app::meshchat
