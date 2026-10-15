#include "core/application/FencingRobotApplication.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace fencing::application {
FencingRobotApplication::FencingRobotApplication(
	ports::driven::ForExecutingRobotMotion& motion,
	ports::driven::ForCalibratingMotionHardware& calibration,
	ports::driven::ForPersistingRobotData& data,
	ports::driven::ForReadingPowerState& power,
	ports::driven::ForReadingHardwareHealth& health,
	ports::driven::ForEnteringSafePowerState& safe_power)
	: motion_(motion), calibration_(calibration), data_(data), power_(power),
	  health_(health), safe_power_(safe_power), orchestrator_(motion),
	  config_(data.loadConfiguration()) {}

bool FencingRobotApplication::reject(std::string reason) {
	last_error_ = std::move(reason);
	return false;
}

std::vector<domain::Drill> FencingRobotApplication::listDrills() const {
	return data_.listDrills();
}

bool FencingRobotApplication::saveDrill(const domain::Drill& drill) {
	if (drill.id.empty() || drill.name.empty() || drill.steps.empty())
		return reject("Drill needs an id, name, and at least one step");
	for (const auto& step : drill.steps) {
		if (!data_.findMovement(step.movement_id)) return reject("Unknown movement: " + step.movement_id);
		if (step.axial && (!std::isfinite(step.axial->speed_mps) ||
			!std::isfinite(step.axial->duration_seconds) || step.axial->duration_seconds <= 0))
			return reject("Invalid axial profile");
	}
	if (!data_.saveDrill(drill)) return reject("Unable to store drill");
	last_error_.clear();
	return true;
}

bool FencingRobotApplication::startDrill(const std::string& id) {
	if (drillState() == domain::DrillState::EmergencyStopped)
		return reject("Emergency stop latched; reset procedure not implemented");
	auto drill = data_.findDrill(id);
	if (!drill) return reject("Drill not found");
	if (!config_.calibrated || config_.handedness == domain::Handedness::Unknown)
		return reject("Robot requires calibrated motors and valid handedness");
	if (!health_.readHealth().healthy) return reject("Hardware health check failed");
	const auto power = power_.readPower();
	if (!power.valid) return reject("Power status unavailable");
	// TODO(reqSystemDrillRequiredEnergy): replace DEMO-ONLY estimate with a
	// measured and conservative energy + peak-power model before live use.
	// THIS SHOULD PROBABLY COME FROM A PORT/ADAPTER!
	const double demo_estimated_joules = static_cast<double>(drill->steps.size()) * 10.0;
	if (power.available_energy_joules < demo_estimated_joules)
		return reject("Insufficient simulated energy");
	if (!orchestrator_.start(*drill, config_.handedness, data_))
		return reject("Motion backend rejected drill");
	last_drill_id_ = id;
	last_error_.clear();
	return true;
}

bool FencingRobotApplication::cancelDrill() {
	return orchestrator_.cancel();
}

domain::DrillState FencingRobotApplication::drillState() const {
	return orchestrator_.state();
}

bool FencingRobotApplication::pauseDrill() { return orchestrator_.pause(); }

bool FencingRobotApplication::resumeDrill() { return orchestrator_.resume(); }

bool FencingRobotApplication::restartDrill() {
	if (last_drill_id_.empty()) return reject("No previous drill");
	if (drillState() == domain::DrillState::Running || drillState() == domain::DrillState::Paused)
		if (!orchestrator_.cancel()) return false;
	return startDrill(last_drill_id_);
}

bool FencingRobotApplication::isIdleForManualControl() const {
	switch (drillState()) {
		case domain::DrillState::Idle:
		case domain::DrillState::Completed:
		case domain::DrillState::Cancelled:
		case domain::DrillState::Failed: return true;
		default: return false;
	}
}

bool FencingRobotApplication::requestManualAxialMovement(double distance_m) {
	if (!isIdleForManualControl()) return reject("Manual base control requires idle state");
	if (!std::isfinite(distance_m)) return reject("Invalid distance");
	// TODO: validated speed/stability limits and dead-man timeout are required.
	return motion_.commandManualAxialMovement(distance_m);
}

bool FencingRobotApplication::requestManualRotation(double degrees) {
	if (!isIdleForManualControl()) return reject("Rotation requires idle state");
	return std::isfinite(degrees) && motion_.commandManualRotation(degrees);
}

void FencingRobotApplication::requestEmergencyStop() {
	// Demonstrates domain latching only. NEVER rely on this for real E-stop.
	orchestrator_.latchEmergencyStop();
	safe_power_.requestEmergencyShutdown();
	last_error_ = "Emergency stop latched";
}
bool FencingRobotApplication::configureHandedness(domain::Handedness handedness) {
	if (!isIdleForManualControl())
		return reject("Handedness can only change while idle");

	if (handedness == domain::Handedness::Unknown)
		return reject("Unknown Handedness selection");
	// TODO: read mechanical handedness sensing and validate physical alignment.
	config_.handedness = handedness;
	if (!data_.saveConfiguration(config_)) return reject("Failed to persist handedness");
	last_error_.clear();
	return true;
}
bool FencingRobotApplication::calibrateRobot() {
	if (!isIdleForManualControl()) return reject("Calibration requires idle state");
	if (!calibration_.calibrateMotorRanges()) return reject("Calibration adapter failed");
	// TODO: persist actual per-joint calibration results, not just this flag.
	config_.calibrated = true;
	if (!data_.saveConfiguration(config_)) return reject("Failed to persist calibration");
	last_error_.clear();
	return true;
}
domain::RobotConfiguration FencingRobotApplication::configuration() const { return config_; }
domain::RobotStatus FencingRobotApplication::checkRobotStatus() const {
	return {drillState(), power_.readPower(), health_.readHealth(), config_, last_error_};
}
bool FencingRobotApplication::saveMovement(const domain::MovementDefinition& movement) {
	// TODO(reqFunctionalCreateMoves): implement range, collision, start/end,
	// path, and time validation before allowing user-authored moves.
	(void)movement;
	return reject("Movement authoring not implemented (Could requirement)");
}
void FencingRobotApplication::tick() { orchestrator_.tick(); }
} // namespace fencing::application
