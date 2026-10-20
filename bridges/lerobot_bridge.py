#!/usr/bin/env python3
"""Run LeRobot behind the small JSON-lines protocol used by the C++ adapter."""

from __future__ import annotations

import argparse
import json
import logging
import math
import queue
import signal
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import draccus

from lerobot.robots.fencing_robot import FencingRobot, FencingRobotConfig
from lerobot.robots.fencing_robot.session import MoveSettings, prepare_move

PROTOCOL_VERSION = 1
logger = logging.getLogger("fencing-bridge")


class ProtocolWriter:
    def __init__(self):
        self._lock = threading.Lock()

    def emit(self, message: dict) -> None:
        message = {**message, "protocol": PROTOCOL_VERSION}
        with self._lock:
            print(json.dumps(message, separators=(",", ":")), flush=True)


@dataclass(frozen=True)
class RuntimeSettings:
    control_hz: float
    counts_per_metre: float
    manual_speed_mps: float
    manual_tolerance_counts: int
    manual_timeout_margin_s: float
    arm_tolerance: float
    arm_settle_timeout_s: float
    movement_aliases: dict[str, dict[str, str]]

    @classmethod
    def from_json(cls, data: dict) -> "RuntimeSettings":
        aliases = data.get("movement_aliases")
        if not isinstance(aliases, dict) or not all(
            isinstance(aliases.get(hand), dict) for hand in ("left", "right")
        ):
            raise ValueError("movement_aliases must contain left and right objects")
        settings = cls(
            control_hz=float(data.get("control_hz", 40.0)),
            counts_per_metre=float(data.get("counts_per_metre", 1998.0)),
            manual_speed_mps=float(data.get("manual_speed_mps", 0.1)),
            manual_tolerance_counts=int(data.get("manual_tolerance_counts", 10)),
            manual_timeout_margin_s=float(data.get("manual_timeout_margin_s", 5.0)),
            arm_tolerance=float(data.get("arm_tolerance", 2.0)),
            arm_settle_timeout_s=float(data.get("arm_settle_timeout_s", 2.0)),
            movement_aliases={
                hand: {str(key): str(value) for key, value in aliases[hand].items()}
                for hand in ("left", "right")
            },
        )
        numeric_positive = (
            settings.control_hz,
            settings.counts_per_metre,
            settings.manual_speed_mps,
            settings.manual_timeout_margin_s,
            settings.arm_tolerance,
            settings.arm_settle_timeout_s,
        )
        if any(not math.isfinite(value) or value <= 0 for value in numeric_positive):
            raise ValueError("Bridge numeric settings must be finite and positive")
        if not 10 <= settings.control_hz <= 100:
            raise ValueError("control_hz must be between 10 and 100")
        if settings.manual_tolerance_counts <= 0:
            raise ValueError("manual_tolerance_counts must be positive")
        return settings


@dataclass
class ActivityClock:
    started_at: float
    paused_at: float | None = None
    paused_total: float = 0.0

    def elapsed(self, now: float) -> float:
        end = self.paused_at if self.paused_at is not None else now
        return max(0.0, end - self.started_at - self.paused_total)

    def pause(self, now: float) -> None:
        if self.paused_at is None:
            self.paused_at = now

    def resume(self, now: float) -> None:
        if self.paused_at is not None:
            self.paused_total += now - self.paused_at
            self.paused_at = None


@dataclass
class ActiveMotion:
    move: object
    source: dict[str, float]
    clock: ActivityClock


@dataclass
class ActivePositioning:
    speed_counts: int
    duration_s: float
    clock: ActivityClock


@dataclass
class ActiveManualMovement:
    direction: int
    speed_counts: int
    clock: ActivityClock
    timeout_s: float
    targets: dict[str, int] | None = None
    dry_duration_s: float = 0.0


