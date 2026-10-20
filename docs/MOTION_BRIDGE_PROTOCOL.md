# LeRobot bridge protocol

`LeRobotMotionAdapter` starts one long-running Python bridge with `QProcess`. Standard input and output carry one compact JSON object per line; standard error is reserved for logs. Every message contains `"protocol": 1`.

## Startup and liveness

The first bridge message is:

```json
{"protocol":1,"type":"ready","calibrated":true,"hardware_enabled":false}
```

`calibrated` reports whether LeRobot found an existing arm calibration. `hardware_enabled` is true only when `--enable-hardware` was supplied and calibration was present. The bridge emits a heartbeat about every 500 ms:

```json
{"protocol":1,"type":"heartbeat"}
```

The C++ adapter has bounded startup, heartbeat, and shutdown timeouts. A malformed bridge message, wrong protocol version, heartbeat timeout, or child exit makes the adapter unhealthy and changes active motion and positioning channels to `failed`.

## Requests

Every request has a string `request_id`. The adapter supports:

```json
{"protocol":1,"request_id":"1","op":"start_motion","movement_id":"cut_3","handedness":"right","default_duration_s":1.5}
{"protocol":1,"request_id":"2","op":"start_positioning","speed_mps":0.2,"duration_s":3.0}
{"protocol":1,"request_id":"3","op":"pause"}
{"protocol":1,"request_id":"4","op":"resume"}
{"protocol":1,"request_id":"5","op":"cancel"}
{"protocol":1,"request_id":"6","op":"manual_axial","distance_m":0.5}
{"protocol":1,"request_id":"7","op":"shutdown"}
```

Motion and positioning are separate concurrent channels. `pause`, `resume`, and `cancel` affect both. Manual axial movement requires both channels to be idle. Manual rotation is rejected in C++ and has no bridge operation.

The bridge acknowledges whether it accepted each request:

```json
{"protocol":1,"type":"response","request_id":"1","accepted":true}
{"protocol":1,"type":"response","request_id":"2","accepted":false,"error":"positioning is already running"}
```

`ForExecutingRobotMotion::start()` means that a request was queued to a healthy child. A later asynchronous rejection changes that channel to `failed`; completion is always observed by polling.

## State reports

The bridge reports channel changes independently:

```json
{"protocol":1,"type":"state","channel":"motion","state":"running"}
{"protocol":1,"type":"state","channel":"positioning","state":"succeeded"}
{"protocol":1,"type":"state","channel":"motion","state":"failed","error":"arm did not reach its saved target"}
```

Channels are `motion`, `positioning`, and `manual`. States are `idle`, `running`, `succeeded`, and `failed`. The C++ port exposes cached motion and positioning states; manual reports are useful for diagnostics but are not exposed as a separate poll method.

## Bridge execution rules

The bridge loads `FencingRobotConfig` with the local LeRobot installation. At each control tick it combines the current arm target and wheel speeds into one `FencingRobot.send_action()` call. Saved recordings and timed keyframes retain their LeRobot duration; `default_duration_s` is used only by LeRobot for an untimed pose/keyframe sequence.

Movement IDs are resolved through separate `movement_aliases.left` and `movement_aliases.right` maps before actuation. Positioning speed is converted with `counts_per_metre`, and positioning stops after `duration_s`. Pause freezes every active channel clock, and pause or cancellation stops the wheels. EOF, signals, exceptions, and orderly shutdown also disconnect the hardware.

Dry-run is the default. It loads configuration and saved motions and exercises the same protocol state machine without connecting to hardware. Hardware use requires the explicit `--enable-hardware` bridge argument and an existing LeRobot calibration; the bridge connects with `calibrate=False` and never starts an interactive calibration.
