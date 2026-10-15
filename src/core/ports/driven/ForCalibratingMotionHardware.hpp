#pragma once
namespace fencing::ports::driven {
class ForCalibratingMotionHardware {
public:
    virtual ~ForCalibratingMotionHardware() = default;
    virtual bool calibrateMotorRanges() = 0;
};
} // namespace fencing::ports::driven
