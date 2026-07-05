#pragma once

#include <driver/gpio.h>

/**
 *  An interrupt handler class which can trigger any given IRAM service routine
 *  for falling/rising edge level changes and oneshot level detection.
 *  This class is needed if the erratum 3.11 from ESP32 Series SoC Errata Version v2.9 applies.
 *  In short, once an edge sensitive interrupt is installed, subsequent interrupts may not work.
 *  This class uses high/low level interrupts to handle both edge detection and oneshot detection of levels
 *  which circumvents this problem.
 */
class LevelInterruptHandler {
public:
    enum class Type {
        HighOneshot,
        LowOneshot,
        PositiveEdge,
        NegativeEdge
    };

    using ServiceRoutine = void(*)(void* ctx);

private:
    enum class State {
        Low,
        High
    };

    const gpio_num_t pin;
    const Type type;
    const ServiceRoutine isr;
    void* context;
    // Must hold a valid value before gpio_isr_handler_add() runs in install():
    // that call enables the interrupt as its final step, so the ISR can fire
    // (and read this) before install() gets to run any code after it.
    State state = State::Low;

    static void handlePositiveEdge(void* isr_arg);
    static void handleNegativeEdge(void* isr_arg);
    static void handleOneshot(void* isr_arg);

public:
    LevelInterruptHandler(const gpio_num_t pin, const Type type, ServiceRoutine isr, void* context)
        : pin(pin)
        , type(type)
        , isr(isr)
        , context(context)
    {}

    virtual ~LevelInterruptHandler() {
        disarm();
    }


    void install();
    void arm();
    void disarm();
};
