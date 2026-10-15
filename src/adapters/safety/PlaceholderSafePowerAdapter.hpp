#pragma once
#include "core/ports/driven/ForEnteringSafePowerState.hpp"
namespace fencing::adapters {
class DemoSafePowerAdapter final : public ports::driven::ForEnteringSafePowerState {
public:
    void requestEmergencyShutdown() override { requested_ = true; }
    bool requested() const { return requested_; }
private:
    bool requested_ = false;
};
} // namespace fencing::adapters
