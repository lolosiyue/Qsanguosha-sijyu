#!/usr/bin/env python3
"""Drive the live Android GUI using only D-pad, OK and Back key events.

Reads the existing controller observer; never writes a game/protocol response.
The basic-room policy is shared with the desktop controller diagnostic driver.
"""
import argparse
import collections
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "autotest"))
from controller_gamepad_driver import Driver  # noqa: E402


class AndroidRemote(Driver):
    KEYS = {"up": 19, "down": 20, "left": 21, "right": 22, "ok": 23, "back": 4}
    BUTTONS = {"dpup": "up", "dpdown": "down", "dpleft": "left", "dpright": "right",
               "south": "ok", "east": "back", "rightshoulder": "down", "leftshoulder": "up"}

    def __init__(self, args):
        self.adb = [args.adb, "-s", args.serial]
        self.remote_trace = args.remote_trace
        self.output = args.output
        self.output.mkdir(parents=True, exist_ok=True)
        self.trace = self.output / "controller.jsonl"
        self.trace.write_bytes(b"")
        self.events = []
        self.observation = {}
        self.offset = 0
        self.pending = b""
        self.deadline = time.monotonic() + args.seconds

    def run(self, *args):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("Remote operation reached its bounded deadline")
        result = subprocess.run(self.adb + list(args), capture_output=True, timeout=min(15, remaining))
        if result.returncode:
            raise RuntimeError(result.stderr.decode(errors="replace") or "ADB command failed")
        return result.stdout

    def read(self):
        # Only append newly published observations; no game state is queried outside
        # the same authorized UI model used by the production controller.
        raw = self.run("exec-out", "run-as", "org.qsanguosha.game", "tail", "-c",
                       "+" + str(self.offset + 1), self.remote_trace)
        self.offset += len(raw)
        self.pending += raw
        lines = self.pending.split(b"\n")
        self.pending = lines.pop()
        with self.trace.open("ab") as stream:
            for line in lines:
                if not line:
                    continue
                event = json.loads(line)
                stream.write(line + b"\n")
                self.events.append(event)
                if event["kind"] == "observation":
                    self.observation = event
                if event["kind"] in ("forbidden-input", "diagnostic-error", "core-rejected"):
                    raise RuntimeError(str(event))
        if self.observation.get("unsupported_controller_interaction"):
            raise RuntimeError("The current interaction is not supported by the controller")
        return self.observation

    def key(self, name):
        self.run("shell", "input", "dpad", "keyevent", str(self.KEYS[name]))
        with (self.output / "remote-input.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps({"key": name, "source": "dpad", "time_ms": int(time.time() * 1000)}) + "\n")
        time.sleep(0.35)
        return self.read()

    def press(self, button, hold=0.065):
        # Desktop policy shortcuts are implemented through visible TV commands.
        # Every injected OS event is one of the six keys in KEYS.
        if button == "west":
            self.table("command", "confirm")
        elif button == "start":
            self.key("up")
        else:
            self.key(self.BUTTONS[button])

    def table(self, kind, identifier):
        for _ in range(12):
            if self.read().get("table_focus", "").startswith(kind + ":"):
                break
            self.key("down")
        else:
            raise RuntimeError(f"Cannot reach group {kind}")
        for _ in range(128):
            if self.read().get("table_focus") == kind + ":" + str(identifier):
                self.key("ok")
                return
            self.key("right")
        raise RuntimeError(f"Cannot reach entry {kind}:{identifier}")

    def widget(self, predicate):
        visited = collections.Counter()
        for _ in range(80):
            controls = self.read().get("controls", [])
            targets = [c for c in controls if predicate(c)]
            if not targets:
                raise RuntimeError("Required visible widget was not found")
            focused = next((c for c in controls if c.get("focused")), None)
            if focused and predicate(focused):
                self.key("ok")
                return
            target = min(targets, key=lambda c: abs(c["x"] - focused["x"]) + abs(c["y"] - focused["y"])) if focused else targets[0]
            if not focused:
                self.key("down")
                continue
            dx, dy = target["x"] - focused["x"], target["y"] - focused["y"]
            horizontal, vertical = ("right" if dx >= 0 else "left"), ("down" if dy >= 0 else "up")
            choices = [horizontal, vertical] if abs(dx) > abs(dy) else [vertical, horizontal]
            signature = (focused.get("id"), focused["x"], focused["y"])
            choice = choices[visited[signature] % 2]
            visited[signature] += 1
            self.key(choice)
        raise RuntimeError("Required widget is unreachable through directional navigation")

    def summary(self):
        kinds = collections.Counter(e["kind"] for e in self.events)
        return {"input": "Android OS D-pad / OK / Back", "counts": dict(kinds),
                "human_seat_trustee": any(e.get("self_state") in ("trust", "robot") for e in self.events),
                "observation": self.observation}


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["observe", "press", "widget", "play"])
    parser.add_argument("--serial", required=True)
    parser.add_argument("--adb", default=str(Path(os.environ["LOCALAPPDATA"]) / "Android/Sdk/platform-tools/adb.exe"))
    parser.add_argument("--remote-trace", default="/data/user/0/org.qsanguosha.game/files/tv-controller.jsonl")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--key", choices=AndroidRemote.KEYS)
    parser.add_argument("--id", help="Visible widget objectName")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 1800:
        parser.error("--seconds must be within 1..1800")
    driver = AndroidRemote(args)
    try:
        driver.read()
        if args.action == "press":
            if not args.key:
                parser.error("press requires --key")
            driver.key(args.key)
        elif args.action == "widget":
            if not args.id:
                parser.error("widget requires --id")
            driver.widget(lambda c: c.get("id") == args.id)
        elif args.action == "play":
            driver.play_basic(args.seconds)
        printed = driver.summary()
        printed["observation"] = dict(driver.observation)
        printed["observation"]["controls"] = [
            {**{k: v for k, v in control.items() if k != "rows"},
             **({"row_count": len(control["rows"])} if "rows" in control else {})}
            for control in driver.observation.get("controls", [])]
        print(json.dumps(printed, ensure_ascii=False))
    finally:
        (args.output / "summary.json").write_text(json.dumps(driver.summary(), ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
