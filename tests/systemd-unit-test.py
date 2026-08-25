#!/usr/bin/env python3

import argparse
from pathlib import Path


def require(text: str, value: str) -> None:
    if value not in text:
        raise SystemExit(f"systemd unit omitted required setting: {value}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--service", required=True, type=Path)
    args = parser.parse_args()
    text = args.service.read_text(encoding="utf-8")

    for value in (
        "Type=simple",
        "DynamicUser=yes",
        "StateDirectory=qsanguosha",
        "LogsDirectory=qsanguosha",
        "ConditionPathExists=/etc/qsanguosha/server.ini",
        "--config /etc/qsanguosha/server.ini",
        "--log-level info",
        "--log-format text",
        "Restart=on-failure",
        "KillSignal=SIGTERM",
        "StandardOutput=journal",
        "StandardError=journal",
        "NoNewPrivileges=yes",
        "ProtectSystem=strict",
        "RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6 AF_NETLINK",
    ):
        require(text, value)

    lowered = text.lower()
    if "daemonize" in lowered or "type=forking" in lowered:
        raise SystemExit("systemd unit must keep the server in the foreground")
    if "@cmake_" in lowered:
        raise SystemExit("systemd unit contains an unresolved CMake placeholder")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
