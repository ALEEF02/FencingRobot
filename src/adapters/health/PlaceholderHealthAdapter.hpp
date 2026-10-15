#pragma once
#include "core/ports/driven/ForReadingHardwareHealth.hpp"
namespace fencing::adapters {
class DemoHealthAdapter final : public ports::driven::ForReadingHardwareHealth {
public:
    domain::HealthSnapshot readHealth() const override { return {.healthy = true}; }
};
} // namespace fencing::adapters
