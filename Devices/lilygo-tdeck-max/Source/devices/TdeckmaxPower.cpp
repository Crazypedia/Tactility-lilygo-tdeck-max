#include "TdeckmaxPower.h"

#include <Tactility/Logger.h>
#include <tactility/drivers/i2c_controller.h>

static const auto LOGGER = tt::Logger("TdeckmaxPower");

// SY6970 charger (newer T-Deck Max revisions; older boards use BQ25896 @ 0x6B).
static constexpr uint8_t SY6970_ADDRESS = 0x6A;
static constexpr uint8_t SY6970_REG_09 = 0x09;
static constexpr uint8_t SY6970_BATFET_DIS = 1 << 5; // Force BATFET off = ship mode

bool TdeckmaxPower::supportsMetric(MetricType type) const {
    switch (type) {
        using enum MetricType;
        case IsCharging:
        case Current:
        case BatteryVoltage:
        case ChargeLevel:
            return true;
        default:
            return false;
    }
}

bool TdeckmaxPower::getMetric(MetricType type, MetricData& data) {
    uint16_t u16 = 0;
    int16_t s16 = 0;
    switch (type) {
        using enum MetricType;
        case IsCharging: {
            Bq27220::BatteryStatus status;
            if (gauge->getBatteryStatus(status)) {
                data.valueAsBool = !status.reg.DSG;
                return true;
            }
            return false;
        }
        case Current:
            if (gauge->getCurrent(s16)) {
                data.valueAsInt32 = s16;
                return true;
            }
            return false;
        case BatteryVoltage:
            if (gauge->getVoltage(u16)) {
                data.valueAsUint32 = u16;
                return true;
            }
            return false;
        case ChargeLevel:
            if (gauge->getStateOfCharge(u16)) {
                data.valueAsUint8 = u16;
                return true;
            }
            return false;
        default:
            return false;
    }
}

void TdeckmaxPower::powerOff() {
    // Ship mode: force the charger's BATFET off (REG09 bit5). Mirrors the vendor
    // XPowersLib PowersSY6970::shutdown(). Note: this only fully powers the board
    // down when running on battery with USB unplugged.
    LOGGER.info("Power off (SY6970 BATFET_DIS)");
    if (i2c_controller_register8_set_bits(i2c, SY6970_ADDRESS, SY6970_REG_09, SY6970_BATFET_DIS, pdMS_TO_TICKS(50)) != ERROR_NONE) {
        LOGGER.error("Failed to write SY6970 shutdown register");
    }
}
