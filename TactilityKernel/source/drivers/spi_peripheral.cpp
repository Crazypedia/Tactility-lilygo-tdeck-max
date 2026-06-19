// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/spi_peripheral.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/driver.h>
#include <tactility/module.h>
#include <tactility/log.h>

#define TAG "spi_peripheral"

#define GET_CONFIG(device) ((const struct SpiPeripheralConfig*)device->config)

extern "C" {

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    if (parent == nullptr || device_get_type(parent) != &SPI_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an SPI controller");
        return ERROR_INVALID_STATE;
    }

    auto* config = GET_CONFIG(device);
    GpioDescriptor* cs = nullptr;
    if (config->pin_cs.gpio_controller != nullptr) {
        cs = gpio_descriptor_acquire(config->pin_cs.gpio_controller, config->pin_cs.pin, GPIO_OWNER_SPI);
        if (cs == nullptr) {
            LOG_E(TAG, "Failed to acquire CS pin");
            return ERROR_RESOURCE;
        }
        gpio_descriptor_set_flags(cs, GPIO_FLAG_DIRECTION_OUTPUT);
        gpio_descriptor_set_level(cs, true);
    }

    device_set_driver_data(device, cs);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* cs = static_cast<GpioDescriptor*>(device_get_driver_data(device));
    if (cs != nullptr) {
        gpio_descriptor_release(cs);
    }
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

static GpioDescriptor* get_cs_descriptor(Device* device) {
    return static_cast<GpioDescriptor*>(device_get_driver_data(device));
}

static const SpiPeripheralApi spi_peripheral_api = {
    .get_cs_descriptor = get_cs_descriptor
};

GpioDescriptor* spi_peripheral_get_cs_descriptor(Device* device) {
    auto* driver = device_get_driver(device);
    return ((const SpiPeripheralApi*)driver->api)->get_cs_descriptor(device);
}

const DeviceType SPI_PERIPHERAL_TYPE = {
    .name = "spi_peripheral"
};

extern Module root_module;

Driver spi_peripheral_driver = {
    .name = "spi_peripheral",
    .compatible = (const char*[]) { "spi-peripheral", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &spi_peripheral_api,
    .device_type = &SPI_PERIPHERAL_TYPE,
    .owner = &root_module,
    .internal = nullptr
};

}
