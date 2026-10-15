#pragma once
#include "core/ports/driven/ForPersistingRobotData.hpp"
#include <unordered_map>

namespace fencing::adapters {
// A deliberate ephemeral stand-in for SQLite; NOT persistent across restarts.
class InMemoryRobotDataAdapter final : public ports::driven::ForPersistingRobotData {
public:
    InMemoryRobotDataAdapter();
    std::vector<domain::Drill> listDrills() const override;
    std::optional<domain::Drill> findDrill(const std::string& id) const override;
    bool saveDrill(const domain::Drill& drill) override;
    std::optional<domain::MovementDefinition> findMovement(const std::string& id) const override;
    bool saveMovement(const domain::MovementDefinition& movement) override;
    domain::RobotConfiguration loadConfiguration() const override;
    bool saveConfiguration(const domain::RobotConfiguration& configuration) override;
private:
    std::unordered_map<std::string, domain::Drill> drills_;
    std::unordered_map<std::string, domain::MovementDefinition> movements_;
    domain::RobotConfiguration configuration_;
};
} // namespace fencing::adapters
