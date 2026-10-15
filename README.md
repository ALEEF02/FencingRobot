# Fencing Robot — C++20 / Qt 6 Starter

**Starter skeleton, not a working fencing robot.** The only functional motion implementation is a simulated adapter that NEVER operates motors. LeRobot and ROS 2 are **non-actuating TODO adapters**.

This repository is designed for you to fill in. It models Cockburn's **Ports & Adapters** boundary from the project's component diagram, and leaves you plenty of implementation work.

## Quick start on a Raspberry Pi 5 (64-bit Raspberry Pi OS)

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build qt6-base-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/fencing_robot_ui --backend=sim
```

Use 64-bit Raspberry Pi OS with a Qt 6 Widgets development package. Qt 6 availability and exact package names depend on the OS release. Python is only needed later for LeRobot integration; it is not needed to build this demo.

If Qt is not installed, CMake skips the UI but still builds the core/tests:

```bash
cmake -S . -B build -DFENCING_BUILD_UI=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

## Try the simulated UI

1. Open **Configuration**, choose Left or Right, click **Apply handedness**.
2. Click **Simulate calibration** (a fake calibration; there is no hardware).
3. Open **Drills**, select a saved drill and click **Start**.
4. The simulator advances a named blade movement, interposed `en_guard`, and a final `lower_blade`. It has no joint trajectory data or physical behavior.
5. Build and save a new named list of movements; persistence is **in memory only**.
6. See **Monitoring** for deliberately fake power/health status.

For completeness, `--backend=lerobot` and `--backend=ros2` select named adapters **that refuse all commands**. Neither mode connects to a robot.

## Structure

```text
src/
  core/                           # no Qt, LeRobot, ROS 2, or SQL headers
    domain/Types.hpp              # domain-only types, units
    ports/driving/                # 5 provided interfaces
    ports/driven/                 # 6 required interfaces
    application/                  # orchestration and business rules
  adapters/                       # technology-specific / fake implementations
    motion/                       # simulated, LeRobot TODO, ROS 2 TODO
    persistence/                  # memory-only adapter, SQLite TODO
    calibration/, power/, health/, safety/
  ui/MainWindow.*                 # Qt Widgets driving adapter
  main/main.cpp                   # composition root: chooses adapter
bridges/                           # illustrative Python wire-protocol stub

docs/
  architecture.puml               # updated PlantUML with split maintenance ports
  ARCHITECTURE.md                 # ports, runtime boundaries, IPC
  REQUIREMENTS_TRACEABILITY.md    # how requirements map to starter components
  LEARNING_PATH.md                # suggested implementation sequence
  MOTION_BRIDGE_PROTOCOL.md       # proposed future LeRobot IPC contract
tests/CoreTests.cpp               # dependency-injected, no-hardware smoke tests
```

## Main design decisions

- **Language:** C++20 in the domain, ports, Qt UI, and adapter stubs. LeRobot itself remains a separate **Python** process to be integrated later.
- **Qt style:** Qt Widgets for a compact onboard touchscreen. No QML needed for this starter.
- **Process boundary:** UI and core are **in one C++ process** initially because the Qt UI can call driving interfaces directly; separate the GUI later if operational constraints warrant it. LeRobot must run outside the UI process.
- **Swap point:** `ForExecutingRobotMotion` is domain-owned. The composition root chooses `SimulatedMotionAdapter`, `LeRobotMotionAdapter`, or `ROS2MotionAdapter`.
- **No fake safety assurance:** emergency stop, power model, torque/contact limits, mechanical handedness sensing, charge management, and driver watchdogs are **not implemented**.
- **Not every diagram box is a separate class yet:** splitting or combining internal classes is intentionally left to you. Ports and the application's boundary are the stable part.

## Safety-critical warning

Do **not** connect this scaffold to live motors. The demonstration's fake health/power/calibration adapters report invented values, and `10 joules/step` is an intentionally meaningless demo placeholder. It is NOT a valid energy estimate or acceptance test.

The project requirements currently specify an emergency-stop sequence that moves to en garde, lowers the blade, and shuts down. Whether moving the blade during an emergency is safe **must be resolved** by an appropriate safety analysis. A safety-rated, independently effective stop/torque-disable mechanism must not depend on Qt, Python, the Linux scheduler, or this application. A normal controlled cancellation/recovery is a distinct use case and is only marked TODO here.

## Next steps

Follow [LEARNING_PATH.md](docs/LEARNING_PATH.md), beginning with the core tests. Implement a real (validated) persistence adapter, then a hardware communication boundary, then safety and control verification. The optional movement-authoring feature is deliberately deferred.
