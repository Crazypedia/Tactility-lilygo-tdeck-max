#include "Tactility/app/meshchat/MeshChatState.h"

#include <Tactility/app/App.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/StringUtils.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/mesh/MeshService.h>

#include <tactility/log.h>

#include <lvgl.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

// Mesh text chat on the mesh service (Meshtastic wire protocol).
// Message history lives in the service, so it survives app close/reopen and
// captures messages that arrive while the app is closed; the selected
// conversation persists in sharedState(). The app is a pure view with three
// panes: the chat window, a searchable chat list, and a searchable node list.
//
// TX only happens when the user presses Send. Channel PSKs beyond the
// default LongFast key are supported by the service (ChannelConfig); the
// entry UI for them lands with the settings app.
namespace tt::app::meshchat {

using service::mesh::MeshService;
using service::mesh::MeshReceiver;

constexpr auto TAG = "MeshChat";

namespace {

std::string nodeIdString(uint32_t nodeId) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "!%08lx", static_cast<unsigned long>(nodeId));
    return buffer;
}

/** Case-insensitive substring match; an empty filter matches everything. */
bool matchesFilter(const std::string& haystack, const std::string& filter) {
    return filter.empty() || string::lowercase(haystack).find(string::lowercase(filter)) != std::string::npos;
}

} // namespace

class MeshChatApp final : public App {

    enum class Pane { Chat, ChatList, NodeList };

    std::shared_ptr<MeshService> mesh;
    MeshService::MessageSubscription messageSubscription = MeshService::NO_SUBSCRIPTION;
    MeshService::TxStatusSubscription txStatusSubscription = MeshService::NO_SUBSCRIPTION;

    // View widgets (LVGL lock required for all access)
    lv_obj_t* toolbar = nullptr;
    lv_obj_t* chatPane = nullptr;
    lv_obj_t* msgList = nullptr;
    lv_obj_t* inputField = nullptr;
    lv_obj_t* chatListPane = nullptr;
    lv_obj_t* chatSearchField = nullptr;
    lv_obj_t* chatList = nullptr;
    lv_obj_t* nodeListPane = nullptr;
    lv_obj_t* nodeSearchField = nullptr;
    lv_obj_t* nodeList = nullptr;
    Pane activePane = Pane::Chat;

    // region Node naming

    /** Best single display name: long name, else short name, else !hex id. */
    std::string nodeDisplayName(const MeshService::NodeInfo& node) const {
        if (!node.longName.empty()) return node.longName;
        if (!node.shortName.empty()) return node.shortName;
        return nodeIdString(node.nodeId);
    }

    std::string nodeDisplayName(uint32_t nodeId) const {
        for (const auto& node : mesh->getNodes()) {
            if (node.nodeId == nodeId) {
                return nodeDisplayName(node);
            }
        }
        return nodeIdString(nodeId);
    }

    /** List label carrying all three identifiers: "SHRT Long Name !hex". */
    std::string nodeLabel(const MeshService::NodeInfo& node) const {
        std::string label;
        if (!node.shortName.empty()) {
            label += node.shortName + " ";
        }
        if (!node.longName.empty()) {
            label += node.longName + " ";
        }
        label += nodeIdString(node.nodeId);
        return label;
    }

    std::string conversationTitle(const ConversationId& id) const {
        if (id.dm) {
            return "DM " + nodeDisplayName(id.nodeId);
        }
        const auto channels = mesh->getChannels();
        if (id.channelIndex < channels.size()) {
            return "#" + channels[id.channelIndex].name;
        }
        return "#?";
    }

    // endregion
    // region Chat pane

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

    void addMessageToList(const MeshService::TextMessage& message) {
        auto* label = lv_label_create(msgList);
        std::string line = (message.isOwn ? "me" : mesh->getNodeName(message.from)) + ": " + message.text;
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
        const auto current = sharedState().getCurrent();
        const auto ownNodeId = mesh->getOwnNodeId();
        for (const auto& message : mesh->getTextMessages()) {
            if (conversationOf(message, ownNodeId) == current) {
                addMessageToList(message);
            }
        }
        lv_obj_scroll_to_y(msgList, LV_COORD_MAX, LV_ANIM_OFF);
    }

