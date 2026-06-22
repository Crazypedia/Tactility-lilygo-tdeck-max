#include <tactility/module.h>

extern "C" {

static error_t start() {
    // Empty for now
    return ERROR_NONE;
}

static error_t stop() {
    // Empty for now
    return ERROR_NONE;
}

struct Module lilygo_tdeckmax_module = {
    .name = "lilygo-tdeckmax",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
