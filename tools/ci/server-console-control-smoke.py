#!/usr/bin/env python3

"""Exercise the server console control commands that need a live client.

The piped gate in tools/ci/server-console-smoke.sh covers every command that
can be judged from console output alone.  Three of them cannot: maintenance
mode is only observable through a signup reply, and close/end-game have to be
told apart by whether a room is actually playing.  This script drives the same
console over a pipe while speaking Protocol V2 on the side.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PROTOCOL_SMOKE = Path(__file__).resolve().parent / "docker-protocol-smoke.py"


def load_protocol_smoke():
    specification = importlib.util.spec_from_file_location("protocol_smoke", PROTOCOL_SMOKE)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


class SmokeFailure(RuntimeError):
    pass


class ServerUnderTest:
    """Owns the server process, its console pipe and its captured output."""

    def __init__(self, executable: Path, config_root: Path, timeout: float) -> None:
        self.timeout = timeout
        self.lines: list[str] = []
        self._lock = threading.Lock()

        config = config_root / "server.ini"
        config.write_text("[General]\nGameMode=02p\nBindAddress=127.0.0.1\n",
                          encoding="utf-8")
        environment = dict(os.environ, XDG_CONFIG_HOME=str(config_root))
        self.process = subprocess.Popen(
            [str(executable), "--config", str(config), "--port", "0",
             "--websocket-port", "0"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, env=environment, text=True, bufsize=1,
            cwd=str(REPOSITORY_ROOT))
        self._reader = threading.Thread(target=self._read_output, daemon=True)
        self._reader.start()

    def _read_output(self) -> None:
        for line in self.process.stdout:
            with self._lock:
                self.lines.append(line.rstrip("\n"))

    def snapshot(self) -> list[str]:
        with self._lock:
            return list(self.lines)

    def mark(self) -> int:
        with self._lock:
            return len(self.lines)

    def console(self, command: str) -> int:
        """Send a console command and return where its output starts."""
        if self.process.poll() is not None:
            raise SmokeFailure(f"server exited before '{command}' could be sent")
        mark = self.mark()
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        return mark

    def wait_for(self, pattern: str, what: str, timeout: float | None = None,
                 start: int = 0) -> str:
        expression = re.compile(pattern)
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        seen = start
        while time.monotonic() < deadline:
            lines = self.snapshot()
            for line in lines[seen:]:
                if expression.search(line):
                    return line
            exited = self.process.poll() is not None
            seen = len(lines)
            if exited and seen == self.mark():
                raise SmokeFailure(f"server exited while waiting for {what}")
            time.sleep(0.1)
        raise SmokeFailure(f"timed out waiting for {what}")

    def console_until(self, command: str, pattern: str, what: str,
                      timeout: float | None = None) -> str:
        """Re-issue a query until its answer matches; room disposal is deferred."""
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        last: SmokeFailure | None = None
        while time.monotonic() < deadline:
            mark = self.console(command)
            try:
                return self.wait_for(pattern, what, timeout=2.0, start=mark)
            except SmokeFailure as failure:
                last = failure
        raise last or SmokeFailure(f"timed out waiting for {what}")

    def port(self) -> int:
        line = self.wait_for(r"Listening on 127\.0\.0\.1:\d+", "the listening endpoint")
        return int(re.search(r"127\.0\.0\.1:(\d+)", line).group(1))

    def terminate(self) -> int:
        if self.process.poll() is None:
            try:
                self.console("shutdown")
            except SmokeFailure:
                pass
        try:
            self.process.wait(timeout=self.timeout)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise SmokeFailure("server did not exit after 'shutdown'")
        return self.process.returncode


class Client:
    """A Protocol V2 signup that stays connected until it is closed."""

    def __init__(self, protocol, commands: dict[str, int], port: int,
                 name: str, timeout: float) -> None:
        self.protocol = protocol
        self.commands = commands
        self.timeout = timeout
        self.connection = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        stream = protocol.ProtocolV2Stream(self.connection, timeout)
        deadline = time.monotonic() + timeout
        protocol.wait_for(stream, deadline,
                          lambda packet: packet.get("command")
                          == commands["S_COMMAND_CHECK_VERSION"]
                          and packet.get("type") == "notification")
        request_id = stream.send(
            "request", "client", "lobby", commands["S_COMMAND_SIGNUP"],
            {"schema_version": 1, "reconnect_requested": False,
             "screen_name": name, "avatar": ""})
        reply = protocol.wait_for(stream, deadline,
                                  lambda packet: packet.get("command")
                                  == commands["S_COMMAND_SIGNUP"]
                                  and packet.get("type") == "reply"
                                  and str(packet.get("reply_to")) == str(request_id))
        payload = protocol.payload_map(reply)
        self.accepted = payload.get("accepted") is True
        self.error_code = payload.get("error_code")
        self.message = payload.get("message")

    def close(self) -> None:
        try:
            self.connection.close()
        except OSError:
            pass


def run(arguments: argparse.Namespace) -> list[str]:
    protocol = load_protocol_smoke()
    commands = protocol.command_values(arguments.protocol_header)
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="qsanguosha-console-control.") as root:
        server = ServerUnderTest(Path(arguments.server), Path(root), arguments.timeout)
        try:
            port = server.port()

            # 2.3 robots/start: an empty waiting room is filled and started.
            mark = server.console("start")
            server.wait_for(r"^Game started\.$", "the console start acknowledgement",
                            start=mark)
            server.wait_for(r"game_started", "the room to report a started game",
                            timeout=arguments.game_timeout)
            mark = server.console("rooms --json")
            server.wait_for(r'"state":"playing"',
                            "rooms --json to report the running game",
                            timeout=arguments.game_timeout, start=mark)

            # 2.1 close/end-game: a running room refuses close and ends cleanly.
            mark = server.console("close 0")
            server.wait_for(r"has a game in progress; use end-game",
                            "close refusing a running room", start=mark)
            mark = server.console("end-game 0")
            server.wait_for(r"game_over .*winner=\.", "the game to end through GAME_OVER",
                            timeout=arguments.game_timeout, start=mark)
            server.console_until("rooms", r"^No rooms\.$",
                                 "the ended room to be reclaimed")

            # 2.2 maintenance: refuse new signups, leave the connected one alone.
            first = Client(protocol, commands, port, "console-smoke-a", arguments.timeout)
            if not first.accepted:
                failures.append(f"signup was refused before maintenance: {first.error_code}")
            mark = server.console("maintenance on")
            server.wait_for(r"^Maintenance mode enabled", "the maintenance acknowledgement",
                            start=mark)
            refused = Client(protocol, commands, port, "console-smoke-b", arguments.timeout)
            if refused.accepted or refused.error_code != "server_maintenance":
                failures.append(
                    f"maintenance accepted a new signup: {refused.error_code!r}")
            refused.close()
            mark = server.console("rooms --json")
            occupied = server.wait_for(r'^\{"rooms":\[', "the room listing under maintenance",
                                       start=mark)
            if '"players":1' not in occupied:
                failures.append(f"maintenance disturbed the connected player: {occupied}")

            mark = server.console("maintenance off")
            server.wait_for(r"^Maintenance mode disabled",
                            "the maintenance-off acknowledgement", start=mark)
            readmitted = Client(protocol, commands, port, "console-smoke-c", arguments.timeout)
            if not readmitted.accepted:
                failures.append(
                    f"signup still refused after maintenance off: {readmitted.error_code}")
            readmitted.close()

            # 2.1 close: a waiting room is dissolved together with its players.
            mark = server.console("close all")
            server.wait_for(r"^Waiting rooms closed: [1-9]", "the waiting room to be closed",
                            start=mark)
            first.close()
            mark = server.console("addrobot 1")
            server.wait_for(r"^No room is waiting", "addrobot to report the empty server",
                            start=mark)

            exit_code = server.terminate()
            if exit_code != 0:
                failures.append(f"server exited with {exit_code}")
        except SmokeFailure as failure:
            failures.append(str(failure))
            if server.process.poll() is None:
                server.process.kill()
                server.process.wait()
        finally:
            if arguments.log_file:
                Path(arguments.log_file).parent.mkdir(parents=True, exist_ok=True)
                Path(arguments.log_file).write_text("\n".join(server.snapshot()) + "\n",
                                                    encoding="utf-8")
    return failures


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("server", help="path to qsanguosha_server")
    parser.add_argument("--protocol-header",
                        type=Path,
                        default=REPOSITORY_ROOT / "src" / "core" / "protocol.h")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--game-timeout", type=float, default=120.0,
                        help="budget for engine start-up and the first safe point")
    parser.add_argument("--log-file", default="")
    arguments = parser.parse_args()
    if not os.access(arguments.server, os.X_OK):
        parser.error(f"server executable does not exist or is not executable: {arguments.server}")
    if not arguments.protocol_header.is_file():
        parser.error(f"protocol header does not exist: {arguments.protocol_header}")
    return arguments


def main() -> int:
    arguments = parse_arguments()
    failures = run(arguments)
    for failure in failures:
        print(f"[server-console-control-smoke] {failure}", file=sys.stderr)
    if failures:
        return 1
    print("[server-console-control-smoke] maintenance, close, end-game and start passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
