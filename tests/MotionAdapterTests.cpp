#include "adapters/motion/LeRobotMotionAdapter.hpp"

#include <QCoreApplication>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return EXIT_FAILURE; \
} } while(false)

namespace {

fencing::adapters::LeRobotProcessConfig configFor(
    const std::string& mode = "normal",
    std::chrono::milliseconds heartbeat_timeout = std::chrono::milliseconds(300)) {
    return {
        .program = FENCING_TEST_PYTHON,
        .arguments = {FENCING_FAKE_BRIDGE, "--mode", mode},
        .startup_timeout = std::chrono::milliseconds(1000),
        .heartbeat_timeout = heartbeat_timeout,
        .shutdown_timeout = std::chrono::milliseconds(300),
    };
}

fencing::domain::MotionPlan testPlan() {
    return {
        .blade_movement = {.id = "cut_3",
                           .display_name = "Cut 3",
                           .duration_seconds = 1.5},
        .handedness = fencing::domain::Handedness::Right,
    };
}

template <typename Poll>
fencing::domain::ExecutionState waitForTerminal(Poll poll) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto state = poll();
        if (state == fencing::domain::ExecutionState::Succeeded ||
            state == fencing::domain::ExecutionState::Failed) {
            return state;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return fencing::domain::ExecutionState::Running;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    using namespace fencing;

    {
        adapters::LeRobotMotionAdapter adapter(configFor());
        CHECK(adapter.isReady());
        CHECK(adapter.isCalibrated());
        CHECK(adapter.start(testPlan()));
        CHECK(adapter.start(domain::PositioningStep{0.0, 0.15, 0.5}));
        CHECK(adapter.pollMotion() == domain::ExecutionState::Running);
        CHECK(adapter.pollPositioning() == domain::ExecutionState::Running);
        CHECK(adapter.pause());
        CHECK(adapter.resume());
        CHECK(adapter.commandManualAxialMovement(0.25));
        CHECK(!adapter.commandManualRotation(45.0));
        const auto motion_result = waitForTerminal([&] { return adapter.pollMotion(); });
        if (motion_result != domain::ExecutionState::Succeeded) {
            std::cerr << "motion error: " << adapter.lastError() << "\n";
        }
        CHECK(motion_result == domain::ExecutionState::Succeeded);
        CHECK(adapter.pollPositioning() == domain::ExecutionState::Running);
        CHECK(waitForTerminal([&] { return adapter.pollPositioning(); }) ==
              domain::ExecutionState::Succeeded);
        CHECK(adapter.cancel());
    }

    {
        adapters::LeRobotMotionAdapter adapter(configFor("reject-motion"));
        CHECK(adapter.start(testPlan()));
        CHECK(waitForTerminal([&] { return adapter.pollMotion(); }) ==
              domain::ExecutionState::Failed);
        CHECK(adapter.lastError() == "unknown saved move");
    }

    {
        adapters::LeRobotMotionAdapter adapter(configFor("malformed"));
        CHECK(adapter.start(testPlan()));
        CHECK(waitForTerminal([&] { return adapter.pollMotion(); }) ==
              domain::ExecutionState::Failed);
        CHECK(!adapter.isReady());
    }

    {
        adapters::LeRobotMotionAdapter adapter(configFor("exit-on-start"));
        CHECK(adapter.start(testPlan()));
        CHECK(waitForTerminal([&] { return adapter.pollMotion(); }) ==
              domain::ExecutionState::Failed);
    }

    {
        adapters::LeRobotMotionAdapter adapter(
            configFor("no-heartbeat", std::chrono::milliseconds(60)));
        CHECK(adapter.isReady());
        CHECK(adapter.start(testPlan()));
        std::this_thread::sleep_for(std::chrono::milliseconds(90));
        CHECK(adapter.pollMotion() == domain::ExecutionState::Failed);
        CHECK(!adapter.isReady());
    }

    std::cout << "PASS: LeRobot process adapter tests\n";
}
