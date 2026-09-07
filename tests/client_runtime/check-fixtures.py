#!/usr/bin/env python3
"""Native rules fixture contract and deterministic-output checks (stdlib only)."""
from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from typing import Any


def expect_subset(actual: Any, expected: Any, where: str = "result") -> None:
    """Objects may specify a subset; arrays, value types and scalars are exact."""
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            raise AssertionError(f"{where}: expected an object, got {actual!r}")
        for key, value in expected.items():
            if key not in actual:
                raise AssertionError(f"{where}.{key}: missing key")
            expect_subset(actual[key], value, f"{where}.{key}")
    elif isinstance(expected, list):
        if not isinstance(actual, list) or len(actual) != len(expected):
            raise AssertionError(f"{where}: array length/type mismatch: {actual!r} != {expected!r}")
        for index, value in enumerate(expected):
            expect_subset(actual[index], value, f"{where}[{index}]")
    elif type(actual) is not type(expected) or actual != expected:
        raise AssertionError(f"{where}: {actual!r} != {expected!r}")


def validate_fixture(fixture: dict[str, Any]) -> None:
    if type(fixture.get("schema_version")) is not int or fixture["schema_version"] != 1:
        raise AssertionError("unsupported fixture schema_version")
    for key in ("name", "self"):
        if not isinstance(fixture.get(key), str) or not fixture[key]:
            raise AssertionError(f"fixture requires {key}")
    queries = fixture.get("queries")
    if not isinstance(queries, list) or not queries:
        raise AssertionError("fixture has no queries")
    names: set[str] = set()
    for query in queries:
        name = query.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise AssertionError("missing/duplicate query name")
        names.add(name)
        identity = query.get("request_id")
        if (not isinstance(identity, str) or not re.fullmatch(r"[1-9][0-9]*", identity)
                or int(identity) > 2**64 - 1):
            raise AssertionError(f"{name}: request_id is not a decimal uint64 string")
        if query.get("reason") not in ("play", "response", "response_use"):
            raise AssertionError(f"{name}: invalid reason")
        if ("card" in query) == ("skill" in query):
            raise AssertionError(f"{name}: exactly one selection source is required")
        if not isinstance(query.get("expect"), dict) or "can_confirm" not in query["expect"]:
            raise AssertionError(f"{name}: checked-in expectations must include can_confirm")


def verify_result(fixture: dict[str, Any], result: dict[str, Any]) -> None:
    expect_subset(result, {"schema_version": 1, "name": fixture["name"]})
    if not re.fullmatch(r"[0-9a-f]{64}", result.get("card_registry_sha256", "")):
        raise AssertionError("missing registry fingerprint")
    queries = result.get("queries", [])
    if len(queries) != len(fixture["queries"]):
        raise AssertionError("runner dropped or added queries")
    for source, actual in zip(fixture["queries"], queries):
        expect_subset(actual, {"name": source["name"], "request_id": source["request_id"]})
        expect_subset(actual, source["expect"], source["name"])
        if actual["can_confirm"]:
            expect_subset(actual, {"known": True, "card_ready": True,
                                   "target_validation": {"known": True, "valid": True}})
            expect_subset(actual["response"], {"request_id": source["request_id"],
                                             "targets": source.get("targets", []),
                                             "card_text": actual["card_text"]})
            expect_subset(actual["wire"], {"reply_to": source["request_id"],
                                         "payload": {"card_text": actual["card_text"]}})
            expected_skill = source.get("skill", {})
            expect_subset(actual["response"], {
                "activation_skill_name": expected_skill.get("name", ""),
                "activation_skill_instance_id": expected_skill.get("instance_id", 0)})
        else:
            expect_subset(actual, {"response": None, "wire": None})


def invoke(args: argparse.Namespace, fixture: Path, tag: str, deterministic_hash: bool,
           error_text: str | None = None) -> bytes:
    destination = args.artifacts / f"{tag}.json"
    destination.unlink(missing_ok=True)
    env = os.environ.copy()
    if deterministic_hash:
        env["QT_HASH_SEED"] = "0"
    else:
        env.pop("QT_HASH_SEED", None)
    # Settings Config is constructed before main(): isolate its Windows
    # config.ini (CWD) and Unix QSettings (XDG) before launching the process.
    with tempfile.TemporaryDirectory(prefix="rules-", dir=args.artifacts) as scratch:
        root = Path(scratch)
        env["XDG_CONFIG_HOME"] = str(root / "config")
        env["XDG_DATA_HOME"] = str(root / "data")
        env["APPDATA"] = str(root / "appdata")
        env["LOCALAPPDATA"] = str(root / "localappdata")
        process = subprocess.run(
            [str(args.runner), "--fixture", str(fixture), "--output", str(destination),
             "--asset-root", str(args.asset_root)],
            cwd=root, env=env, capture_output=True, timeout=60, check=False)
    (args.artifacts / f"{tag}.stdout.log").write_bytes(process.stdout)
    (args.artifacts / f"{tag}.stderr.log").write_bytes(process.stderr)
    if error_text is not None:
        if process.returncode == 0 or destination.exists():
            raise AssertionError(f"{tag}: invalid fixture succeeded or published a result")
        logs = (process.stdout + process.stderr).decode("utf-8", errors="replace")
        if error_text not in logs:
            raise AssertionError(f"{tag}: failed for the wrong reason; wanted {error_text!r}: {logs[-3000:]}")
        return b""
    if process.returncode != 0 or not destination.is_file():
        raise AssertionError(f"{tag}: runner exit {process.returncode}: "
                             + process.stderr.decode("utf-8", errors="replace")[-4000:])
    return destination.read_bytes()


