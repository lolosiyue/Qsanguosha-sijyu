#!/usr/bin/env python3
"""Build a self-contained offline Web/Solo distribution directory.

The destination is deliberately a directory rather than a zip.  It can be
zipped by the release job without requiring Python or Node on the target PC.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
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


def _safe_relative(relative: str) -> str:
    if not isinstance(relative, str) or "\\" in relative or not relative \
            or relative.startswith("/") or any(part in {"", ".", ".."} for part in relative.split("/")) \
            or not re.fullmatch(r"[A-Za-z0-9_./-]+", relative):
        raise _error("invalid content path")
    return relative


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
    is_v2 = isinstance(manifest, dict) and manifest.get("schema_version") == 2 \
        and manifest.get("profile") == "declared-v2"
    is_v3 = isinstance(manifest, dict) and manifest.get("schema_version") == 3 \
        and manifest.get("profile") == "packages-v1"
    if not isinstance(manifest, dict) or not (is_v2 or is_v3) \
            or not isinstance(manifest.get("runtime_content"), dict) \
            or not isinstance(manifest.get("files"), list):
        raise _error("rules bundle lacks supported runtime content")
    package_values = manifest["runtime_content"].get("packages", [])
    if not isinstance(package_values, list) or not isinstance(manifest["runtime_content"].get("extensions"), list):
        raise _error("invalid runtime content descriptor")
    package_ids = {item.get("id") for item in package_values
                   if isinstance(item, dict) and isinstance(item.get("id"), str)}
    result: list[dict] = []
    seen: set[str] = set()
    for entry in manifest["files"]:
        if not isinstance(entry, dict) or set(entry) != CONTENT_KEYS:
            raise _error("invalid rules_content manifest entry")
        relative, role, expected_size, expected_hash = (entry["path"], entry["role"],
                                                         entry["size"], entry["sha256"])
        if relative in seen or role not in {"rules", "presentation", "ai"} or not isinstance(expected_size, int) \
                or isinstance(expected_size, bool) or expected_size < 0 \
                or not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            raise _error("invalid rules_content manifest entry")
        if relative.startswith("packages/") and is_v3:
            parts = relative.split("/")
            package_id = parts[1] if len(parts) > 2 else ""
            valid_role = package_id in package_ids and ((role == "presentation"
                and relative.startswith(f"packages/{package_id}/translation/") and relative.endswith(".lua"))
                or (role == "ai" and relative.startswith(f"packages/{package_id}/lua/ai/") and relative.endswith(".lua"))
                or (role == "rules" and relative.startswith(f"packages/{package_id}/lua/")
                    and not relative.startswith(f"packages/{package_id}/lua/ai/") and relative.endswith(".lua")))
        elif role == "presentation":
            valid_role = relative.startswith("lang/") and relative.endswith(".lua")
        elif role == "ai":
            valid_role = relative.startswith("lua/ai/") or relative == MIDDLECLASS
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


def _load_packages(package_root: Path) -> list[dict]:
    """Validate package inventories and return deterministic local URL records."""
    if _unsafe(package_root) or not package_root.exists():
        return []
    if not package_root.is_dir():
        raise _error("packages root must be a directory")
    manifests: list[tuple[str, Path, dict]] = []
    folded_ids: set[str] = set()
    for directory in sorted(package_root.iterdir(), key=lambda item: item.name.casefold()):
        if directory.name.casefold() == "readme.md" and directory.is_file() and not _unsafe(directory):
            continue
        if _unsafe(directory) or not directory.is_dir():
            raise _error(f"package root contains unsupported entry: {directory}")
        if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", directory.name):
            raise _error(f"invalid package directory id: {directory.name}")
        if directory.name.casefold() in folded_ids:
            raise _error(f"case-insensitive package id collision: {directory.name}")
        folded_ids.add(directory.name.casefold())
        manifest_path = _require_file(directory / "manifest.json", "package manifest")
        try:
            manifest = json.loads(manifest_path.read_bytes())
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise _error(f"invalid package manifest: {manifest_path}") from error
        if not isinstance(manifest, dict) or manifest.get("schema_version") != 1 \
                or manifest.get("id") != directory.name or not isinstance(manifest.get("version"), str) \
                or manifest.get("engine_api") != 1 or not isinstance(manifest.get("dependencies"), list) \
                or not isinstance(manifest.get("files"), list):
            raise _error(f"invalid package manifest metadata: {manifest_path}")
        manifests.append((directory.name, directory, manifest))
    ids = {package_id for package_id, _, _ in manifests}
    records: list[dict] = []
    global_asset_aliases: set[str] = set()
    for package_id, directory, manifest in manifests:
        dependencies = manifest["dependencies"]
        if any(not isinstance(value, str) or value not in ids for value in dependencies):
            raise _error(f"package dependency is missing or invalid: {package_id}")
        if len({value.casefold() for value in dependencies}) != len(dependencies) \
                or package_id in dependencies:
            raise _error(f"package has duplicate or self dependency: {package_id}")
        raw_assets = manifest.get("assets")
        if not isinstance(raw_assets, dict) or any(not isinstance(alias, str) or not isinstance(target, str)
                                                   for alias, target in raw_assets.items()):
            raise _error(f"invalid package asset mapping: {package_id}")
        for alias in raw_assets:
            _safe_relative(alias)
            target = raw_assets[alias]
            _safe_relative(target)
            if (not alias.startswith(("image/", "audio/"))
                    or not target.startswith(("image/", "audio/", "data/"))
                    or alias.split("/", 1)[0] != target.split("/", 1)[0]):
                raise _error(f"invalid package asset mapping: {alias} -> {target}")
            if alias.casefold() in global_asset_aliases:
                raise _error(f"package asset alias collision: {alias}")
            global_asset_aliases.add(alias.casefold())
        extensions = []
        lua_files = []
        extension_names: set[str] = set()
        raw_extensions = manifest.get("extensions")
        if not isinstance(raw_extensions, list):
            raise _error(f"invalid package extension list: {package_id}")
        for extension in raw_extensions:
            if not isinstance(extension, dict) or not isinstance(extension.get("name"), str) \
                    or extension["name"] in extension_names:
                raise _error(f"invalid package extension declaration: {package_id}")
            extension_names.add(extension["name"])
            paths: dict[str, list[str]] = {}
            for field, prefix, role in (("libs", "lua/", "rules"),
                                         ("lang", "translation/", "presentation"),
                                         ("ai", "lua/ai/", "ai")):
                values = extension.get(field)
                if not isinstance(values, list) or any(not isinstance(value, str) for value in values):
                    raise _error(f"invalid {field} declaration: {package_id}/{extension['name']}")
                paths[field] = [f"packages/{package_id}/{_safe_relative(value)}" for value in values]
            script = extension.get("script")
            _safe_relative(script)
            if not script.startswith("lua/") or script.startswith("lua/ai/") or not script.endswith(".lua"):
                raise _error(f"invalid package extension script: {package_id}/{script}")
            dependencies_for_extension = extension.get("dependencies")
            if not isinstance(dependencies_for_extension, list) or not all(
                    isinstance(value, str) for value in dependencies_for_extension):
                raise _error(f"invalid extension dependencies: {package_id}/{extension['name']}")
            extensions.append({"name": extension["name"], "script": f"packages/{package_id}/{script}",
                "dependencies": dependencies_for_extension,
                "libs": paths["libs"], "lang": paths["lang"], "ai": paths["ai"]})
        declared_code = {extension["script"] for extension in extensions}
        for extension in extensions:
            declared_code.update(extension["libs"])
            declared_code.update(extension["lang"])
            declared_code.update(extension["ai"])
        seen: set[str] = set()
        for entry in manifest["files"]:
            if not isinstance(entry, dict) or set(entry) != CONTENT_KEYS:
                raise _error(f"invalid package file record: {package_id}")
            relative, role, expected_size, expected_hash = (entry["path"], entry["role"],
                entry["size"], entry["sha256"])
            _safe_relative(relative)
            if relative.casefold() in seen or role not in {"rules", "ai", "presentation", "data"} \
                    or not isinstance(expected_size, int) or isinstance(expected_size, bool) or expected_size < 0 \
                    or not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
                raise _error(f"invalid package file record: {package_id}/{relative}")
            valid_role = (role == "rules" and relative.startswith("lua/") and not relative.startswith("lua/ai/") and relative.endswith(".lua")) \
                or (role == "ai" and relative.startswith("lua/ai/") and relative.endswith(".lua")) \
                or (role == "presentation" and relative.startswith("translation/") and relative.endswith(".lua")) \
                or (role == "data" and relative.startswith(("image/", "audio/", "data/")))
            if not valid_role or relative.casefold() == "manifest.json":
                raise _error(f"package path does not match role: {package_id}/{relative}")
            seen.add(relative.casefold())
            source = _require_file(directory / relative, f"package file {package_id}/{relative}")
            size, digest = _hash_file(source)
            if size != expected_size or digest != expected_hash:
                raise _error(f"package file hash or size mismatch: {package_id}/{relative}")
            if role != "data":
                namespaced = f"packages/{package_id}/{relative}"
                if namespaced not in declared_code:
                    raise _error(f"package Lua file is not declared by an extension: {namespaced}")
                lua_files.append({"path": namespaced, "role": role, "size": size, "sha256": digest})
        # Verify the sealed inventory covers every physical package file.
        listed = {entry["path"] for entry in manifest["files"]}
        for current, directories, filenames in os.walk(directory, followlinks=False):
            current_path = Path(current)
            for name in directories:
                if _unsafe(current_path / name):
                    raise _error(f"package contains a symlink: {package_id}/{_relative(current_path / name, directory)}")
            for name in filenames:
                path = current_path / name
                relative = _relative(path, directory)
                if _unsafe(path) or not path.is_file():
                    raise _error(f"package contains a non-regular path: {package_id}/{relative}")
                if relative != "manifest.json" and relative not in listed:
                    raise _error(f"unlisted package file: {package_id}/{relative}")
        # Keep the complete declared alias map, including optional targets absent
        # from this package's sealed payload; consumers use absence to avoid fallback.
        declared_relative = {path.split("/", 2)[2].casefold() for path in declared_code}
        if not declared_relative.issubset(seen):
            raise _error(f"package extension declarations are missing file records: {package_id}")
        records.append({"id": package_id, "version": manifest["version"],
                        "dependencies": dependencies, "assets": raw_assets,
                        "extensions": extensions, "lua_files": lua_files,
                        "root": directory, "files": manifest["files"]})
    by_id = {package["id"]: package for package in records}
    ordered: list[dict] = []
    emitted: set[str] = set()
    pending = list(records)
    while pending:
        ready = next((package for package in pending
                      if all(dependency in by_id and dependency in emitted
                             for dependency in package["dependencies"])), None)
        if ready is None:
            unresolved = ", ".join(package["id"] for package in pending)
            raise _error(f"package dependencies are missing or cyclic: {unresolved}")
        ordered.append(ready)
        emitted.add(ready["id"])
        pending.remove(ready)
    return ordered


def _copy_packages(packages: list[dict], destination: Path) -> None:
    for package in packages:
        package_target = destination / package["id"]
        package_target.mkdir(parents=True, exist_ok=True)
        source_manifest = package["root"] / "manifest.json"
        shutil.copyfile(source_manifest, package_target / "manifest.json")
        for entry in package["files"]:
            relative = entry["path"]
            source = _require_file(package["root"] / relative, f"package file {relative}")
            target = package_target.joinpath(*relative.split("/"))
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            if _hash_file(target) != (entry["size"], entry["sha256"]):
                raise _error(f"package changed during packaging: {package['id']}/{relative}")


def _declared_entries(config_path: Path, allow_empty: bool = False) -> list[str]:
    config = config_path.read_text(encoding="utf-8")
    block = re.search(r"extension_names\s*=\s*\{(.*?)\}", config, re.S)
    if block is None:
        raise _error("lua/config.lua has no extension_names declaration")
    entries = re.findall(r'"([^"\n]+)"', block.group(1))
    if not entries and not allow_empty:
        raise _error("lua/config.lua extension_names declaration is empty")
    return entries


def _declared_closure(asset_root: Path) -> list[str]:
    config = _require_file(asset_root / "lua/config.lua", "lua/config.lua")
    packages = _load_packages(asset_root / "packages")
    package_extension_names = {extension["name"] for package in packages
                               for extension in package["extensions"]}
    paths = list(CORE_BOOTSTRAP)
    seen = set(paths)
    for entry in _declared_entries(config, allow_empty=bool(packages)):
        fields = entry.split(";")
        script = fields[0]
        if not re.fullmatch(r"extensions/[A-Za-z0-9_.-]+\.lua", script):
            raise _error(f"invalid extension script declaration: {script}")
        # A package extension replaces its same-named legacy entry as a unit.
        if Path(script).stem in package_extension_names:
            continue
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
    for package in packages:
        manifest_relative = f"packages/{package['id']}/manifest.json"
        paths.append(manifest_relative)
        seen.add(manifest_relative)
        for entry in package["files"]:
            relative = f"packages/{package['id']}/{entry['path']}"
            if relative in seen:
                raise _error(f"duplicate declared package path: {relative}")
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
    try:
        bundle_payload = json.loads(bundle.read_bytes())
        runtime_content = bundle_payload["rules_content"]["runtime_content"]
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        raise _error("rules bundle runtime content descriptor is missing") from error
    ai = _collect_ai(asset_root)
    packages = _load_packages(asset_root / "packages")
    # Native client-rules exports omit server-only files. The Solo server still
    # needs every declared package AI payload in its verified content closure.
    for package in packages:
        ai.extend((f"packages/{package['id']}/{entry['path']}", package["root"] / entry["path"])
                  for entry in package["files"] if entry["role"] == "ai")
    bundle_rules_content = bundle_payload.get("rules_content", {})
    if packages and (bundle_rules_content.get("schema_version") != 3
                     or bundle_rules_content.get("profile") != "packages-v1"):
        raise _error("package catalogs require a fresh packages-v1 rules bundle; re-export the rules bundle")
    rules = _load_rules(bundle, asset_root)
    package_descriptors = [{key: package[key] for key in ("id", "version", "dependencies", "assets")}
                           for package in packages]
    if packages:
        rules_manifest = bundle_rules_content
        exported_runtime = runtime_content
        if exported_runtime.get("schema_version") != 3 \
                or exported_runtime.get("profile") != "packages-v1":
            raise _error("package catalogs require a fresh packages-v1 rules bundle; re-export the rules bundle")
        if exported_runtime.get("packages") != package_descriptors:
            raise _error("rules bundle package metadata differs from the local package catalog; re-export it")
        exported_extensions = exported_runtime.get("extensions", [])
        exported_by_name = {entry.get("name"): entry for entry in exported_extensions
                            if isinstance(entry, dict) and isinstance(entry.get("name"), str)}
        if len(exported_by_name) != len(exported_extensions):
            raise _error("rules bundle has invalid or duplicate extension declarations; re-export it")
        local_package_extensions = {extension["name"]: extension for package in packages
                                    for extension in package["extensions"]}
        exported_package_extensions = {entry["name"]: entry for entry in exported_extensions
                                       if isinstance(entry.get("script"), str)
                                       and entry["script"].startswith("packages/")}
        if exported_package_extensions != local_package_extensions:
            raise _error("rules bundle package extensions differ from local package catalog; re-export it")
        for package in packages:
            for extension in package["extensions"]:
                if exported_by_name.get(extension["name"]) != extension:
                    raise _error("rules bundle extension declaration differs from local package catalog; re-export it")
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
    copied_roots = [web_dist, *(asset_root / name for name in
        ("image", "audio", "lua", "extensions", "lang", "packages"))]
    if any(destination == root or root in destination.parents for root in copied_roots):
        raise _error("destination must be outside copied input trees")

    # Validate all output names and collisions before touching the destination.
    if packages and (web_dist / "packages").exists() and any((web_dist / "packages").iterdir()):
        raise _error("Web distribution already contains package paths")
    web_assets = web_dist / "assets"
    web_asset_names = set()
    if web_assets.exists():
        web_asset_names = {path.relative_to(web_assets).as_posix() for path in web_assets.rglob("*") if path.is_file()}
    image_root = asset_root / "image"
    audio_root = asset_root / "audio"
    media_files = [path.relative_to(image_root).as_posix() for path in image_root.rglob("*") if path.is_file()] \
        if image_root.is_dir() else []
    if web_asset_names.intersection(media_files):
        raise _error("image files collide with Vite assets")

    content = rules[:]
    content_by_path = {entry["path"]: entry for entry in content}
    for relative, path in ai:
        size, digest = _hash_file(path)
        entry = {"path": relative, "role": "ai", "size": size, "sha256": digest}
        previous = content_by_path.get(relative)
        if previous is not None and previous != entry:
            raise _error(f"AI content conflicts with rules bundle: {relative}")
        if previous is None:
            content.append(entry)
            content_by_path[relative] = entry
    if packages:
        runtime_v3 = {"schema_version": 3, "profile": "packages-v1",
                      "extensions": list(runtime_content.get("extensions", [])),
                      "packages": package_descriptors}
        manifest["content"] = {"schema_version": 3, "profile": "packages-v1",
                               "runtime_content": runtime_v3, "files": content}
    elif runtime_content.get("schema_version") == 3:
        if runtime_content.get("packages", []):
            raise _error("packages-v1 rules bundle requires its package roots under asset-root/packages")
        manifest["content"] = {"schema_version": 3, "profile": "packages-v1",
            "runtime_content": runtime_content, "files": content}
    else:
        # Existing runtime-content-v2 bundles remain supported during migration.
        manifest["content"] = {"schema_version": 2, "profile": "declared-v2",
                                "runtime_content": runtime_content, "files": content}

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".qsan-solo-", dir=str(destination.parent)) as temporary:
        staged = Path(temporary)
        _copy_tree(web_dist, staged)
        assets = staged / "assets"
        assets.mkdir(parents=True, exist_ok=True)
        if image_root.is_dir() and (not packages or any(path.is_file() for path in image_root.rglob("*"))):
            _copy_media(image_root, assets, "image", web_asset_names.copy())
        if audio_root.is_dir() and (not packages or any(path.is_file() for path in audio_root.rglob("*"))):
            _copy_media(audio_root, staged / "audio", "audio", set())
        if packages:
            _copy_packages(packages, staged / "packages")
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
