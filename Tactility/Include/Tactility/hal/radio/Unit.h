#pragma once

#include <string>

namespace tt::hal::radio {

class Unit {
public:
    enum class Prefix {
        Femto,
        Pico,
        Nano,
        Milli,
        None,
        Kilo,
        Mega,
        Giga,
        Terra,
        Peta
    };

    enum class Name
    {
        None,
        BitsPerSecond,
        BytesPerSecond,
        Herz,
        Decibel,
        DecibelMilliwatts
    };

    const Prefix prefix;
    const Name unit;

    explicit Unit(const Prefix si, const Name unit)
        : prefix(si), unit(unit) {}
    explicit Unit(const Name unit)
        : prefix(Prefix::None), unit(unit) {}

    std::string toString() const;
};

const char* toString(const Unit::Prefix prefix);
const char* toString(const Unit::Name unit);

}
