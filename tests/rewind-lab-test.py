#!/usr/bin/env python3
"""Exercise the opt-in console through stdin and its real RoomThread worker."""
import json
import os
import sqlite3
import subprocess
import sys
import tempfile


def run(commands):
    with tempfile.TemporaryDirectory(prefix="qsan-rewind-lab-") as profile:
        environment = dict(os.environ, XDG_DATA_HOME=profile + "/data",
                           XDG_CONFIG_HOME=profile + "/config", XDG_CACHE_HOME=profile + "/cache")
        completed = subprocess.run(
        [sys.argv[1], "--asset-root", sys.argv[2], "--user-data-root", sys.argv[2], "--seed", "1"],
        input="\n".join(commands + ["quit", ""]), text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45, env=environment,
        )
        records = [json.loads(line) for line in completed.stdout.splitlines() if line.startswith("{")]
        assert completed.returncode == 0, completed.stdout
        for record in records:
            assert record["statistics_root"] == record["root_game_id"], record
            assert record["statistics_generation"] == record["generation"], record
        # The real worker called the production Room hook and asynchronous writer.
        # This ongoing lab was not declared terminal: its DB row is a fence, not
        # a completed ordinary match. Terminal recomputation is tested separately.
        with sqlite3.connect(records[-1]["statistics_path"]) as database:
            row = database.execute("SELECT root, generation, terminal FROM battle_matches").fetchall()
            assert row == [(records[-1]["root_game_id"], int(records[-1]["generation"]), 0)], row
    assert completed.returncode == 0, completed.stdout
    assert "CARD_LIFETIME_WORKER_FINAL retired=0" in completed.stdout, completed.stdout
    gauges = [json.loads(line.partition("CARD_LIFETIME_ZERO ")[2])
              for line in completed.stdout.splitlines() if line.startswith("CARD_LIFETIME_ZERO ")]
    assert len(gauges) == 1 and gauges[0]["managed_live"] == 0 and gauges[0]["pending_delete"] == 0
    assert len(records) == len(commands) + 1, completed.stdout
    assert records[0]["command"] == "ready" and records[0]["ok"], completed.stdout
    assert [r["command"] for r in records[1:]] == commands, completed.stdout
    return records


def state(record):
    # Transport generation/observer counts are intentionally not rewound.
    return {key: record[key] for key in (
        "room_id", "root_game_id", "current_player", "round", "players",
        "draw", "discard", "rng_draws", "history_sha256",
    )}


commands = [
    "step", "step", "step", "rewind turn", "step", "rewind round", "step", "step",
    "rewind round", "rewind round", "rewind round", "rewind turn", "step",
    "rewind turn", "step", "step", "rewind turn", "rewind turn", "rewind turn", "status",
]
r = run(commands)
for index in (11, 12, 19):
    assert not r[index]["ok"] and "not available" in r[index]["error"], r[index]
for index, record in enumerate(r):
    if index not in (11, 12, 19):
        assert record["ok"], record
    assert record["room_id"] == r[0]["room_id"]
    assert record["root_game_id"] == r[0]["root_game_id"]
    assert record["generation"] == record["restore_notifications"], record
for left, right in ((4, 2), (5, 3), (6, 1), (7, 2), (8, 3), (9, 1),
                    (11, 10), (12, 10), (13, 0), (14, 10), (15, 0),
                    (16, 1), (17, 0), (18, 10), (19, 18), (20, 18)):
    assert state(r[left]) == state(r[right]), (left, right, r[left], r[right])
assert r[-1]["generation"] == "7"
assert r[10]["current_player"] == "lab1"
assert all(player["turns"] == 0 and not player["hand"] for player in r[10]["players"])

# Cross reshuffles, exhaust the independent turn retention window, then continue
# from the oldest retained state. Failures may not mutate native/history/RNG state.
r = run(["step"] * 24 + ["rewind turn"] * 10 + ["step", "status"])
assert all(record["ok"] for record in r[:33]), r
assert all(not record["ok"] for record in r[33:35]), r[33:35]
assert state(r[32]) == state(r[33]) == state(r[34])
assert r[32]["generation"] == r[33]["generation"] == r[34]["generation"] == "8"
assert state(r[35]) == state(r[17]) == state(r[36]), (r[35], r[17])
assert all(record["generation"] == record["restore_notifications"] for record in r)
print("rewind-lab: real worker, both anchors, repeated restores, deterministic continuation, "
      "history/RNG/card equality, bounded retention, exactly-once observer and clean shutdown passed")
