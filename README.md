# Fencing Robot

Remote-controlled fencing dummy with preset & custom drills.

## Quick start on a Raspberry Pi 5 (64-bit Raspberry Pi OS)

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build qt6-base-dev libsqlite3-dev python3
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/fencing_robot_ui --backend=sim
```
Python is only required for the bridge tests and LeRobot backend.

If Qt is not installed, CMake skips the UI but still builds the core/tests:
```bash
cmake -S . -B build -DFENCING_BUILD_UI=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

### Run the UI on a PI through SSH
```bash
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=wayland-0
export QT_QPA_PLATFORM=wayland
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus

./build/fencing_robot_ui --backend=sim
```

### Try the simulated UI

1. Open **Configuration**, choose Left or Right, click **Apply handedness**.
2. Click **Simulate calibration** (a fake calibration; there is no hardware).
3. Open **Drills**, select a saved drill and click **Start**.
4. The simulator advances the scheduled blade and positioning timelines. It has no joint trajectory data or physical behavior.
5. Build and save a new named list of movements. The normal application stores drills, movements, and configuration in SQLite.
6. See **Monitoring** for deliberately fake power/health status.

The default database is `fencing_robot.sqlite3` under Qt's per-user application-data directory. Pass `--database=/path/to/file.sqlite3` to choose an explicit location. A new database is seeded once with the seven built-in movements and sample drill; reopening it does not replace user data.

## LeRobot backend

Install `uv` and prepare the local `lerobot-fencing` environment first. Copy `build/bridges/lerobot_bridge.example.json` to a writable location, then update:
- robot.arm.id
- both arm serial paths
- RoboClaw serial path
- left_motor, m1_sign, and m2_sign if non-default
- counts_per_metre
- drive.max_speed
- all movement aliases

### Start in dry-run mode
Dry-run is the default: it loads the real LeRobot configuration and saved motions, validates commands and timing, but does not connect to hardware.

```bash
./build/fencing_robot_ui \
  --backend=lerobot \
  --lerobot-root=/path/to/lerobot-fencing \
  --lerobot-config=/path/to/lerobot_bridge.json
```

### Run with hardware
Calibrate the arm separately with the LeRobot tooling before enabling hardware. The bridge never starts an interactive calibration; it checks that calibration already exists and connects with `calibrate=False`. Only after dry-run validation, add the explicit hardware flag:

```bash
./build/fencing_robot_ui \
  --backend=lerobot \
  --lerobot-root=/path/to/lerobot-fencing \
  --lerobot-config=/path/to/lerobot_bridge.json \
  --enable-hardware
```

```bash
export LEROBOT_ROOT="$HOME/lerobot-fencing"
export FENCING_ROOT="$HOME/FencingRobot"
export ROBOT_DATABASE="$HOME/.local/share/fencing-robot/fencing_robot.sqlite3"
export BRIDGE_CONFIG="$HOME/.config/fencing-robot/lerobot_bridge.json"
"$FENCING_ROOT/build/fencing_robot_ui" \
  --backend=lerobot \
  --database="$ROBOT_DATABASE" \
  --lerobot-root="$LEROBOT_ROOT" \
  --lerobot-config="$BRIDGE_CONFIG" \
  --bridge-script="$FENCING_ROOT/build/bridges/lerobot_bridge.py"
```
add `--enable-hardware` when ready.

This will
- Open the RoboClaw and stop the wheels.
- Connect both arm buses with calibrate=False.
- Read the current arm position.
- Set that position as the initial goal.
- Enable arm torque.

Use `--bridge-script=/path/to/lerobot_bridge.py` only when overriding the copy beside the executable. Manual axial movement is supported; manual rotation deliberately returns `false` until the chassis geometry is known. See [the implemented protocol](docs/MOTION_BRIDGE_PROTOCOL.md) for bridge messages and failure behavior.

## Structure

```text
src/
  core/                           # no Qt, LeRobot, ROS 2, or SQL headers
    domain/Types.hpp              # domain-only types, units
    ports/driving/                # 5 provided interfaces
    ports/driven/                 # 6 required interfaces
    application/                  # orchestration and business rules
  adapters/                       # technology-specific / fake implementations
    motion/                       # simulated, LeRobot process, ROS 2 placeholder
    persistence/                  # SQLite plus in-memory test adapter
    calibration/, power/, health/, safety/
  ui/MainWindow.*                 # Qt Widgets driving adapter
  main/main.cpp                   # composition root: chooses adapter
bridges/                           # LeRobot Python bridge and example config

docs/
  architecture.puml               # updated PlantUML with split maintenance ports
  ARCHITECTURE.md                 # ports, runtime boundaries, IPC
  REQUIREMENTS_TRACEABILITY.md    # how requirements map to starter components
  LEARNING_PATH.md                # suggested implementation sequence
  MOTION_BRIDGE_PROTOCOL.md       # implemented LeRobot IPC contract
tests/                             # core, SQLite, adapter, and bridge tests
```

## Main design decisions

- **Language:** C++20 in the domain, ports, Qt UI, and adapters. LeRobot remains a separate **Python** child process.
- **Qt style:** Qt Widgets for a compact onboard touchscreen. No QML needed for this starter.
- **Process boundary:** UI and core are in one C++ process. The LeRobot adapter owns a long-running Python child and communicates over newline-delimited JSON on standard input/output.
- **Swap point:** `ForExecutingRobotMotion` is domain-owned. The composition root chooses `SimulatedMotionAdapter`, `LeRobotMotionAdapter`, or `ROS2MotionAdapter`.
- **No fake safety assurance:** emergency stop, power model, torque/contact limits, mechanical handedness sensing, charge management, and driver watchdogs are **not implemented**.

## TODO
- [ ] The health, power, and software safe-power adapters still report simulated values, and `10 joules/step` is an intentionally meaningless demo placeholder.
- [ ] The project requirements currently specify an emergency-stop sequence that moves to en garde, lowers the blade, and shuts down. Whether moving the blade during an emergency is safe **must be resolved** by an appropriate safety analysis. A safety-rated, independently effective stop/torque-disable mechanism must not depend on Qt, Python, the Linux scheduler, or this application. A normal controlled cancellation/recovery is a distinct use case and is only marked TODO here.
- [ ] The LeRobot integration has automated fake-device and dry bridge tests, but no automated test enables hardware. Validate wheel polarity, encoder scaling, joint limits, saved movements, timeout behavior, and physical stopping independently before any live use.
- [ ] The GUI calibration action does not calibrate LeRobot hardware.
- [ ] The bridge checks for a calibration file but does not interactively verify calibration against the motors during startup.
- [ ] The five-second Python startup timeout may be too short on a Pi.
- [ ] Pause/resume in the current MotionOrchestrator only forwards to the backend while an arm motion is active. Avoid pausing during a positioning-only interval until this is corrected.
- [ ] The software emergency stop is not safety-rated.
- [ ] Manual rotation is unavailable.
- [ ] There is no automatic bridge reconnection.
- [ ] There is no collision avoidance, chassis odometry, contact-force limiting, or independently verified stopping system.
- [ ] A SIGKILL, USB disconnect, or Linux failure can bypass orderly cleanup; the RoboClaw serial timeout and physical power stop remain necessary.
- [ ] When closing, we must safely dampen the joints instead of simply releasing control (Likely a new method in ForExecutingRobotMotion or ForEnteringSafePowerState)
- [ ] There is potential for a ForExecutingRobotMotion implementation to not support yet all of the required motions. Determine how we should handle this.