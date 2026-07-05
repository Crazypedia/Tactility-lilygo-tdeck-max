#include <Tactility/app/App.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/lvgl/LvglSync.h>

#include <Tactility/service/mesh/MeshService.h>

#include <tactility/log.h>

#include <meshtastic/mesh.pb.h>
#include <meshtastic/telemetry.pb.h>
#include <pb_decode.h>

#include <lvgl.h>

#include <cstdio>
#include <string>

// Phase 1/2 mesh monitor: RX-only view of decoded mesh traffic. The mesh
// service owns the radio and keeps receiving while this app is hidden;
// this app only enables the service and subscribes for display.
// Nothing here transmits.
namespace tt::app::meshmonitor {

using service::mesh::MeshService;
using service::mesh::MeshReceiver;

constexpr auto TAG = "MeshMonitor";

class MeshMonitorApp final : public App {

    std::shared_ptr<MeshService> mesh;
    lv_obj_t* logLabel = nullptr;
    MeshService::MessageSubscription messageSubscription = MeshService::NO_SUBSCRIPTION;
    std::string logText;

    // Append a line to the on-screen log and mirror it to serial. Caller must
    // hold the LVGL lock for the label update.
    void appendLog(const std::string& line) {
        LOG_I(TAG, "%s", line.c_str());
        logText += line;
        logText += "\n";
        if (logText.size() > 800) {
            logText.erase(0, logText.size() - 800);
        }
        if (logLabel != nullptr) {
            lv_label_set_text(logLabel, logText.c_str());
        }
    }

    std::string describe(const MeshReceiver::ReceivedPacket& packet) {
        const std::string name = mesh->getNodeName(packet.header.from);
        char line[96];

        switch (packet.data.portnum) {
            case meshtastic_PortNum_TEXT_MESSAGE_APP: {
                std::string text(reinterpret_cast<const char*>(packet.data.payload.bytes), packet.data.payload.size);
                snprintf(line, sizeof(line), "[%s] %s", name.c_str(), text.c_str());
                break;
            }
            case meshtastic_PortNum_NODEINFO_APP: {
                meshtastic_User user = meshtastic_User_init_zero;
                pb_istream_t stream = pb_istream_from_buffer(packet.data.payload.bytes, packet.data.payload.size);
                if (pb_decode(&stream, meshtastic_User_fields, &user)) {
                    snprintf(line, sizeof(line), "[%s] info: %s", user.short_name, user.long_name);
                } else {
                    snprintf(line, sizeof(line), "[%s] info: <bad>", name.c_str());
                }
                break;
            }
            case meshtastic_PortNum_POSITION_APP: {
                meshtastic_Position position = meshtastic_Position_init_zero;
                pb_istream_t stream = pb_istream_from_buffer(packet.data.payload.bytes, packet.data.payload.size);
                if (pb_decode(&stream, meshtastic_Position_fields, &position)) {
                    snprintf(line, sizeof(line), "[%s] pos %.4f,%.4f", name.c_str(), position.latitude_i * 1e-7, position.longitude_i * 1e-7);
                } else {
                    snprintf(line, sizeof(line), "[%s] pos <bad>", name.c_str());
                }
                break;
            }
            case meshtastic_PortNum_TELEMETRY_APP: {
                meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
                pb_istream_t stream = pb_istream_from_buffer(packet.data.payload.bytes, packet.data.payload.size);
                if (pb_decode(&stream, meshtastic_Telemetry_fields, &telemetry) && telemetry.which_variant == meshtastic_Telemetry_device_metrics_tag) {
                    snprintf(line, sizeof(line), "[%s] telem bat=%lu%%", name.c_str(), static_cast<unsigned long>(telemetry.variant.device_metrics.battery_level));
                } else {
                    snprintf(line, sizeof(line), "[%s] telem", name.c_str());
                }
                break;
            }
            default:
                snprintf(line, sizeof(line), "[%s] port %d", name.c_str(), static_cast<int>(packet.data.portnum));
                break;
        }

        char suffix[24];
        snprintf(suffix, sizeof(suffix), " %.0fdBm", packet.rssi);
        return std::string(line) + suffix;
    }

public:

    void onShow(AppContext& /*context*/, lv_obj_t* parent) override {
        logLabel = lv_label_create(parent);
        lv_obj_set_width(logLabel, lv_pct(100));
        lv_label_set_long_mode(logLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(logLabel, "");

        mesh = service::mesh::findService();
        if (mesh == nullptr) {
            appendLog("Mesh service not running");
            return;
        }

        if (!mesh->enable()) {
            appendLog("No radio / radio failed");
            return;
        }

        messageSubscription = mesh->subscribeMessages([this](const MeshReceiver::ReceivedPacket& packet) {
            if (lvgl::lock(100 / portTICK_PERIOD_MS)) {
                appendLog(describe(packet));
                lvgl::unlock();
            }
        });

        appendLog("LongFast 906.875 RX-only");
        // Show what the service already heard while this app was closed.
        for (const auto& node : mesh->getNodes()) {
            char line[64];
            snprintf(line, sizeof(line), "%s heard %lux %.0fdBm", node.shortName.empty() ? mesh->getNodeName(node.nodeId).c_str() : node.shortName.c_str(), static_cast<unsigned long>(node.packetsHeard), node.lastRssi);
            appendLog(line);
        }
    }

    void onHide(AppContext& /*context*/) override {
        // Only drop the display subscription: the service keeps receiving.
        if (mesh != nullptr) {
            mesh->unsubscribeMessages(messageSubscription);
            mesh = nullptr;
        }
    }
};

extern const AppManifest manifest = {
    .appId = "MeshMonitor",
    .appName = "Mesh Monitor",
    .appCategory = Category::System,
    .createApp = create<MeshMonitorApp>
};

} // namespace tt::app::meshmonitor
