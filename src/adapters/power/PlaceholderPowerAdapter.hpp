#pragma once
#include "core/ports/driven/ForReadingPowerState.hpp"
namespace fencing::adapters {
class DemoPowerAdapter final : public ports::driven::ForReadingPowerState {
public:
    domain::PowerSnapshot readPower() const override {
        return {.valid = true, .charge_percent = 87.0,
                .available_energy_joules = 1000.0, .charging = false,
                .fully_charged = false}; // Fake numbers only.
    }
};
} // namespace fencing::adapters
