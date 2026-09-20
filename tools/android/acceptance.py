#!/usr/bin/env python3
"""Collect Android acceptance evidence without replacing visible gameplay."""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = "org.qsanguosha.game"
ACTIVITY = PACKAGE + "/org.qtproject.qt.android.bindings.QtActivity"
APK = ROOT / "builds/android-x86_64-debug/cmake/android-app/android-build/build/outputs/apk/debug/android-build-debug.apk"


def stamp():
    return dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f")


def save_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


class Device:
    def __init__(self, args):
        self.command = [args.adb, "-s", args.serial]

    def run(self, *args, timeout=15, check=True):
        result = subprocess.run(self.command + list(args), capture_output=True, timeout=timeout)
        if check and result.returncode:
            raise RuntimeError(result.stderr.decode("utf-8", errors="replace")
                               or result.stdout.decode("utf-8", errors="replace"))
        return result

    def pid(self):
        result = self.run("shell", "pidof", PACKAGE, check=False)
        if result.returncode not in (0, 1):
            raise RuntimeError("ADB pid lookup failed; do not interpret a disconnected device as app exit")
        if not result.stdout.strip() and self.run("get-state").stdout.strip() != b"device":
            raise RuntimeError("Device disconnected while checking the app PID")
        return result.stdout.decode().strip()

    def screenshot(self, path):
        data = self.run("exec-out", "screencap", "-p").stdout
        if not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise RuntimeError("screencap did not return a PNG")
        path.write_bytes(data)


