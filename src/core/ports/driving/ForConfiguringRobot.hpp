#pragma once
#include "core/domain/Types.hpp"

namespace fencing::ports::driving {
class ForConfiguringRobot {
public:
    virtual ~ForConfiguringRobot() = default;
    virtual bool configureHandedness(domain::Handedness handedness) = 0;
    virtual bool calibrateRobot() = 0;
    virtual domain::RobotConfiguration configuration() const = 0;
};
} // namespace fencing::ports::driving
