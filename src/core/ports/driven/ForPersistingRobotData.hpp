#pragma once
#include "core/domain/Types.hpp"
#include <optional>
#include <string>
#include <vector>

namespace fencing::ports::driven {
class ForPersistingRobotData {
public:
    virtual ~ForPersistingRobotData() = default;
    virtual std::vector<domain::Drill> listDrills() const = 0;
    virtual std::optional<domain::Drill> findDrill(const std::string& id) const = 0;
    virtual bool saveDrill(const domain::Drill& drill) = 0;
    virtual std::optional<domain::MovementDefinition> findMovement(const std::string& id) const = 0;
    virtual bool saveMovement(const domain::MovementDefinition& movement) = 0;
    virtual domain::RobotConfiguration loadConfiguration() const = 0;
    virtual bool saveConfiguration(const domain::RobotConfiguration& configuration) = 0;
};
} // namespace fencing::ports::driven
