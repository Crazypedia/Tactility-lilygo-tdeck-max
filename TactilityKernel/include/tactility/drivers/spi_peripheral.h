// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SpiPeripheralConfig {
    struct GpioPinSpec pin_cs;
};

struct SpiPeripheralApi {
    struct GpioDescriptor* (*get_cs_descriptor)(struct Device* device);
};

struct GpioDescriptor* spi_peripheral_get_cs_descriptor(struct Device* device);

extern const struct DeviceType SPI_PERIPHERAL_TYPE;

#ifdef __cplusplus
}
#endif
