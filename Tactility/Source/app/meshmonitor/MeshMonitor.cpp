#include <Tactility/app/App.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/hal/radio/RadioDevice.h>
#include <Tactility/lvgl/LvglSync.h>

#include <Tactility/service/mesh/MeshCrypto.h>
#include <Tactility/service/mesh/MeshReceiver.h>

#include <tactility/log.h>

#include <meshtastic/mesh.pb.h>
#include <meshtastic/telemetry.pb.h>
#include <pb_decode.h>

#include <lvgl.h>

#include <cstdio>
#include <map>
#include <string>

// Phase 1 mesh monitor: RX-ONLY proof that this node hears the local mesh.
// Configures the LongFast (US/915) PHY, feeds every received frame through
// the MeshReceiver pipeline, and scrolls decoded traffic on screen. This app
// never transmits: no ACKs, no NodeInfo, nothing on air.
//
// Frequency is the well-known US LongFast primary slot. Region/channel become
// configurable when this logic moves into the mesh service (Phase 2/3).
namespace tt::app::meshmonitor {

using tt::hal::radio::RadioDevice;
using service::mesh::MeshReceiver;

constexpr auto TAG = "MeshMonitor";
constexpr float LONGFAST_US_FREQUENCY_MHZ = 906.875;

class MeshMonitorApp final : public App {

    std::shared_ptr<RadioDevice> radio;
    lv_obj_t* logLabel = nullptr;
    RadioDevice::RxSubscriptionId rxSubscription = 0;
    MeshReceiver receiver;
    std::map<uint32_t, std::string> shortNames; // NodeDB-lite, fed by NODEINFO
    std::string logText;
    int okCount = 0;
    int otherCount = 0;

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

    std::string nodeName(uint32_t from) {
        const auto it = shortNames.find(from);
        if (it != shortNames.end()) {
            return it->second;
        }
        char fallback[12];
        snprintf(fallback, sizeof(fallback), "!%08lx", static_cast<unsigned long>(from));
        return fallback;
    }

    bool configureRadio() {
        if (!radio->setModulation(RadioDevice::Modulation::LoRa)) {
            LOG_E(TAG, "setModulation(LoRa) failed");
            return false;
        }
        // Meshtastic LongFast, US/915 primary frequency slot.
        using P = RadioDevice::Parameter;
        radio->setParameter(P::Frequency, LONGFAST_US_FREQUENCY_MHZ);
        radio->setParameter(P::Bandwidth, 250.0);
        radio->setParameter(P::SpreadFactor, 11);
        radio->setParameter(P::CodingRate, 5);
        radio->setParameter(P::SyncWord, service::mesh::LORA_SYNC_WORD);
        radio->setParameter(P::PreambleLength, 16);
        radio->setParameter(P::BoostedGain, 1);
        return true;
    }

    std::string describe(const MeshReceiver::ReceivedPacket& packet) {
        const std::string name = nodeName(packet.header.from);
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
                    shortNames[packet.header.from] = user.short_name;
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

    void onRx(const tt::hal::radio::RxPacket& rxPacket) {
        MeshReceiver::ReceivedPacket packet;
        const auto result = receiver.process(rxPacket.data.data(), rxPacket.data.size(), rxPacket.rssi, rxPacket.snr, packet);

        std::string line;
        switch (result) {
            case MeshReceiver::Result::Ok:
                okCount++;
                line = describe(packet);
                break;
            case MeshReceiver::Result::Duplicate:
                // Rebroadcasts of packets we already showed: count silently.
                otherCount++;
                LOG_I(TAG, "dup from=%08lx id=%lu", static_cast<unsigned long>(packet.header.from), static_cast<unsigned long>(packet.header.id));
                return;
            case MeshReceiver::Result::UnknownChannel: {
                otherCount++;
                char buffer[48];
                snprintf(buffer, sizeof(buffer), "enc ch=%02x %ub %.0fdBm", packet.header.channelHash, static_cast<unsigned>(rxPacket.data.size()), rxPacket.rssi);
                line = buffer;
                break;
            }
            case MeshReceiver::Result::DecodeFailed: {
                otherCount++;
                char buffer[48];
                snprintf(buffer, sizeof(buffer), "?dec %ub %.0fdBm", static_cast<unsigned>(rxPacket.data.size()), rxPacket.rssi);
                line = buffer;
                break;
            }
            case MeshReceiver::Result::TooShort:
                otherCount++;
                LOG_W(TAG, "Frame too short: %u bytes", static_cast<unsigned>(rxPacket.data.size()));
                return;
        }

        if (lvgl::lock(100 / portTICK_PERIOD_MS)) {
            appendLog(line);
            lvgl::unlock();
        }
    }

public:

    void onShow(AppContext& /*context*/, lv_obj_t* parent) override {
        logLabel = lv_label_create(parent);
        lv_obj_set_width(logLabel, lv_pct(100));
        lv_label_set_long_mode(logLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(logLabel, "");

        radio = tt::hal::findFirstDevice<RadioDevice>(tt::hal::Device::Type::Radio);
        if (radio == nullptr) {
            appendLog("No radio found");
            LOG_E(TAG, "No radio device of type Radio found");
            return;
        }

        rxSubscription = radio->subscribeRx([this](tt::hal::Device::Id, const tt::hal::radio::RxPacket& packet) {
            onRx(packet);
        });

        if (!configureRadio()) {
            appendLog("Config failed");
            return;
        }

        if (!radio->start()) {
            appendLog("Radio start() failed");
            LOG_E(TAG, "radio->start() failed");
            return;
        }

        appendLog("LongFast 906.875 RX-only");
    }

    void onHide(AppContext& /*context*/) override {
        if (radio != nullptr) {
            radio->unsubscribeRx(rxSubscription);
            radio->stop();
            radio = nullptr;
        }
        LOG_I(TAG, "Session: %d decoded, %d other", okCount, otherCount);
    }
};

extern const AppManifest manifest = {
    .appId = "MeshMonitor",
    .appName = "Mesh Monitor",
    .appCategory = Category::System,
    .createApp = create<MeshMonitorApp>
};

} // namespace tt::app::meshmonitor
