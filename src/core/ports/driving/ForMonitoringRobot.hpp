#pragma once
#include "core/domain/Types.hpp"

namespace fencing::ports::driving {
class ForMonitoringRobot {
public:
    virtual ~ForMonitoringRobot() = default;
    virtual domain::RobotStatus checkRobotStatus() const = 0;
};
} // namespace fencing::ports::driving
