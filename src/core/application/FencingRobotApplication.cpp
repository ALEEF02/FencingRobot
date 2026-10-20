#include "core/application/FencingRobotApplication.hpp"
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace fencing::application {
namespace {

const std::array<domain::MovementDefinition, 7>& initialMovementLibrary() {
	static const std::array<domain::MovementDefinition, 7> movements{{
		{.id = "parry_3", .display_name = "Parry 3", .duration_seconds = 3.0,
		 .built_in = true},
		{.id = "parry_4", .display_name = "Parry 4", .duration_seconds = 3.0,
		 .built_in = true},
		{.id = "parry_5", .display_name = "Parry 5", .duration_seconds = 3.0,
		 .built_in = true},
		{.id = "cut_3", .display_name = "Cut 3", .duration_seconds = 1.5,
		 .built_in = true},
		{.id = "cut_4", .display_name = "Cut 4", .duration_seconds = 1.5,
		 .built_in = true},
		{.id = "cut_5", .display_name = "Cut 5", .duration_seconds = 1.5,
		 .built_in = true},
		{.id = "en_garde", .display_name = "En Garde", .duration_seconds = 1.0,
		 .built_in = true},
	}};
	return movements;
}

void seedInitialData(ports::driven::ForPersistingRobotData& data) {
	for (const auto& movement : initialMovementLibrary()) {
		// Persistence adapters store the library; they do not decide its contents.
		if (!data.findMovement(movement.id) && !data.saveMovement(movement))
			throw std::runtime_error("Unable to store initial movement: " + movement.id);
	}

	if (!data.findDrill("sample")) {
		const domain::Drill sample{
			.id = "sample",
			.name = "Sample drill",
			.motion_steps = {
				{"cut_3", 0.0},
				{"parry_5", 1.5},
				{"en_garde", 6.0},
				{"cut_4", 10.0},
			},
		};
		if (!data.saveDrill(sample))
			throw std::runtime_error("Unable to store the initial sample drill");
	}
}

} // namespace

FencingRobotApplication::FencingRobotApplication(
	ports::driven::ForExecutingRobotMotion& motion,
	ports::driven::ForCalibratingMotionHardware& calibration,
	ports::driven::ForPersistingRobotData& data,
	ports::driven::ForReadingPowerState& power,
	ports::driven::ForReadingHardwareHealth& health,
	ports::driven::ForEnteringSafePowerState& safe_power)
	: motion_(motion), calibration_(calibration), data_(data), power_(power),
	  health_(health), safe_power_(safe_power), orchestrator_(motion),
	  config_(data.loadConfiguration()) {
	seedInitialData(data_);
}

bool FencingRobotApplication::reject(std::string reason) {
	last_error_ = std::move(reason);
	return false;
}

std::vector<domain::Drill> FencingRobotApplication::listDrills() const {
	return data_.listDrills();
}

std::vector<domain::MovementDefinition> FencingRobotApplication::listMovements() const {
	return data_.listMovements();
}

bool FencingRobotApplication::saveDrill(const domain::Drill& drill) {
	if (drill.id.empty() || drill.name.empty() || drill.motion_steps.empty())
		return reject("Drill needs an id, name, and at least one motion step");

	for (std::size_t i = 0; i < drill.motion_steps.size(); ++i) {
		const auto& step = drill.motion_steps[i];
		if (!data_.findMovement(step.movement_id)) return reject("Unknown movement: " + step.movement_id);
		const auto& movement = data_.findMovement(step.movement_id);
		if (!std::isfinite(step.start_time_seconds) || !std::isfinite(movement->duration_seconds) ||
		movement->duration_seconds <= 0)
			return reject("Invalid motion step timing");
		const double start = step.start_time_seconds;
		const double end = start + movement->duration_seconds;
		for (std::size_t j = 0; j < i; ++j) {
			const auto& other = drill.motion_steps[j];
			const auto& otherMovement = data_.findMovement(step.movement_id);
			const double other_end = other.start_time_seconds + otherMovement->duration_seconds;
			if (start < other_end && other.start_time_seconds < end)
				return reject("Motion steps cannot overlap");
		}
	}

	for (std::size_t i = 0; i < drill.positioning_steps.size(); ++i) {
		const auto& step = drill.positioning_steps[i];
		if (!std::isfinite(step.speed_mps))
			return reject("Invalid positioning speed");
		if (!std::isfinite(step.start_time_seconds) || !std::isfinite(step.duration_seconds) ||
			step.duration_seconds <= 0)
			return reject("Invalid positioning step timing");
		const double start = step.start_time_seconds;
		const double end = start + step.duration_seconds;
		for (std::size_t j = 0; j < i; ++j) {
			const auto& other = drill.positioning_steps[j];
			const double other_end = other.start_time_seconds + other.duration_seconds;
			if (start < other_end && other.start_time_seconds < end)
				return reject("Positioning steps cannot overlap");
		}
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
	const double demo_estimated_joules = static_cast<double>(drill->motion_steps.size()) * 10.0;
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
