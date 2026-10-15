# Suggested implementation path (you do most of the work)

Each item is intentionally left as a manageable milestone.

1. **Get comfortable with ports and tests.** Read the C++ driving/driven interfaces. Run `ctest`. Add a test that a zero-energy `ForReadingPowerState` fake prevents drilling. Add one for incorrect handedness. Avoid touching the Qt UI while doing this.
2. **Learn Qt Widgets.** In `MainWindow.cpp`, support removing/reordering a drill step, editing a saved drill, and displaying better feedback/progress. Then make it fit your actual touchscreen dimensions. Don't put movement validation into the UI.
3. **Persist domain data.** Replace `InMemoryRobotDataAdapter` with a SQLite adapter implementing exactly `ForPersistingRobotData`. Store drills, handedness, movement version, and per-joint calibration values. Migration/versioning and replay-after-reboot tests are essential.
4. **Build the motion transport.** Write a C++ implementation of `LeRobotMotionAdapter` that invokes an out-of-process Python service. Implement a fake server first, then contract tests, THEN add actual LeRobot APIs. Ensure operations never block the Qt event loop. Provide a backend timeout and a heartbeat.
5. **Document the motion model.** Add real keyframes for the six cuts/parries, guard and blade-lowering, and define physically valid starting/ending states. Test simultaneous axial/base and blade timing. Add a reliable failure/abort path.
6. **Design and verify safety.** Define safe contact-force/energy/impulse limits, emergency stopping state, speed limits, sensor fault handling, power gating, and the reset protocol. Build an **independent hardware safety path**; never rely only on C++/Qt/Linux/Python.
7. **Remote controller.** Create a driving adapter that reads the handheld remote and invokes `ForDrillOperation`. Test response latency at 6m and reject manual motion in incompatible states. Keep the physical E-stop path separate.
8. **Monitoring + calibration.** Replace placeholder BMS, health, and calibration adapters. Store calibrated motor limits and verify them after restart. Provide a separate view for power and fault status.
9. **Optional custom moves (Could).** Implement `ForAuthoringMovements`, add keyframe/teleop driving adapters, validation, naming and versioned storage. Reject invalid movements before replay.
10. **Optional ROS 2 backend.** Implement `ROS2MotionAdapter` with the same domain contract, then rerun *the same contract tests* used for the LeRobot adapter.

## Stop and inspect at each milestone

- Does any Qt, ROS 2, LeRobot, JSON wire-format, or SQL type appear under `src/core`? If yes, fix the dependency direction.
- Can you test `FencingRobotApplication` with only test doubles and no real devices?
- Does every external effect pass through an explicit **driven** port?
- Does a successful UI response mean only "request accepted", or that motion actually completed? Keep those distinct.
- Can a child process die without leaving an actuator running? **Do not deploy until this is proven.**
