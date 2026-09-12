#!/usr/bin/env python3
"""Build a self-contained offline Web/Solo distribution directory.

The destination is deliberately a directory rather than a zip.  It can be
zipped by the release job without requiring Python or Node on the target PC.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import tempfile


CONTENT_KEYS = {"path", "role", "size", "sha256"}
CONTENT_ROLES = {"rules", "presentation"}
AI_ROOT = "lua/ai"
MIDDLECLASS = "lua/lib/middleclass.lua"
AI_RUNTIME_PARTS = {".git", "logs", "data", "temp", "runtime", "backup", "backups"}
CORE_BOOTSTRAP = ("lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua",
                  "lua/sgs_ex.lua", "lua/lib/json.lua")


def _error(message: str) -> ValueError:
    return ValueError(message)


def _is_reparse(path: Path) -> bool:
    try:
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
    except OSError:
        return False
    return bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))


def _unsafe(path: Path) -> bool:
    return path.is_symlink() or _is_reparse(path)


def _require_tree(path: Path, label: str) -> Path:
    if _unsafe(path):
        raise _error(f"{label} must be a real directory")
    path = path.resolve(strict=True)
    if _unsafe(path) or not path.is_dir():
        raise _error(f"{label} must be a real directory")
    for parent in path.parents:
        if _unsafe(parent):
            raise _error(f"{label} has a symlinked or reparse parent")
    return path


def _require_file(path: Path, label: str, nonempty: bool = True) -> Path:
    if _unsafe(path):
        raise _error(f"{label} must not be symlinked or reparse-pointed")
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise _error(f"missing {label}: {path}") from error
    if _unsafe(resolved) or not resolved.is_file():
        raise _error(f"missing {label}: {path}")
    if nonempty and resolved.stat().st_size == 0:
        raise _error(f"empty {label}: {path}")
    for parent in resolved.parents:
        if _unsafe(parent):
            raise _error(f"{label} has a symlinked or reparse parent")
    return resolved


def _relative(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def _safe_relative(relative: str) -> None:
    if not isinstance(relative, str) or "\\" in relative or not relative \
            or relative.startswith("/") or any(part in {"", ".", ".."} for part in relative.split("/")) \
            or not re.fullmatch(r"[A-Za-z0-9_./-]+", relative):
        raise _error("invalid content path")


def _declared_source(root: Path, relative: str) -> Path:
    _safe_relative(relative)
    if not relative.endswith(".lua"):
        raise _error("declared content must be Lua")
    path = root.joinpath(*relative.split("/"))
    return _require_file(path, f"declared content {relative}")


def _hash_file(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            size += len(chunk)
            digest.update(chunk)
    return size, digest.hexdigest()


def _load_rules(bundle_path: Path, asset_root: Path) -> list[dict]:
    try:
        bundle = json.loads(bundle_path.read_bytes())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise _error(f"invalid rules bundle: {bundle_path}") from error
    manifest = bundle.get("rules_content") if isinstance(bundle, dict) else None
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 2 \
            or manifest.get("profile") != "declared-v2" \
            or not isinstance(manifest.get("runtime_content"), dict) \
            or not isinstance(manifest.get("files"), list):
        raise _error("rules bundle lacks a declared-v2 rules_content manifest")
    result: list[dict] = []
    seen: set[str] = set()
    for entry in manifest["files"]:
        if not isinstance(entry, dict) or set(entry) != CONTENT_KEYS:
            raise _error("invalid rules_content manifest entry")
        relative, role, expected_size, expected_hash = (entry["path"], entry["role"],
                                                         entry["size"], entry["sha256"])
        if relative in seen or role not in CONTENT_ROLES or not isinstance(expected_size, int) \
                or isinstance(expected_size, bool) or expected_size < 0 \
                or not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            raise _error("invalid rules_content manifest entry")
        if role == "presentation":
            valid_role = relative.startswith("lang/") and relative.endswith(".lua")
        else:
            valid_role = (re.fullmatch(r"extensions/[A-Za-z0-9_.-]+\.lua", relative) is not None
                          or relative.startswith("lua/")) and not relative.startswith("lua/ai/") \
                and relative != MIDDLECLASS and relative.endswith(".lua")
        if not valid_role:
            raise _error(f"declared content path does not match role: {relative}")
        source = _declared_source(asset_root, relative)
        size, digest = _hash_file(source)
        if size != expected_size or digest != expected_hash:
            raise _error(f"declared content hash or size mismatch: {relative}")
        seen.add(relative)
        result.append({"path": relative, "role": role, "size": size, "sha256": digest})
    return result


def _declared_entries(config_path: Path) -> list[str]:
    config = config_path.read_text(encoding="utf-8")
    block = re.search(r"extension_names\s*=\s*\{(.*?)\}", config, re.S)
    if block is None:
        raise _error("lua/config.lua has no extension_names declaration")
    entries = re.findall(r'"([^"\n]+)"', block.group(1))
    if not entries:
        raise _error("lua/config.lua extension_names declaration is empty")
    return entries


def _declared_closure(asset_root: Path) -> list[str]:
    config = _require_file(asset_root / "lua/config.lua", "lua/config.lua")
    paths = list(CORE_BOOTSTRAP)
    seen = set(paths)
    for entry in _declared_entries(config):
        fields = entry.split(";")
        script = fields[0]
        if not re.fullmatch(r"extensions/[A-Za-z0-9_.-]+\.lua", script):
            raise _error(f"invalid extension script declaration: {script}")
        if script in seen:
            raise _error(f"duplicate declared content path: {script}")
        paths.append(script)
        seen.add(script)
        field_keys: set[str] = set()
        for field in fields[1:]:
            key, separator, values = field.partition("=")
            if not separator or key in field_keys or key not in {"libs", "lang", "ai"}:
                raise _error(f"invalid extension declaration field: {entry}")
            field_keys.add(key)
            if key == "ai":
                continue
            for relative in values.split(","):
                if not relative:
                    raise _error(f"empty path in extension declaration: {entry}")
                _safe_relative(relative)
                expected_prefix = "lang/" if key == "lang" else "lua/"
                if not relative.startswith(expected_prefix) or not relative.endswith(".lua"):
                    raise _error(f"invalid {key} path in extension declaration: {relative}")
                if relative in seen:
                    raise _error(f"duplicate declared content path: {relative}")
                paths.append(relative)
                seen.add(relative)
    for relative in paths:
        _require_file(asset_root / relative, f"declared content {relative}")
    return paths


def prepare_content(asset_root_path: Path, destination_path: Path) -> None:
    asset_root = _require_tree(asset_root_path, "asset root")
    destination = destination_path.absolute()
    if _unsafe(destination) or (destination.exists() and
                                (not destination.is_dir() or any(destination.iterdir()))):
        raise _error("destination must be new or empty")
    if any(_unsafe(parent) for parent in destination.parents):
        raise _error("destination has a symlinked or reparse parent")
    if destination == asset_root:
        raise _error("destination must differ from asset root")
    paths = _declared_closure(asset_root)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".qsan-content-", dir=str(destination.parent)) as temporary:
        staged = Path(temporary)
        for relative in paths:
            source = asset_root / relative
            target = staged / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        destination.mkdir(parents=True, exist_ok=True)
        for child in staged.iterdir():
            shutil.move(str(child), str(destination / child.name))


def _collect_ai(asset_root: Path) -> list[tuple[str, Path]]:
    ai_root = asset_root / AI_ROOT
    middleclass = _require_file(asset_root / MIDDLECLASS, "AI dependency lua/lib/middleclass.lua")
    if _unsafe(ai_root):
        raise _error("lua/ai must not be symlinked or reparse-pointed")
    if not ai_root.is_dir():
        raise _error("missing AI directory: lua/ai")
    files: list[tuple[str, Path]] = []
    for path in sorted(ai_root.rglob("*"), key=lambda item: item.as_posix()):
        if _unsafe(path):
            raise _error(f"AI tree contains a symlink or reparse point: {path}")
        if not path.is_file():
            continue
        relative_parts = {part.lower() for part in path.relative_to(ai_root).parts[:-1]}
        if path.suffix.lower() != ".lua" or relative_parts.intersection(AI_RUNTIME_PARTS):
            continue
        relative = _relative(path, asset_root)
        files.append((relative, _require_file(path, f"AI file {relative}")))
    if not files:
        raise _error("lua/ai contains no Lua files")
    files.append((MIDDLECLASS, middleclass))
    return files


def _copy_tree(source: Path, destination: Path) -> None:
    for path in sorted(source.rglob("*"), key=lambda item: item.as_posix()):
        relative = path.relative_to(source)
        target = destination / relative
        if _unsafe(path):
            raise _error(f"Web distribution contains a symlink or reparse point: {path}")
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif path.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
        else:
            raise _error(f"Web distribution contains unsupported entry: {path}")


def _copy_media(source: Path, destination: Path, label: str, collisions: set[str]) -> None:
    if _unsafe(source) or not source.is_dir():
        raise _error(f"missing {label} directory: {source}")
    files = [path for path in source.rglob("*") if path.is_file()]
    if not files:
        raise _error(f"{label} directory is empty: {source}")
    for path in sorted(source.rglob("*"), key=lambda item: item.as_posix()):
        if _unsafe(path):
            raise _error(f"{label} contains a symlink or reparse point: {path}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise _error(f"{label} contains unsupported entry: {path}")
        relative = path.relative_to(source).as_posix()
        if relative in collisions:
            raise _error(f"{label} collides with a Vite asset: assets/{relative}")
        collisions.add(relative)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)


def _ini_value(root: Path, key: str, fallback: str) -> str:
    path = root / "config.ini"
    if not path.is_file() or _unsafe(path):
        return fallback
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(key + "="):
            return line[len(key) + 1:].strip()
    return fallback


def _skin_value(root: Path, key: str, fallback: str = "") -> str:
    path = root / "skins/fulldefaultSkin.image.json"
    if not path.is_file() or _unsafe(path):
        return fallback
    match = re.search(r'"' + re.escape(key) + r'"\s*:\s*"([^"]+)"',
                      path.read_text(encoding="utf-8"))
    return match.group(1) if match else fallback


def _asset_url(value: str) -> str:
    if value.startswith("image/"):
        return "/assets/" + value[len("image/"):]
    return value


def _ui_config(asset_root: Path) -> bytes:
    kingdoms = ("wei", "shu", "wu", "qun", "jin", "god")
    table_by_kingdom = {}
    for kingdom in kingdoms:
        value = _skin_value(asset_root, "tableBg" + kingdom)
        if value:
            table_by_kingdom[kingdom] = _asset_url(value)
    config = {
        "backgroundImage": _asset_url(_ini_value(asset_root, "BackgroundImage", "image/system/backdrop/2.jpg")),
        "tableBgImage": _asset_url(_ini_value(asset_root, "TableBgImage",
                                               _skin_value(asset_root, "tableBg", "image/system/backdrop/default.jpg"))),
        "enableAutoBackgroundChange": _ini_value(asset_root, "EnableAutoBackgroundChange", "true") == "true",
        "tableBgByKingdom": table_by_kingdom,
    }
    return json.dumps(config, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def _runtime(module: Path) -> tuple[Path, Path, dict]:
    if module.name != "qsanguosha_solo_wasm.mjs":
        raise _error("--solo-module must name qsanguosha_solo_wasm.mjs")
    module = _require_file(module, "solo WASM module")
    wasm = module.with_suffix(".wasm")
    _require_file(wasm, "solo WASM binary")
    if wasm.read_bytes()[:8] != b"\x00asm\x01\x00\x00\x00":
        raise _error("solo WASM binary has an invalid header")
    module_size, module_hash = _hash_file(module)
    wasm_size, wasm_hash = _hash_file(wasm)
    return module, wasm, {"schema_version": 1, "runtime": {"module": {
        "path": "qsanguosha_solo_wasm.mjs", "size": module_size, "sha256": module_hash},
        "wasm": {"path": "qsanguosha_solo_wasm.wasm", "size": wasm_size, "sha256": wasm_hash}}}


def _runtime_seal(module: Path, wasm: Path) -> dict:
    return {"schema_version": 1, "files": {
        "qsanguosha_solo_wasm.mjs": _hash_file(module)[1],
        "qsanguosha_solo_wasm.wasm": _hash_file(wasm)[1],
    }}


def seal_runtime(module_path: Path) -> None:
    module, wasm, _ = _runtime(module_path)
    sidecar = module.with_suffix(".bundle.json")
    if _unsafe(sidecar):
        raise _error("solo runtime seal must not be symlinked or reparse-pointed")
    sidecar.write_bytes(json.dumps(_runtime_seal(module, wasm), sort_keys=True,
                                   separators=(",", ":")).encode("utf-8") + b"\n")


def _verify_client_runtime(web_dist: Path) -> None:
    rules = web_dist / "rules"
    sidecar = _require_file(rules / "qsanguosha_client_wasm.bundle.json", "Web client runtime seal")
    try:
        manifest = json.loads(sidecar.read_bytes())
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise _error("invalid Web client runtime seal") from error
    if not isinstance(manifest, dict) or set(manifest) != {"schema_version", "bridge_schema", "files"} \
            or manifest.get("schema_version") != 1 \
            or not isinstance(manifest.get("bridge_schema"), int) \
            or isinstance(manifest.get("bridge_schema"), bool) \
            or not isinstance(manifest.get("files"), dict) \
            or set(manifest["files"]) != {"qsanguosha_client_wasm.mjs", "qsanguosha_client_wasm.wasm"}:
        raise _error("unsupported Web client runtime seal schema")
    for name, expected in manifest["files"].items():
        if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
            raise _error("invalid Web client runtime seal hash")
        source = _require_file(rules / name, f"Web client runtime {name}")
        if _hash_file(source)[1] != expected:
            raise _error(f"Web client runtime seal mismatch: {name}")


def package(args: argparse.Namespace) -> None:
    web_dist = _require_tree(args.web_dist, "Web distribution")
    asset_root = _require_tree(args.asset_root, "asset root")
    launcher = _require_file(args.launcher, "native launcher")
    bundle = _require_file(args.rules_bundle, "rules bundle")
    module, wasm, manifest = _runtime(args.solo_module)
    solo_sidecar = module.with_suffix(".bundle.json")
    if _unsafe(solo_sidecar) or not solo_sidecar.is_file():
        raise _error("solo runtime seal is missing or symlinked; run --seal-runtime after building")
    try:
        actual_seal = json.loads(solo_sidecar.read_bytes())
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise _error("invalid solo runtime seal") from error
    if actual_seal != _runtime_seal(module, wasm):
        raise _error("solo runtime seal mismatch; rebuild or reseal the paired artifacts")
    _verify_client_runtime(web_dist)
    rules = _load_rules(bundle, asset_root)
    try:
        bundle_payload = json.loads(bundle.read_bytes())
        runtime_content = bundle_payload["rules_content"]["runtime_content"]
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        raise _error("rules bundle runtime content descriptor is missing") from error
    ai = _collect_ai(asset_root)
    destination = args.destination.absolute()
    if _unsafe(destination):
        raise _error("destination must not be a symlink or reparse point")
    if destination.exists() and (not destination.is_dir() or any(destination.iterdir())):
        raise _error("destination must be new or empty")
    if any(_unsafe(parent) for parent in destination.parents):
        raise _error("destination has a symlinked or reparse parent")
    destination = destination.resolve()
    if destination == web_dist or destination == asset_root:
        raise _error("destination must differ from input roots")
    # Staging beneath a copied tree would recursively copy the staging directory.
    copied_roots = [web_dist, *(asset_root / name for name in ("image", "audio", "lua", "extensions", "lang"))]
    if any(destination == root or root in destination.parents for root in copied_roots):
        raise _error("destination must be outside copied input trees")

    # Validate all output names and collisions before touching the destination.
    web_assets = web_dist / "assets"
    web_asset_names = set()
    if web_assets.exists():
        web_asset_names = {path.relative_to(web_assets).as_posix() for path in web_assets.rglob("*") if path.is_file()}
    media_files = [path.relative_to(asset_root / "image").as_posix() for path in (asset_root / "image").rglob("*") if path.is_file()] \
        if (asset_root / "image").is_dir() else []
    if web_asset_names.intersection(media_files):
        raise _error("image files collide with Vite assets")

    content = rules[:]
    for relative, path in ai:
        size, digest = _hash_file(path)
        content.append({"path": relative, "role": "ai", "size": size, "sha256": digest})
    manifest["content"] = {"schema_version": 2, "profile": "declared-v2",
                            "runtime_content": runtime_content,
                            "files": content}

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".qsan-solo-", dir=str(destination.parent)) as temporary:
        staged = Path(temporary)
        _copy_tree(web_dist, staged)
        assets = staged / "assets"
        assets.mkdir(parents=True, exist_ok=True)
        _copy_media(asset_root / "image", assets, "image", web_asset_names.copy())
        _copy_media(asset_root / "audio", staged / "audio", "audio", set())
        rules_content = staged / "rules/content"
        for entry in content:
            source = _declared_source(asset_root, entry["path"]) if entry["role"] in CONTENT_ROLES \
                else _require_file(asset_root / entry["path"], f"AI file {entry['path']}")
            target = rules_content / entry["sha256"]
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists() and target.read_bytes() != source.read_bytes():
                raise _error(f"content hash collision: {entry['sha256']}")
            if not target.exists():
                shutil.copyfile(source, target)
            if _hash_file(target) != (entry["size"], entry["sha256"]):
                raise _error(f"content changed during packaging: {entry['path']}")
        solo = staged / "solo"
        solo.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(module, solo / "qsanguosha_solo_wasm.mjs")
        shutil.copyfile(wasm, solo / "qsanguosha_solo_wasm.wasm")
        if _runtime_seal(solo / module.name, solo / wasm.name) != actual_seal:
            raise _error("solo runtime changed during packaging")
        _verify_client_runtime(staged)
        (solo / "manifest.json").write_bytes(json.dumps(manifest, ensure_ascii=False,
                                                          separators=(",", ":")).encode("utf-8") + b"\n")
        (solo / "enabled.json").write_bytes(b'{"schema_version":1}\n')
        shutil.copyfile(launcher, staged / "StartGame.exe")
        (staged / "game-ui-config.json").write_bytes(_ui_config(asset_root) + b"\n")
        (staged / "README-offline.txt").write_text(
            "QSanguosha offline solo bundle\n\n"
            "Launch StartGame.exe on Windows 10/11 x64 with Chrome or Edge installed.\n"
            "The Web UI, WASM rules runtime, rules content, Lua AI, images and audio are bundled locally.\n"
            "This offline package does not save matches.\n", encoding="utf-8")
        destination.mkdir(parents=True, exist_ok=True)
        for child in staged.iterdir():
            shutil.move(str(child), str(destination / child.name))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--web-dist", type=Path)
    parser.add_argument("--asset-root", type=Path)
    parser.add_argument("--rules-bundle", type=Path)
    parser.add_argument("--solo-module", type=Path)
    parser.add_argument("--launcher", type=Path)
    parser.add_argument("--destination", type=Path)
    parser.add_argument("--seal-runtime", action="store_true")
    parser.add_argument("--prepare-content", action="store_true")
    args = parser.parse_args()
    try:
        if args.seal_runtime and args.prepare_content:
            parser.error("--seal-runtime and --prepare-content are mutually exclusive")
        if args.seal_runtime:
            if args.solo_module is None or any(value is not None for value in (args.web_dist, args.asset_root,
                                                    args.rules_bundle, args.launcher, args.destination)):
                parser.error("--seal-runtime accepts only --solo-module")
            seal_runtime(args.solo_module)
        elif args.prepare_content:
            if args.asset_root is None or args.destination is None \
                    or any(value is not None for value in (args.web_dist, args.rules_bundle,
                                                            args.solo_module, args.launcher)):
                parser.error("--prepare-content requires only --asset-root and --destination")
            prepare_content(args.asset_root, args.destination)
        else:
            if any(value is None for value in (args.web_dist, args.asset_root,
                                               args.rules_bundle, args.launcher, args.destination)):
                parser.error("full packaging requires --web-dist, --asset-root, --rules-bundle, --solo-module, --launcher and --destination")
            if args.solo_module is None:
                parser.error("full packaging requires --solo-module")
            package(args)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"offline solo packaging failed: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
