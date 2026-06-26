#include <Tactility/app/App.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/hal/radio/RadioDevice.h>
#include <Tactility/lvgl/LvglSync.h>

#include <tactility/log.h>

#include <lvgl.h>

#include <cstdio>
#include <string>

// Minimal built-in smoke test for the SX1262 radio. Configures LoRa, starts the
// radio, transmits a small packet every few seconds, and logs any received
// packets. A single board cannot hear its own transmission (half duplex), so a
// clean run proves config + SPI + TCXO + RF switch + DIO1 IRQ + TX completion;
// RX is validated when a second LoRa device is transmitting on the same params.
//
// Frequency is region-dependent. 915 MHz = US ISM. Change for your locale.
namespace tt::app::radiotest {

using tt::hal::radio::RadioDevice;

constexpr auto TAG = "RadioTest";
constexpr float TEST_FREQUENCY_MHZ = 915.0;
constexpr uint32_t TX_INTERVAL_MS = 5000;

class RadioTestApp final : public App {

    std::shared_ptr<RadioDevice> radio;
    lv_obj_t* logLabel = nullptr;
    lv_timer_t* txTimer = nullptr;
    RadioDevice::RxSubscriptionId rxSubscription = 0;
    RadioDevice::StateSubscriptionId stateSubscription = 0;
    std::string logText;
    int txCount = 0;
    int rxCount = 0;

    // Append a line to the on-screen log (keeps it short for the small e-paper)
    // and mirror it to the serial console. Caller must hold the LVGL lock.
    void appendLog(const std::string& line) {
        logText += line;
        logText += "\n";
        if (logText.size() > 600) {
            logText.erase(0, logText.size() - 600);
        }
        if (logLabel != nullptr) {
            lv_label_set_text(logLabel, logText.c_str());
        }
    }

    bool configureRadio() {
        if (!radio->setModulation(RadioDevice::Modulation::LoRa)) {
            LOG_E(TAG, "setModulation(LoRa) failed");
            return false;
        }
        // Mirrors the vendor LoRa_sx1262 example (known-good on this hardware).
        using P = RadioDevice::Parameter;
        radio->setParameter(P::Frequency, TEST_FREQUENCY_MHZ);
        radio->setParameter(P::Bandwidth, 125.0);
        radio->setParameter(P::SpreadFactor, 10);
        radio->setParameter(P::CodingRate, 6);
        radio->setParameter(P::SyncWord, 0xAB);
        radio->setParameter(P::Power, 22);
        radio->setParameter(P::PreambleLength, 15);
        return true;
    }

    void transmitOnce() {
        char message[48];
        int len = snprintf(message, sizeof(message), "Hello T-Deck Max #%d", txCount++);
        tt::hal::radio::TxPacket packet{
            .data = std::vector<uint8_t>(message, message + len),
            .address = 0
        };
        radio->transmit(packet, [this](RadioDevice::TxId id, RadioDevice::TransmissionState state) {
            const char* name = "?";
            switch (state) {
                case RadioDevice::TransmissionState::Queued: name = "queued"; break;
                case RadioDevice::TransmissionState::PendingTransmit: name = "sending"; break;
                case RadioDevice::TransmissionState::Transmitted: name = "sent"; break;
                case RadioDevice::TransmissionState::Timeout: name = "timeout"; break;
                case RadioDevice::TransmissionState::Error: name = "error"; break;
            }
            LOG_I(TAG, "TX %d: %s", id, name);
            // The callback runs on the radio thread; lock LVGL to touch the UI.
            if (lvgl::lock(100 / portTICK_PERIOD_MS)) {
                appendLog(std::string("TX ") + std::to_string(id) + ": " + name);
                lvgl::unlock();
            }
        });
    }

public:

    void onShow(AppContext& /*context*/, lv_obj_t* parent) override {
        logLabel = lv_label_create(parent);
        lv_obj_set_width(logLabel, lv_pct(100));
        lv_label_set_long_mode(logLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(logLabel, "");

        radio = tt::hal::findFirstDevice<RadioDevice>(tt::hal::Device::Type::Radio);
        if (radio == nullptr) {
            appendLog("No SX1262 radio found");
            LOG_E(TAG, "No radio device of type Radio found");
            return;
        }

        appendLog(std::string("Radio: ") + radio->getName());

        stateSubscription = radio->subscribeStateChange([this](tt::hal::Device::Id, RadioDevice::State state) {
            LOG_I(TAG, "State -> %d", static_cast<int>(state));
        });

        rxSubscription = radio->subscribeRx([this](tt::hal::Device::Id, const tt::hal::radio::RxPacket& packet) {
            rxCount++;
            LOG_I(TAG, "RX %u bytes, rssi=%.1f snr=%.1f", (unsigned)packet.data.size(), packet.rssi, packet.snr);
            if (lvgl::lock(100 / portTICK_PERIOD_MS)) {
                char line[64];
                snprintf(line, sizeof(line), "RX #%d %ub rssi=%.0f snr=%.0f", rxCount, (unsigned)packet.data.size(), packet.rssi, packet.snr);
                appendLog(line);
                lvgl::unlock();
            }
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
        appendLog(std::string("Started @ ") + std::to_string((int)TEST_FREQUENCY_MHZ) + " MHz");

        // transmit() is non-blocking (queues), so it's safe from the LVGL timer.
        txTimer = lv_timer_create([](lv_timer_t* timer) {
            static_cast<RadioTestApp*>(lv_timer_get_user_data(timer))->transmitOnce();
        }, TX_INTERVAL_MS, this);
    }

    void onHide(AppContext& /*context*/) override {
        if (txTimer != nullptr) {
            lv_timer_delete(txTimer);
            txTimer = nullptr;
        }
        if (radio != nullptr) {
            radio->unsubscribeRx(rxSubscription);
            radio->unsubscribeStateChange(stateSubscription);
            radio->stop();
            radio = nullptr;
        }
    }
};

extern const AppManifest manifest = {
    .appId = "RadioTest",
    .appName = "Radio Test",
    .appCategory = Category::System,
    .createApp = create<RadioTestApp>
};

} // namespace tt::app::radiotest
