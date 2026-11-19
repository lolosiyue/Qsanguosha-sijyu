#!/usr/bin/env python3
"""Plan change-scoped verification; never runs checks, builds, games, or API calls."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path, PurePosixPath


def git(root: str, *args: str, binary: bool = False) -> bytes | str:
    result = subprocess.run(
        ["git", "-C", root, *args], check=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if binary:
        return result.stdout
    return result.stdout.decode("utf-8", errors="surrogateescape").rstrip("\n")


def path_in_repo(root: Path, raw: str) -> str:
    candidate = Path(raw)
    if not candidate.is_absolute():
        candidate = root / candidate
    resolved = candidate.resolve(strict=False)
    try:
        rel = resolved.relative_to(root.resolve())
    except ValueError as exc:
        raise ValueError(f"changed path is outside repository: {raw}") from exc
    text = PurePosixPath(rel.as_posix()).as_posix()
    return "" if text == "." else text


def classify(paths: list[str]) -> dict:
    common: dict[str, dict] = {}
    builds: dict[str, dict] = {}
    required: dict[str, dict] = {}
    deferred: dict[str, dict] = {}
    unknown: list[str] = []

    def add(bucket: dict, key: str, path: str, reason: str, command: str | None = None) -> None:
        item = bucket.setdefault(key, {"reasons": [], "paths": [], "commands": []})
        if reason not in item["reasons"]:
            item["reasons"].append(reason)
        if path not in item["paths"]:
            item["paths"].append(path)
        if command and command not in item["commands"]:
            item["commands"].append(command)

    def target(name: str, path: str, reason: str) -> None:
        add(builds, name, path, reason, f"cmake --build --preset debug --target {name}")

    def engine_targets(path: str, reason: str, names: tuple[str, ...] = ("QSanguosha", "qsanguosha_server", "qsanguosha_tui")) -> None:
        for name in names:
            target(name, path, reason)

    for path in paths:
        p = path.replace("\\", "/")
        low = p.lower()
        known = False
        is_provider = (
            low.startswith(("tools/external_agents/", "tools/external-agents/", "tools/providers/", "tools/provider/", "src/server/external-agent"))
            or "external_agent" in low or "external-agent" in low
            or "/provider" in low or "provider" in low.split("/")
        )

        # Provider review is orthogonal to native ownership: C++ adapters still
        # need their owning executable builds and runtime checks below.
        if is_provider:
            add(common, "provider_contract_review", p, "Review provider schema, routing, and provenance against the shared external-agent contract.")
            api_boundary = any(token in low for token in ("transport", "http", "request", "api", "endpoint"))
            if api_boundary:
                add(deferred, "provider_live_api", p, "Transport/API contract changed; select a minimal live API verification for the affected provider. No request is made by this planner.")
            else:
                add(deferred, "provider_live_api_if_contract_changed", p, "Only select a minimal live API check if review shows transport/API contract changed.")
            known = True

        if low.endswith("builds/sanguosha.ts") or low == "builds/sanguosha.ts":
            target("QSanguosha", p, "Tracked Qt translation source changed; GUI target runs qt_add_lrelease to regenerate the QM output.")
            add(common, "translation_review", p, "Check translation context, key coverage, and placeholder parity; the GUI target performs lrelease.")
            known = True
        elif low.startswith(("lua/", "extensions/")) or low.endswith(".lua"):
            add(common, "lua_change_review", p, "Review Lua syntax, API use, and the isolated/SmartAI boundary relevant to this file.")
            add(required, "affected_lua_runtime", p, "Exercise the affected Lua path in its owning mode; this plan does not start a game.")
            known = True
        elif low.startswith(("src/ui/", "ui-script/", "qml/")) or low.endswith(".qml"):
            target("QSanguosha", p, "Local UI/QML source is linked into the GUI only.")
            add(common, "ui_translation_and_layout_review", p, "Review affected translation keys and UI behavior at relevant resolutions.")
            add(required, "desktop_ui_smoke", p, "Exercise the changed desktop view; planner does not launch the GUI.")
            add(deferred, "android_gui_smoke", p, "GUI source is shared with Android; defer device/emulator validation.")
            add(deferred, "winxp_gui_smoke", p, "GUI source is shared with the WinXP legacy GUI; defer compatibility validation.")
            known = True
        elif low == "cmakepresets.json":
            add(common, "configure_preset_review", p, "CMake preset changed; inspect generator, architecture, and cache-variable scope before selecting configure/build commands.")
            known = True
        elif low == "cmakelists.txt" or low.startswith("cmake/") or low == "qsanguoshasources.cmake":
            engine_targets(p, "Build configuration changed; inspect affected options, then configure/build only the impacted targets.")
            add(common, "build_configuration_review", p, "Review configure/preset and target wiring; do not infer unrelated platform builds.")
            known = True
        elif low.startswith(("src/", "swig/")) or low.endswith((".cpp", ".cc", ".cxx", ".h", ".hpp", ".i")):
            if low.startswith("src/tui/"):
                target("qsanguosha_tui", p, "TUI frontend source is isolated to the TUI executable.")
                add(required, "tui_focused_smoke", p, "Exercise the changed TUI flow; planner does not launch the client.")
                known = True
            elif low.startswith("src/client/core/"):
                target("QSanguosha", p, "Client core is shared by GUI and Web Solo clients.")
                target("qsanguosha_tui", p, "Client core is shared by GUI and TUI clients.")
                add(required, "gui_tui_client_smoke", p, "Check changed shared client behavior in GUI and TUI modes as applicable.")
                add(deferred, "wasm_web_solo", p, "Shared client core reaches Web Solo; defer WASM/browser validation.")
                add(deferred, "android_client_smoke", p, "Shared GUI client source reaches Android; defer device/emulator validation.")
                add(deferred, "winxp_client_smoke", p, "Shared GUI client source reaches WinXP32; defer compatibility validation.")
                known = True
            elif low.startswith("src/client/"):
                target("QSanguosha", p, "Client source is linked into the GUI.")
                target("qsanguosha_tui", p, "Client source is linked into the TUI client.")
                add(required, "gui_tui_client_smoke", p, "Check changed client behavior in GUI and TUI modes as applicable.")
                known = True
            elif low.startswith("src/server/"):
                target("QSanguosha", p, "Server implementation is embedded in the GUI client.")
                target("qsanguosha_server", p, "Server implementation is linked into the dedicated server.")
                add(required, "embedded_and_dedicated_server_smoke", p, "Check the changed server behavior in GUI embedded and dedicated-server modes.")
                if any(token in low for token in ("protocol", "websocket", "http", "gateway")):
                    add(required, "shared_protocol_clients", p, "Protocol/gateway change may affect GUI, Web, and external clients; select compatible client smokes.")
                add(deferred, "wasm_web_solo", p, "Shared server engine change can affect Web Solo; defer WASM build/browser smoke.")
                add(deferred, "android_engine_smoke", p, "Shared server engine change can affect Android; defer device/emulator validation.")
                add(deferred, "winxp_engine_smoke", p, "Shared server engine change can affect WinXP32; defer compatibility validation.")
                known = True
            elif low.startswith(("src/core/", "src/package/", "src/scenario/", "src/util/", "src/lua/")):
                engine_targets(p, "Shared native game engine source is used by GUI, dedicated server, and TUI targets.")
                add(required, "native_engine_focused_smoke", p, "Exercise the affected engine flow in an applicable product mode; no full game is started automatically.")
                for key, reason in (
                    ("wasm_web_solo", "Shared native engine source reaches the Web Solo product; defer WASM/browser smoke."),
                    ("android_engine_smoke", "Shared native engine source reaches Android; defer device/emulator validation."),
                    ("winxp_engine_smoke", "Shared native engine source reaches WinXP32; defer compatibility validation."),
                ):
                    add(deferred, key, p, reason)
                known = True
            elif low.startswith("swig/") or low.endswith(".i"):
                engine_targets(p, "SWIG binding source changed; normal CMake build regenerates and compiles the wrapper.")
                add(common, "swig_binding_review", p, "Inspect the binding contract and generated wrapper/build output.")
                known = True
            elif low.startswith("src/"):
                engine_targets(p, "Native source in an unclassified source area; include common executable targets pending review.")
                add(common, "native_source_scope_review", p, "Unclassified native source; confirm its owning targets and platform reach.")
                known = True

        if low.startswith(("image/", "audio/", "qml/home/icons/", "qml/home/backdrops/", "assets/")) or low.endswith((".png", ".jpg", ".jpeg", ".ogg", ".mp3", ".wav", ".svg")):
            add(common, "asset_content_review", p, "Content asset changed; inspect packaging/resource references and deployment behavior.")
            add(deferred, "asset_deploy_smoke", p, "Confirm the changed content is present in the relevant deployed runtime/package.")
            known = True
        if low.startswith(("web/", "browser/", "client/")):
            add(required, "web_client_smoke", p, "Web/browser client changed; select matching browser solo or server-connected smoke.")
            known = True
        if low.startswith(("tools/", "docs/", "translations/")):
            add(common, "change_specific_review", p, "Tooling, documentation, or runtime data changed; review its direct contract and invocation.")
            known = True
        if not known:
            unknown.append(p)
            add(common, "manual_scope_review", p, "Path has no planner rule; identify affected checks before treating the change as verified.")

    return {
        "changed_paths": paths,
        "common_review": common,
        "native_incremental_build_targets": builds,
        "required_runtime_checks": required,
        "deferred_platform_or_release_matrix": deferred,
        "unknown_paths_requiring_review": unknown,
        "automatic_execution": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".", help="repository root (default: current directory)")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--path", action="append", help="changed path; repeat for multiple paths")
    source.add_argument("--base", help="Git ref; includes tracked staged/unstaged changes, excludes untracked paths by default")
    parser.add_argument("--include-untracked", action="store_true", help="also include non-ignored untracked paths (bounded to 5000 paths)")
    parser.add_argument("--json", action="store_true", help="emit JSON only")
    args = parser.parse_args()
    try:
        root = Path(args.repo).resolve()
        branch = str(git(str(root), "branch", "--show-current"))
        head = str(git(str(root), "rev-parse", "HEAD"))
        base_resolved = None
        if args.path is not None:
            paths = sorted({path_in_repo(root, p) for p in args.path} - {""})
            states = []
            for p in paths:
                f = root / Path(p)
                if f.is_file():
                    content = f.read_bytes()
                    states.append((p, "file", hashlib.sha256(content).hexdigest()))
                elif f.is_dir():
                    raise ValueError(f"--path expects file paths, not directories: {p}")
                else:
                    states.append((p, "missing", ""))
            material = json.dumps(states, ensure_ascii=True, separators=(",", ":")).encode()
            source_name = "explicit_paths_with_content_state"
        else:
            base_resolved = str(git(str(root), "rev-parse", "--verify", "--end-of-options", f"{args.base}^{{commit}}"))
            paths_bytes = git(str(root), "diff", "--name-only", "-z", base_resolved, "--", binary=True)
            names = paths_bytes.split(b"\0")
            paths = sorted({path_in_repo(root, n.decode("utf-8", errors="surrogateescape")) for n in names if n})
            patch_bytes = git(str(root), "diff", "--binary", base_resolved, "--", binary=True)
            material = b"tracked-diff\0" + patch_bytes
            if args.include_untracked:
                raw_untracked = git(str(root), "ls-files", "--others", "--exclude-standard", "-z", binary=True)
                untracked = sorted({path_in_repo(root, n.decode("utf-8", errors="surrogateescape")) for n in raw_untracked.split(b"\0") if n})
                if len(untracked) > 5000:
                    raise ValueError(f"--include-untracked found {len(untracked)} paths; limit is 5000")
                untracked = [p for p in untracked if not p.startswith(("debug/", "release/", "relwithdebinfo/", "builds/"))]
                states = []
                for p in untracked:
                    f = root / Path(p)
                    states.append((p, hashlib.sha256(f.read_bytes()).hexdigest() if f.is_file() else "missing"))
                paths = sorted(set(paths) | set(untracked))
                material += b"\0untracked\0" + json.dumps(states, ensure_ascii=True, separators=(",", ":")).encode()
            source_name = "git_diff"
        digest = hashlib.sha256(material).hexdigest()
        result = {
            "schema_version": 2,
            "planner": "change-based-verification-scope",
            "source": source_name,
            "base_input": args.base,
            "base_commit": base_resolved,
            "branch": branch,
            "head": head,
            "diff_id": digest,
            "scope_freshness": "Replan or confirm branch, head, base_commit, and diff_id still match before using this plan.",
            "git_mode_untracked_policy": "included non-ignored paths" if args.include_untracked and args.base else ("excluded; pass --include-untracked to add bounded non-ignored paths" if args.base else None),
            **classify(paths),
        }
    except (subprocess.CalledProcessError, OSError, ValueError) as exc:
        print(f"verification scope planning failed: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result, ensure_ascii=True, indent=2))
    else:
        print(f"Verification scope: {result['source']} | branch={branch or '(detached)'} | head={head[:12]} | diff_id={digest[:16]}")
        for title, key in (("Common review", "common_review"), ("Incremental native builds", "native_incremental_build_targets"), ("Required runtime checks", "required_runtime_checks"), ("Deferred platform/release matrix", "deferred_platform_or_release_matrix")):
            print(f"\n{title}:")
            if not result[key]:
                print("  (none selected)")
            for name, entry in result[key].items():
                print(f"  - {name}: {'; '.join(entry['reasons'])}")
                for command in entry["commands"]:
                    print(f"      {command}")
        if result["unknown_paths_requiring_review"]:
            print("\nUnknown paths requiring review:")
            for p in result["unknown_paths_requiring_review"]:
                print(f"  - {p}")
        print("\nNo checks, builds, API calls, or games were run.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
