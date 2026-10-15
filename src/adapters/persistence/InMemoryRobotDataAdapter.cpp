#include "adapters/persistence/InMemoryRobotDataAdapter.hpp"
#include <algorithm>

namespace fencing::adapters {
InMemoryRobotDataAdapter::InMemoryRobotDataAdapter() {
    // Names ONLY. There are NO weapon trajectories here yet.
    for (const auto& [id, name] : {
            std::pair{"cut_3", "Cut 3"}, {"cut_4", "Cut 4"},
            {"cut_5", "Cut 5"}, {"parry_3", "Parry 3"},
            {"parry_4", "Parry 4"}, {"parry_5", "Parry 5"},
            {"en_garde", "En Garde"}}) {
        movements_.emplace(id, domain::MovementDefinition{
            .id = id, .display_name = name, .built_in = true});
    }
    domain::Drill demo{.id = "sample", .name = "Sample drill (simulation)"};
    demo.motion_steps.push_back({"cut_3", 0});
    demo.motion_steps.push_back({"parry_5", 5});
    demo.motion_steps.push_back({"en_garde", 6});
    demo.motion_steps.push_back({"cut_4", 10});
    drills_.emplace(demo.id, demo);
}
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
