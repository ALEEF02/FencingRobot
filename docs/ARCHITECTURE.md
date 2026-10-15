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
| `ForExecutingRobotMotion` | Start, poll, pause, resume, cancel, axial, rotation | `SimulatedMotionAdapter`, unimplemented ROS 2/LeRobot |
| `ForCalibratingMotionHardware` | Hardware calibration | Simulated/unimplemented |
| `ForPersistingRobotData` | Drills, movements, configuration | In-memory; replace with SQLite |
| `ForReadingPowerState` | Power and charge telemetry | Fake |
| `ForReadingHardwareHealth` | Hardware faults | Fake |
| `ForEnteringSafePowerState` | Software request to safety/power subsystem | Fake; NOT an E-stop |

## Threading and responsiveness

The current Qt window and business logic share a process. The UI invokes the driving interfaces synchronously because the simulated backend performs no blocking work. `MotionOrchestrator` advances via `tick()`; **the Qt timer is not used for generating motor control commands**. A real adapter should send commands asynchronously, return quickly, and report status separately.

A future LeRobot adapter will require an IPC client (e.g. a local Unix-domain socket with JSON messages or protobuf) and a Python subprocess supervised by systemd. Do **not** block the Qt thread waiting for long LeRobot calls. When you integrate it, consider moving the application to its own process or using a worker plus thread-safe event/command dispatch.

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