    void createChatPane(lv_obj_t* parent) {
        chatPane = createPane(parent);

        msgList = lv_list_create(chatPane);
        lv_obj_set_flex_grow(msgList, 1);
        lv_obj_set_width(msgList, LV_PCT(100));
        lv_obj_set_style_border_width(msgList, 0, 0);
        lv_obj_set_style_pad_ver(msgList, 2, 0);
        lv_obj_set_style_pad_hor(msgList, 4, 0);

        auto* inputWrapper = lv_obj_create(chatPane);
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

    // endregion
    // region Chat list pane

    void refreshChatList() {
        lv_obj_clean(chatList);
        const std::string filter = lv_textarea_get_text(chatSearchField);

        // Channels always show; DM threads show once they have history.
        const auto channels = mesh->getChannels();
        for (size_t i = 0; i < channels.size(); i++) {
            const std::string title = "#" + channels[i].name;
            if (matchesFilter(title, filter)) {
                addChatButton(ConversationId::channel(i), title);
            }
        }

        std::set<uint32_t> dmPeers;
        const auto ownNodeId = mesh->getOwnNodeId();
        for (const auto& message : mesh->getTextMessages()) {
            const auto conversation = conversationOf(message, ownNodeId);
            if (conversation.dm) {
                dmPeers.insert(conversation.nodeId);
            }
        }
        for (const auto peer : dmPeers) {
            // Search matches any identifier: short name, long name, or !hex.
            const std::string haystack = "DM " + nodeSearchText(peer);
            if (matchesFilter(haystack, filter)) {
                addChatButton(ConversationId::directMessage(peer), "DM " + nodeDisplayName(peer));
            }
        }
    }

    std::string nodeSearchText(uint32_t nodeId) const {
        for (const auto& node : mesh->getNodes()) {
            if (node.nodeId == nodeId) {
                return nodeLabel(node);
            }
        }
        return nodeIdString(nodeId);
    }

    void addChatButton(const ConversationId& id, const std::string& title) {
        auto* button = lv_list_add_button(chatList, nullptr, title.c_str());
        auto* idCopy = new ConversationId(id);
        lv_obj_set_user_data(button, this);
        lv_obj_add_event_cb(button, [](lv_event_t* e) {
            auto* self = static_cast<MeshChatApp*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
            auto* conversation = static_cast<ConversationId*>(lv_event_get_user_data(e));
            self->openConversation(*conversation);
        }, LV_EVENT_CLICKED, idCopy);
        lv_obj_add_event_cb(button, [](lv_event_t* e) {
            delete static_cast<ConversationId*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, idCopy);
    }

    void createChatListPane(lv_obj_t* parent) {
        chatListPane = createPane(parent);
        chatSearchField = createSearchField(chatListPane, [](lv_event_t* e) {
            static_cast<MeshChatApp*>(lv_event_get_user_data(e))->refreshActivePane();
        });
        chatList = createListWidget(chatListPane);
    }

    // endregion
    // region Node list pane

    void refreshNodeList() {
        lv_obj_clean(nodeList);
        const std::string filter = lv_textarea_get_text(nodeSearchField);

        const auto nodes = mesh->getNodes();
        size_t shown = 0;
        for (const auto& node : nodes) {
            const auto label = nodeLabel(node);
            if (!matchesFilter(label, filter)) {
                continue;
            }
            auto* button = lv_list_add_button(nodeList, nullptr, label.c_str());
            lv_obj_set_user_data(button, this);
            // nodeId rides in the event user data; it fits a pointer even on
            // 32-bit targets, so no heap copy is needed.
            lv_obj_add_event_cb(button, [](lv_event_t* e) {
                auto* self = static_cast<MeshChatApp*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
                const auto nodeId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
                self->openConversation(ConversationId::directMessage(nodeId));
            }, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(node.nodeId)));
            shown++;
        }

        if (shown == 0) {
            auto* label = lv_label_create(nodeList);
            lv_label_set_text(label, nodes.empty() ? "No nodes heard yet" : "No matches");
            lv_obj_set_style_pad_all(label, 4, 0);
        }
    }

    void createNodeListPane(lv_obj_t* parent) {
        nodeListPane = createPane(parent);
        nodeSearchField = createSearchField(nodeListPane, [](lv_event_t* e) {
            static_cast<MeshChatApp*>(lv_event_get_user_data(e))->refreshActivePane();
        });
        nodeList = createListWidget(nodeListPane);
    }

    // endregion
    // region Pane plumbing

    lv_obj_t* createPane(lv_obj_t* parent) {
        auto* pane = lv_obj_create(parent);
        lv_obj_set_width(pane, LV_PCT(100));
        lv_obj_set_flex_grow(pane, 1);
        lv_obj_set_flex_flow(pane, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(pane, 0, 0);
        lv_obj_set_style_pad_row(pane, 0, 0);
        lv_obj_set_style_border_opa(pane, 0, LV_STATE_DEFAULT);
        lv_obj_add_flag(pane, LV_OBJ_FLAG_HIDDEN);
        return pane;
    }

    lv_obj_t* createSearchField(lv_obj_t* parent, lv_event_cb_t onChanged) {
        auto* field = lv_textarea_create(parent);
        lv_obj_set_width(field, LV_PCT(100));
        lv_textarea_set_placeholder_text(field, "Search...");
        lv_textarea_set_one_line(field, true);
        lv_obj_add_event_cb(field, onChanged, LV_EVENT_VALUE_CHANGED, this);
        return field;
    }

    lv_obj_t* createListWidget(lv_obj_t* parent) {
        auto* list = lv_list_create(parent);
        lv_obj_set_width(list, LV_PCT(100));
        lv_obj_set_flex_grow(list, 1);
        lv_obj_set_style_border_width(list, 0, 0);
        return list;
    }

    void showPane(Pane pane) {
        activePane = pane;
        lv_obj_add_flag(chatPane, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(chatListPane, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(nodeListPane, LV_OBJ_FLAG_HIDDEN);
        switch (pane) {
            case Pane::Chat:
                lv_obj_remove_flag(chatPane, LV_OBJ_FLAG_HIDDEN);
                lvgl::toolbar_set_title(toolbar, mesh != nullptr ? conversationTitle(sharedState().getCurrent()) : "Mesh Chat");
                break;
            case Pane::ChatList:
                lv_obj_remove_flag(chatListPane, LV_OBJ_FLAG_HIDDEN);
                lvgl::toolbar_set_title(toolbar, "Chats");
                break;
            case Pane::NodeList:
                lv_obj_remove_flag(nodeListPane, LV_OBJ_FLAG_HIDDEN);
                lvgl::toolbar_set_title(toolbar, "Nodes");
                break;
        }
        refreshActivePane();
    }

    void refreshActivePane() {
        if (mesh == nullptr) {
            return;
        }
        switch (activePane) {
            case Pane::Chat: refreshMessageList(); break;
            case Pane::ChatList: refreshChatList(); break;
            case Pane::NodeList: refreshNodeList(); break;
        }
    }

    void openConversation(const ConversationId& id) {
        sharedState().setCurrent(id);
        showPane(Pane::Chat);
    }

    // endregion
    // region Events

    void onSendClicked() {
        const char* text = lv_textarea_get_text(inputField);
        if (mesh == nullptr || text == nullptr || strlen(text) == 0) {
            return;
        }

        const auto conversation = sharedState().getCurrent();
        // DMs go out on the primary channel with the node as destination.
        const size_t channelIndex = conversation.dm ? 0 : conversation.channelIndex;
        const uint32_t destination = conversation.dm ? conversation.nodeId : service::mesh::BROADCAST_ADDRESS;

        if (mesh->sendText(channelIndex, destination, text) == 0) {
            LOG_E(TAG, "Send failed");
            return;
        }

        lv_textarea_set_text(inputField, "");
        refreshMessageList();
    }

    void onMeshMessage(const MeshReceiver::ReceivedPacket& packet) {
        // The service already recorded the message; only the view updates here.
        if (packet.data.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) {
            return;
        }
        if (lvgl::lock(100 / portTICK_PERIOD_MS)) {
            refreshActivePane();
            lvgl::unlock();
        }
    }

    void onTxStatus() {
        // Radio thread: the service store is already updated, just redraw.
        if (lvgl::lock(100 / portTICK_PERIOD_MS)) {
            if (activePane == Pane::Chat) {
                refreshMessageList();
            }
            lvgl::unlock();
        }
    }

    // endregion

public:

    void onShow(AppContext& context, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        toolbar = lvgl::toolbar_create(parent, context);
        lvgl::toolbar_add_text_button_action(toolbar, LV_SYMBOL_LIST, [](lv_event_t* e) {
            static_cast<MeshChatApp*>(lv_event_get_user_data(e))->showPane(Pane::ChatList);
        }, this);
        lvgl::toolbar_add_text_button_action(toolbar, LV_SYMBOL_GPS, [](lv_event_t* e) {
            static_cast<MeshChatApp*>(lv_event_get_user_data(e))->showPane(Pane::NodeList);
        }, this);

        createChatPane(parent);
        createChatListPane(parent);
        createNodeListPane(parent);

        mesh = service::mesh::findService();
        if (mesh == nullptr || !mesh->enable()) {
            mesh = nullptr;
            showPane(Pane::Chat);
            auto* label = lv_label_create(msgList);
            lv_label_set_text(label, "Mesh unavailable (no radio?)");
            return;
        }

        messageSubscription = mesh->subscribeMessages([this](const MeshReceiver::ReceivedPacket& packet) {
            onMeshMessage(packet);
        });
        txStatusSubscription = mesh->subscribeTxStatus([this](uint32_t, MeshService::TxStatus) {
            onTxStatus();
        });

        // Reopen where the user left off; history comes from the service.
        showPane(Pane::Chat);
    }

    void onHide(AppContext& /*context*/) override {
        // Drop only the subscriptions: the service (and mesh RX) keeps running.
        if (mesh != nullptr) {
            mesh->unsubscribeMessages(messageSubscription);
            mesh->unsubscribeTxStatus(txStatusSubscription);
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
