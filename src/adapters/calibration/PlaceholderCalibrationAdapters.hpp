#pragma once
#include "core/ports/driven/ForCalibratingMotionHardware.hpp"

namespace fencing::adapters {
class SimulatedCalibrationAdapter final : public ports::driven::ForCalibratingMotionHardware {
public:
    // SIM ONLY: never probes a real joint; just makes the test demo proceed.
    bool calibrateMotorRanges() override { return true; }
};
class UnimplementedCalibrationAdapter final : public ports::driven::ForCalibratingMotionHardware {
public:
    bool calibrateMotorRanges() override { return false; }
};
} // namespace fencing::adapters
