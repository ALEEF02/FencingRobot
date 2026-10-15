#include "core/application/MotionOrchestrator.hpp"
#include <utility>

namespace fencing::application {
namespace {
// Transitional movement names are DOMAIN terms; real trajectories must be provided.
domain::MovementDefinition named(const char* id) {
    return {.id = id, .display_name = id, .built_in = true};
}
} // namespace

bool MotionOrchestrator::start(
    const domain::Drill& drill, domain::Handedness handedness,
    const ports::driven::ForPersistingRobotData& data) {
    if (state_ == domain::DrillState::Running ||
        state_ == domain::DrillState::Paused ||
        state_ == domain::DrillState::EmergencyStopped || drill.steps.empty()) return false;

    std::vector<domain::MotionPlan> proposed;
    // Requirement: default en-garde transitions between every fencing movement.
    for (std::size_t i = 0; i < drill.steps.size(); ++i) {
        const auto& step = drill.steps[i];
        auto movement = data.findMovement(step.movement_id);
        if (!movement) return false;
        proposed.push_back({*movement, handedness, step.axial});
        if (i + 1 < drill.steps.size()) {
            proposed.push_back({named("en_guard"), handedness, std::nullopt});
        }
    }
    // Normal completion: return to guard, then lower blade.
    proposed.push_back({named("en_guard"), handedness, std::nullopt});
    proposed.push_back({named("lower_blade"), handedness, std::nullopt});

    queue_ = std::move(proposed);
    next_index_ = 0;
    state_ = domain::DrillState::Running;
    return beginNextPlan();
}

bool MotionOrchestrator::beginNextPlan() {
    if (next_index_ >= queue_.size()) {
        state_ = domain::DrillState::Completed;
        return true;
    }
    if (!motion_.start(queue_[next_index_])) {
        state_ = domain::DrillState::Failed;
        return false;
    }
    ++next_index_;
    return true;
}

void MotionOrchestrator::tick() {
    if (state_ != domain::DrillState::Running) return;
    switch (motion_.poll()) {
        case domain::ExecutionState::Succeeded: (void)beginNextPlan(); break;
        case domain::ExecutionState::Failed: state_ = domain::DrillState::Failed; break;
        default: break;
    }
}

bool MotionOrchestrator::pause() {
    if (state_ != domain::DrillState::Running || !motion_.pause()) return false;
    state_ = domain::DrillState::Paused;
    return true;
}
bool MotionOrchestrator::resume() {
    if (state_ != domain::DrillState::Paused || !motion_.resume()) return false;
    state_ = domain::DrillState::Running;
    return true;
}
bool MotionOrchestrator::cancel() {
    if (state_ != domain::DrillState::Running && state_ != domain::DrillState::Paused)
        return false;
    if (!motion_.cancel()) return false;
    state_ = domain::DrillState::Cancelled;
    queue_.clear();
    // TODO(reqSystemRestingPosition): engineer a VERIFIED controlled-cancel
    // routine (en-garde then lower blade). A generic cancellation must never
    // trigger an unvalidated recovery movement on live hardware.
    return true;
}
void MotionOrchestrator::latchEmergencyStop() {
    (void)motion_.cancel();
    queue_.clear();
    state_ = domain::DrillState::EmergencyStopped;
}
} // namespace fencing::application
