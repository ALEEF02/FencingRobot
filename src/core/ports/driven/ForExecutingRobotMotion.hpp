#pragma once
#include "core/domain/Types.hpp"

namespace fencing::ports::driven {
class ForExecutingRobotMotion {
public:
    virtual ~ForExecutingRobotMotion() = default;
    // Nonblocking start. poll() reports progression; implementation owns timing.
    virtual bool start(const domain::MotionPlan& plan) = 0;
    virtual domain::ExecutionState poll() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual bool cancel() = 0;
    virtual bool commandManualAxialSpeed(double speed_mps) = 0;
    virtual bool rotateWhileIdle(double degrees) = 0;
};
} // namespace fencing::ports::driven
