#pragma once

#include "core/ports/driven/ForPersistingRobotData.hpp"

#include <filesystem>

struct sqlite3;

namespace fencing::adapters {

class SqliteRobotDataAdapter final : public ports::driven::ForPersistingRobotData {
public:
    explicit SqliteRobotDataAdapter(const std::filesystem::path& database_path);
    ~SqliteRobotDataAdapter() override;

    SqliteRobotDataAdapter(const SqliteRobotDataAdapter&) = delete;
    SqliteRobotDataAdapter& operator=(const SqliteRobotDataAdapter&) = delete;

    std::vector<domain::Drill> listDrills() const override;
    std::optional<domain::Drill> findDrill(const std::string& id) const override;
    bool saveDrill(const domain::Drill& drill) override;

    std::vector<domain::MovementDefinition> listMovements() const override;
    std::optional<domain::MovementDefinition> findMovement(const std::string& id) const override;
    bool saveMovement(const domain::MovementDefinition& movement) override;

    domain::RobotConfiguration loadConfiguration() const override;
    bool saveConfiguration(const domain::RobotConfiguration& configuration) override;

private:
    void initialize();

    sqlite3* database_{};
};

} // namespace fencing::adapters
