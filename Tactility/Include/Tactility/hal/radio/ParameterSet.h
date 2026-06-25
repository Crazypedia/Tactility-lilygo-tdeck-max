#pragma once

#include "RadioDevice.h"

#include <map>

namespace tt::hal::radio {

class ParameterSet {
private:
    struct ParameterHash
    {
        std::size_t operator()(RadioDevice::Parameter t) const
        {
            return static_cast<std::size_t>(t);
        }
    };

    using Map = std::unordered_map<RadioDevice::Parameter, float, ParameterHash>;
    Map parameters;

public:
    explicit ParameterSet() {}
    explicit ParameterSet(const ParameterSet& other) { parameters = other.parameters; }
    ~ParameterSet() = default;

    float get(const RadioDevice::Parameter parameter) { return parameters[parameter]; }
    void set(const RadioDevice::Parameter parameter, const float value) { parameters[parameter] = value; }
    bool has(const RadioDevice::Parameter parameter) { return parameters.contains(parameter); }
    bool erase(const RadioDevice::Parameter parameter) {
        if (has(parameter)) {
            parameters.erase(parameter);
            return true;
        }
        return false;
    }
    void clear() { parameters.clear(); }

    bool apply(RadioDevice &radio) {
        bool successful = true;
        for (const auto& [parameter, value] : parameters) {
            // No break on error chosen to apply all parameters,
            // a bad one doesn't make the successive tries any more invalid
            successful &= (radio.setParameter(parameter, value) == RadioDevice::ParameterStatus::Success);
        }
        return successful;
    }

    void load(const RadioDevice &radio) {
        // This loop has to be ajusted for each new parameter.
        // Could be made more maintainable with a template enum iterator in an utility header.
        for (RadioDevice::Parameter p = RadioDevice::Parameter::Power;
             p < RadioDevice::Parameter::NarrowGrid;
             p = static_cast<RadioDevice::Parameter>((size_t)p + 1)) {
            float value = 0.0;
            if (radio.getParameter(p, value) == RadioDevice::ParameterStatus::Success) {
                set(p, value);
            }
        }
    }
};

}
