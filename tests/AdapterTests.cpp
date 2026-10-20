#include "adapters/persistence/SqliteRobotDataAdapter.hpp"

#include <sqlite3.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return EXIT_FAILURE; \
} } while(false)

namespace {

bool closeEnough(double left, double right) {
    return std::abs(left - right) < 1e-9;
}

} // namespace

int main() {
    using namespace fencing;
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::current_path() /
                           ("fencing-adapter-tests-" + std::to_string(suffix));
    const auto database_path = directory / "robot.sqlite3";

    {
        adapters::SqliteRobotDataAdapter data(database_path);
        CHECK(data.listMovements().size() == 7);
        const auto sample = data.findDrill("sample");
        CHECK(sample && sample->motion_steps.size() == 4);

        domain::MovementDefinition movement{
            .id = "custom_move",
            .display_name = "Custom move",
            .joint_names = {"shoulder", "elbow"},
            .waypoints = {{0.1, {0.2, 0.3}}, {0.8, {0.5, 0.7}}},
            .duration_seconds = 1.0,
        };
        CHECK(data.saveMovement(movement));
        const auto loaded_movement = data.findMovement("custom_move");
        CHECK(loaded_movement);
        CHECK(loaded_movement->joint_names == movement.joint_names);
        CHECK(loaded_movement->waypoints.size() == 2);
        CHECK(closeEnough(loaded_movement->waypoints[1].positions_radians[1], 0.7));

        movement.waypoints = {{0.25, {0.4, 0.6}}};
        CHECK(data.saveMovement(movement));
        CHECK(data.findMovement("custom_move")->waypoints.size() == 1);

        const domain::Drill drill{
            .id = "concurrent",
            .name = "Concurrent lanes",
            .motion_steps = {{"custom_move", 0.0}, {"cut_3", 2.0}},
            .positioning_steps = {{0.0, 0.15, 1.5}, {3.0, -0.1, 2.0}},
        };
        CHECK(data.saveDrill(drill));
        const auto loaded_drill = data.findDrill("concurrent");
        CHECK(loaded_drill && loaded_drill->motion_steps.size() == 2);
        CHECK(loaded_drill->positioning_steps.size() == 2);
        CHECK(closeEnough(loaded_drill->positioning_steps[1].speed_mps, -0.1));

        auto replacement = drill;
        replacement.name = "Replacement schedule";
        replacement.motion_steps = {{"custom_move", 0.5}};
        replacement.positioning_steps = {{0.25, 0.2, 4.0}};
        CHECK(data.saveDrill(replacement));
        const auto replaced_drill = data.findDrill("concurrent");
        CHECK(replaced_drill && replaced_drill->motion_steps.size() == 1);
        CHECK(replaced_drill->positioning_steps.size() == 1);
        CHECK(closeEnough(replaced_drill->motion_steps[0].start_time_seconds, 0.5));

        auto invalid_update = replacement;
        invalid_update.name = "Should roll back";
        invalid_update.motion_steps = {{"missing_move", 0.0}};
        CHECK(!data.saveDrill(invalid_update));
        CHECK(data.findDrill("concurrent")->name == "Replacement schedule");

        CHECK(data.saveConfiguration({.handedness = domain::Handedness::Left,
                                      .calibrated = true}));
    }

    {
        adapters::SqliteRobotDataAdapter reopened(database_path);
        CHECK(reopened.findMovement("custom_move")->waypoints.size() == 1);
        CHECK(reopened.findDrill("concurrent")->positioning_steps.size() == 1);
        const auto configuration = reopened.loadConfiguration();
        CHECK(configuration.handedness == domain::Handedness::Left);
        CHECK(configuration.calibrated);
        CHECK(reopened.listMovements().size() == 8);
    }

    const auto future_path = directory / "future.sqlite3";
    sqlite3* future_database = nullptr;
    CHECK(sqlite3_open(future_path.string().c_str(), &future_database) == SQLITE_OK);
    CHECK(sqlite3_exec(future_database, "PRAGMA user_version = 99", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(future_database);
    bool rejected_future = false;
    try {
        adapters::SqliteRobotDataAdapter future(future_path);
    } catch (const std::runtime_error&) {
        rejected_future = true;
    }
    CHECK(rejected_future);

    std::filesystem::remove_all(directory);
    std::cout << "PASS: SQLite adapter tests\n";
}
