#include "LevelInterruptHandler.h"

#include <esp_attr.h>

void IRAM_ATTR LevelInterruptHandler::handlePositiveEdge(void* isr_arg) {
    LevelInterruptHandler* self = static_cast<LevelInterruptHandler*>(isr_arg);
    switch (self->state) {
        case State::Low:
            gpio_set_intr_type(self->pin, GPIO_INTR_HIGH_LEVEL);
            self->state = State::High;
            break;
        case State::High:
            gpio_set_intr_type(self->pin, GPIO_INTR_LOW_LEVEL);
            self->isr(self->context);
            self->state = State::Low;
            break;
    }
}

void IRAM_ATTR LevelInterruptHandler::handleNegativeEdge(void* isr_arg) {
    LevelInterruptHandler* self = static_cast<LevelInterruptHandler*>(isr_arg);
    switch (self->state) {
        case State::High:
            gpio_set_intr_type(self->pin, GPIO_INTR_LOW_LEVEL);
            self->state = State::Low;
            break;
        case State::Low:
            gpio_set_intr_type(self->pin, GPIO_INTR_HIGH_LEVEL);
            self->isr(self->context);
            self->state = State::High;
                break;
    }
}

void IRAM_ATTR LevelInterruptHandler::handleOneshot(void* isr_arg) {
    LevelInterruptHandler* self = static_cast<LevelInterruptHandler*>(isr_arg);
    gpio_intr_disable(self->pin);
    self->isr(self->context);
}

void LevelInterruptHandler::install() {
    switch (type) {
        case Type::HighOneshot:
            gpio_set_intr_type(pin, GPIO_INTR_HIGH_LEVEL);
            gpio_isr_handler_add(pin, handleOneshot, this);
            break;
        case Type::LowOneshot:
            gpio_set_intr_type(pin, GPIO_INTR_LOW_LEVEL);
            gpio_isr_handler_add(pin, handleOneshot, this);
            break;
        case Type::PositiveEdge:
            // State must be set before gpio_isr_handler_add(): it enables the
            // interrupt as its final step, and a low idle line fires the ISR
            // immediately. With a stale state the switch in the handler matches
            // nothing, the level type never flips, and the ISR storms until the
            // interrupt watchdog panics.
            state = State::Low;
            gpio_set_intr_type(pin, GPIO_INTR_LOW_LEVEL);
            gpio_isr_handler_add(pin, handlePositiveEdge, this);
            break;
        case Type::NegativeEdge:
            state = State::High;
            gpio_set_intr_type(pin, GPIO_INTR_HIGH_LEVEL);
            gpio_isr_handler_add(pin, handleNegativeEdge, this);
            break;
    }
}

void LevelInterruptHandler::arm() {
    gpio_intr_enable(pin);
}

void LevelInterruptHandler::disarm() {
    gpio_intr_disable(pin);
}
