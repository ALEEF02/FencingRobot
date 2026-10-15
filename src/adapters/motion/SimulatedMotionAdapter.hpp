#pragma once
#include "core/ports/driven/ForExecutingRobotMotion.hpp"
#include <chrono>
#include <string>
#include <vector>

namespace fencing::adapters {
// DEMO/TEST ONLY. No GPIO, actuator, ROS 2, or LeRobot interaction.
class SimulatedMotionAdapter final : public ports::driven::ForExecutingRobotMotion {
public:
    bool start(const domain::MotionPlan& plan) override;
    domain::ExecutionState poll() override;
    bool pause() override;
    bool resume() override;
    bool cancel() override;
    bool commandManualAxialMovement(double) override { return true; }
    bool commandManualRotation(double) override { return true; }
    const std::vector<std::string>& startedMovements() const { return started_; }
private:
    domain::ExecutionState state_{domain::ExecutionState::Idle};
    std::chrono::steady_clock::time_point began_{};
    std::chrono::steady_clock::duration elapsed_before_pause_{};
    bool paused_{false};
    std::vector<std::string> started_;
};
} // namespace fencing::adapters
