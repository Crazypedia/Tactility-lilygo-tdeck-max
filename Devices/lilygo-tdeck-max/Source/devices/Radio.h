#pragma once

#include <Tactility/hal/radio/RadioDevice.h>
#include <memory>

std::shared_ptr<tt::hal::radio::RadioDevice> createRadio();
