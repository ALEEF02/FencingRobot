#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fencing::domain {

enum class Handedness { Unknown, Left, Right };
enum class DrillState { Idle, Running, Paused, Completed, Cancelled, Failed, EmergencyStopped };
enum class ExecutionState { Idle, Running, Succeeded, Failed };

inline const char* toString(DrillState state) {
    switch (state) {
        case DrillState::Idle: return "Idle";
        case DrillState::Running: return "Running";
        case DrillState::Paused: return "Paused";
        case DrillState::Completed: return "Completed";
        case DrillState::Cancelled: return "Cancelled";
        case DrillState::Failed: return "Failed";
        case DrillState::EmergencyStopped: return "EmergencyStopped";
    }
    return "Unknown";
}

struct JointWaypoint {
    double seconds_from_start{};
    // Domain-unit convention: joint positions in radians. No ROS/LeRobot types.
    std::vector<double> positions_radians;
};

struct MovementDefinition {
    std::string id;
    std::string display_name;
    std::vector<std::string> joint_names;
    std::vector<JointWaypoint> waypoints;
    double duration_seconds;
    bool built_in{false};
};

struct PositioningStep {
    double start_time_seconds{};
    double speed_mps{};
    double duration_seconds{};
};

struct MotionStep {
    std::string movement_id;
    double start_time_seconds{};
};

struct Drill {
    std::string id;
    std::string name;
    std::vector<MotionStep> motion_steps;
    std::vector<PositioningStep> positioning_steps;
};

struct MotionPlan {
    MovementDefinition blade_movement;
    Handedness handedness{Handedness::Unknown};
};

struct RobotConfiguration {
    Handedness handedness{Handedness::Unknown};
    bool calibrated{false};
};

struct PowerSnapshot {
    bool valid{false};
    double charge_percent{};
    double available_energy_joules{};
    bool charging{false};
    bool fully_charged{false};
};

struct HealthSnapshot {
    bool healthy{false};
    std::vector<std::string> faults;
};

struct RobotStatus {
    DrillState drill_state{DrillState::Idle};
    PowerSnapshot power;
    HealthSnapshot health;
    RobotConfiguration configuration;
    std::string last_error;
};

} // namespace fencing::domain
