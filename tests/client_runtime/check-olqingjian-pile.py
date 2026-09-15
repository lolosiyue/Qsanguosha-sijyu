#!/usr/bin/env python3
"""Run the E2 probe with builtin assets and settings isolated from the checkout."""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    runner = args.runner.resolve(strict=True)
    source = args.asset_root.resolve(strict=True)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    # Reuse the fixture runner's declared-content staging rather than inventing
    # extension stubs. Disable bytecode writes into its source directory too.
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(
        "rules_fixtures", Path(__file__).with_name("check-fixtures.py"))
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load builtin asset staging helper")
    fixtures = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixtures)

    with tempfile.TemporaryDirectory(prefix="olqingjian-pile-") as directory:
        root = Path(directory)
        assets = root / "assets"
        fixtures.stage_builtin_assets(source, assets)
        env = os.environ.copy()
        # Config has static initialization before main(); isolate CWD and the
        # platform settings directories before launching the native process.
        for key, relative in (("XDG_CONFIG_HOME", "config"),
                              ("XDG_DATA_HOME", "data"),
                              ("APPDATA", "appdata"),
                              ("LOCALAPPDATA", "localappdata")):
            path = root / relative
            path.mkdir()
            env[key] = str(path)
        env["QSAN_ASSET_ROOT"] = str(assets)
        env["QSAN_USER_DATA_ROOT"] = str(root / "userdata")
        return subprocess.run(
            [str(runner), "--asset-root", str(assets), "--output", str(output)],
            cwd=root, env=env, timeout=55, check=False).returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"OLQINGJIAN_PILE_SYNC launcher failed: {error}", file=sys.stderr)
        sys.exit(1)
