#!/usr/bin/env python3
"""Static gate: config.lua declares exactly the extensions on disk, in order."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]


def declared_entries(config_text: str) -> list[str]:
    block = re.search(r"\n\textension_names = \{\n(.*?)\t\},\n", config_text, re.S)
    if not block:
        raise AssertionError("lua/config.lua has no extension_names block")
    lines = block.group(1).splitlines()
    entries = re.findall(r'^\t\t"([^"]+)",$', block.group(1), re.M)
    if len(entries) != len([line for line in lines if line.strip()]):
        raise AssertionError("extension_names contains a line that is not a quoted entry")
    return entries


def on_disk(root: Path) -> list[str]:
    extensions = root / "extensions"
    if extensions.is_symlink():
        raise AssertionError("extensions/ must not be a symlink")
    if not extensions.is_dir():
        raise AssertionError("extensions/ is missing")
    children = list(extensions.iterdir())
    paths = [path for path in children if path.is_file() and path.name.endswith(".lua")]
    if any(path.is_symlink() for path in children):
        raise AssertionError("extensions/ contains a symlink")
    names = [path.name for path in paths]
    lowered = [name.lower() for name in names]
    if len(set(lowered)) != len(lowered):
        raise AssertionError("extensions/ has names that collide case-insensitively")
    return ["extensions/" + name for name in sorted(names, key=str.lower)]


def check(root: Path) -> None:
    declared = declared_entries((root / "lua/config.lua").read_text(encoding="utf-8"))
    scripts = [entry.split(";", 1)[0] for entry in declared]
    disk = on_disk(root)
    if len(scripts) != len(set(scripts)):
        raise AssertionError("extension_names contains duplicate scripts")
    missing = sorted(set(disk) - set(scripts))
    extra = sorted(set(scripts) - set(disk))
    if missing:
        raise AssertionError("extensions on disk are not declared: " + ", ".join(missing))
    if extra:
        raise AssertionError("declared extensions are missing from disk: " + ", ".join(extra))
    if scripts != disk:
        raise AssertionError("declared order does not match the migrated filename order")


class SelfTest(unittest.TestCase):
    @staticmethod
    def config(entries: list[str]) -> str:
        body = "".join(f'\t\t"{entry}",\n' for entry in entries)
        return f"\n\textension_names = {{\n{body}\t}},\n"

    def test_declared_entries_parses_satellites(self):
        text = '\n\textension_names = {\n\t\t"extensions/a.lua",\n\t\t"extensions/b.lua;ai=lua/ai/b-ai.lua",\n\t},\n'
        self.assertEqual(declared_entries(text),
                         ["extensions/a.lua", "extensions/b.lua;ai=lua/ai/b-ai.lua"])

    def test_missing_block_is_an_error(self):
        with self.assertRaisesRegex(AssertionError, "no extension_names block"):
            declared_entries("config = {}\n")

    def test_case_insensitive_collision_is_an_error(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "extensions/Ab.lua").write_text("")
            (root / "extensions/aB.lua").write_text("")
            with self.assertRaisesRegex(AssertionError, "collide case-insensitively"):
                on_disk(root)

    def test_symlink_directory_is_an_error(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "real").mkdir()
            (root / "extensions").symlink_to(root / "real", target_is_directory=True)
            with self.assertRaisesRegex(AssertionError, "must not be a symlink"):
                on_disk(root)

    def test_symlink_file_is_an_error(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "real.lua").write_text("")
            (root / "extensions/link.lua").symlink_to(root / "real.lua")
            with self.assertRaisesRegex(AssertionError, "contains a symlink"):
                on_disk(root)

    def test_generator_rejects_case_collision(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "extensions/Ab.lua").write_text("")
            (root / "extensions/aB.lua").write_text("")
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/generate-extension-manifest.py"), str(root)],
                capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("collide case-insensitively", result.stderr)

    def test_empty_manifest_matches_empty_directory(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "lua").mkdir()
            (root / "lua/config.lua").write_text(self.config([]))
            check(root)

    def test_missing_extra_duplicate_and_order_are_errors(self):
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "extensions").mkdir()
            (root / "lua").mkdir()
            for name in ("a.lua", "b.lua"):
                (root / "extensions" / name).write_text("")
            with self.assertRaisesRegex(AssertionError, "not declared"):
                (root / "lua/config.lua").write_text(self.config(["extensions/a.lua"]))
                check(root)
            with self.assertRaisesRegex(AssertionError, "missing from disk"):
                (root / "lua/config.lua").write_text(self.config(["extensions/a.lua", "extensions/b.lua", "extensions/c.lua"]))
                check(root)
            with self.assertRaisesRegex(AssertionError, "duplicate scripts"):
                (root / "lua/config.lua").write_text(self.config(["extensions/a.lua", "extensions/a.lua"]))
                check(root)
            with self.assertRaisesRegex(AssertionError, "order"):
                (root / "lua/config.lua").write_text(self.config(["extensions/b.lua", "extensions/a.lua"]))
                check(root)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.main(argv=[sys.argv[0]], exit=False).result.wasSuccessful() else 1
    check(args.root)
    print("extension manifest: declaration matches extensions/ exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
