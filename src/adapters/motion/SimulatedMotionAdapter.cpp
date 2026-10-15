#include "adapters/motion/SimulatedMotionAdapter.hpp"

namespace fencing::adapters {
bool SimulatedMotionAdapter::start(const domain::MotionPlan& plan) {
    if (motion_.state == domain::ExecutionState::Running || paused_) return false;
    started_.push_back(plan.blade_movement.id);
    motion_.state = domain::ExecutionState::Running;
    motion_.began = std::chrono::steady_clock::now();
    motion_.elapsed_before_pause = {};
    return true;
}

bool SimulatedMotionAdapter::start(const domain::PositioningStep&) {
    if (positioning_.state == domain::ExecutionState::Running || paused_) return false;
    positioning_.state = domain::ExecutionState::Running;
    positioning_.began = std::chrono::steady_clock::now();
    positioning_.elapsed_before_pause = {};
    return true;
}

domain::ExecutionState SimulatedMotionAdapter::poll(Channel& channel) {
    if (channel.state != domain::ExecutionState::Running || paused_)
        return channel.state;
    // Deliberately artificial 40ms movement duration for demo/tests.
    if (channel.elapsed_before_pause + (std::chrono::steady_clock::now() - channel.began)
        >= std::chrono::milliseconds(40)) {
        channel.state = domain::ExecutionState::Succeeded;
    }
    return channel.state;
}

domain::ExecutionState SimulatedMotionAdapter::pollMotion() { return poll(motion_); }
domain::ExecutionState SimulatedMotionAdapter::pollPositioning() { return poll(positioning_); }

bool SimulatedMotionAdapter::pause() {
    const bool anything_running = motion_.state == domain::ExecutionState::Running ||
                                  positioning_.state == domain::ExecutionState::Running;
    if (!anything_running || paused_) return false;
    const auto now = std::chrono::steady_clock::now();
    if (motion_.state == domain::ExecutionState::Running)
        motion_.elapsed_before_pause += now - motion_.began;
    if (positioning_.state == domain::ExecutionState::Running)
        positioning_.elapsed_before_pause += now - positioning_.began;
    paused_ = true;
    return true;
}
bool SimulatedMotionAdapter::resume() {
    const bool anything_running = motion_.state == domain::ExecutionState::Running ||
                                  positioning_.state == domain::ExecutionState::Running;
    if (!anything_running || !paused_) return false;
    const auto now = std::chrono::steady_clock::now();
    if (motion_.state == domain::ExecutionState::Running) motion_.began = now;
    if (positioning_.state == domain::ExecutionState::Running) positioning_.began = now;
    paused_ = false;
    return true;
}
bool SimulatedMotionAdapter::cancel() {
    motion_.state = domain::ExecutionState::Idle;
    positioning_.state = domain::ExecutionState::Idle;
    paused_ = false;
    return true;
}
} // namespace fencing::adapters
