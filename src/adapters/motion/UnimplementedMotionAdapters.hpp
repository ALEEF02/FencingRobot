#pragma once
#include "core/ports/driven/ForExecutingRobotMotion.hpp"

namespace fencing::adapters {
// Safe-to-compile outlines: no motion commands are ever sent.
// TODO: implement an out-of-process protocol to the Python LeRobot service.
// The Python service must acknowledge execution state and enforce watchdogs.
class LeRobotMotionAdapter final : public ports::driven::ForExecutingRobotMotion {
public:
    bool start(const domain::MotionPlan&) override { return false; }
    bool start(const domain::PositioningStep&) override { return false; }
    domain::ExecutionState pollMotion() override { return domain::ExecutionState::Failed; }
    domain::ExecutionState pollPositioning() override { return domain::ExecutionState::Failed; }
    bool pause() override { return false; }
    bool resume() override { return false; }
    bool cancel() override { return false; }
    bool commandManualAxialMovement(double) override { return false; }
    bool commandManualRotation(double) override { return false; }
};

// TODO: implement ROS 2 adapter (rclcpp actions/services/controllers).
// Keep ROS 2 headers/messages out of src/core entirely.
class ROS2MotionAdapter final : public ports::driven::ForExecutingRobotMotion {
public:
    bool start(const domain::MotionPlan&) override { return false; }
    bool start(const domain::PositioningStep&) override { return false; }
    domain::ExecutionState pollMotion() override { return domain::ExecutionState::Failed; }
    domain::ExecutionState pollPositioning() override { return domain::ExecutionState::Failed; }
    bool pause() override { return false; }
    bool resume() override { return false; }
    bool cancel() override { return false; }
    bool commandManualAxialMovement(double) override { return false; }
    bool commandManualRotation(double) override { return false; }
};
} // namespace fencing::adapters
