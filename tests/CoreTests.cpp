#include "adapters/calibration/PlaceholderCalibrationAdapters.hpp"
#include "adapters/health/PlaceholderHealthAdapter.hpp"
#include "adapters/motion/SimulatedMotionAdapter.hpp"
#include "adapters/persistence/InMemoryRobotDataAdapter.hpp"
#include "adapters/power/PlaceholderPowerAdapter.hpp"
#include "adapters/safety/PlaceholderSafePowerAdapter.hpp"
#include "core/application/FencingRobotApplication.hpp"
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <thread>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return EXIT_FAILURE; \
} } while(false)

int main() {
    using namespace fencing;
    adapters::SimulatedMotionAdapter motion;
    adapters::SimulatedCalibrationAdapter calibration;
    adapters::InMemoryRobotDataAdapter data;
    adapters::DemoPowerAdapter power;
    adapters::DemoHealthAdapter health;
    adapters::DemoSafePowerAdapter safe;
    application::FencingRobotApplication app(motion, calibration, data, power, health, safe);

    CHECK(!app.startDrill("sample") && "Unknown handedness/uncalibrated: fail closed");
    CHECK(app.configureHandedness(domain::Handedness::Right));
    CHECK(app.calibrateRobot());
    CHECK(app.startDrill("sample"));
    CHECK(app.drillState() == domain::DrillState::Running);
    CHECK(!app.requestManualAxialMovement(0.1) && "Manual overrides blocked during drill");

    for (int i = 0; i < 300 && app.drillState() == domain::DrillState::Running; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        app.tick();
    }
    CHECK(app.drillState() == domain::DrillState::Completed);
    // cut_3, guard, parry_5, guard, cut_4, guard, lower blade
    const std::vector<std::string> expected{
        "cut_3", "en_guard", "parry_5", "en_guard", "cut_4", "en_guard", "lower_blade"};
    CHECK(motion.startedMovements() == expected);
    CHECK(app.requestManualRotation(360.0));
    CHECK(!app.saveMovement({.id="user_move", .display_name="Custom"}));

    CHECK(app.restartDrill());
    app.requestEmergencyStop();
    CHECK(safe.requested());
    CHECK(app.drillState() == domain::DrillState::EmergencyStopped);
    CHECK(!app.startDrill("sample") && "Emergency stop remains latched");
    std::cout << "PASS: simulation-only architecture smoke tests\n";
}
