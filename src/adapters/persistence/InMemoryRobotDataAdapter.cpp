#include "adapters/persistence/InMemoryRobotDataAdapter.hpp"
#include <algorithm>

namespace fencing::adapters {
InMemoryRobotDataAdapter::InMemoryRobotDataAdapter() = default;
std::vector<domain::Drill> InMemoryRobotDataAdapter::listDrills() const {
    std::vector<domain::Drill> result;
    for (const auto& [id, drill] : drills_) result.push_back(drill);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    return result;
}
std::optional<domain::Drill> InMemoryRobotDataAdapter::findDrill(const std::string& id) const {
    if (const auto it = drills_.find(id); it != drills_.end()) return it->second;
    return std::nullopt;
}
bool InMemoryRobotDataAdapter::saveDrill(const domain::Drill& drill) {
    drills_[drill.id] = drill;
    return true;
}
std::vector<domain::MovementDefinition> InMemoryRobotDataAdapter::listMovements() const {
    std::vector<domain::MovementDefinition> result;
    result.reserve(movements_.size());
    for (const auto& [id, movement] : movements_) result.push_back(movement);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.display_name < b.display_name;
    });
    return result;
}
std::optional<domain::MovementDefinition> InMemoryRobotDataAdapter::findMovement(const std::string& id) const {
    if (const auto it = movements_.find(id); it != movements_.end()) return it->second;
    return std::nullopt;
}
bool InMemoryRobotDataAdapter::saveMovement(const domain::MovementDefinition& movement) {
    movements_[movement.id] = movement;
    return true;
}
domain::RobotConfiguration InMemoryRobotDataAdapter::loadConfiguration() const {
    return configuration_;
}
bool InMemoryRobotDataAdapter::saveConfiguration(const domain::RobotConfiguration& configuration) {
    configuration_ = configuration;
    return true;
}
} // namespace fencing::adapters
