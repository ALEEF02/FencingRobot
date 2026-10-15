# Proposed LeRobot bridge — NOT IMPLEMENTED

Keep the main application C++. The existing LeRobot backend can remain Python. A practical integration is a local Unix-domain socket or a framed stream between the C++ driven adapter and Python. Choose the actual transport after benchmarking latency and failure modes.

## Message proposal (illustrative JSON; never send to live hardware as-is)

```json
{"request_id":"42","op":"start","plan":{"movement_id":"cut_3","handedness":"right","axial":{"speed_mps":0.2,"duration_seconds":0.5}}}
```

Response (proposed):

```json
{"request_id":"42","accepted":false,"error":"NOT_IMPLEMENTED"}
```

Other operations to design: `poll`, `pause`, `resume`, `cancel`, `manual_axial`, `idle_rotate`, `calibrate`, `health`, `heartbeat`, `version`, `capabilities`.

**Things intentionally missing:** API compatibility negotiation, process launch, IPC connection, sequence numbers, timestamps, collision/limit validation, hardware capability detection, reconnect semantics, stale-action handling, deadlines, watchdogs, concurrency synchronization, and real LeRobot device calls.

`bridges/lerobot_server_stub.py` only illustrates parsing the request shape and always replies `NOT_IMPLEMENTED`. The C++ `LeRobotMotionAdapter` does not communicate with it yet; connecting them is an exercise.

Think carefully about whether LeRobot actually controls the wheeled base in your hardware configuration. If not, your LeRobot adapter will need to coordinate an additional low-level base driver or you may want separate arm/base driven ports. The *domain* should not learn which runtime implements which controller.
