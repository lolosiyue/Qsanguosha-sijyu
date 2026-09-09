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
    output.unlink(missing_ok=True)
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


def expect_bootstrap_failure(args, assets, scratch, env, tag):
    """A missing declared file fails before identity export is publishable.

    This is intentionally separate from ``rules_content_unsupported``: a
    bootstrap omission is a hard startup failure and may have no JSON error.
    """
    output = args.artifacts / (tag + ".json")
    output.unlink(missing_ok=True)
    result = subprocess.run([str(args.native_runner), "--export-rules-bundle", "--output", str(output),
                             "--asset-root", str(assets)], env=env, cwd=scratch,
                            capture_output=True, timeout=60, check=False)
    (args.artifacts / (tag + ".stdout.log")).write_bytes(result.stdout)
    (args.artifacts / (tag + ".stderr.log")).write_bytes(result.stderr)
    if result.returncode != 1 or output.exists():
        raise AssertionError("expected clean bootstrap rejection, got %s: %s" % (result.returncode, tag))


PROBE_MANIFEST = [
    "extensions/probe.one.lua",
    "extensions/probe.two.lua",
]


def make_probe_source(destination):
    """Create three tiny, self-contained card packages for P1 order probes."""
    native = module("native-probe-source", ROOT / "tests/client_runtime/check-fixtures.py")
    for relative in native.CORE_BOOTSTRAP:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / relative, target)
    language = destination / "lang/zh_CN/Common.lua"
    language.parent.mkdir(parents=True, exist_ok=True)
    language.write_text("return {}\n", encoding="utf-8")
    scripts = {
        "probe.one.lua": ("probe_one", "ProbeOneCard", 1),
        "probe.two.lua": ("probe_two", "ProbeTwoCard", 2),
        "probe.three.lua": ("probe_three", "ProbeThreeCard", 3),
    }
    for filename, (package, class_name, number) in scripts.items():
        path = destination / "extensions" / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            f'local extension = sgs.Package("{package}", sgs.Package_CardPack)\n'
            f'local card = sgs.CreateBasicCard{{name="{package}_card", class_name="{class_name}", '
            f'suit=sgs.Card_Spade, number={number}}}\ncard:setParent(extension)\nreturn extension\n',
            encoding="utf-8")
    return destination


def registry_pairs(bundle):
    return [(entry["id"], entry["object_name"]) for entry in bundle["registry"]]


def check_registration_order(args, source, scratch, env, baseline):
    """G1: manifest order is the registry order, independent of directory noise."""
    native = module("native-order", ROOT / "tests/client_runtime/check-fixtures.py")
    cases = {
        "tail": PROBE_MANIFEST + ["extensions/probe.three.lua"],
        "middle": [PROBE_MANIFEST[0], "extensions/probe.three.lua", PROBE_MANIFEST[1]],
    }
    variants = {}
    for tag, declarations in cases.items():
        assets = scratch / ("order-" + tag)
        native.stage_builtin_assets(source, assets, declarations)
        variant = export_native(args, assets, scratch, env, "native-order-" + tag)
        variants[tag] = variant
        if tag == "tail":
            if variant["card_count"] <= baseline["card_count"]:
                raise AssertionError("appending a manifest entry did not add cards")
            prefix = registry_pairs(variant)[:len(registry_pairs(baseline))]
            if prefix != registry_pairs(baseline):
                raise AssertionError("appending a manifest entry changed existing card IDs")
        elif variant["rules_bundle"]["card_registry_hash"] == variants["tail"]["rules_bundle"]["card_registry_hash"]:
            raise AssertionError("inserting a manifest entry did not change registry identity")

    same_set = scratch / "order-reversed"
    native.stage_builtin_assets(source, same_set, list(reversed(PROBE_MANIFEST)))
    reversed_bundle = export_native(args, same_set, scratch, env, "native-order-reversed")
    def registered_cards(bundle):
        return sorted(json.dumps({key: value for key, value in card.items() if key != "id"},
                                 sort_keys=True) for card in bundle["registry"])
    if registered_cards(reversed_bundle) != registered_cards(baseline):
        raise AssertionError("order probe changed the registered set")
    if reversed_bundle["rules_bundle"]["bundle_id"] == baseline["rules_bundle"]["bundle_id"]:
        raise AssertionError("declared order was not sealed into bundle_id")
    if reversed_bundle["rules_bundle"]["card_registry_hash"] == baseline["rules_bundle"]["card_registry_hash"]:
        raise AssertionError("changing order within the same set did not change registry identity")