def collect(args, device):
    out = args.output or ROOT / "builds" / ("android-acceptance-" + stamp())
    # Never overwrite a previous attempt, including one that failed at installation.
    out.mkdir(parents=True, exist_ok=False)
    print("Evidence: " + str(out.resolve()), flush=True)
    result = {"serial": args.serial, "started": dt.datetime.now().isoformat(),
              "game_over": "MANUAL_REVIEW_REQUIRED", "clean_exit": "MANUAL_REVIEW_REQUIRED",
              "samples": [], "apk_installed_this_run": False}
    collector = None
    log = None
    try:
        existing = device.pid()
        if existing and (args.install or not args.attach):
            raise RuntimeError("App is running. Exit normally, or use --attach without --install.")
        if args.attach and args.install:
            raise RuntimeError("--attach and --install cannot be combined")
        if args.attach and args.auto_robots:
            raise RuntimeError("--auto-robots only applies to a fresh launch")
        if args.attach and not existing:
            raise RuntimeError("--attach requires an already running app")
        (out / "device.txt").write_bytes(device.run("shell", "getprop").stdout)
        (out / "exit-before.txt").write_bytes(device.run(
            "shell", "dumpsys", "activity", "exit-info", PACKAGE, check=False).stdout)
        if args.install:
            # Fingerprint only the APK, never scan/hash the installed media tree.
            digest = hashlib.sha256()
            with args.apk.open("rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(block)
            result["apk"] = {"path": str(args.apk.resolve()), "sha256": digest.hexdigest(),
                             "bytes": args.apk.stat().st_size}
            installed = device.run("install", "--no-streaming", "-r", str(args.apk),
                                   timeout=180, check=False)
            (out / "install.txt").write_bytes(installed.stdout + installed.stderr)
            if installed.returncode or b"Success" not in installed.stdout:
                raise RuntimeError("Cover installation failed; see install.txt. App data was not cleared.")
            device.run("shell", "sync", timeout=30)
            result["apk_installed_this_run"] = True
        (out / "package.txt").write_bytes(device.run("shell", "dumpsys", "package", PACKAGE).stdout)
        log = (out / "logcat.txt").open("wb")
        collector = subprocess.Popen(device.command + ["logcat", "-v", "threadtime", "-T", "1"],
                                     stdout=log, stderr=subprocess.STDOUT)
        if not args.attach:
            launch_args = ["shell", "am", "start", "-W", "-n", ACTIVITY]
            if args.auto_robots:
                launch_args += ["--es", "applicationArguments", "--auto-robots"]
            launched = device.run(*launch_args, timeout=30, check=False)
            (out / "launch.txt").write_bytes(launched.stdout + launched.stderr)
            if launched.returncode or b"Error:" in launched.stdout + launched.stderr:
                raise RuntimeError("Activity launch failed; see launch.txt")
        start = time.monotonic()
        seen_pid = existing
        result["ended_reason"] = "timeout; app left running"
        while time.monotonic() - start < args.seconds:
            if collector.poll() is not None:
                raise RuntimeError("logcat collector stopped unexpectedly")
            elapsed = round(time.monotonic() - start, 1)
            pid = device.pid()
            if seen_pid and pid and pid != seen_pid:
                raise RuntimeError("App PID changed during acceptance; inspect crash/restart evidence")
            seen_pid = pid or seen_pid
            result["samples"].append({"elapsed": elapsed, "pid": pid})
            save_json(out / "progress.json", result)
            device.screenshot(out / ("screen-%06d.png" % int(elapsed)))
            if not pid and (seen_pid or elapsed >= 15):
                result["ended_reason"] = "process exited" if seen_pid else "no app process observed"
                break
            time.sleep(min(args.interval, max(0, args.seconds - (time.monotonic() - start))))
    except KeyboardInterrupt:
        result["ended_reason"] = "collector interrupted; app not stopped"
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        result["ended_reason"] = "collection failed"
        result["error"] = str(error)
    finally:
        # Stop only our log reader. Never force-stop, uninstall, clear data, or
        # manufacture a game result when a timeout/exception ends observation.
        if collector is not None:
            collector.terminate()
            try:
                collector.wait(timeout=5)
            except subprocess.TimeoutExpired:
                collector.kill()
                collector.wait()
        if log is not None:
            log.close()
        for name, command in {
            "exit-after.txt": ["shell", "dumpsys", "activity", "exit-info", PACKAGE],
            "listeners-after.txt": ["shell", "ss", "-ltn"],
            "pid-after.txt": ["shell", "pidof", PACKAGE],
        }.items():
            try:
                evidence = device.run(*command, check=False)
                (out / name).write_bytes(evidence.stdout + evidence.stderr)
            except (OSError, subprocess.TimeoutExpired) as error:
                result.setdefault("collection_errors", []).append(str(error))
        result["finished"] = dt.datetime.now().isoformat()
        save_json(out / "result.json", result)
    print(result["ended_reason"] + "; this is not an automatic gameplay PASS", flush=True)
    return 0 if result["ended_reason"] == "process exited" else 2


def main():
    sdk_adb = Path(os.environ.get("LOCALAPPDATA", "")) / "Android/Sdk/platform-tools/adb.exe"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["check", "run", "capture"])
    parser.add_argument("--adb", default=str(sdk_adb) if sdk_adb.is_file() else shutil.which("adb") or "adb")
    parser.add_argument("--serial", default="emulator-5586")
    parser.add_argument("--apk", type=Path, default=APK)
    parser.add_argument("--output", type=Path, help="new directory for run; existing attempt directory for capture")
    parser.add_argument("--install", action="store_true", help="install -r before launching; app must be stopped")
    parser.add_argument("--attach", action="store_true", help="observe an existing app without restarting it")
    parser.add_argument("--auto-robots", action="store_true", help="pass the existing product option; does not click Quick Join")
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--interval", type=int, default=5)
    parser.add_argument("--label", default="checkpoint", help="capture label, e.g. game-over or returned-home")
    args = parser.parse_args()
    if args.seconds <= 0 or args.interval <= 0:
        parser.error("--seconds and --interval must be positive")
    device = Device(args)
    if device.run("get-state").stdout.strip() != b"device":
        raise RuntimeError("Selected device is not online; reuse the existing AVD")
    if args.action == "check":
        print(json.dumps({"serial": args.serial, "pid": device.pid(),
                          "device_abi": device.run("shell", "getprop", "ro.product.cpu.abi").stdout.decode().strip(),
                          "apk": str(args.apk), "apk_exists": args.apk.is_file()}, indent=2))
        return 0
    if args.action == "capture":
        if not args.output or not args.output.is_dir() or not re.fullmatch(r"[A-Za-z0-9_-]+", args.label):
            parser.error("capture needs an existing --output and an ASCII alphanumeric/dash/underscore --label")
        path = args.output / (args.label + "-" + stamp() + ".png")
        device.screenshot(path)
        print(path)
        return 0
    return collect(args, device)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        raise SystemExit(str(error))