class BridgeController:
    """Merge independent arm and base channels into one robot command stream."""

    def __init__(
        self,
        robot: FencingRobot,
        settings: RuntimeSettings,
        hardware_enabled: bool,
        emit: Callable[[dict], None],
        clock: Callable[[], float] = time.monotonic,
        prepare: Callable = prepare_move,
    ):
        self.robot = robot
        self.settings = settings
        self.hardware_enabled = hardware_enabled
        self.emit = emit
        self.clock = clock
        self.prepare = prepare
        self.motion: ActiveMotion | None = None
        self.positioning: ActivePositioning | None = None
        self.manual: ActiveManualMovement | None = None
        self.paused = False
        self.shutdown_requested = False

    def handle(self, request: dict) -> None:
        request_id = request.get("request_id")
        if request.get("protocol") != PROTOCOL_VERSION:
            self._respond(request_id, False, "unsupported protocol version")
            return
        operation = request.get("op")
        try:
            if operation == "start_motion":
                self._start_motion(request)
            elif operation == "start_positioning":
                self._start_positioning(request)
            elif operation == "pause":
                self._pause()
            elif operation == "resume":
                self._resume()
            elif operation == "cancel":
                self._cancel()
            elif operation == "manual_axial":
                self._start_manual(request)
            elif operation == "shutdown":
                self.shutdown_requested = True
            else:
                raise ValueError(f"unsupported operation: {operation!r}")
        except (KeyError, TypeError, ValueError, RuntimeError) as error:
            self._respond(request_id, False, str(error))
            return
        self._respond(request_id, True)

    def tick(self) -> None:
        if self.paused:
            return
        now = self.clock()
        arm_target = self._motion_target(now)
        wheel_speeds = self._wheel_speeds(now)
        has_activity = self.motion is not None or self.positioning is not None or self.manual is not None

        if self.hardware_enabled and has_activity:
            action = dict(arm_target or {})
            action["base.left_speed"] = wheel_speeds["left"]
            action["base.right_speed"] = wheel_speeds["right"]
            self.robot.send_action(action)

        self._finish_motion_if_ready(now, arm_target)
        self._finish_positioning_if_ready(now)
        self._finish_manual_if_ready(now, wheel_speeds)

    def close(self) -> None:
        if not self.hardware_enabled:
            return
        try:
            if self.robot.drive.is_connected:
                self.robot.stop()
        finally:
            if self.robot.is_connected:
                self.robot.disconnect()

    def _start_motion(self, request: dict) -> None:
        if self.motion is not None:
            raise RuntimeError("a blade motion is already running")
        if self.manual is not None:
            raise RuntimeError("manual base movement is running")
        movement_id = str(request["movement_id"])
        handedness = str(request["handedness"])
        if handedness not in ("left", "right"):
            raise ValueError("handedness must be left or right")
        try:
            saved_name = self.settings.movement_aliases[handedness][movement_id]
        except KeyError as error:
            raise ValueError(f"no {handedness}-hand alias for {movement_id!r}") from error
        default_duration = float(request["default_duration_s"])
        if not math.isfinite(default_duration) or default_duration <= 0:
            raise ValueError("default_duration_s must be finite and positive")
        move = self.prepare(self.robot, MoveSettings(saved_name), default_duration)
        if not math.isfinite(move.duration_s) or move.duration_s <= 0:
            raise ValueError("saved motion has an invalid duration")
        if self.hardware_enabled:
            observation = self.robot.arm.get_observation()
            source = {
                key: float(observation[key]) for key in self.robot.arm.action_features
            }
        else:
            source = {key: 0.0 for key in self.robot.arm.action_features}
        self.motion = ActiveMotion(move, source, ActivityClock(self.clock()))
        self._state("motion", "running")

    def _start_positioning(self, request: dict) -> None:
        if self.positioning is not None:
            raise RuntimeError("positioning is already running")
        if self.manual is not None:
            raise RuntimeError("manual base movement is running")
        speed_mps = float(request["speed_mps"])
        duration_s = float(request["duration_s"])
        if not math.isfinite(speed_mps) or speed_mps == 0:
            raise ValueError("speed_mps must be finite and non-zero")
        if not math.isfinite(duration_s) or duration_s <= 0:
            raise ValueError("duration_s must be finite and positive")
        speed_counts = round(speed_mps * self.settings.counts_per_metre)
        if speed_counts == 0 or abs(speed_counts) > self.robot.config.drive.max_speed:
            raise ValueError("positioning speed is outside the configured wheel limits")
        self.positioning = ActivePositioning(
            speed_counts, duration_s, ActivityClock(self.clock())
        )
        self._state("positioning", "running")

    def _start_manual(self, request: dict) -> None:
        if self.motion is not None or self.positioning is not None or self.manual is not None:
            raise RuntimeError("manual movement requires idle motion channels")
        distance_m = float(request["distance_m"])
        if not math.isfinite(distance_m) or distance_m == 0:
            raise ValueError("distance_m must be finite and non-zero")
        delta_counts = round(distance_m * self.settings.counts_per_metre)
        speed_counts = round(self.settings.manual_speed_mps * self.settings.counts_per_metre)
        if delta_counts == 0 or speed_counts <= 0 or speed_counts > self.robot.config.drive.max_speed:
            raise ValueError("manual movement is outside the configured wheel limits")
        direction = 1 if delta_counts > 0 else -1
        targets = None
        if self.hardware_enabled:
            start = self.robot.drive.get_encoders()
            targets = {wheel: value + delta_counts for wheel, value in start.items()}
        duration = abs(distance_m) / self.settings.manual_speed_mps
        self.manual = ActiveManualMovement(
            direction=direction,
            speed_counts=speed_counts,
            clock=ActivityClock(self.clock()),
            timeout_s=duration + self.settings.manual_timeout_margin_s,
            targets=targets,
            dry_duration_s=duration,
        )
        self._state("manual", "running")

    def _pause(self) -> None:
        if self.paused:
            raise RuntimeError("bridge is already paused")
        now = self.clock()
        for activity in (self.motion, self.positioning, self.manual):
            if activity is not None:
                activity.clock.pause(now)
        self.paused = True
        if self.hardware_enabled and self.robot.drive.is_connected:
            self.robot.stop()

    def _resume(self) -> None:
        if not self.paused:
            raise RuntimeError("bridge is not paused")
        now = self.clock()
        for activity in (self.motion, self.positioning, self.manual):
            if activity is not None:
                activity.clock.resume(now)
        self.paused = False

    def _cancel(self) -> None:
        self.motion = None
        self.positioning = None
        self.manual = None
        self.paused = False
        if self.hardware_enabled and self.robot.drive.is_connected:
            self.robot.stop()
        self._state("motion", "idle")
        self._state("positioning", "idle")
        self._state("manual", "idle")

    def _motion_target(self, now: float) -> dict[str, float] | None:
        if self.motion is None:
            return None
        elapsed = min(self.motion.clock.elapsed(now), self.motion.move.duration_s)
        return self.motion.move.sample(self.motion.source, elapsed)

    def _wheel_speeds(self, now: float) -> dict[str, int]:
        if self.manual is not None:
            if not self.hardware_enabled:
                speed = self.manual.direction * self.manual.speed_counts
                return {"left": speed, "right": speed}
            encoders = self.robot.drive.get_encoders()
            speeds = {}
            for wheel, target in self.manual.targets.items():
                remaining = self.manual.direction * (target - encoders[wheel])
                speeds[wheel] = (
                    0
                    if remaining <= self.settings.manual_tolerance_counts
                    else self.manual.direction * self.manual.speed_counts
                )
            return speeds
        if self.positioning is not None and (
            self.positioning.clock.elapsed(now) < self.positioning.duration_s
        ):
            return dict.fromkeys(("left", "right"), self.positioning.speed_counts)
        return {"left": 0, "right": 0}

    def _finish_motion_if_ready(
        self, now: float, target: dict[str, float] | None
    ) -> None:
        if self.motion is None or self.motion.clock.elapsed(now) < self.motion.move.duration_s:
            return
        if not self.hardware_enabled:
            self.motion = None
            self._state("motion", "succeeded")
            return
        observation = self.robot.arm.get_observation()
        if all(
            abs(float(observation[key]) - target[key]) <= self.settings.arm_tolerance
            for key in target
        ):
            self.motion = None
            self._state("motion", "succeeded")
            return
        if self.motion.clock.elapsed(now) >= (
            self.motion.move.duration_s + self.settings.arm_settle_timeout_s
        ):
            self.motion = None
            self._state("motion", "failed", "arm did not reach its saved target")

    def _finish_positioning_if_ready(self, now: float) -> None:
        if self.positioning is None or (
            self.positioning.clock.elapsed(now) < self.positioning.duration_s
        ):
            return
        self.positioning = None
        self._state("positioning", "succeeded")

    def _finish_manual_if_ready(self, now: float, speeds: dict[str, int]) -> None:
        if self.manual is None:
            return
        elapsed = self.manual.clock.elapsed(now)
        completed = (
            elapsed >= self.manual.dry_duration_s
            if not self.hardware_enabled
            else not any(speeds.values())
        )
        if completed:
            self.manual = None
            self._state("manual", "succeeded")
        elif elapsed >= self.manual.timeout_s:
            self.manual = None
            self._state("manual", "failed", "manual axial movement timed out")

    def _respond(self, request_id, accepted: bool, error: str | None = None) -> None:
        response = {
            "type": "response",
            "request_id": request_id,
            "accepted": accepted,
        }
        if error:
            response["error"] = error
        self.emit(response)

    def _state(self, channel: str, state: str, error: str | None = None) -> None:
        message = {"type": "state", "channel": channel, "state": state}
        if error:
            message["error"] = error
        self.emit(message)


