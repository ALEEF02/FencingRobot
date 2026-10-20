#!/usr/bin/env python3
"""Harmless protocol peer used by the C++ adapter tests."""

import argparse
import json
import os
import sys
import threading
import time

PROTOCOL = 1
write_lock = threading.Lock()
stopping = threading.Event()


def emit(message):
    message["protocol"] = PROTOCOL
    with write_lock:
        print(json.dumps(message, separators=(",", ":")), flush=True)


def heartbeat_loop(enabled):
    while enabled and not stopping.wait(0.02):
        emit({"type": "heartbeat"})


def complete_later(channel, delay):
    def complete():
        if not stopping.is_set():
            emit({"type": "state", "channel": channel, "state": "succeeded"})

    timer = threading.Timer(delay, complete)
    timer.daemon = True
    timer.start()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", default="normal")
    args = parser.parse_args()

    emit({"type": "ready", "calibrated": True, "hardware_enabled": False})
    threading.Thread(
        target=heartbeat_loop,
        args=(args.mode != "no-heartbeat",),
        daemon=True,
    ).start()

    for line in sys.stdin:
        request = json.loads(line)
        operation = request.get("op")
        request_id = request.get("request_id")
        if operation == "shutdown":
            emit({"type": "response", "request_id": request_id, "accepted": True})
            stopping.set()
            return
        if operation == "start_motion" and args.mode == "exit-on-start":
            os._exit(3)
        if operation == "start_motion" and args.mode == "malformed":
            with write_lock:
                print("{not-json", flush=True)
            continue
        if operation == "start_motion" and args.mode == "reject-motion":
            emit(
                {
                    "type": "response",
                    "request_id": request_id,
                    "accepted": False,
                    "error": "unknown saved move",
                }
            )
            continue

        emit({"type": "response", "request_id": request_id, "accepted": True})
        if operation == "start_motion":
            emit({"type": "state", "channel": "motion", "state": "running"})
            if args.mode != "no-heartbeat":
                complete_later("motion", 0.08)
        elif operation == "start_positioning":
            emit({"type": "state", "channel": "positioning", "state": "running"})
            complete_later("positioning", 0.18)
        elif operation == "cancel":
            emit({"type": "state", "channel": "motion", "state": "idle"})
            emit({"type": "state", "channel": "positioning", "state": "idle"})


if __name__ == "__main__":
    main()
