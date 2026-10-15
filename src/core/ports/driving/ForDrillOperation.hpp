#pragma once
#include "core/domain/Types.hpp"

namespace fencing::ports::driving {
class ForDrillOperation {
public:
    virtual ~ForDrillOperation() = default;
    virtual domain::DrillState drillState() const = 0;
    virtual bool startDrill(const std::string& drill_id) = 0;
    virtual bool cancelDrill() = 0;
    virtual bool pauseDrill() = 0;
    virtual bool resumeDrill() = 0;
    virtual bool restartDrill() = 0;
    virtual bool requestManualAxialSpeed(double speed_mps) = 0;
    virtual bool requestIdleRotation(double degrees) = 0;
    virtual void requestEmergencyStop() = 0;
};
} // namespace fencing::ports::driving
