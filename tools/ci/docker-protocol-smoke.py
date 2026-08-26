#!/usr/bin/env python3

"""Perform the existing Level 2 handshake/signup sequence against a container."""

from __future__ import annotations

import argparse
import base64
import json
import re
import socket
import sys
import time
from pathlib import Path
from typing import Any


class SmokeFailure(RuntimeError):
    pass


def command_values(protocol_header: Path) -> dict[str, int]:
    text = protocol_header.read_text(encoding="utf-8")
    match = re.search(r"enum\s+CommandType\s*\{(?P<body>.*?)\};", text, re.DOTALL)
    if match is None:
        raise SmokeFailure(f"CommandType enum not found in {protocol_header}")

    body = re.sub(r"//[^\n]*", "", match.group("body"))
    values: dict[str, int] = {}
    current = -1
    for entry in body.split(","):
        entry = entry.strip()
        if not entry:
            continue
        if "=" in entry:
            name, raw_value = entry.split("=", 1)
            try:
                current = int(raw_value.strip(), 0)
            except ValueError as error:
                raise SmokeFailure(f"unsupported CommandType value: {entry}") from error
        else:
            name = entry
            current += 1
        values[name.strip()] = current

    required = {
        "S_COMMAND_CHECK_VERSION",
        "S_COMMAND_SETUP",
        "S_COMMAND_SET_PROPERTY",
        "S_COMMAND_SIGNUP",
    }
    missing = sorted(required.difference(values))
    if missing:
        raise SmokeFailure(f"required protocol commands are missing: {', '.join(missing)}")
    return values


class PacketStream:
    def __init__(self, connection: socket.socket, timeout: float) -> None:
        self.connection = connection
        self.timeout = timeout
        self.buffer = bytearray()

    def receive(self, deadline: float) -> list[Any]:
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SmokeFailure(f"timed out after {self.timeout:.1f}s waiting for a packet")
            self.connection.settimeout(remaining)
            try:
                data = self.connection.recv(65536)
            except TimeoutError as error:
                raise SmokeFailure(
                    f"timed out after {self.timeout:.1f}s waiting for a packet"
                ) from error
            if not data:
                raise SmokeFailure("server closed the connection while waiting for a packet")
            self.buffer.extend(data)

        raw_line, _, remainder = self.buffer.partition(b"\n")
        self.buffer = bytearray(remainder)
        raw_line = raw_line.rstrip(b"\r")
        try:
            packet = json.loads(raw_line)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            preview = raw_line[:200].decode("utf-8", errors="replace")
            raise SmokeFailure(f"invalid JSON packet from server: {preview}") from error
        if not isinstance(packet, list) or not 4 <= len(packet) <= 5:
            raise SmokeFailure(f"invalid protocol packet shape: {packet!r}")
        if any(not isinstance(value, int) for value in packet[:4]):
            raise SmokeFailure(f"invalid protocol packet header: {packet!r}")
        return packet


def packet_body(packet: list[Any]) -> Any:
    return packet[4] if len(packet) == 5 else None


def run_smoke(arguments: argparse.Namespace) -> tuple[str, str, str]:
    commands = command_values(arguments.protocol_header)
    deadline = time.monotonic() + arguments.timeout
    try:
        connection = socket.create_connection(
            (arguments.host, arguments.port), timeout=arguments.timeout
        )
    except OSError as error:
        raise SmokeFailure(
            f"TCP connect to {arguments.host}:{arguments.port} failed: {error}"
        ) from error

    with connection:
        stream = PacketStream(connection, arguments.timeout)
        handshake: dict[int, Any] = {}
        required_handshake = {
            commands["S_COMMAND_CHECK_VERSION"],
            commands["S_COMMAND_SETUP"],
        }
        while required_handshake.difference(handshake):
            packet = stream.receive(deadline)
            command = packet[3]
            if command in required_handshake:
                handshake[command] = packet_body(packet)

        version = handshake[commands["S_COMMAND_CHECK_VERSION"]]
        setup = handshake[commands["S_COMMAND_SETUP"]]
        if not isinstance(version, str) or not version:
            raise SmokeFailure("version handshake body is not a non-empty string")
        if not isinstance(setup, str) or not setup:
            raise SmokeFailure("setup handshake body is not a non-empty string")

        encoded_name = base64.b64encode(arguments.name.encode("utf-8")).decode("ascii")
        client_notification_to_room = 0x40 | 0x04 | 0x100
        signup = [
            0,
            0,
            client_notification_to_room,
            commands["S_COMMAND_SIGNUP"],
            [False, encoded_name, ""],
        ]
        connection.sendall(
            json.dumps(signup, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
            + b"\n"
        )

        player_id: str | None = None
        owner: bool | None = None
        while player_id is None or owner is None:
            packet = stream.receive(deadline)
            if packet[3] != commands["S_COMMAND_SET_PROPERTY"]:
                continue
            body = packet_body(packet)
            if not isinstance(body, list) or len(body) < 3 or body[0] != "MG_SELF":
                continue
            if body[1] == "objectName" and isinstance(body[2], str) and body[2]:
                player_id = body[2]
            elif body[1] == "owner":
                owner = body[2] is True or str(body[2]).lower() == "true"

        if not owner:
            raise SmokeFailure("first signed-up player was not assigned room ownership")
        return version, setup, player_id


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--name", default="docker-smoke-player")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--protocol-header", type=Path, required=True)
    arguments = parser.parse_args()
    if not 1 <= arguments.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if arguments.timeout <= 0:
        parser.error("--timeout must be positive")
    if not arguments.protocol_header.is_file():
        parser.error(f"protocol header does not exist: {arguments.protocol_header}")
    return arguments


def main() -> int:
    arguments = parse_arguments()
    try:
        version, setup, player_id = run_smoke(arguments)
    except (OSError, SmokeFailure) as error:
        print(f"[docker-protocol-smoke] FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "[docker-protocol-smoke] PASS: "
        f"version={version!r} setup={setup!r} player_id={player_id} owner=true"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
