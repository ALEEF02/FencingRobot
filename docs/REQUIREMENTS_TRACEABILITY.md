# High-level requirements-to-scaffold map

This is a **starter map, not a compliance claim**. The project requirements use MoSCoW priorities; only the simulation behavior listed below exists. No acceptance test involving real motors, humans, batteries, safety, positioning accuracy or timing can pass yet.

| Requirement(s) | Intended component(s) | Starter coverage |
|---|---|---|
| `reqUserCreateDrills`, `reqUserPerformDrills`, `reqFunctionalDrillSequence` | ForManagingDrills, MainWindow, Drill manager/orchestrator | Demo sequence builder and simulated playback only |
| `reqUserAxialMovement` | DrillStep axial profile, ForExecutingRobotMotion | Profile conveyed to fake backend; no wheels or sync verification |
| `reqUserRotationalMovement`, `reqSystemMobileBaseCapability` | ForDrillOperation, motion adapter | Method signature only; no 360-degree/2s or speed test |
| `reqUserRemoteDrillControl`, `reqUserRemoteDistanceOverride`, `reqSystemRemoteInterface` | ForDrillOperation, future remote driving adapter | Core methods; wireless remote and 6m/0.5s tests missing |
| `reqUserEmergencyStop`, `reqSystemEmergencyStopBehavior` | Safety coordinator, ForEnteringSafePowerState | Demo latch ONLY; physical safety and precise semantics unresolved |
| `reqSystemBasicMotions` | Movement definitions + motion backend | Six names only; no joint trajectories, accuracy or timing |
| `reqSystemIntermittentPosition`, `reqSystemRestingPosition` | MotionOrchestrator | Demo en-garde between motions and after normal finish; safe cancellation recovery TODO |
| `reqSystemDrillRequiredEnergy`, `reqSystemDrillInsufficientPower`, `reqSystemQueryPower` | Read power, energy estimator, readiness policy | Fake power & deliberately invented estimate; no compliance |
| `reqSystemHandednessConfiguration`, `reqSystemHandednessValidation`, `reqUserChangeHand` | ForConfiguringRobot, persistence, safety | Logical selection only; no mechanical sensing or real validation |
| `reqSystemCalibration`, `reqUserCalibration` | ForConfiguringRobot, ForCalibratingMotionHardware | Simulated success flag only; range calibration not implemented |
| `reqUserSystemCheck`, `reqSystemSystemCheck`, `reqUserPowerCheck` | ForMonitoringRobot, health/power adapters | Fake status only |
| `reqUserAddMoves`, `reqFunctionalCreateMoves` | ForAuthoringMovements, future movement editor | Intentionally rejects; Could |
| `reqFunctionalMotionTransitions`, `reqFunctionalCustomMotionTransition` | MotionOrchestrator / movement library | TODO; Could |
| `reqSystemPowerOnOff`, `reqUserPowerOnOff`, `reqSystemCharge`, `reqUserCharge`, `reqSystemChargeIndicator` | External physical power subsystem | Outside software starter |
| `reqSystemContactSafety`, `reqSystemElectricalSafety`, `reqSystemTargetStrikableGeometry`, `reqUserTargetShape` | Hardware and independent safety engineering | Outside software starter |
| `reqSystemUserInterface` | Qt Widget main window | UI shell only; usability / placement tests missing |

**Important:** The requirement text still contains TBD limits for contact forces, timing, target dimensions and emergency response. Those cannot be silently replaced with guessed values.
