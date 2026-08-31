#!/usr/bin/env python3

"""Perform the Level 2 Protocol V2 handshake/signup sequence against a container."""

from __future__ import annotations

import argparse
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
        "S_COMMAND_READY",
    }
    missing = sorted(required.difference(values))
    if missing:
        raise SmokeFailure(f"required protocol commands are missing: {', '.join(missing)}")
    return values


class ProtocolV2Stream:
    def __init__(self, connection: socket.socket, timeout: float) -> None:
        self.connection = connection
        self.timeout = timeout
        self.buffer = bytearray()
        self.outgoing_id = 0

    def next_message_id(self) -> int:
        self.outgoing_id += 1
        return self.outgoing_id

    def receive(self, deadline: float) -> dict[str, Any]:
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
        if not isinstance(packet, dict) or packet.get("v") != 2:
            raise SmokeFailure(f"invalid Protocol V2 envelope: {packet!r}")
        return packet

    def send(
        self,
        message_type: str,
        source: str,
        destination: str,
        command: int,
        payload: dict[str, Any],
        *,
        message_id: int | None = None,
        reply_to: int | None = None,
    ) -> int:
        outgoing_id = message_id if message_id is not None else self.next_message_id()
        message: dict[str, Any] = {
            "v": 2,
            "type": message_type,
            "source": source,
            "destination": destination,
            "message_id": str(outgoing_id),
            "command": command,
            "payload": payload,
        }
        if reply_to is not None:
            message["reply_to"] = str(reply_to)
        wire = json.dumps(message, separators=(",", ":"), ensure_ascii=False).encode("utf-8") + b"\n"
        self.connection.sendall(wire)
        return outgoing_id


def payload_map(packet: dict[str, Any]) -> dict[str, Any]:
    payload = packet.get("payload")
    if not isinstance(payload, dict):
        raise SmokeFailure(f"packet payload is not an object: {packet!r}")
    return payload


def wait_for_command(
    stream: ProtocolV2Stream,
    deadline: float,
    command: int,
    *,
    message_type: str | None = None,
) -> dict[str, Any]:
    while True:
        packet = stream.receive(deadline)
        if packet.get("command") != command:
            continue
        if message_type is not None and packet.get("type") != message_type:
            continue
        return packet


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
        stream = ProtocolV2Stream(connection, arguments.timeout)

        hello = wait_for_command(
            stream,
            deadline,
            commands["S_COMMAND_CHECK_VERSION"],
            message_type="notification",
        )
        if hello.get("source") != "lobby" or hello.get("destination") != "client":
            raise SmokeFailure("first server frame is not typed SERVER_HELLO")
        hello_payload = payload_map(hello)
        version = hello_payload.get("game_version")
        if not isinstance(version, str) or not version:
            raise SmokeFailure("version handshake body is not a non-empty string")

        signup_id = stream.send(
            "request",
            "client",
            "lobby",
            commands["S_COMMAND_SIGNUP"],
            {
                "schema_version": 1,
                "reconnect_requested": False,
                "screen_name": arguments.name,
                "avatar": "",
            },
        )

        signup_reply: dict[str, Any] | None = None
        while signup_reply is None:
            packet = stream.receive(deadline)
            if packet.get("command") != commands["S_COMMAND_SIGNUP"]:
                continue
            if packet.get("type") != "reply":
                continue
            if str(packet.get("reply_to")) != str(signup_id):
                continue
            signup_reply = packet

        signup_payload = payload_map(signup_reply)
        if signup_payload.get("accepted") is not True:
            raise SmokeFailure(
                f"signup was rejected: {signup_payload.get('error_code')!r} "
                f"{signup_payload.get('message')!r}"
            )
        player_id = signup_payload.get("player_id")
        if not isinstance(player_id, str) or not player_id:
            raise SmokeFailure("accepted signup reply is missing player_id")

        setup_packet = wait_for_command(
            stream,
            deadline,
            commands["S_COMMAND_SETUP"],
            message_type="notification",
        )
        if setup_packet.get("source") != "lobby":
            raise SmokeFailure("setup frame is not a lobby notification")
        setup_payload = payload_map(setup_packet)
        setup = setup_payload.get("game_mode")
        if not isinstance(setup, str) or not setup:
            raise SmokeFailure("setup handshake body is not a non-empty string")

        stream.send(
            "notification",
            "client",
            "room",
            commands["S_COMMAND_READY"],
            {"schema_version": 1, "ready": True},
        )

        owner: bool | None = None
        while owner is None:
            packet = stream.receive(deadline)
            if packet.get("command") != commands["S_COMMAND_SET_PROPERTY"]:
                continue
            body = payload_map(packet)
            if body.get("action") != "property":
                continue
            if body.get("player_name") != "MG_SELF":
                continue
            if body.get("property_name") == "owner":
                value = body.get("string_value")
                owner = value is True or str(value).lower() == "true"

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
