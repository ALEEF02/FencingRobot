#include "adapters/persistence/SqliteRobotDataAdapter.hpp"

#include <sqlite3.h>

#include <cmath>
#include <stdexcept>
#include <string_view>

namespace fencing::adapters {
namespace {

class Statement {
public:
    Statement(sqlite3* database, std::string_view sql) : database_(database) {
        if (sqlite3_prepare_v2(database, sql.data(), static_cast<int>(sql.size()),
                               &statement_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(database));
        }
    }

    ~Statement() { sqlite3_finalize(statement_); }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(int index, const std::string& value) {
        check(sqlite3_bind_text(statement_, index, value.c_str(),
                                static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }
    void bind(int index, double value) {
        check(sqlite3_bind_double(statement_, index, value));
    }
    void bind(int index, int value) {
        check(sqlite3_bind_int(statement_, index, value));
    }

    bool step() {
        const int result = sqlite3_step(statement_);
        if (result == SQLITE_ROW) return true;
        if (result == SQLITE_DONE) return false;
        throw std::runtime_error(sqlite3_errmsg(database_));
    }

    void execute() {
        if (step()) throw std::runtime_error("Unexpected row returned by SQLite statement");
    }

    std::string text(int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        return value ? reinterpret_cast<const char*>(value) : std::string{};
    }
    double real(int column) const { return sqlite3_column_double(statement_, column); }
    int integer(int column) const { return sqlite3_column_int(statement_, column); }

private:
    void check(int result) const {
        if (result != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(database_));
    }

    sqlite3* database_{};
    sqlite3_stmt* statement_{};
};

void execute(sqlite3* database, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &error) == SQLITE_OK) return;
    const std::string message = error ? error : sqlite3_errmsg(database);
    sqlite3_free(error);
    throw std::runtime_error(message);
}

int schemaVersion(sqlite3* database) {
    Statement statement(database, "PRAGMA user_version");
    if (!statement.step()) throw std::runtime_error("SQLite did not return a schema version");
    return statement.integer(0);
}

bool validMovement(const domain::MovementDefinition& movement) {
    if (movement.id.empty() || movement.display_name.empty() ||
        !std::isfinite(movement.duration_seconds) || movement.duration_seconds <= 0.0) {
        return false;
    }
    double previous = -1.0;
    for (const auto& waypoint : movement.waypoints) {
        if (!std::isfinite(waypoint.seconds_from_start) ||
            waypoint.seconds_from_start < 0.0 ||
            waypoint.seconds_from_start <= previous ||
            waypoint.positions_radians.size() != movement.joint_names.size()) {
            return false;
        }
        previous = waypoint.seconds_from_start;
        for (double position : waypoint.positions_radians) {
            if (!std::isfinite(position)) return false;
        }
    }
    return true;
}

bool validDrill(const domain::Drill& drill) {
    if (drill.id.empty() || drill.name.empty()) return false;
    for (const auto& step : drill.motion_steps) {
        if (step.movement_id.empty() || !std::isfinite(step.start_time_seconds) ||
            step.start_time_seconds < 0.0) {
            return false;
        }
    }
    for (const auto& step : drill.positioning_steps) {
        if (!std::isfinite(step.start_time_seconds) || step.start_time_seconds < 0.0 ||
            !std::isfinite(step.speed_mps) || step.speed_mps == 0.0 ||
            !std::isfinite(step.duration_seconds) || step.duration_seconds <= 0.0) {
            return false;
        }
    }
    return true;
}

domain::Handedness handednessFromInteger(int value) {
    if (value == 1) return domain::Handedness::Left;
    if (value == 2) return domain::Handedness::Right;
    return domain::Handedness::Unknown;
}

int handednessToInteger(domain::Handedness value) {
    if (value == domain::Handedness::Left) return 1;
    if (value == domain::Handedness::Right) return 2;
    return 0;
}

} // namespace

SqliteRobotDataAdapter::SqliteRobotDataAdapter(
    const std::filesystem::path& database_path) {
    if (database_path != ":memory:" && !database_path.parent_path().empty()) {
        std::filesystem::create_directories(database_path.parent_path());
    }

    const std::string path = database_path.string();
    if (sqlite3_open_v2(path.c_str(), &database_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        const std::string message = database_ ? sqlite3_errmsg(database_) : "Unable to open SQLite";
        if (database_) sqlite3_close(database_);
        database_ = nullptr;
        throw std::runtime_error(message);
    }

    try {
        sqlite3_busy_timeout(database_, 2000);
        initialize();
    } catch (...) {
        sqlite3_close(database_);
        database_ = nullptr;
        throw;
    }
}

SqliteRobotDataAdapter::~SqliteRobotDataAdapter() {
    if (database_) sqlite3_close(database_);
}

void SqliteRobotDataAdapter::initialize() {
    execute(database_, "PRAGMA foreign_keys = ON");
    const int version = schemaVersion(database_);
    if (version > 1) {
        throw std::runtime_error("Database schema is newer than this application supports");
    }
    if (version == 1) return;

    execute(database_, R"sql(
        BEGIN IMMEDIATE;
        CREATE TABLE movements (
            id TEXT PRIMARY KEY,
            display_name TEXT NOT NULL,
            duration_seconds REAL NOT NULL CHECK(duration_seconds > 0),
            built_in INTEGER NOT NULL CHECK(built_in IN (0, 1))
        );
        CREATE TABLE movement_joints (
            movement_id TEXT NOT NULL REFERENCES movements(id) ON DELETE CASCADE,
            joint_index INTEGER NOT NULL,
            name TEXT NOT NULL,
            PRIMARY KEY (movement_id, joint_index)
        );
        CREATE TABLE movement_waypoints (
            movement_id TEXT NOT NULL REFERENCES movements(id) ON DELETE CASCADE,
            waypoint_index INTEGER NOT NULL,
            seconds_from_start REAL NOT NULL,
            PRIMARY KEY (movement_id, waypoint_index)
        );
        CREATE TABLE waypoint_positions (
            movement_id TEXT NOT NULL,
            waypoint_index INTEGER NOT NULL,
            joint_index INTEGER NOT NULL,
            position_radians REAL NOT NULL,
            PRIMARY KEY (movement_id, waypoint_index, joint_index),
            FOREIGN KEY (movement_id, waypoint_index)
                REFERENCES movement_waypoints(movement_id, waypoint_index) ON DELETE CASCADE
        );
        CREATE TABLE drills (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL
        );
        CREATE TABLE motion_steps (
            drill_id TEXT NOT NULL REFERENCES drills(id) ON DELETE CASCADE,
            step_index INTEGER NOT NULL,
            movement_id TEXT NOT NULL REFERENCES movements(id),
            start_time_seconds REAL NOT NULL,
            PRIMARY KEY (drill_id, step_index)
        );
        CREATE TABLE positioning_steps (
            drill_id TEXT NOT NULL REFERENCES drills(id) ON DELETE CASCADE,
            step_index INTEGER NOT NULL,
            start_time_seconds REAL NOT NULL,
            speed_mps REAL NOT NULL,
            duration_seconds REAL NOT NULL,
            PRIMARY KEY (drill_id, step_index)
        );
        CREATE TABLE robot_configuration (
            singleton INTEGER PRIMARY KEY CHECK(singleton = 1),
            handedness INTEGER NOT NULL CHECK(handedness BETWEEN 0 AND 2),
            calibrated INTEGER NOT NULL CHECK(calibrated IN (0, 1))
        );

        INSERT INTO robot_configuration VALUES (1, 0, 0);
        INSERT INTO movements VALUES
            ('parry_3', 'Parry 3', 3.0, 1),
            ('parry_4', 'Parry 4', 3.0, 1),
            ('parry_5', 'Parry 5', 3.0, 1),
            ('cut_3', 'Cut 3', 1.5, 1),
            ('cut_4', 'Cut 4', 1.5, 1),
            ('cut_5', 'Cut 5', 1.5, 1),
            ('en_garde', 'En Garde', 1.0, 1);
        INSERT INTO drills VALUES ('sample', 'Sample drill');
        INSERT INTO motion_steps VALUES
            ('sample', 0, 'cut_3', 0.0),
            ('sample', 1, 'parry_5', 1.5),
            ('sample', 2, 'en_garde', 6.0),
            ('sample', 3, 'cut_4', 10.0);
        PRAGMA user_version = 1;
        COMMIT;
    )sql");
}

std::vector<domain::MovementDefinition> SqliteRobotDataAdapter::listMovements() const {
    std::vector<domain::MovementDefinition> result;
    Statement statement(database_, "SELECT id FROM movements ORDER BY display_name, id");
    while (statement.step()) {
        auto movement = findMovement(statement.text(0));
        if (movement) result.push_back(std::move(*movement));
    }
    return result;
}

std::optional<domain::MovementDefinition> SqliteRobotDataAdapter::findMovement(
    const std::string& id) const {
    Statement movement_statement(
        database_,
        "SELECT display_name, duration_seconds, built_in FROM movements WHERE id = ?");
    movement_statement.bind(1, id);
    if (!movement_statement.step()) return std::nullopt;

    domain::MovementDefinition movement{
        .id = id,
        .display_name = movement_statement.text(0),
        .duration_seconds = movement_statement.real(1),
        .built_in = movement_statement.integer(2) != 0,
    };

    Statement joint_statement(
        database_,
        "SELECT name FROM movement_joints WHERE movement_id = ? ORDER BY joint_index");
    joint_statement.bind(1, id);
    while (joint_statement.step()) movement.joint_names.push_back(joint_statement.text(0));

    Statement waypoint_statement(
        database_,
        "SELECT waypoint_index, seconds_from_start FROM movement_waypoints "
        "WHERE movement_id = ? ORDER BY waypoint_index");
    waypoint_statement.bind(1, id);
    while (waypoint_statement.step()) {
        domain::JointWaypoint waypoint{.seconds_from_start = waypoint_statement.real(1)};
        Statement position_statement(
            database_,
            "SELECT position_radians FROM waypoint_positions "
            "WHERE movement_id = ? AND waypoint_index = ? ORDER BY joint_index");
        position_statement.bind(1, id);
        position_statement.bind(2, waypoint_statement.integer(0));
        while (position_statement.step()) {
            waypoint.positions_radians.push_back(position_statement.real(0));
        }
        movement.waypoints.push_back(std::move(waypoint));
    }
    return movement;
}

bool SqliteRobotDataAdapter::saveMovement(
    const domain::MovementDefinition& movement) {
    if (!validMovement(movement)) return false;
    try {
        execute(database_, "BEGIN IMMEDIATE");
        Statement upsert(
            database_,
            "INSERT INTO movements(id, display_name, duration_seconds, built_in) VALUES(?, ?, ?, ?) "
            "ON CONFLICT(id) DO UPDATE SET display_name=excluded.display_name, "
            "duration_seconds=excluded.duration_seconds, built_in=excluded.built_in");
        upsert.bind(1, movement.id);
        upsert.bind(2, movement.display_name);
        upsert.bind(3, movement.duration_seconds);
        upsert.bind(4, movement.built_in ? 1 : 0);
        upsert.execute();

        Statement delete_waypoints(database_,
            "DELETE FROM movement_waypoints WHERE movement_id = ?");
        delete_waypoints.bind(1, movement.id);
        delete_waypoints.execute();
        Statement delete_joints(database_,
            "DELETE FROM movement_joints WHERE movement_id = ?");
        delete_joints.bind(1, movement.id);
        delete_joints.execute();

        for (std::size_t index = 0; index < movement.joint_names.size(); ++index) {
            Statement insert(database_,
                "INSERT INTO movement_joints VALUES(?, ?, ?)");
            insert.bind(1, movement.id);
            insert.bind(2, static_cast<int>(index));
            insert.bind(3, movement.joint_names[index]);
            insert.execute();
        }
        for (std::size_t waypoint_index = 0;
             waypoint_index < movement.waypoints.size(); ++waypoint_index) {
            const auto& waypoint = movement.waypoints[waypoint_index];
            Statement insert_waypoint(database_,
                "INSERT INTO movement_waypoints VALUES(?, ?, ?)");
            insert_waypoint.bind(1, movement.id);
            insert_waypoint.bind(2, static_cast<int>(waypoint_index));
            insert_waypoint.bind(3, waypoint.seconds_from_start);
            insert_waypoint.execute();
            for (std::size_t joint_index = 0;
                 joint_index < waypoint.positions_radians.size(); ++joint_index) {
                Statement insert_position(database_,
                    "INSERT INTO waypoint_positions VALUES(?, ?, ?, ?)");
                insert_position.bind(1, movement.id);
                insert_position.bind(2, static_cast<int>(waypoint_index));
                insert_position.bind(3, static_cast<int>(joint_index));
                insert_position.bind(4, waypoint.positions_radians[joint_index]);
                insert_position.execute();
            }
        }
        execute(database_, "COMMIT");
        return true;
    } catch (...) {
        try { execute(database_, "ROLLBACK"); } catch (...) {}
        return false;
    }
}

std::vector<domain::Drill> SqliteRobotDataAdapter::listDrills() const {
    std::vector<domain::Drill> result;
    Statement statement(database_, "SELECT id FROM drills ORDER BY name, id");
    while (statement.step()) {
        auto drill = findDrill(statement.text(0));
        if (drill) result.push_back(std::move(*drill));
    }
    return result;
}

std::optional<domain::Drill> SqliteRobotDataAdapter::findDrill(
    const std::string& id) const {
    Statement drill_statement(database_, "SELECT name FROM drills WHERE id = ?");
    drill_statement.bind(1, id);
    if (!drill_statement.step()) return std::nullopt;
    domain::Drill drill{.id = id, .name = drill_statement.text(0)};

    Statement motion_statement(
        database_,
        "SELECT movement_id, start_time_seconds FROM motion_steps "
        "WHERE drill_id = ? ORDER BY step_index");
    motion_statement.bind(1, id);
    while (motion_statement.step()) {
        drill.motion_steps.push_back({motion_statement.text(0), motion_statement.real(1)});
    }

    Statement positioning_statement(
        database_,
        "SELECT start_time_seconds, speed_mps, duration_seconds FROM positioning_steps "
        "WHERE drill_id = ? ORDER BY step_index");
    positioning_statement.bind(1, id);
    while (positioning_statement.step()) {
        drill.positioning_steps.push_back({positioning_statement.real(0),
                                           positioning_statement.real(1),
                                           positioning_statement.real(2)});
    }
    return drill;
}

bool SqliteRobotDataAdapter::saveDrill(const domain::Drill& drill) {
    if (!validDrill(drill)) return false;
    try {
        execute(database_, "BEGIN IMMEDIATE");
        Statement upsert(
            database_,
            "INSERT INTO drills(id, name) VALUES(?, ?) "
            "ON CONFLICT(id) DO UPDATE SET name=excluded.name");
        upsert.bind(1, drill.id);
        upsert.bind(2, drill.name);
        upsert.execute();

        Statement delete_motion(database_, "DELETE FROM motion_steps WHERE drill_id = ?");
        delete_motion.bind(1, drill.id);
        delete_motion.execute();
        Statement delete_positioning(database_,
                                     "DELETE FROM positioning_steps WHERE drill_id = ?");
        delete_positioning.bind(1, drill.id);
        delete_positioning.execute();

        for (std::size_t index = 0; index < drill.motion_steps.size(); ++index) {
            const auto& step = drill.motion_steps[index];
            Statement insert(database_, "INSERT INTO motion_steps VALUES(?, ?, ?, ?)");
            insert.bind(1, drill.id);
            insert.bind(2, static_cast<int>(index));
            insert.bind(3, step.movement_id);
            insert.bind(4, step.start_time_seconds);
            insert.execute();
        }
        for (std::size_t index = 0; index < drill.positioning_steps.size(); ++index) {
            const auto& step = drill.positioning_steps[index];
            Statement insert(database_, "INSERT INTO positioning_steps VALUES(?, ?, ?, ?, ?)");
            insert.bind(1, drill.id);
            insert.bind(2, static_cast<int>(index));
            insert.bind(3, step.start_time_seconds);
            insert.bind(4, step.speed_mps);
            insert.bind(5, step.duration_seconds);
            insert.execute();
        }
        execute(database_, "COMMIT");
        return true;
    } catch (...) {
        try { execute(database_, "ROLLBACK"); } catch (...) {}
        return false;
    }
}

domain::RobotConfiguration SqliteRobotDataAdapter::loadConfiguration() const {
    Statement statement(
        database_,
        "SELECT handedness, calibrated FROM robot_configuration WHERE singleton = 1");
    if (!statement.step()) return {};
    return {.handedness = handednessFromInteger(statement.integer(0)),
            .calibrated = statement.integer(1) != 0};
}

bool SqliteRobotDataAdapter::saveConfiguration(
    const domain::RobotConfiguration& configuration) {
    try {
        Statement statement(
            database_,
            "UPDATE robot_configuration SET handedness = ?, calibrated = ? WHERE singleton = 1");
        statement.bind(1, handednessToInteger(configuration.handedness));
        statement.bind(2, configuration.calibrated ? 1 : 0);
        statement.execute();
        return sqlite3_changes(database_) == 1;
    } catch (...) {
        return false;
    }
}

} // namespace fencing::adapters
