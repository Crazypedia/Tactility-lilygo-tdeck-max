#include "Tactility/hal/radio/Unit.h"
#include <cstring>

namespace tt::hal::radio {

std::string Unit::toString() const {
    using tt::hal::radio::toString;
    return std::string(toString(prefix))+std::string(toString(unit));
}

const char* toString(const Unit::Prefix prefix) {
    using enum Unit::Prefix;

    switch (prefix) {
        case Femto:
            return "f";
        case Pico:
            return "p";
        case Nano:
            return "n";
        case Milli:
            return "m";
        case None:
            return "";
        case Kilo:
            return "k";
        case Mega:
            return "M";
        case Giga:
            return "G";
        case Terra:
            return "T";
        case Peta:
            return "P";
    }

    return "?";
}

const char* toString(const Unit::Name unit) {
    using enum Unit::Name;

    switch (unit) {
        case None:
            return "";
        case BitsPerSecond:
            return "bps";
        case BytesPerSecond:
            return "Bps";
        case Herz:
            return "Hz";
        case Decibel:
            return "dB";
        case DecibelMilliwatts:
            return "dBm";
    }

    return "?";
}

}