def read_requests(target: queue.SimpleQueue) -> None:
    for line in sys.stdin:
        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                raise ValueError("request must be a JSON object")
            target.put(request)
        except (json.JSONDecodeError, ValueError) as error:
            target.put({"protocol": PROTOCOL_VERSION, "op": "invalid", "parse_error": str(error)})
    target.put({"protocol": PROTOCOL_VERSION, "op": "shutdown", "request_id": None})


def load_runtime(path: Path) -> tuple[FencingRobot, RuntimeSettings]:
    with path.open(encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict) or not isinstance(data.get("robot"), dict):
        raise ValueError("Bridge config must contain a robot object")
    robot_config = draccus.decode(FencingRobotConfig, data["robot"])
    return FencingRobot(robot_config), RuntimeSettings.from_json(data)


def run(config_path: Path, enable_hardware: bool) -> None:
    writer = ProtocolWriter()
    robot, settings = load_runtime(config_path)
    calibrated = bool(robot.arm.calibration)
    hardware_enabled = enable_hardware and calibrated
    if enable_hardware and not calibrated:
        logger.error("Hardware was requested, but the configured arm has no valid calibration")
    if hardware_enabled:
        robot.connect(calibrate=False)
        robot.arm.prepare_for_motion()

    controller = BridgeController(robot, settings, hardware_enabled, writer.emit)
    requests: queue.SimpleQueue = queue.SimpleQueue()
    threading.Thread(target=read_requests, args=(requests,), daemon=True).start()
    stopping = threading.Event()

    def stop(signum, frame):
        stopping.set()

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    writer.emit(
        {
            "type": "ready",
            "calibrated": calibrated,
            "hardware_enabled": hardware_enabled,
        }
    )

    interval = 1.0 / settings.control_hz
    next_tick = time.monotonic()
    next_heartbeat = next_tick
    try:
        while not stopping.is_set() and not controller.shutdown_requested:
            while True:
                try:
                    request = requests.get_nowait()
                except queue.Empty:
                    break
                controller.handle(request)
            controller.tick()
            now = time.monotonic()
            if now >= next_heartbeat:
                writer.emit({"type": "heartbeat"})
                next_heartbeat = now + 0.5
            next_tick = max(next_tick + interval, now)
            time.sleep(max(0.0, next_tick - time.monotonic()))
    except BaseException as error:
        writer.emit({"type": "state", "channel": "motion", "state": "failed", "error": str(error)})
        writer.emit(
            {"type": "state", "channel": "positioning", "state": "failed", "error": str(error)}
        )
        raise
    finally:
        controller.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--enable-hardware", action="store_true")
    parser.add_argument("--log-level", default="INFO")
    args = parser.parse_args()
    logging.basicConfig(level=args.log_level, stream=sys.stderr)
    run(args.config, args.enable_hardware)


if __name__ == "__main__":
    main()
