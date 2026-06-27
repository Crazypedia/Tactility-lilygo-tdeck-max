#pragma once

#include <Tactility/hal/sdcard/SdCardDevice.h>
#include <memory>

std::shared_ptr<tt::hal::sdcard::SdCardDevice> createSdCard();
