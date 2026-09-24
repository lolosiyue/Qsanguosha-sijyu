#!/usr/bin/env python3

import argparse
import json
from datetime import datetime
from pathlib import Path


REQUIRED_KEYS = {
    "timestamp",
    "level",
    "component",
    "room_id",
    "player_id",
    "message",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--path", required=True, type=Path)
    args = parser.parse_args()

    records = []
    for number, line in enumerate(args.path.read_text(encoding="utf-8").splitlines(), 1):
        if not line:
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SystemExit(f"invalid JSON log line {number}: {error}") from error
        missing = REQUIRED_KEYS - record.keys()
        if missing:
            raise SystemExit(f"JSON log line {number} omitted: {sorted(missing)}")
        datetime.fromisoformat(record["timestamp"].replace("Z", "+00:00"))
        records.append(record)

    listening = next(
        (record for record in records if record["component"] == "server"
         and record["message"].startswith("Listening on 127.0.0.1:")),
        None,
    )
    if listening is None:
        raise SystemExit("JSON log omitted the actual listening endpoint")
    if listening.get("address") != "127.0.0.1":
        raise SystemExit("JSON listening address mismatch")
    port = listening.get("port")
    if not isinstance(port, int) or not 1 <= port <= 65535:
        raise SystemExit("JSON listening port is not an allocated TCP port")
    if not any(record["message"] == "stopping" for record in records):
        raise SystemExit("JSON log omitted server stopping")
    if not any(record["message"] == "stopped" for record in records):
        raise SystemExit("JSON log omitted server stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