def check(args: argparse.Namespace) -> None:
    args.artifacts.mkdir(parents=True, exist_ok=True)
    paths = sorted(args.fixtures.glob("*.json"))
    if not paths:
        raise AssertionError("no checked-in fixtures found")
    count = 0
    baseline: dict[str, Any] | None = None
    for path in paths:
        fixture = json.loads(path.read_text(encoding="utf-8"))
        validate_fixture(fixture)
        if baseline is None and "card" in fixture["queries"][0]:
            baseline = fixture
        first = invoke(args, path, path.stem + ".first", True)
        second = invoke(args, path, path.stem + ".second", False)
        verify_result(fixture, json.loads(first))
        verify_result(fixture, json.loads(second))
        if first != second:
            raise AssertionError(f"{path.name}: output drifted between fresh processes/hash seeds")
        count += len(fixture["queries"])
    if baseline is None:
        raise AssertionError("negative tests need a physical-card fixture")
    invalid = args.artifacts / "invalid"
    invalid.mkdir(exist_ok=True)
    malformed = invalid / "malformed.json"
    malformed.write_text("{", encoding="utf-8")
    invoke(args, malformed, "negative-malformed", True, "schema_version=1")
    for name, key, value, diagnostic in (
        ("reason", "reason", "typo", "unknown card-use reason"),
        ("alias", "card", "missing-fixture-key", "unknown fixture card key"),
        ("numeric-id", "request_id", 42, "request_id"),
        ("overflow-id", "request_id", "18446744073709551616", "request_id"),
    ):
        damaged = copy.deepcopy(baseline)
        damaged["queries"] = [damaged["queries"][0]]
        damaged["queries"][0][key] = value
        path = invalid / f"{name}.json"
        path.write_text(json.dumps(damaged), encoding="utf-8")
        invoke(args, path, "negative-" + name, True, diagnostic)
    damaged = copy.deepcopy(baseline)
    damaged["schema_version"] = 2
    path = invalid / "schema.json"
    path.write_text(json.dumps(damaged), encoding="utf-8")
    invoke(args, path, "negative-schema", True, "schema_version=1")
    print(f"[AUTOTEST] CLIENT_RULES_FIXTURES_RESULT status=PASS fixtures={len(paths)} "
          f"queries={count} deterministic_runs={len(paths) * 2} negative_cases=6")


class HarnessTests(unittest.TestCase):
    def test_process_configuration_is_isolated(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            args = argparse.Namespace(artifacts=root, runner=Path(sys.executable), asset_root=root)
            launched: list[Path] = []

            def fake_process(command: list[str], **kwargs: Any) -> subprocess.CompletedProcess[bytes]:
                cwd = Path(kwargs["cwd"])
                self.assertNotEqual(cwd, root)
                self.assertTrue(cwd.is_dir())
                self.assertEqual(Path(kwargs["env"]["XDG_CONFIG_HOME"]).parent, cwd)
                self.assertEqual(Path(kwargs["env"]["APPDATA"]).parent, cwd)
                launched.append(cwd)
                Path(command[command.index("--output") + 1]).write_bytes(b"{}\n")
                return subprocess.CompletedProcess(command, 0, b"", b"")

            with patch("subprocess.run", side_effect=fake_process):
                self.assertEqual(invoke(args, root / "fixture.json", "isolation", True), b"{}\n")
            self.assertFalse(launched[0].exists())

    def test_subset(self) -> None:
        expect_subset({"nested": {"ok": True, "extra": 2}}, {"nested": {"ok": True}})

    def test_missing(self) -> None:
        with self.assertRaises(AssertionError):
            expect_subset({}, {"can_confirm": False})

    def test_boolean_is_not_integer(self) -> None:
        with self.assertRaises(AssertionError):
            expect_subset({"ok": 1}, {"ok": True})

    def test_array_order(self) -> None:
        with self.assertRaises(AssertionError):
            expect_subset(["sgs2", "sgs1"], ["sgs1", "sgs2"])

    def test_duplicate_votes_are_significant(self) -> None:
        with self.assertRaises(AssertionError):
            expect_subset(["sgs2"], ["sgs2", "sgs2"])

    def test_null_is_not_empty(self) -> None:
        with self.assertRaises(AssertionError):
            expect_subset({}, None)

    def test_large_ids_stay_strings(self) -> None:
        expect_subset("18446744073709551615", "18446744073709551615")
        with self.assertRaises(AssertionError):
            expect_subset(18446744073709551615, "18446744073709551615")

    def test_output_mismatch_fails(self) -> None:
        with self.assertRaises(AssertionError):
            expect_subset({"can_confirm": True}, {"can_confirm": False})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--validate-fixtures", action="store_true")
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--fixtures", type=Path)
    parser.add_argument("--asset-root", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(HarnessTests)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    if args.validate_fixtures:
        if args.fixtures is None:
            parser.error("--fixtures is required")
        paths = sorted(args.fixtures.glob("*.json"))
        if not paths:
            raise AssertionError("no fixture files")
        for path in paths:
            validate_fixture(json.loads(path.read_text(encoding="utf-8")))
        print(f"Fixture input/expectation structure validated: {len(paths)} files (no native execution)")
        return 0
    for name in ("runner", "fixtures", "asset_root", "artifacts"):
        if getattr(args, name) is None:
            parser.error(f"--{name.replace('_', '-')} is required")
        setattr(args, name, getattr(args, name).resolve())
    if not args.runner.is_file():
        parser.error("runner executable does not exist")
    try:
        check(args)
    except (AssertionError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"[AUTOTEST] CLIENT_RULES_FIXTURES_RESULT status=FAIL detail={error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
