#!/usr/bin/env python3
"""SAFE stub of a proposed LeRobot bridge.

No lerobot import, no motor commands, no sockets. Example:
    echo '{"request_id":"42","op":"start"}' | python3 bridges/lerobot_server_stub.py
The response is always NOT_IMPLEMENTED.
"""
import json
import sys


def main() -> None:
    for line in sys.stdin:
        try:
            request = json.loads(line)
            response = {
                "request_id": request.get("request_id"),
                "accepted": False,
                "error": "NOT_IMPLEMENTED",
            }
        except (json.JSONDecodeError, TypeError, AttributeError):
            response = {"request_id": None, "accepted": False, "error": "BAD_REQUEST"}
        print(json.dumps(response), flush=True)


if __name__ == "__main__":
    main()
