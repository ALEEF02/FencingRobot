#pragma once
namespace fencing::ports::driven {
class ForEnteringSafePowerState {
public:
    virtual ~ForEnteringSafePowerState() = default;
    virtual void requestEmergencyShutdown() = 0;
};
} // namespace fencing::ports::driven
