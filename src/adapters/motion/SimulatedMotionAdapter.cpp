#include "adapters/motion/SimulatedMotionAdapter.hpp"

namespace fencing::adapters {
bool SimulatedMotionAdapter::start(const domain::MotionPlan& plan) {
    if (state_ == domain::ExecutionState::Running || paused_) return false;
    started_.push_back(plan.blade_movement.id);
    state_ = domain::ExecutionState::Running;
    began_ = std::chrono::steady_clock::now();
    elapsed_before_pause_ = {};
    return true;
}
domain::ExecutionState SimulatedMotionAdapter::poll() {
    if (state_ != domain::ExecutionState::Running || paused_)
        return state_;
    // Deliberately artificial 40ms movement duration for demo/tests.
    if (elapsed_before_pause_ + (std::chrono::steady_clock::now() - began_)
        >= std::chrono::milliseconds(40)) {
        state_ = domain::ExecutionState::Succeeded;
    }
    return state_;
}
bool SimulatedMotionAdapter::pause() {
    if (state_ != domain::ExecutionState::Running || paused_) return false;
    elapsed_before_pause_ += std::chrono::steady_clock::now() - began_;
    paused_ = true;
    return true;
}
bool SimulatedMotionAdapter::resume() {
    if (state_ != domain::ExecutionState::Running || !paused_) return false;
    began_ = std::chrono::steady_clock::now();
    paused_ = false;
    return true;
}
bool SimulatedMotionAdapter::cancel() {
    state_ = domain::ExecutionState::Idle;
    paused_ = false;
    return true;
}
} // namespace fencing::adapters
