#pragma once
#include "core/domain/Types.hpp"
#include <string>
#include <vector>

namespace fencing::ports::driving {
class ForManagingDrills {
public:
    virtual ~ForManagingDrills() = default;
    virtual std::vector<domain::Drill> listDrills() const = 0;
    virtual std::vector<domain::MovementDefinition> listMovements() const = 0;
    virtual bool saveDrill(const domain::Drill& drill) = 0;
};
} // namespace fencing::ports::driving
