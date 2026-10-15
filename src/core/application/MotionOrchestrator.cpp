#include "core/application/MotionOrchestrator.hpp"
#include <algorithm>
#include <chrono>
#include <utility>

namespace fencing::application {

bool MotionOrchestrator::start(
    const domain::Drill& drill, domain::Handedness handedness,
    const ports::driven::ForPersistingRobotData& data) {
    if (state_ == domain::DrillState::Running ||
        state_ == domain::DrillState::Paused ||
        state_ == domain::DrillState::EmergencyStopped ||
        drill.motion_steps.empty()) {
        return false;
    }

    // Build temporary schedules first. If a movement cannot be resolved, the
    // currently stored schedule is left untouched.
    std::vector<ScheduledMotion> motions;
    motions.reserve(drill.motion_steps.size());

    for (const auto& step : drill.motion_steps) {
        auto movement = data.findMovement(step.movement_id);
        if (!movement) return false;

        motions.push_back({
            .timing = step,
            .plan = {.blade_movement = std::move(*movement),
                     .handedness = handedness},
        });
    }
    // Keeping each timeline ordered makes finding the next due step simple.
    std::ranges::sort(motions, {}, [](const ScheduledMotion& scheduled) {
        return scheduled.timing.start_time_seconds;
    });

    std::vector<ScheduledPositioning> positionings;
    positionings.reserve(drill.positioning_steps.size());
    for (const auto& step : drill.positioning_steps) {
        positionings.push_back({.step = step});
    }
    std::ranges::sort(positionings, {}, [](const ScheduledPositioning& scheduled) {
        return scheduled.step.start_time_seconds;
    });

    motions_ = std::move(motions);
    positionings_ = std::move(positionings);
    active_motion_index_.reset();
    active_positioning_index_.reset();
    elapsed_before_pause_ = Clock::duration::zero();
    last_resumed_at_ = Clock::now();
    state_ = domain::DrillState::Running;

    // This also starts any steps whose start time is zero.
    tick();
    return state_ == domain::DrillState::Running ||
           state_ == domain::DrillState::Completed;
}

double MotionOrchestrator::elapsedSeconds() const {
    auto elapsed = elapsed_before_pause_;
    if (state_ == domain::DrillState::Running) {
        elapsed += Clock::now() - last_resumed_at_;
    }
    return std::chrono::duration<double>(elapsed).count();
}

void MotionOrchestrator::tickMotionTimeline(double elapsed_seconds) {
    // First update the motion that is already running, if there is one.
    if (active_motion_index_) {
        auto& active = motions_[*active_motion_index_];
        switch (motion_.pollMotion()) {
            case domain::ExecutionState::Succeeded:
                active.state = StepState::Completed;
                active_motion_index_.reset();
                break;
            case domain::ExecutionState::Failed:
                state_ = domain::DrillState::Failed;
                return;
            case domain::ExecutionState::Idle:
                // Ignore an unexpected Idle result for now. The scheduled
                // motion remains active and will be polled again next tick.
                break;
            case domain::ExecutionState::Running:
                // duration_seconds is advisory scheduling information, not a
                // deadline. Allow the adapter to finish the complete motion.
                break;
        }
    }

    // The application rejects overlapping motion steps, so at most one motion is active.
    if (active_motion_index_) return;

    const auto due = std::ranges::find_if(motions_, [elapsed_seconds](const auto& scheduled) {
        return scheduled.state == StepState::Pending &&
               scheduled.timing.start_time_seconds <= elapsed_seconds;
    });
    if (due == motions_.end()) return;

    // Start the earliest overdue motion as soon as this timeline is free.
    // Because the schedule is sorted, later overdue motions will follow in
    // order and each will still run to completion.
    if (!motion_.start(due->plan)) {
        state_ = domain::DrillState::Failed;
        return;
    }

    due->state = StepState::Running;
    active_motion_index_ = static_cast<std::size_t>(due - motions_.begin());
}

void MotionOrchestrator::tickPositioningTimeline(double elapsed_seconds) {
    // First update the positioning that is already running, if there is one.
    if (active_positioning_index_) {
        auto& active = positionings_[*active_positioning_index_];
        switch (motion_.pollPositioning()) {
            case domain::ExecutionState::Succeeded:
                active.state = StepState::Completed;
                active_positioning_index_.reset();
                break;
            case domain::ExecutionState::Failed:
                state_ = domain::DrillState::Failed;
                return;
            case domain::ExecutionState::Idle:
                // Ignore an unexpected Idle result for now. The scheduled
                // positioning remains active and will be polled again next tick.
                break;
            case domain::ExecutionState::Running:
                // duration_seconds is advisory scheduling information, not a
                // deadline. Allow the adapter to finish the full positioning.
                break;
        }
    }

    // The application rejects overlapping positioning steps, so at most one positioning is active.
    if (active_positioning_index_) return;

    const auto due = std::ranges::find_if(positionings_, [elapsed_seconds](const auto& scheduled) {
        return scheduled.state == StepState::Pending &&
               scheduled.step.start_time_seconds <= elapsed_seconds;
    });
    if (due == positionings_.end()) return;

    // Start the earliest overdue positioning as soon as this timeline is free.
    // Because the schedule is sorted, later overdue positionings will follow
    // in order and each will still run to completion.
    if (!motion_.start(due->step)) {
        state_ = domain::DrillState::Failed;
        return;
    }

    due->state = StepState::Running;
    active_positioning_index_ = static_cast<std::size_t>(due - positionings_.begin());
}

bool MotionOrchestrator::timelinesComplete() const {
    const auto completed = [](const auto& scheduled) {
        return scheduled.state == StepState::Completed;
    };
    return std::ranges::all_of(motions_, completed) &&
           std::ranges::all_of(positionings_, completed);
}

void MotionOrchestrator::tick() {
    if (state_ != domain::DrillState::Running) return;

    const double elapsed_seconds = elapsedSeconds();
    tickMotionTimeline(elapsed_seconds);
    if (state_ != domain::DrillState::Running) return;

    tickPositioningTimeline(elapsed_seconds);
    if (state_ == domain::DrillState::Running && timelinesComplete()) {
        state_ = domain::DrillState::Completed;
    }
}

bool MotionOrchestrator::pause() {
    if (state_ != domain::DrillState::Running) return false;

    // An empty part of the timeline can be paused without asking an idle
    // backend to pause. Add the equivalent positioning call when its port is
    // introduced; consider rollback if one backend pauses and the other fails.
    if (active_motion_index_ && !motion_.pause()) return false;

    elapsed_before_pause_ += Clock::now() - last_resumed_at_;
    state_ = domain::DrillState::Paused;
    return true;
}

bool MotionOrchestrator::resume() {
    if (state_ != domain::DrillState::Paused) return false;

    // Add the independent positioning resume beside this call.
    if (active_motion_index_ && !motion_.resume()) return false;

    last_resumed_at_ = Clock::now();
    state_ = domain::DrillState::Running;
    return true;
}

void MotionOrchestrator::clearSchedule() {
    motions_.clear();
    positionings_.clear();
    active_motion_index_.reset();
    active_positioning_index_.reset();
    elapsed_before_pause_ = Clock::duration::zero();
}

bool MotionOrchestrator::cancel() {
    if (state_ != domain::DrillState::Running &&
        state_ != domain::DrillState::Paused) {
        return false;
    }

    if (!motion_.cancel()) return false;

    clearSchedule();
    state_ = domain::DrillState::Cancelled;
    // TODO(reqSystemRestingPosition): engineer a VERIFIED controlled-cancel
    // routine (en-garde then lower blade). A generic cancellation must never
    // trigger an unvalidated recovery movement on live hardware.
    return true;
}

void MotionOrchestrator::latchEmergencyStop() {
    // TODO: Cancel/disable both hardware channels. Software orchestration must
    // not be the robot's safety-rated emergency-stop mechanism.
    (void)motion_.cancel();
    clearSchedule();
    state_ = domain::DrillState::EmergencyStopped;
}
} // namespace fencing::application
