#pragma once
#include "core/domain/Types.hpp"

namespace fencing::ports::driving {
class ForAuthoringMovements {
public:
    virtual ~ForAuthoringMovements() = default;
    // Could: manually defined keyframes OR teleoperation recordings arrive here.
    virtual bool saveMovement(const domain::MovementDefinition& movement) = 0;
};
} // namespace fencing::ports::driving
