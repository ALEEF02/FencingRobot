#pragma once
#include "core/application/MotionOrchestrator.hpp"
#include "core/ports/driving/ForAuthoringMovements.hpp"
#include "core/ports/driving/ForConfiguringRobot.hpp"
#include "core/ports/driving/ForManagingDrills.hpp"
#include "core/ports/driving/ForMonitoringRobot.hpp"
#include "core/ports/driving/ForDrillOperation.hpp"
#include "core/ports/driven/ForCalibratingMotionHardware.hpp"
#include "core/ports/driven/ForEnteringSafePowerState.hpp"
#include "core/ports/driven/ForReadingHardwareHealth.hpp"
#include "core/ports/driven/ForReadingPowerState.hpp"
#include <string>

namespace fencing::application {

// Application = the HEXAGON. Owns domain contracts, depends ONLY on ports.
// Inputs are the five DRIVING interfaces; outputs are the six DRIVEN ports.
class FencingRobotApplication final :
	public ports::driving::ForManagingDrills,
	public ports::driving::ForDrillOperation,
	public ports::driving::ForConfiguringRobot,
	public ports::driving::ForMonitoringRobot,
	public ports::driving::ForAuthoringMovements {
        
        public:
            FencingRobotApplication(
                ports::driven::ForExecutingRobotMotion& motion,
                ports::driven::ForCalibratingMotionHardware& calibration,
                ports::driven::ForPersistingRobotData& data,
                ports::driven::ForReadingPowerState& power,
                ports::driven::ForReadingHardwareHealth& health,
                ports::driven::ForEnteringSafePowerState& safe_power);

            // ForManagingDrills
            std::vector<domain::Drill> listDrills() const override;
            bool saveDrill(const domain::Drill& drill) override;

            // ForDrillOperation
            domain::DrillState drillState() const override;
            bool startDrill(const std::string& drill_id) override;
            bool cancelDrill() override;
            bool pauseDrill() override;
            bool resumeDrill() override;
            bool restartDrill() override;
            bool requestManualAxialMovement(double distance_m) override;
            bool requestManualRotation(double degrees) override;
            void requestEmergencyStop() override;

            // ForConfiguringRobot
            bool configureHandedness(domain::Handedness handedness) override;
            bool calibrateRobot() override;
            domain::RobotConfiguration configuration() const override;

            // ForMonitoringRobot
            domain::RobotStatus checkRobotStatus() const override;

            // ForAuthoringMovements
            bool saveMovement(const domain::MovementDefinition& movement) override;

            // Called periodically by the process entrypoint; no Qt timer in the core.
            void tick();

        private:
            bool isIdleForManualControl() const;
            bool reject(std::string reason);

            ports::driven::ForExecutingRobotMotion& motion_;
            ports::driven::ForCalibratingMotionHardware& calibration_;
            ports::driven::ForPersistingRobotData& data_;
            ports::driven::ForReadingPowerState& power_;
            ports::driven::ForReadingHardwareHealth& health_;
            ports::driven::ForEnteringSafePowerState& safe_power_;
            MotionOrchestrator orchestrator_;
            domain::RobotConfiguration config_;
            std::string last_drill_id_;
            std::string last_error_;
};
} // namespace fencing::application
