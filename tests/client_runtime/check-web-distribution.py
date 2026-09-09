#!/usr/bin/env python3
"""Validate and smoke-test a built web distribution through real Chromium."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


def browser_tools():
    spec = importlib.util.spec_from_file_location("browser_fixtures", HERE / "check-browser-fixtures.py")
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load browser fixture server")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def files_for(dist: Path) -> dict[str, Path]:
    if dist.is_symlink() or not dist.is_dir():
        raise AssertionError("distribution must be a real directory")
    result: dict[str, Path] = {}
    for path in dist.rglob("*"):
        if path.is_symlink():
            raise AssertionError("distribution contains a symlink: " + path.relative_to(dist).as_posix())
        if path.is_file():
            result["/" + path.relative_to(dist).as_posix()] = path
    return result


def json_file(files: dict[str, Path], name: str):
    path = files.get("/" + name)
    if path is None:
        raise AssertionError("distribution is missing " + name)
    try:
        return json.loads(path.read_bytes())
    except (OSError, ValueError) as error:
        raise AssertionError("invalid " + name) from error


def validate_distribution(dist: Path) -> dict:
    files = files_for(dist)
    index = files.get("/index.html")
    if index is None:
        raise AssertionError("distribution is missing index.html")
    index_text = index.read_text(encoding="utf-8")
    # Vite's generated entry references the complete hashed JS/CSS boot graph;
    # every direct asset must be present in the exact distribution allowlist.
    import re
    references = re.findall(r'(?:src|href)="([^"#?]+)', index_text)
    for reference in references:
        if reference.startswith("/") and reference not in files:
            raise AssertionError("index references missing asset: " + reference)
    translations = json_file(files, "translations.json")
    if not isinstance(translations, dict) or not translations:
        raise AssertionError("translations.json is not a non-empty object")
    if not all(isinstance(key, str) and isinstance(value, str)
               for key, value in translations.items()):
        raise AssertionError("translations.json is not a non-empty string map")
    if "slash" not in translations:
        raise AssertionError("translations.json has no slash translation")
    cards = json_file(files, "cards.json")
    if not isinstance(cards, dict) or not cards:
        raise AssertionError("cards.json is not a non-empty object")
    for key, card in cards.items():
        if not key.isdigit() or not isinstance(card, dict):
            raise AssertionError("cards.json contains an invalid card record")
        if not isinstance(card.get("object_name"), str) or not card["object_name"]:
            raise AssertionError("cards.json card has no object_name")
    if not any(card.get("object_name") == "slash" for card in cards.values()):
        raise AssertionError("cards.json has no slash card")
    wasm_path = files.get("/rules/qsanguosha_client_wasm.wasm")
    bundle_path = files.get("/rules/qsanguosha_client_wasm.bundle.json")
    module_path = files.get("/rules/qsanguosha_client_wasm.mjs")
    if not wasm_path or not bundle_path or not module_path:
        raise AssertionError("distribution is missing rules runtime files")
    if wasm_path.read_bytes()[:8] != b"\0asm\1\0\0\0":
        raise AssertionError("invalid WebAssembly magic")
    bundle = json_file(files, "rules/qsanguosha_client_wasm.bundle.json")
    entries = bundle.get("files") if isinstance(bundle, dict) else None
    if not isinstance(entries, dict):
        raise AssertionError("rules bundle has no file hashes")
    for name, path in (("qsanguosha_client_wasm.mjs", module_path),
                       ("qsanguosha_client_wasm.wasm", wasm_path)):
        expected = entries.get(name)
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if expected != actual:
            raise AssertionError("rules bundle hash mismatch: " + name)
    return {"files": files, "index_references": references,
            "translation_count": len(translations), "card_count": len(cards)}


def smoke(args: argparse.Namespace, validated: dict) -> dict:
    browser = browser_tools()
    root_routes = validated["files"]
    with browser.ProbeServer({}, root_routes) as server:
        command = [str(args.browser), "--headless=new", "--disable-gpu", "--no-first-run",
                   "--no-default-browser-check", "--disable-background-networking",
                   "--disable-extensions", "--disable-dev-shm-usage",
                   "--virtual-time-budget=10000", "--dump-dom", server.origin + "/index.html",
                   *args.browser_arg]
        process = subprocess.run(command, capture_output=True, timeout=args.timeout, check=False)
        dom = process.stdout.decode("utf-8", "replace")
        (args.artifacts / "browser.stdout.html").write_text(dom, encoding="utf-8")
        (args.artifacts / "browser.stderr.log").write_bytes(process.stderr)
        requests = list(server.requests)
    (args.artifacts / "http-requests.json").write_text(json.dumps(requests, indent=2), encoding="utf-8")
    if process.returncode != 0:
        raise AssertionError("Chromium dump-dom failed: " + process.stderr.decode("utf-8", "replace")[-3000:])
    required = {"/index.html", "/translations.json", "/cards.json"}
    required.update(reference for reference in validated["index_references"]
                    if reference.startswith("/"))
    requested = {item.split("?", 1)[0] for item in requests}
    missing = sorted(required - requested)
    if missing:
        raise AssertionError("browser did not request required files: " + ", ".join(missing))
    optional_missing = {"/favicon.ico", "/game-ui-config.json", "/assets/logo/logo.png"}
    unknown = sorted(path for path in requested if path not in root_routes and path not in optional_missing)
    if unknown:
        raise AssertionError("browser requested files outside distribution: " + ", ".join(unknown))
    if '<form class="form"' not in dom or "暱稱" not in dom:
        raise AssertionError("built application did not render the connection form")
    return {"status": "PASS", "requests": len(requests), **{k: v for k, v in validated.items() if k != "files"}}


class DistributionTests(unittest.TestCase):
    def test_empty_distribution_rejected(self):
        with tempfile.TemporaryDirectory() as scratch:
            with self.assertRaisesRegex(AssertionError, "real directory"):
                validate_distribution(Path(scratch) / "dist")

    def test_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch) / "dist"
            root.mkdir()
            (root / "index.html").write_text("ok")
            (root / "link").symlink_to(root / "index.html")
            with self.assertRaisesRegex(AssertionError, "symlink"):
                files_for(root)

    def test_bad_wasm_rejected(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch) / "dist"
            (root / "rules").mkdir(parents=True)
            (root / "index.html").write_text("<html>")
            for name, value in (("translations.json", {"slash": "殺"}),
                                ("cards.json", {"0": {"object_name": "slash"}}),
                                ("rules/qsanguosha_client_wasm.bundle.json", {"files": {}})):
                (root / name).write_text(json.dumps(value))
            (root / "rules/qsanguosha_client_wasm.wasm").write_bytes(b"bad")
            (root / "rules/qsanguosha_client_wasm.mjs").write_text("")
            with self.assertRaisesRegex(AssertionError, "WebAssembly"):
                validate_distribution(root)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--dist", type=Path)
    parser.add_argument("--browser", type=Path)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--browser-arg", action="append", default=[])
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.TextTestRunner(verbosity=2).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(DistributionTests)).wasSuccessful() else 1
    for name in ("dist", "browser", "artifacts"):
        if getattr(args, name) is None:
            parser.error("--" + name + " is required")
        setattr(args, name, getattr(args, name).resolve())
    args.artifacts.mkdir(parents=True, exist_ok=True)
    try:
        validated = validate_distribution(args.dist)
        result = smoke(args, validated)
    except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
        result = {"status": "FAIL", "detail": str(error)}
    (args.artifacts / "distribution-summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print("[AUTOTEST] WEB_DISTRIBUTION status=" + result["status"])
    if result["status"] != "PASS":
        print(result["detail"], file=sys.stderr)
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
