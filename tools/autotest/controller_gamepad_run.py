#!/usr/bin/env python3
"""Run a bounded SDL virtual-gamepad GUI smoke test in a private directory.

This runner only starts the supplied server/GUI and the adjacent physical-input
driver. It does not synthesize protocol replies or claim physical hardware
coverage. Runtime assets must already be staged in --runtime.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import stat
import subprocess
import sys
import time
from typing import Any


STARTUP_TIMEOUT_SECONDS = 20.0
POLL_INTERVAL_SECONDS = 0.1
PROCESS_STOP_TIMEOUT_SECONDS = 4.0
MAX_SEED = (1 << 64) - 1


class RunnerFailure(RuntimeError):
    pass


def parse_uint(value: str) -> int:
    try:
        number = int(value, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a non-negative decimal integer") from exc
    if number < 0:
        raise argparse.ArgumentTypeError("expected a non-negative decimal integer")
    return number


def parse_port(value: str) -> int:
    number = parse_uint(value)
    if not 1 <= number <= 65534:
        raise argparse.ArgumentTypeError("port must be in 1..65534 (websocket uses port + 1)")
    return number


def parse_seed(value: str) -> int:
    number = parse_uint(value)
    if number > MAX_SEED:
        raise argparse.ArgumentTypeError("seed must fit an unsigned 64-bit integer")
    return number


def parse_seconds(value: str) -> int:
    number = parse_uint(value)
    if not 1 <= number <= 86400:
        raise argparse.ArgumentTypeError("seconds must be in 1..86400")
    return number


def absolute_existing_file(value: str, description: str) -> Path:
    path = Path(value)
    if not path.is_absolute():
        raise RunnerFailure(f"{description} must be an absolute path: {value}")
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise RunnerFailure(f"{description} does not exist: {value}: {exc}") from exc
    # Reject symlink aliases in every path component, not just a symlink at the
    # final filename. The child is always executed through the real path.
    if Path(os.path.abspath(value)) != resolved:
        raise RunnerFailure(f"{description} must not be reached through a symlink: {value}")
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise RunnerFailure(f"{description} must be an executable regular file: {resolved}")
    return resolved


def existing_file(value: str, description: str) -> Path:
    try:
        path = Path(value).expanduser().resolve(strict=True)
    except OSError as exc:
        raise RunnerFailure(f"{description} does not exist: {value}: {exc}") from exc
    if not path.is_file():
        raise RunnerFailure(f"{description} is not a regular file: {path}")
    return path


def existing_directory(value: str, description: str) -> Path:
    try:
        path = Path(value).expanduser().resolve(strict=True)
    except OSError as exc:
        raise RunnerFailure(f"{description} does not exist: {value}: {exc}") from exc
    if not path.is_dir():
        raise RunnerFailure(f"{description} is not a directory: {path}")
    return path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def private_directory(path: Path) -> None:
    path.mkdir(mode=0o700)
    path.chmod(0o700)


def private_file(path: Path, data: bytes = b"") -> None:
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
    except BaseException:
        try:
            path.unlink()
        except OSError:
            pass
        raise
    path.chmod(0o600)


def write_json_private(path: Path, document: dict[str, Any]) -> None:
    temporary = path.with_name(path.name + ".tmp")
    encoded = (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        path.chmod(0o600)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def process_alive(process: subprocess.Popen[bytes]) -> bool:
    return process.poll() is None


def stop_process(process: subprocess.Popen[bytes] | None) -> None:
    if process is None:
        return
    try:
        # Every child is started in its own session/process group, so this only
        # signals processes created by this runner.
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        if process.poll() is not None:
            return
    except OSError:
        try:
            process.terminate()
        except OSError:
            pass
    try:
        process.wait(timeout=PROCESS_STOP_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        pass
    deadline = time.monotonic() + PROCESS_STOP_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        try:
            os.killpg(process.pid, 0)
        except ProcessLookupError:
            return
        except OSError:
            return
        time.sleep(0.1)
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    except OSError:
        try:
            process.kill()
        except OSError:
            pass
    try:
        process.wait(timeout=PROCESS_STOP_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        pass


def launch(command: list[str], cwd: Path, environment: dict[str, str], log_path: Path) -> subprocess.Popen[bytes]:
    try:
        with log_path.open("ab") as log_stream:
            process = subprocess.Popen(
                command,
                cwd=str(cwd),
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=log_stream,
                stderr=subprocess.STDOUT,
                start_new_session=True,
                close_fds=True,
            )
    except OSError as exc:
        raise RunnerFailure(f"could not start {command[0]}: {exc}") from exc
    return process


def wait_for_server(process: subprocess.Popen[bytes], log_path: Path, port: int) -> None:
    deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
    marker = re.compile(rf"Listening on .*:{port}(?:\D|$)")
    while time.monotonic() < deadline:
        if not process_alive(process):
            raise RunnerFailure(
                f"server exited before listening (exit {process.returncode}); see {log_path}"
            )
        try:
            log_text = log_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            log_text = ""
        if any(marker.search(line) for line in log_text.splitlines()):
            return
        time.sleep(POLL_INTERVAL_SECONDS)
    raise RunnerFailure(f"server did not report Listening on port {port} within 20 seconds; see {log_path}")


def wait_for_gui(process: subprocess.Popen[bytes], trace_path: Path, socket_path: Path) -> None:
    deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        if not process_alive(process):
            raise RunnerFailure(
                f"GUI exited before diagnostic-ready (exit {process.returncode}); see {trace_path.parent / 'gui.log'}"
            )
        try:
            ready = '"kind":"diagnostic-ready"' in trace_path.read_text(
                encoding="utf-8", errors="replace"
            )
        except OSError:
            ready = False
        if ready and socket_path.exists():
            return
        time.sleep(POLL_INTERVAL_SECONDS)
    raise RunnerFailure(
        f"GUI did not report diagnostic-ready with a controller socket within 20 seconds; "
        f"see {trace_path.parent / 'gui.log'}"
    )


def runner_result(level: str, failure: str, metadata_path: Path) -> dict[str, Any]:
    return {
        "level": level,
        "physical_hardware_tested": False,
        "passed": False,
        "failures": [failure],
        "metadata": str(metadata_path),
    }


def create_metadata(args: argparse.Namespace, paths: dict[str, Path], level: str) -> dict[str, Any]:
    return {
        "level": level,
        "offscreen_requested": bool(args.offscreen),
        "qt_qpa_platform": os.environ.get("QT_QPA_PLATFORM") if not args.offscreen else "offscreen",
        "paths": {key: str(path) for key, path in paths.items()},
        "sha256": {
            "server": sha256_file(paths["server"]),
            "gui": sha256_file(paths["gui"]),
        },
        "qt_version": args.qt_version,
        "physical_hardware_tested": False,
        "seed": args.seed,
        "runtime_source": "caller-provided staged assets",
        "port": args.port,
        "websocket_port": args.port + 1,
        "seconds": args.seconds,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True, help="existing staged assets directory and child cwd")
    parser.add_argument("--server", required=True, help="absolute real server executable; symlink paths are rejected")
    parser.add_argument("--gui", required=True, help="absolute real GUI executable; symlink paths are rejected")
    parser.add_argument("--server-ini", required=True, help="existing server INI")
    parser.add_argument("--gui-ini", help="optional INI copied into the isolated Qt user config")
    parser.add_argument("--run-dir", required=True, help="new output directory; existing paths are never overwritten")
    parser.add_argument("--port", type=parse_port, default=19549)
    parser.add_argument("--seed", type=parse_seed, default=367154)
    parser.add_argument("--seconds", type=parse_seconds, default=90)
    parser.add_argument("--offscreen", action="store_true", help="explicitly set QT_QPA_PLATFORM=offscreen")
    parser.add_argument("--qt-version", help="optional caller-supplied Qt version marker")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not sys.platform.startswith("linux"):
        print("controller_gamepad_run.py requires Linux", file=sys.stderr)
        return 2

    run_dir = Path(os.path.abspath(os.path.expanduser(args.run_dir)))
    if run_dir.exists() or run_dir.is_symlink():
        print(f"run directory already exists; refusing to overwrite: {run_dir}", file=sys.stderr)
        return 2
    try:
        runtime = existing_directory(args.runtime, "runtime assets directory")
        server = absolute_existing_file(args.server, "server executable")
        gui = absolute_existing_file(args.gui, "GUI executable")
        server_ini = existing_file(args.server_ini, "server INI")
        gui_ini = existing_file(args.gui_ini, "GUI INI") if args.gui_ini else None
        driver = Path(__file__).resolve(strict=True).with_name("controller_gamepad_driver.py")
        if not driver.is_file():
            raise RunnerFailure(f"adjacent controller driver is missing: {driver}")
        run_dir.parent.mkdir(parents=True, exist_ok=True)
        private_directory(run_dir)
    except (OSError, RunnerFailure) as exc:
        print(f"runner setup failed: {exc}", file=sys.stderr)
        return 2

    paths = {
        "runtime": runtime,
        "server": server,
        "gui": gui,
        "server_ini": server_ini,
        "run_dir": run_dir,
        "driver": driver,
        "server_log": run_dir / "server.log",
        "gui_log": run_dir / "gui.log",
        "driver_log": run_dir / "driver.log",
        "server_trace": run_dir / "server.jsonl",
        "gui_trace": run_dir / "gui.jsonl",
        "controller_socket": run_dir / "controller.sock",
        "result": run_dir / "result.json",
        "metadata": run_dir / "metadata.json",
    }
    if gui_ini:
        paths["gui_ini_source"] = gui_ini

    process_environment = os.environ.copy()
    config_home = run_dir / "user-config"
    data_home = run_dir / "user-data"
    runtime_home = run_dir / "runtime-dir"
    for directory in (config_home, data_home, runtime_home):
        private_directory(directory)
    process_environment["XDG_CONFIG_HOME"] = str(config_home)
    process_environment["XDG_DATA_HOME"] = str(data_home)
    process_environment["XDG_RUNTIME_DIR"] = str(runtime_home)
    paths.update({
        "xdg_config_home": config_home,
        "xdg_data_home": data_home,
        "xdg_runtime_dir": runtime_home,
    })
    if args.offscreen:
        process_environment["QT_QPA_PLATFORM"] = "offscreen"
    level = "offscreen" if process_environment.get("QT_QPA_PLATFORM", "").lower() == "offscreen" else "real-GUI"

    try:
        app_config_dir = config_home / "QSanguosha.org"
        private_directory(app_config_dir)
        if gui_ini:
            copied_ini = app_config_dir / "QSanguosha.conf"
            shutil.copyfile(gui_ini, copied_ini)
            copied_ini.chmod(0o600)
            paths["gui_ini_copy"] = copied_ini
        for log_key in ("server_log", "gui_log", "driver_log", "server_trace", "gui_trace"):
            private_file(paths[log_key])
        metadata = create_metadata(args, paths, level)
        write_json_private(paths["metadata"], metadata)
    except OSError as exc:
        result = runner_result(level, f"private run setup failed: {exc}", paths["metadata"])
        try:
            write_json_private(paths["result"], result)
        except OSError:
            pass
        print(result["failures"][0], file=sys.stderr)
        return 1

    server_command = [
        str(server),
        "--asset-root", str(runtime),
        "--config", str(server_ini),
        "--port", str(args.port),
        "--websocket-port", str(args.port + 1),
        "--seed", str(args.seed),
        "--operation-timeout", "0",
        "--ai-delay", "0",
    ]
    gui_command = [
        str(gui),
        "-connect:127.0.0.1:" + str(args.port),
        "--seed", str(args.seed),
        "--controller-virtual-input", str(paths["controller_socket"]),
        "--controller-trace", str(paths["gui_trace"]),
    ]
    driver_command = [
        sys.executable, str(driver),
        "--socket", str(paths["controller_socket"]),
        "--trace", str(paths["gui_trace"]),
        "--server-trace", str(paths["server_trace"]),
        "--play-basic",
        "--return-menu",
        "--seconds", str(args.seconds),
        "--result", str(paths["result"]),
    ]
    server_environment = process_environment.copy()
    server_environment["QSAN_CONTROLLER_SERVER_TRACE"] = str(paths["server_trace"])

    server_process = None
    gui_process = None
    driver_process = None
    failure = None
    driver_exit = 1
    try:
        server_process = launch(server_command, runtime, server_environment, paths["server_log"])
        wait_for_server(server_process, paths["server_log"], args.port)

        gui_process = launch(gui_command, runtime, process_environment, paths["gui_log"])
        wait_for_gui(gui_process, paths["gui_trace"], paths["controller_socket"])

        driver_process = launch(driver_command, runtime, process_environment, paths["driver_log"])
        try:
            driver_exit = driver_process.wait(timeout=args.seconds + 25)
        except subprocess.TimeoutExpired as exc:
            raise RunnerFailure(
                f"driver exceeded bounded runtime of {args.seconds + 25} seconds; see {paths['driver_log']}"
            ) from exc
    except RunnerFailure as exc:
        failure = str(exc)
    finally:
        stop_process(driver_process)
        stop_process(gui_process)
        stop_process(server_process)

    if failure:
        failed = runner_result(level, failure, paths["metadata"])
        write_json_private(paths["result"], failed)
        print(json.dumps(failed, ensure_ascii=False, indent=2), file=sys.stderr)
        return 124 if failure.startswith("driver exceeded bounded runtime") else 1

    try:
        if not paths["result"].is_file():
            raise RunnerFailure(f"driver exited {driver_exit} without creating result.json")
        result = json.loads(paths["result"].read_text(encoding="utf-8"))
        if not isinstance(result, dict):
            raise RunnerFailure("driver result.json is not an object")
        paths["result"].chmod(0o600)
    except (OSError, json.JSONDecodeError, RunnerFailure) as exc:
        failed = runner_result(level, f"invalid or missing driver result: {exc}", paths["metadata"])
        write_json_private(paths["result"], failed)
        print(json.dumps(failed, ensure_ascii=False, indent=2), file=sys.stderr)
        return driver_exit if driver_exit != 0 else 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return driver_exit


if __name__ == "__main__":
    raise SystemExit(main())
