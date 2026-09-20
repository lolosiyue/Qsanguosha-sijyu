#!/usr/bin/env python3
"""Run a native server contract test from an isolated builtin asset root."""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load fixture helper {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path, required=True)
    parser.add_argument("child", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.child or args.child[0] != "--":
        parser.error("expected '--' followed by native test arguments")

    source = args.asset_root.resolve()
    runner = args.runner.resolve()
    if not runner.is_file():
        parser.error(f"runner does not exist: {runner}")
    if not source.is_dir():
        parser.error(f"asset root does not exist: {source}")

    fixtures = load_module("qsan_fixture_helpers", ROOT / "tests/client_runtime/check-fixtures.py")
    rules = load_module("qsan_rules_helpers", ROOT / "tests/client_runtime/check-rules-bundle.py")
    with tempfile.TemporaryDirectory(prefix="qsan-server-contract-") as temporary:
        staged = Path(temporary)
        user_data = staged / "user-data"
        config_home = staged / "config"
        data_home = staged / "data"
        # Keep the native process on the real builtin rules and SmartAI files,
        # while excluding unrelated extensions and all user-machine state.
        fixtures.stage_builtin_assets(source, staged)
        rules.stage_server_ai(source, staged)
        for directory in (user_data, config_home, data_home):
            directory.mkdir()
        environment = os.environ.copy()
        environment["QSAN_ASSET_ROOT"] = str(staged)
        environment["QSAN_USER_DATA_ROOT"] = str(user_data)
        environment["XDG_CONFIG_HOME"] = str(config_home)
        environment["XDG_DATA_HOME"] = str(data_home)
        environment["APPDATA"] = str(config_home)
        environment["LOCALAPPDATA"] = str(data_home)
        command = [str(runner), *args.child[1:]]
        completed = subprocess.run(command, cwd=staged, env=environment)
        return completed.returncode


if __name__ == "__main__":
    sys.exit(main())