def test_ban_packages_is_not_an_identity_input():
    """Static guard: BanPackages belongs to runtime selection, not bundle ID."""
    for relative in ("src/core/rules-bundle-exporter.cpp", "src/core/rules-content-manifest.cpp"):
        if "BanPackages" in (ROOT / relative).read_text(encoding="utf-8"):
            raise AssertionError(relative + " must not consume BanPackages")


def check_declared_gate(args, source, scratch, env, baseline):
    """G2: declaration closure, roles, missing files and symlink policy."""
    native = module("native-gate", ROOT / "tests/client_runtime/check-fixtures.py")
    assets = scratch / "declared"
    native.stage_builtin_assets(source, assets, PROBE_MANIFEST)
    # An unlisted Lua file is rejected even when it is otherwise harmless.
    extra = assets / "extensions/unlisted.lua"
    extra.write_text("-- undeclared\n", encoding="utf-8")
    export_native(args, assets, scratch, env, "native-unlisted", unsupported=True)
    extra.unlink()
    # A declared script cannot disappear silently; no unsupported JSON is
    # required for this startup/bootstrap failure.
    (assets / PROBE_MANIFEST[0]).unlink()
    expect_bootstrap_failure(args, assets, scratch, env, "native-missing-declared")

    # The manifest is a dense ordered string array.  Invalid Lua types and
    # sparse numeric keys must fail at bootstrap instead of being coerced into
    # an empty declaration (which would silently change registration).
    for tag, value in (("manifest-not-array", '"extensions/legends.lua"'),
                       ("manifest-sparse", '{ [2] = "extensions/legends.lua" }')):
        malformed = scratch / tag
        native.stage_builtin_assets(source, malformed, PROBE_MANIFEST)
        config = malformed / "lua/config.lua"
        text = config.read_text(encoding="utf-8")
        text = re.sub(r"extension_names\s*=\s*\{.*?\}",
                      "extension_names = " + value, text, count=1, flags=re.S)
        config.write_text(text, encoding="utf-8")
        expect_bootstrap_failure(args, malformed, scratch, env, "native-" + tag)

    # Translation is delivered content but presentation-only: identity remains
    # stable when the same declaration is edited.
    translated = scratch / "translated"
    native.stage_builtin_assets(source, translated,
                               [PROBE_MANIFEST[0] + ";lang=lang/zh_CN/Common.lua",
                                PROBE_MANIFEST[1]])
    language = translated / "lang/zh_CN/Common.lua"
    language.parent.mkdir(parents=True, exist_ok=True)
    language.write_text("return { ['probe'] = 'before' }\n", encoding="utf-8")
    first = export_native(args, translated, scratch, env, "native-lang-before")
    language.write_bytes(language.read_bytes().replace(b"before", b"after"))
    second = export_native(args, translated, scratch, env, "native-lang-after")
    if first["rules_bundle"] != second["rules_bundle"]:
        raise AssertionError("editing declared lang changed rules identity")
    if first.get("rules_content") == second.get("rules_content"):
        raise AssertionError("editing declared lang did not change rules_content")

    link_assets = scratch / "symlink"
    native.stage_builtin_assets(source, link_assets, PROBE_MANIFEST)
    link = link_assets / "extensions/linked.lua"
    link.symlink_to(link_assets / PROBE_MANIFEST[0])
    # The symlink itself is undeclared; exporter must reject it rather than
    # following it into the declared closure.
    export_native(args, link_assets, scratch, env, "native-symlink", unsupported=True)

    # Satellite libraries are executable rules bytes; translation bytes are not.
    library_source = source / "lua/probe_lib.lua"
    library_source.write_text("return true\n", encoding="utf-8")
    with_lib = scratch / "with-lib"
    native.stage_builtin_assets(source, with_lib,
                               [PROBE_MANIFEST[0] + ";libs=lua/probe_lib.lua", PROBE_MANIFEST[1]])
    library = with_lib / "lua/probe_lib.lua"
    before = export_native(args, with_lib, scratch, env, "native-lib-before")
    library.write_text("return false\n", encoding="utf-8")
    after = export_native(args, with_lib, scratch, env, "native-lib-after")
    if before["rules_bundle"]["lua_hash"] == after["rules_bundle"]["lua_hash"]:
        raise AssertionError("library bytes were not sealed")
    if before["rules_bundle"]["code_id"] != after["rules_bundle"]["code_id"]:
        raise AssertionError("library bytes changed code identity")
    library.unlink()
    export_native(args, with_lib, scratch, env, "native-missing-lib", unsupported=True)
    language.unlink()
    export_native(args, translated, scratch, env, "native-missing-lang", unsupported=True)

    # Reject presentation files without declarations too.
    extra_language = link_assets / "lang/zh_CN/stray.lua"
    link.unlink()
    extra_language.parent.mkdir(parents=True, exist_ok=True)
    extra_language.write_text("return {}\n", encoding="utf-8")
    export_native(args, link_assets, scratch, env, "native-unlisted-lang", unsupported=True)
    extra_language.unlink()

    # A dangling symlink must not disappear from QDirIterator's inventory.
    link.symlink_to(link_assets / "nonexistent.lua")
    export_native(args, link_assets, scratch, env, "native-dangling-symlink", unsupported=True)
    link.unlink()
    ai_directory = link_assets / "lua/ai"
    ai_directory.mkdir()
    (ai_directory / "policy.lua").symlink_to(link_assets / PROBE_MANIFEST[0])
    export_native(args, link_assets, scratch, env, "native-ai-symlink", unsupported=True)

    # A VM must not be relabelled from bytes replaced during extension execution.
    replaced = scratch / "replaced-during-load"
    native.stage_builtin_assets(source, replaced,
                               [PROBE_MANIFEST[0] + ";libs=lua/probe_lib.lua", PROBE_MANIFEST[1]])
    script = replaced / PROBE_MANIFEST[0]
    script.write_text("local f = assert(io.open('lua/probe_lib.lua', 'a'))\n"
                      "f:write(' -- changed during bootstrap')\nf:close()\n"
                      + script.read_text(encoding="utf-8"), encoding="utf-8")
    export_native(args, replaced, scratch, env, "native-replaced-after-snapshot", unsupported=True)

    # AI declarations describe optional server policy, not client requirements.
    optional = scratch / "optional-ai"
    native.stage_builtin_assets(source, optional,
                               [PROBE_MANIFEST[0] + ";ai=lua/ai/policy.lua", PROBE_MANIFEST[1]])
    without_ai = export_native(args, optional, scratch, env, "native-declared-ai-missing")
    policy = optional / "lua/ai/policy.lua"
    policy.parent.mkdir(parents=True, exist_ok=True)
    policy.write_text("-- optional server policy\n", encoding="utf-8")
    if export_native(args, optional, scratch, env, "native-declared-ai-present") != without_ai:
        raise AssertionError("optional AI bytes entered client identity")


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
        # G4 must exercise one real declared extension, not only synthetic
        # builtin bootstrap content.
        manifest_tool = module("extension-manifest", ROOT / "tools/generate-extension-manifest.py")
        presentations = manifest_tool.presentation_files(ROOT)
        declarations = ["extensions/addFunction.lua;lang=" + ",".join(presentations),
                        "extensions/sijyu.lua", "extensions/animecard.lua"]
        native.stage_builtin_assets(ROOT, assets, declarations, extension_root=args.extension_root)
        env = os.environ.copy()
        env.update({"QSAN_ASSET_ROOT": str(assets), "QSAN_USER_DATA_ROOT": str(scratch / "userdata"),
                    "XDG_CONFIG_HOME": str(scratch / "config"), "XDG_DATA_HOME": str(scratch / "data"),
                    "APPDATA": str(scratch / "appdata"), "LOCALAPPDATA": str(scratch / "localappdata")})
        identity = export_native(args, assets, scratch, env, "native-rules")
        content_root = scratch / "rules-content"
        content_root.mkdir()
        content_bundle = args.artifacts / "native-rules.json"
        package = subprocess.run([
            "python3", str(ROOT / "tools/package-rules-content.py"),
            "--bundle", str(content_bundle), "--asset-root", str(assets),
            "--destination", str(content_root)], capture_output=True, text=True,
            timeout=60, check=False)
        if package.returncode != 0:
            raise AssertionError("native rules content packaging failed: " + package.stderr)
        # Same rules manifest with one presentation byte changed.  Exporting
        # the second staged tree proves bundle identity is stable while the
        # content manifest and native translation table change.
        changed_assets = scratch / "changed-assets"
        shutil.copytree(assets, changed_assets)
        translation_file = changed_assets / "lang/zh_CN/Package/StandardPackage.lua"
        original_translation = translation_file.read_bytes()
        anchor = b'["slash"] = "' + "杀".encode() + b'"'
        if original_translation.count(anchor) != 1:
            raise AssertionError("slash translation anchor changed")
        translation_file.write_bytes(original_translation.replace(anchor,
            b'["slash"] = "TEST' + "杀".encode() + b'"'))
        changed_identity = export_native(args, changed_assets, scratch, env, "native-presentation-changed")
        if changed_identity["rules_bundle"] != identity["rules_bundle"]:
            raise AssertionError("presentation edit changed rules identity")
        changed_content_root = scratch / "rules-content-changed"
        changed_content_root.mkdir()
        changed_package = subprocess.run([
            "python3", str(ROOT / "tools/package-rules-content.py"),
            "--bundle", str(args.artifacts / "native-presentation-changed.json"),
            "--asset-root", str(changed_assets), "--destination", str(changed_content_root)],
            capture_output=True, text=True, timeout=60, check=False)
        if changed_package.returncode != 0:
            raise AssertionError("changed rules content packaging failed: " + changed_package.stderr)
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
                    if hello["payload"].get("rules_content") != identity.get("rules_content"):
                        raise AssertionError("live server content manifest differs from native export")
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
                    "qsanguosha_client_wasm.bundle.json")}
                content = identity.get("rules_content", {})
                for entry in content.get("files", []):
                    if entry.get("role") == "ai":
                        continue
                    path = content_root / entry["sha256"]
                    if not path.is_file():
                        raise AssertionError("packaged rules content missing: " + entry["path"])
                    root_routes["/rules/content/" + entry["sha256"]] = path
                report = browser.browser_report(args.browser, routes, args.artifacts / "browser", 120, [], root_routes)
                if report.get("status") != "PASS" or report.get("identity") != identity["rules_bundle"]:
                    raise AssertionError("production Worker/admission failed: " + str(report))
                expected = ["real server/WASM signup reaches active", "native/WASM exporter identity equality"]
                expected += [f"{name}: {error}" for name, error in (
                    ("old-web", "rules_identity_required"), ("bad-seal", "rules_identity_invalid"),
                    ("code-id", "rules_identity_invalid"),
                    ("card-order", "rules_version_mismatch"), ("lua-content", "rules_version_mismatch"),
                    ("old-bridge", "rules_version_mismatch"), ("unsupported", "rules_content_unsupported"),
                    ("missing-interaction", "rules_interaction_unsupported"), ("reconnect-mismatch", "rules_version_mismatch"))]
                if not all(label in report.get("checks", []) for label in expected):
                    raise AssertionError("browser omitted a required acceptance case")
                requests = json.loads((args.artifacts / "browser/http-requests.json").read_text())
                content_routes = {route for route in root_routes if route.startswith("/rules/content/")}
                if not content_routes.issubset(set(requests)):
                    raise AssertionError("browser did not fetch every declared rules content hash")
                presentation_entries = {entry["path"]: entry for entry in content.get("files", [])
                                        if entry.get("path", "").startswith("lang/")}
                presentation_routes = {"/rules/content/" + entry["sha256"]
                                       for entry in presentation_entries.values()}
                if set(presentation_entries) != set(presentations) \
                        or not presentation_routes.issubset(set(requests)):
                    raise AssertionError("browser did not fetch every declared presentation translation")
                if any(route.endswith("assets.json") for route in requests):
                    raise AssertionError("production Worker fetched removed assets.json")
                routes["/case.json"] = json.dumps({"ws": f"ws://127.0.0.1:{ws}", "expect_code_mismatch": True}).encode()
                wrong_code = browser.browser_report(args.browser, routes, args.artifacts / "code-mismatch", 60, [], root_routes)
                label = "valid different code_id rejected before content fetch"
                if wrong_code.get("status") != "PASS" or label not in wrong_code.get("checks", []):
                    raise AssertionError("different code identity was not rejected: " + str(wrong_code))
                wrong_requests = json.loads((args.artifacts / "code-mismatch/http-requests.json").read_text())
                if any(route.startswith("/rules/content/") for route in wrong_requests):
                    raise AssertionError("code mismatch fetched rules content before rejection")
                report["checks"].append(label)
                corrupted = dict(root_routes)
                changed_route = next(iter(sorted(content_routes)))
                changed_bytes = root_routes[changed_route].read_bytes()
                if not changed_bytes:
                    raise AssertionError("corruption fixture must be nonempty")
                corrupted[changed_route] = bytes([changed_bytes[0] ^ 1]) + changed_bytes[1:]
                routes["/case.json"] = json.dumps({"ws": f"ws://127.0.0.1:{ws}",
                    "expect_reload": True, "corrupt_content": True}).encode()
                corrupt = browser.browser_report(args.browser, routes, args.artifacts / "corrupt-content", 60, [], corrupted)
                label = "changed content byte rejected before admission"
                if corrupt.get("status") != "PASS" or label not in corrupt.get("checks", []):
                    raise AssertionError("changed content byte was not rejected: " + str(corrupt))
                report["checks"].append(label)
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
                stale_requests = json.loads((args.artifacts / "stale-loader/http-requests.json").read_text())
                if any(route.startswith("/rules/content/") for route in stale_requests):
                    raise AssertionError("code mismatch fetched rules content before rejection")
                report["checks"].append(label)
                changed_content = changed_identity["rules_content"]
                for entry in changed_content.get("files", []):
                    if entry.get("role") == "ai":
                        continue
                    path = changed_content_root / entry["sha256"]
                    if not path.is_file():
                        raise AssertionError("changed packaged content missing: " + entry["path"])
                    root_routes["/rules/content/" + entry["sha256"]] = path
                routes["/case.json"] = json.dumps({"ws": f"ws://127.0.0.1:{ws}",
                    "expect_presentation_change": True, "changed_translations": changed_identity["translations"],
                    "native": changed_identity, "variants": variants}).encode()
                changed_report = browser.browser_report(args.browser, routes,
                    args.artifacts / "presentation-changed", 120, [], root_routes)
                changed_label = "Web displays changed presentation translation"
                if changed_report.get("status") != "PASS" or changed_label not in changed_report.get("checks", []):
                    raise AssertionError("changed presentation was not rendered: " + str(changed_report))
                report["checks"].append(changed_label)
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


