#pragma once
#include "core/domain/Types.hpp"

namespace fencing::ports::driven {
class ForReadingPowerState {
public:
    virtual ~ForReadingPowerState() = default;
    virtual domain::PowerSnapshot readPower() const = 0;
};
} // namespace fencing::ports::driven
