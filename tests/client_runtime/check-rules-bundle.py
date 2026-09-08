#!/usr/bin/env python3
"""W2: real native exporter, production WASM Worker and server admission gate."""
from __future__ import annotations
import argparse
import contextlib
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

def commands():
    return {name: int(number) for name, number in re.findall(r"^  ([A-Z_0-9]+): (\d+),?", (ROOT / "web/src/protocol.ts").read_text(), re.M)}

def read_frame(stream):
    line = stream.readline(1048577)
    if not line or len(line) > 1048576:
        raise AssertionError("missing or oversized protocol frame")
    return json.loads(line)

def export_native(args, assets, scratch, env, tag, unsupported=False):
    output = args.artifacts / (tag + ".json")
    result = subprocess.run([str(args.native_runner), "--export-rules-bundle", "--output", str(output),
                             "--asset-root", str(assets)], env=env, cwd=scratch,
                            capture_output=True, timeout=60, check=False)
    (args.artifacts / (tag + ".stdout.log")).write_bytes(result.stdout)
    (args.artifacts / (tag + ".stderr.log")).write_bytes(result.stderr)
    if unsupported:
        if (result.returncode != 4 or output.exists()
                or b"rules_content_unsupported" not in result.stderr):
            raise AssertionError("extra rules Lua was not rejected: " + tag)
        return None
    if result.returncode or not output.is_file():
        raise AssertionError("native rules export failed: " + tag)
    return json.loads(output.read_bytes())


def stage_server_ai(source, destination):
    """Deploy server policy separately; never add it to the WASM asset closure."""
    ai = source / "lua/ai"
    paths = [(path, Path("lua/ai") / path.relative_to(ai), source)
             for directory in (ai, ai / "isolated") for path in sorted(directory.glob("*.lua"))]
    if not (ai / "smart-ai.lua").is_file():
        raise AssertionError("server AI source is missing lua/ai/smart-ai.lua")
    paths.append((ROOT / "lua/lib/middleclass.lua", Path("lua/lib/middleclass.lua"), ROOT))
    for path, relative, base in paths:
        # Check ancestors too: copying must not hide a symlink from the exporter.
        if any(parent.is_symlink() for parent in (path, *path.parents)
               if parent == base or base in parent.parents):
            raise AssertionError("symlinked server AI source")
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)


def check_server_ai_identity(args, assets, scratch, env, baseline):
    stage_server_ai(args.server_ai_root, assets)
    if export_native(args, assets, scratch, env, "native-server-ai") != baseline:
        raise AssertionError("server-only AI changed client rules identity")
    for index, relative in enumerate(("lua/ai/smart-ai.lua", "lua/lib/middleclass.lua")):
        path = assets / relative
        original = path.read_bytes()
        try:
            path.write_bytes(original + b"\n-- server-only policy revision\n")
            if export_native(args, assets, scratch, env, f"native-ai-revision-{index}") != baseline:
                raise AssertionError("server-only AI revision changed client rules identity")
        finally:
            path.write_bytes(original)
    # A similar name outside the dedicated AI directory must not bypass rules checks.
    for index, relative in enumerate(("lua/ai-extra.lua", "lua/lib/middleclass-extra.lua")):
        path = assets / relative
        try:
            path.write_text("-- unsupported rules content\n", encoding="utf-8")
            export_native(args, assets, scratch, env, f"native-extra-lua-{index}", unsupported=True)
        finally:
            path.unlink()


def negative_exports(args, assets, scratch, env, baseline):
    variants = {}
    for tag, file, before, after in (
        ("card-order", "lua/config.lua", b"StandardCard,StandardExCard", b"StandardExCard,StandardCard"),
        ("lua-content", "lua/sanguosha.lua", b"return math.floor(math.log(level) / math.log(2)) + 1",
         b"return math.floor(math.log(level) / math.log(2)) + 2")):
        path = assets / file
        original = path.read_bytes()
        if original.count(before) != 1:
            raise AssertionError("negative fixture source anchor changed: " + tag)
        try:
            path.write_bytes(original.replace(before, after))
            changed = export_native(args, assets, scratch, env, "native-" + tag)
        finally:
            path.write_bytes(original)
        if changed["card_count"] != baseline["card_count"]:
            raise AssertionError("negative fixture changed card count")
        key = "card_registry_hash" if tag == "card-order" else "lua_hash"
        if changed["rules_bundle"][key] == baseline["rules_bundle"][key]:
            raise AssertionError("native exporter failed to distinguish " + tag)
        if tag == "lua-content" and changed["registry"] != baseline["registry"]:
            raise AssertionError("Lua-only mutation changed the physical registry")
        variants[tag] = changed["rules_bundle"]
    return variants