def check_native_only(args):
    """Run the P1 native G1/G2 checks without requiring W2 binaries."""
    args.artifacts.mkdir(parents=True, exist_ok=True)
    native = module("native-only", ROOT / "tests/client_runtime/check-fixtures.py")
    with tempfile.TemporaryDirectory(prefix="rules-native-", dir=args.artifacts) as directory:
        scratch = Path(directory)
        source = make_probe_source(scratch / "probe-source")
        assets = scratch / "assets"
        native.stage_builtin_assets(source, assets, PROBE_MANIFEST)
        env = os.environ.copy()
        env.update({"QSAN_ASSET_ROOT": str(assets), "QSAN_USER_DATA_ROOT": str(scratch / "userdata"),
                    "XDG_CONFIG_HOME": str(scratch / "config"), "XDG_DATA_HOME": str(scratch / "data"),
                    "APPDATA": str(scratch / "appdata"), "LOCALAPPDATA": str(scratch / "localappdata")})
        identity = export_native(args, assets, scratch, env, "native-only-baseline")
        check_registration_order(args, source, scratch, env, identity)
        check_declared_gate(args, source, scratch, env, identity)
        ai = assets / "lua/ai/smart-ai.lua"
        ai.parent.mkdir(parents=True, exist_ok=True)
        ai.write_text("-- server policy\n", encoding="utf-8")
        if export_native(args, assets, scratch, env, "native-only-ai") != identity:
            raise AssertionError("server AI staging changed client identity")
        original = ai.read_bytes()
        ai.write_bytes(original + b"-- revision\n")
        if export_native(args, assets, scratch, env, "native-only-ai-revision") != identity:
            raise AssertionError("server AI bytes changed client identity")
    summary = {"status": "PASS", "checks": [
        "append adds cards without moving existing IDs", "same-set order changes registry and bundle",
        "undeclared content rejected", "missing scripts fail bootstrap", "malformed manifest rejected",
        "translation bytes excluded", "libraries sealed and missing satellites rejected",
        "symlinks including dangling and AI links rejected", "post-snapshot replacement rejected",
        "AI bytes excluded and declarations optional on clients"],
        "ban_packages": "source guard only"}
    (args.artifacts / "native-manifest-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print("PASS: native-only declared manifest G1/G2")

class PackagingTests(unittest.TestCase):
    def test_ban_packages_is_not_an_identity_input(self):
        test_ban_packages_is_not_an_identity_input()

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
            paths = [loader, loader.with_suffix(".wasm")]
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
                                 {"/rules/allowed.mjs": b"verified", "/rules/content/" + "a" * 64: b"lua"}) as server:
            with urllib.request.urlopen(server.origin + "/rules/allowed.mjs") as response:
                self.assertEqual(response.read(), b"verified")
                self.assertEqual(response.headers.get_content_type(), "text/javascript")
            with urllib.request.urlopen(server.url + "/browser/assets/probe.js") as response:
                self.assertEqual(response.read(), b"probe")
                self.assertEqual(response.headers.get_content_type(), "text/javascript")
            with urllib.request.urlopen(server.origin + "/rules/content/" + "a" * 64) as response:
                self.assertEqual(response.read(), b"lua")
            for path in ("/rules/unknown.mjs", "/rules/content/" + "b" * 64,
                         "/rules/content/../allowed.mjs", "/rules/content/" + "a" * 64 + "?extra=1"):
                with self.assertRaises(urllib.error.HTTPError):
                    urllib.request.urlopen(server.origin + path)

if __name__ == "__main__":
    import sys
    if sys.argv[1:] == ["--self-test"]:
        unittest.main(argv=[sys.argv[0]])
    else:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--native-only", action="store_true",
                            help="run native P1 manifest checks without W2 server/browser assets")
        parser.add_argument("--extension-root", type=lambda value: Path(value).resolve(), default=ROOT,
                            help="root containing the real extension fixture scripts")
        parser.add_argument("--server-ai-root", type=lambda value: Path(value).resolve(), default=ROOT,
                            help="server-only deployment source containing lua/ai (default: repository)")
        parser.add_argument("--native-runner", type=lambda value: Path(value).resolve(), required=True)
        parser.add_argument("--artifacts", type=lambda value: Path(value).resolve(), required=True)
        for option in ("server", "browser", "probe", "runtime"):
            parser.add_argument("--" + option, type=lambda value: Path(value).resolve())
        parsed = parser.parse_args()
        if parsed.native_only:
            check_native_only(parsed)
        else:
            missing = [name for name in ("server", "browser", "probe", "runtime")
                       if getattr(parsed, name) is None]
            if missing:
                parser.error("full mode requires: " + ", ".join("--" + name for name in missing))
            check(parsed)
