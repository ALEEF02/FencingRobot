import importlib.util
import sys
import types
import unittest
from dataclasses import dataclass
from pathlib import Path


def load_bridge_module():
    draccus = types.ModuleType("draccus")
    draccus.decode = lambda cls, data: data
    sys.modules["draccus"] = draccus

    lerobot = types.ModuleType("lerobot")
    robots = types.ModuleType("lerobot.robots")
    fencing_robot = types.ModuleType("lerobot.robots.fencing_robot")
    session = types.ModuleType("lerobot.robots.fencing_robot.session")

    class PlaceholderRobot:
        pass

    class PlaceholderConfig:
        pass

    @dataclass
    class MoveSettings:
        name: str
        duration_s: float | None = None

    fencing_robot.FencingRobot = PlaceholderRobot
    fencing_robot.FencingRobotConfig = PlaceholderConfig
    session.MoveSettings = MoveSettings
    session.prepare_move = lambda *args: None
    sys.modules["lerobot"] = lerobot
    sys.modules["lerobot.robots"] = robots
    sys.modules["lerobot.robots.fencing_robot"] = fencing_robot
    sys.modules["lerobot.robots.fencing_robot.session"] = session

    path = Path(__file__).parents[1] / "bridges" / "lerobot_bridge.py"
    spec = importlib.util.spec_from_file_location("lerobot_bridge_under_test", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


bridge = load_bridge_module()


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now


class FakeArm:
    def __init__(self):
        self.action_features = {"joint.pos": float}
        self.observation = {"joint.pos": 0.0}
        self.calibration = {"joint": object()}

    def get_observation(self):
        return dict(self.observation)

    def prepare_for_motion(self):
        return dict(self.observation)


class FakeDrive:
    def __init__(self):
        self.is_connected = True
        self.encoders = {"left": 0, "right": 0}

    def get_encoders(self):
        return dict(self.encoders)


class FakeRobot:
    def __init__(self):
        self.arm = FakeArm()
        self.drive = FakeDrive()
        self.config = types.SimpleNamespace(
            drive=types.SimpleNamespace(max_speed=500)
        )
        self.is_connected = True
        self.actions = []
        self.stop_calls = 0
        self.disconnected = False

    def send_action(self, action):
        self.actions.append(dict(action))
        if "joint.pos" in action:
            self.arm.observation["joint.pos"] = action["joint.pos"]
        self.drive.encoders["left"] += int(action.get("base.left_speed", 0) / 10)
        self.drive.encoders["right"] += int(action.get("base.right_speed", 0) / 10)
        return dict(action)

    def stop(self):
        self.stop_calls += 1

    def disconnect(self):
        self.disconnected = True
        self.is_connected = False


class PreparedMove:
    duration_s = 2.0

    @staticmethod
    def sample(source, elapsed):
        return {"joint.pos": min(20.0, elapsed * 10.0)}


class BridgeControllerTests(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.robot = FakeRobot()
        self.messages = []
        self.prepared_names = []
        self.settings = bridge.RuntimeSettings(
            control_hz=40.0,
            counts_per_metre=2000.0,
            manual_speed_mps=0.1,
            manual_tolerance_counts=10,
            manual_timeout_margin_s=5.0,
            arm_tolerance=0.1,
            arm_settle_timeout_s=1.0,
            movement_aliases={
                "left": {"cut_3": "cut3_left"},
                "right": {"cut_3": "cut3"},
            },
        )

        def prepare(robot, move_settings, default_duration):
            self.prepared_names.append((move_settings.name, default_duration))
            return PreparedMove()

        self.controller = bridge.BridgeController(
            self.robot,
            self.settings,
            True,
            self.messages.append,
            clock=self.clock,
            prepare=prepare,
        )

    def request(self, operation, **values):
        self.controller.handle(
            {
                "protocol": bridge.PROTOCOL_VERSION,
                "request_id": str(len(self.messages) + 1),
                "op": operation,
                **values,
            }
        )

    def test_combines_full_motion_and_positioning(self):
        self.request(
            "start_motion",
            movement_id="cut_3",
            handedness="right",
            default_duration_s=1.0,
        )
        self.request("start_positioning", speed_mps=0.15, duration_s=1.0)
        self.assertEqual(self.prepared_names, [("cut3", 1.0)])

        self.clock.now = 0.5
        self.controller.tick()
        self.assertEqual(self.robot.actions[-1]["base.left_speed"], 300)
        self.assertIn("joint.pos", self.robot.actions[-1])

        self.clock.now = 1.1
        self.controller.tick()
        self.assertIsNotNone(self.controller.motion)
        self.assertIsNone(self.controller.positioning)

        self.clock.now = 2.1
        self.controller.tick()
        self.assertIsNone(self.controller.motion)
        states = [(message.get("channel"), message.get("state")) for message in self.messages]
        self.assertIn(("positioning", "succeeded"), states)
        self.assertIn(("motion", "succeeded"), states)

    def test_left_alias_and_pause_freeze_elapsed_time(self):
        self.request(
            "start_motion",
            movement_id="cut_3",
            handedness="left",
            default_duration_s=1.5,
        )
        self.request("start_positioning", speed_mps=0.1, duration_s=1.0)
        self.assertEqual(self.prepared_names[0], ("cut3_left", 1.5))
        self.clock.now = 0.5
        self.request("pause")
        self.clock.now = 5.5
        self.request("resume")
        self.controller.tick()
        self.assertAlmostEqual(self.robot.actions[-1]["joint.pos"], 5.0)
        self.assertIsNotNone(self.controller.positioning)
        self.assertEqual(self.robot.stop_calls, 1)

    def test_missing_hand_alias_is_rejected_before_motion(self):
        self.request(
            "start_motion",
            movement_id="parry_4",
            handedness="left",
            default_duration_s=1.0,
        )
        self.assertIsNone(self.controller.motion)
        self.assertFalse(self.messages[-1]["accepted"])

    def test_cancel_and_cleanup_stop_hardware(self):
        self.request("start_positioning", speed_mps=-0.1, duration_s=2.0)
        self.request("cancel")
        self.assertIsNone(self.controller.positioning)
        self.assertGreaterEqual(self.robot.stop_calls, 1)
        self.controller.close()
        self.assertTrue(self.robot.disconnected)


if __name__ == "__main__":
    unittest.main()
