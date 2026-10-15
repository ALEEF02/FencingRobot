#pragma once
#include "core/domain/Types.hpp"

namespace fencing::ports::driven {
class ForReadingHardwareHealth {
public:
    virtual ~ForReadingHardwareHealth() = default;
    virtual domain::HealthSnapshot readHealth() const = 0;
};
} // namespace fencing::ports::driven
