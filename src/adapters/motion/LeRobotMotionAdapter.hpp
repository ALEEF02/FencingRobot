#pragma once

#include "core/ports/driven/ForExecutingRobotMotion.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace fencing::adapters {

struct LeRobotProcessConfig {
    std::string program;
    std::vector<std::string> arguments;
    std::chrono::milliseconds startup_timeout{5000};
    std::chrono::milliseconds heartbeat_timeout{3000};
    std::chrono::milliseconds shutdown_timeout{1000};
};

class LeRobotMotionAdapter final : public ports::driven::ForExecutingRobotMotion {
public:
    explicit LeRobotMotionAdapter(LeRobotProcessConfig config);
    ~LeRobotMotionAdapter() override;

    LeRobotMotionAdapter(const LeRobotMotionAdapter&) = delete;
    LeRobotMotionAdapter& operator=(const LeRobotMotionAdapter&) = delete;

    bool start(const domain::MotionPlan& plan) override;
    bool start(const domain::PositioningStep& step) override;
    domain::ExecutionState pollMotion() override;
    domain::ExecutionState pollPositioning() override;
    bool pause() override;
    bool resume() override;
    bool cancel() override;
    bool commandManualAxialMovement(double distance_m) override;
    bool commandManualRotation(double degrees) override;

    bool isReady() const;
    bool isCalibrated() const;
    const std::string& lastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> implementation_;
};

} // namespace fencing::adapters
