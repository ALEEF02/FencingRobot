# Architecture notes

## Hexagonal Architecture, as used here

The **FencingRobotApplication** is the hexagon. Its *provided* driving ports are used by the Qt UI, handheld remote adapter, optional authoring adapters, and automated tests. Its *required* driven ports are implemented by persistence, power, motion, calibration, health, and safe-power adapters. It has **no source dependencies** on Qt, Python, LeRobot, ROS 2, DDS, SQLite, or Linux-specific transports.

The *composition root* (`src/main/main.cpp`) constructs the adapters and injects their **port interfaces** into the application. Adapters depend on the port declarations, never the reverse. The same core tests run with in-memory stand-ins.

The requirement to split `For Maintaining the Robot` has been applied:

- `ForConfiguringRobot`: `configureHandedness()`, `calibrateRobot()`, `configuration()`.
- `ForMonitoringRobot`: `checkRobotStatus()`; read-only status query.

### Driving ports

| Interface | Role |
|---|---|
| `ForManagingDrills` | Save, list, start and cancel drills |
| `ForDrillOperation` | Pause, resume, restart, manual axial/idle rotation, E-stop request |
| `ForConfiguringRobot` | Configuration and calibration |
| `ForMonitoringRobot` | Read-only status |
| `ForAuthoringMovements` | Optional custom movement definition |

### Driven ports

| Interface | Role | Starter adapter |
|---|---|---|
| `ForExecutingRobotMotion` | Start, poll, pause, resume, cancel, axial, rotation | Simulated, LeRobot process; ROS 2 placeholder |
| `ForCalibratingMotionHardware` | Hardware calibration | Simulated/unimplemented |
| `ForPersistingRobotData` | Drills, movements, configuration | SQLite in the application; in-memory in isolated tests |
| `ForReadingPowerState` | Power and charge telemetry | Fake |
| `ForReadingHardwareHealth` | Hardware faults | Fake |
| `ForEnteringSafePowerState` | Software request to safety/power subsystem | Fake; NOT an E-stop |

## Threading and responsiveness

The current Qt window and business logic share a process. `MotionOrchestrator` advances via `tick()`; **the Qt timer is not used for generating motor control commands**. The LeRobot adapter queues short JSON requests and polls cached states. Its Python child owns the independent 40 Hz hardware loop.

`LeRobotMotionAdapter` owns its child through `QProcess` and uses newline-delimited JSON over standard input/output. Startup is bounded by a readiness timeout; normal calls do not wait for motion completion. Malformed output, heartbeat loss, and child-process exit fail active work. See [MOTION_BRIDGE_PROTOCOL.md](MOTION_BRIDGE_PROTOCOL.md).

## The architecture cannot make systems interchangeable by itself

LeRobot typically represents robot sensors/actions/policies; ROS 2 provides message/action/controller primitives. Your adapter must implement the *semantic* contract:

- Same coordinate frames, joint order, and radian/meter units.
- Start, completion, failure, pause, cancellation, watchdog and timeouts.
- Concurrent axial-base and arm movement against the same time reference.
- Calibrated joint limits, handedness and safe command ranges.
- Observability and failure recovery.

Swapping adapter classes is easy; proving their **behavioral equivalence is the real engineering task**.

## Physical hardware outside the hexagon

The on/off switch, charger/BMS, charge indicator, torso target, physical E-stop circuit, blade, arm, actuators and wheels are not application-core classes. Some of them have no software interface. They must be designed, powered and tested independently of this repository.

## Unit conventions (provisional)

- Joint positions: radians, indexed by `joint_names`.
- Axial speed: meters/second (positive forward, negative backward).
- Time: seconds from start, per movement.
- Idle rotation: degrees **for UI convenience**, translate carefully in adapters.

These conventions are proposed implementation details, not explicitly mandated by the requirements. Decide them before writing real adapters; adding frame IDs and clock semantics may be necessary.

## Diagrams

`architecture.puml` is copied from the previous agreed component diagram, updating the `For Maintaining the Robot` driving port into `For Configuring the Robot` and `For Monitoring the Robot` and their routing/test lines.
