#pragma once
#include "core/domain/Types.hpp"
#include "core/ports/driven/ForExecutingRobotMotion.hpp"
#include "core/ports/driven/ForPersistingRobotData.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace fencing::application {

// Internal application component. No Qt, ROS 2, LeRobot, or transport types.
class MotionOrchestrator {
public:
    explicit MotionOrchestrator(ports::driven::ForExecutingRobotMotion& motion)
        : motion_(motion) {}

    bool start(const domain::Drill& drill, domain::Handedness handedness,
               const ports::driven::ForPersistingRobotData& data);
    void tick();
    bool pause();
    bool resume();
    bool cancel();
    void latchEmergencyStop();
    domain::DrillState state() const { return state_; }

private:
    bool beginNextPlan();
    ports::driven::ForExecutingRobotMotion& motion_;
    std::vector<domain::MotionPlan> queue_;
    std::size_t next_index_{};
    domain::DrillState state_{domain::DrillState::Idle};
};
} // namespace fencing::application
