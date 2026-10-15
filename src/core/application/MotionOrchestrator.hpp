#pragma once
#include "core/domain/Types.hpp"
#include "core/ports/driven/ForExecutingRobotMotion.hpp"
#include "core/ports/driven/ForPersistingRobotData.hpp"
#include <chrono>
#include <cstddef>
#include <optional>
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
    // Each timeline is its own small state machine. A motion may therefore be
    // Running at the same time as a positioning step.
    enum class StepState { Pending, Running, Completed };

    struct ScheduledMotion {
        domain::MotionStep timing;
        domain::MotionPlan plan;
        StepState state{StepState::Pending};
    };

    struct ScheduledPositioning {
        domain::PositioningStep step;
        StepState state{StepState::Pending};
    };

    using Clock = std::chrono::steady_clock;

    double elapsedSeconds() const;
    void tickMotionTimeline(double elapsed_seconds);
    void tickPositioningTimeline(double elapsed_seconds);
    bool timelinesComplete() const;
    void clearSchedule();

    ports::driven::ForExecutingRobotMotion& motion_;
    std::vector<ScheduledMotion> motions_;
    std::vector<ScheduledPositioning> positionings_;
    std::optional<std::size_t> active_motion_index_;
    std::optional<std::size_t> active_positioning_index_;
    Clock::time_point last_resumed_at_{};
    Clock::duration elapsed_before_pause_{};
    domain::DrillState state_{domain::DrillState::Idle};
};
} // namespace fencing::application