def check(args):
    args.artifacts.mkdir(parents=True, exist_ok=True)
    native = module("native", ROOT / "tests/client_runtime/check-fixtures.py")
    browser = module("browser", ROOT / "tests/client_runtime/check-browser-fixtures.py")
    command = commands()
    with tempfile.TemporaryDirectory(prefix="rules-bundle-", dir=args.artifacts) as scratch:
        scratch = Path(scratch)
        assets = scratch / "assets"
        native.stage_builtin_assets(ROOT, assets)
        env = os.environ.copy()
        env.update({"QSAN_ASSET_ROOT": str(assets), "QSAN_USER_DATA_ROOT": str(scratch / "userdata"),
                    "XDG_CONFIG_HOME": str(scratch / "config"), "XDG_DATA_HOME": str(scratch / "data"),
                    "APPDATA": str(scratch / "appdata"), "LOCALAPPDATA": str(scratch / "localappdata")})
        identity = export_native(args, assets, scratch, env, "native-rules")
        variants = negative_exports(args, assets, scratch, env, identity)
        check_server_ai_identity(args, assets, scratch, env, identity)
        tcp, ws = free_port(), free_port()
        while ws == tcp:
            ws = free_port()
        with (args.artifacts / "server.log").open("wb") as log:
            server = subprocess.Popen([str(args.server), "--port", str(tcp), "--websocket-port", str(ws),
                "--bind-address", "127.0.0.1", "--game-mode", "03_1v2", "--ai", "off"],
                cwd=scratch, env=env, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 40
                while True:
                    if server.poll() is not None:
                        raise AssertionError("server exited before listen")
                    try:
                        sock = socket.create_connection(("127.0.0.1", tcp), timeout=2)
                        break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise AssertionError("server startup timeout")
                        time.sleep(0.1)
                with sock, sock.makefile("rb") as stream:
                    sock.settimeout(10)
                    hello = read_frame(stream)
                    if hello["payload"].get("rules_bundle") != identity["rules_bundle"]:
                        raise AssertionError("live server identity differs from native export")
                    signup = {"v": 2, "type": "request", "source": "client", "destination": "lobby", "message_id": "1",
                              "command": command["SIGNUP"], "payload": {"schema_version": 2, "reconnect_requested": False,
                              "screen_name": "w2-legacy-tcp", "avatar": "caocao"}}
                    sock.sendall(json.dumps(signup, separators=(",", ":")).encode() + b"\n")
                    reply = read_frame(stream)
                    if reply["command"] != command["SIGNUP"] or reply["payload"].get("accepted") is not True:
                        raise AssertionError("legacy TCP signup rejected")
                routes = {"/browser/" + path.relative_to(args.probe).as_posix(): path
                          for path in args.probe.rglob("*") if path.is_file()}
                routes["/case.json"] = json.dumps({"ws": f"ws://127.0.0.1:{ws}", "native": identity, "variants": variants}).encode()
                root_routes = {"/rules/" + name: args.runtime / name for name in (
                    "qsanguosha_client_wasm.mjs", "qsanguosha_client_wasm.wasm",
                    "qsanguosha_client_wasm.assets.json", "qsanguosha_client_wasm.bundle.json")}
                report = browser.browser_report(args.browser, routes, args.artifacts / "browser", 120, [], root_routes)
                if report.get("status") != "PASS" or report.get("identity") != identity["rules_bundle"]:
                    raise AssertionError("production Worker/admission failed: " + str(report))
                expected = ["real server/WASM signup reaches active", "native/WASM exporter identity equality"]
                expected += [f"{name}: {error}" for name, error in (
                    ("old-web", "rules_identity_required"), ("bad-seal", "rules_identity_invalid"),
                    ("card-order", "rules_version_mismatch"), ("lua-content", "rules_version_mismatch"),
                    ("old-bridge", "rules_reload_required"), ("unsupported", "rules_content_unsupported"),
                    ("missing-interaction", "rules_interaction_unsupported"), ("reconnect-mismatch", "rules_version_mismatch"))]
                if not all(label in report.get("checks", []) for label in expected):
                    raise AssertionError("browser omitted a required acceptance case")
                # Keep the old compiled Web loader, but serve a newly paired,
                # valid WASM binary (an inert custom section preserves its ABI).
                replacement = {name: path.read_bytes() for name, path in root_routes.items()}
                replacement["/rules/qsanguosha_client_wasm.wasm"] += b"\x00\x02\x01x"
                bundle_name = "/rules/qsanguosha_client_wasm.bundle.json"
                bundle = json.loads(replacement[bundle_name])
                bundle["files"] = {name: hashlib.sha256(replacement["/rules/" + name]).hexdigest()
                                   for name in bundle["files"]}
                replacement[bundle_name] = (json.dumps(bundle, sort_keys=True, separators=(",", ":")) + "\n").encode()
                routes["/case.json"] = json.dumps({"ws": f"ws://127.0.0.1:{ws}", "expect_reload": True}).encode()
                stale = browser.browser_report(args.browser, routes, args.artifacts / "stale-loader", 60, [], replacement)
                label = "old Web loader rejects new paired WASM before admission"
                if stale.get("status") != "PASS" or label not in stale.get("checks", []):
                    raise AssertionError("stale production Web loader was not rejected: " + str(stale))
                report["checks"].append(label)
                report["legacy_tcp_accepted"] = True
                report["server_ai_excluded_from_identity"] = True
                (args.artifacts / "rules-bundle-summary.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
            finally:
                if server.poll() is None:
                    with contextlib.suppress(OSError):
                        server.stdin.write(b"quit\n"); server.stdin.flush()
                    try:
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill(); server.wait(timeout=5)
    print("PASS: native/WASM identity, live admission and legacy TCP")

class PackagingTests(unittest.TestCase):
    def test_server_ai_staging_excludes_other_runtime_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, destination = root / "source", root / "server"
            for relative in ("lua/ai/smart-ai.lua", "lua/ai/isolated/ask-for-use-card.lua",
                             "lua/ai/logs/runtime.lua", "lua/ai/.git/private.lua",
                             "lua/luaoldenemy_lib.lua", "extensions/random.lua"):
                path = source / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("-- fixture\n", encoding="utf-8")
            stage_server_ai(source, destination)
            self.assertEqual({p.relative_to(destination).as_posix() for p in destination.rglob("*")
                              if p.is_file()}, {"lua/ai/smart-ai.lua", "lua/ai/isolated/ask-for-use-card.lua",
                                                "lua/lib/middleclass.lua"})
            self.assertTrue((source / "extensions/random.lua").is_file())
            with self.assertRaisesRegex(AssertionError, "missing lua/ai/smart-ai.lua"):
                stage_server_ai(root / "missing", root / "incomplete")

    def test_build_manifest_detects_every_artifact_change(self):
        packaging = module("packaging", ROOT / "tools/package-web-runtime.py")
        with tempfile.TemporaryDirectory() as root:
            loader = Path(root) / "qsanguosha_client_wasm.mjs"
            paths = [loader, loader.with_suffix(".wasm"), loader.with_suffix(".assets.json")]
            for i, path in enumerate(paths):
                path.write_bytes(bytes([i + 1]))
            packaging.write_bundle(loader)
            expected = json.loads(loader.with_suffix(".bundle.json").read_bytes())
            self.assertEqual(expected, packaging.deployment_bundle(loader))
            self.assertEqual(expected["bridge_schema"], 2)
            for path in paths:
                original = path.read_bytes(); path.write_bytes(original + b"stale")
                self.assertNotEqual(expected, packaging.deployment_bundle(loader))
                path.write_bytes(original)

    def test_root_routes_are_an_exact_allowlist(self):
        browser = module("browser", ROOT / "tests/client_runtime/check-browser-fixtures.py")
        import urllib.request
        import urllib.error
        with browser.ProbeServer({"/browser/assets/probe.js": b"probe"},
                                 {"/rules/allowed.mjs": b"verified"}) as server:
            with urllib.request.urlopen(server.origin + "/rules/allowed.mjs") as response:
                self.assertEqual(response.read(), b"verified")
                self.assertEqual(response.headers.get_content_type(), "text/javascript")
            with urllib.request.urlopen(server.url + "/browser/assets/probe.js") as response:
                self.assertEqual(response.read(), b"probe")
                self.assertEqual(response.headers.get_content_type(), "text/javascript")
            with self.assertRaises(urllib.error.HTTPError):
                urllib.request.urlopen(server.origin + "/rules/unknown.mjs")

if __name__ == "__main__":
    import sys
    if sys.argv[1:] == ["--self-test"]:
        unittest.main(argv=[sys.argv[0]])
    else:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--server-ai-root", type=lambda value: Path(value).resolve(), default=ROOT,
                            help="server-only deployment source containing lua/ai (default: repository)")
        for option in ("native-runner", "server", "browser", "probe", "runtime", "artifacts"):
            parser.add_argument("--" + option, type=lambda value: Path(value).resolve(), required=True)
        check(parser.parse_args())
