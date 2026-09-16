#!/usr/bin/env python3
"""Inspect and migrate legacy Lua extensions into explicit packages.

The tool parses declarations as data. It never evaluates Lua or infers asset
ownership from filenames.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat

PACKAGE_ID = re.compile(r"[a-z0-9][a-z0-9_-]*\Z")
ASSET_PREFIXES = ("image/", "audio/")


class PackageError(ValueError):
    pass


def safe_rel(value: object) -> str:
    if not isinstance(value, str) or not value or "\\" in value or "\x00" in value:
        raise PackageError(f"invalid relative path: {value!r}")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in value.split("/")):
        raise PackageError(f"unsafe relative path: {value!r}")
    if not re.fullmatch(r"[A-Za-z0-9_./-]+", value):
        raise PackageError(f"unsupported path characters: {value!r}")
    return path.as_posix()


def check_real_path(path: Path) -> None:
    current = path
    while True:
        try:
            mode = current.lstat().st_mode
        except FileNotFoundError:
            current = current.parent
            if current == current.parent:
                return
            continue
        attrs = getattr(current.lstat(), "st_file_attributes", 0)
        if stat.S_ISLNK(mode) or attrs & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
            raise PackageError(f"symlink/reparse path is not allowed: {current}")
        if current == current.parent:
            return
        current = current.parent


def contained_file(root: Path, relative: str) -> Path:
    relative = safe_rel(relative)
    path = root.joinpath(*relative.split("/"))
    check_real_path(path)
    if not path.is_file():
        raise PackageError(f"missing declared file: {relative}")
    return path


def digest(path: Path) -> tuple[int, str]:
    h = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(chunk)
            h.update(chunk)
    return size, h.hexdigest()


def load_json(path: Path) -> dict:
    check_real_path(path)
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise PackageError(f"cannot parse JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise PackageError(f"expected a JSON object: {path}")
    return value


def declaration(root: Path) -> list[str]:
    config = root / "lua/config.lua"
    check_real_path(config)
    if config.exists():
        try:
            text = config.read_text(encoding="utf-8")
        except OSError as exc:
            raise PackageError(f"cannot read lua/config.lua: {exc}") from exc
        # Accept only literal quoted entries in the declared table; never run Lua.
        block = re.search(r"extension_names\s*=\s*\{(.*?)\n\s*\}", text, re.S)
        if not block:
            raise PackageError("could not find a literal extension_names table")
        body = re.sub(r"--[^\n]*", "", block.group(1))
        entries = re.findall(r'"([^"\n]+)"', body)
        if not entries:
            raise PackageError("extension_names contains no literal entries")
        return entries
    descriptor_path = root / "runtime-content.json"
    check_real_path(descriptor_path)
    if descriptor_path.exists():
        descriptor = load_json(descriptor_path)
        runtime = descriptor.get("runtime_content", descriptor)
        extensions = runtime.get("extensions") if isinstance(runtime, dict) else None
        if not isinstance(extensions, list) or not extensions:
            raise PackageError("runtime-content.json has no literal extensions array")
        entries = []
        for extension in extensions:
            if not isinstance(extension, dict) or not isinstance(extension.get("script"), str):
                raise PackageError("runtime-content.json has an invalid extension entry")
            fields = [extension["script"]]
            for key in ("libs", "lang", "ai"):
                values = extension.get(key, [])
                if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
                    raise PackageError(f"runtime-content.json has invalid {key} paths")
                if values:
                    fields.append(f"{key}={','.join(values)}")
            entries.append(";".join(fields))
        return entries
    raise PackageError("neither lua/config.lua nor runtime-content.json is available")


def declared_paths(entries: list[str]) -> list[str]:
    paths: list[str] = []
    for raw in entries:
        fields = raw.split(";")
        script = safe_rel(fields[0])
        if not script.startswith("extensions/") or not script.endswith(".lua"):
            raise PackageError(f"unsupported extension script declaration: {script}")
        paths.append(script)
        for field in fields[1:]:
            key, sep, values = field.partition("=")
            if not sep or key not in {"libs", "lang", "ai"}:
                raise PackageError(f"unsupported declaration field: {field}")
            if key == "ai" and values in ("", "true"):
                # Legacy implicit AI is unresolved; never expand it by filename guess.
                continue
            for value in values.split(","):
                value = safe_rel(value)
                prefix = {"libs": "lua/", "lang": "lang/", "ai": "lua/ai/"}[key]
                if not value.startswith(prefix) or not value.endswith(".lua"):
                    raise PackageError(f"invalid {key} path: {value}")
                paths.append(value)
    return paths


def inventory(root: Path) -> dict:
    entries = declaration(root)
    paths = declared_paths(entries)
    unresolved = []
    missing = []
    for relative in paths:
        candidate = root.joinpath(*relative.split("/"))
        check_real_path(candidate)
        if not candidate.exists():
            missing.append(relative)
        else:
            contained_file(root, relative)
    for area in ("image", "audio"):
        folder = root / area
        check_real_path(folder)
        if folder.is_dir():
            for path in sorted(folder.rglob("*"), key=lambda item: item.as_posix().casefold()):
                check_real_path(path)
                if path.is_file():
                    unresolved.append({"source": path.relative_to(root).as_posix(),
                                       "reason": "asset ownership must be declared in mapping"})
    runtime = None
    descriptor = root / "runtime-content.json"
    check_real_path(descriptor)
    if descriptor.exists():
        runtime = load_json(descriptor)
    unresolved_ai = [entry for entry in entries if re.search(r"(?:^|;)ai=(?:true)?(?:;|$)", entry)]
    return {"declaration_order": entries, "runtime_content": runtime,
            "unresolved_ai_ownership": unresolved_ai,
            "declared_files": list(dict.fromkeys(paths)),
            "missing_files": sorted(set(missing)), "unresolved_assets": unresolved}


def validate_manifest(manifest: dict) -> None:
    if manifest.get("schema_version") != 1 or not isinstance(manifest.get("id"), str) \
            or not PACKAGE_ID.fullmatch(manifest["id"]):
        raise PackageError("manifest needs schema_version 1 and a lowercase package id")
    if not isinstance(manifest.get("version"), str) or not manifest["version"].strip():
        raise PackageError("manifest version must be a non-empty string")
    if manifest.get("engine_api") != 1:
        raise PackageError("engine_api must be 1")
    if not isinstance(manifest.get("dependencies"), list) or not all(
            isinstance(item, str) and PACKAGE_ID.fullmatch(item) for item in manifest["dependencies"]):
        raise PackageError("dependencies must be package ids")
    extensions = manifest.get("extensions")
    if not isinstance(extensions, list):
        raise PackageError("extensions must be an ordered list")
    folded: set[str] = set()
    extension_names: set[str] = set()
    for extension in extensions:
        if not isinstance(extension, dict) or not isinstance(extension.get("name"), str):
            raise PackageError("each extension needs a name")
        if not extension["name"] or extension["name"] in extension_names:
            raise PackageError("extension names must be non-empty and unique")
        extension_names.add(extension["name"])
        for key, prefix in (("script", "lua/"),):
            rel = safe_rel(extension.get(key))
            if not rel.startswith(prefix) or rel.startswith("lua/ai/") or not rel.endswith(".lua"):
                raise PackageError(f"invalid extension {key}: {rel}")
        for field, prefix in (("libs", "lua/"), ("lang", "translation/"), ("ai", "lua/ai/")):
            values = extension.get(field)
            if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
                raise PackageError(f"extension {field} must be a string array")
            for value in values:
                rel = safe_rel(value)
                if not rel.startswith(prefix) or (field == "libs" and rel.startswith("lua/ai/")) \
                        or not rel.endswith(".lua"):
                    raise PackageError(f"invalid {field} path: {rel}")
                key = rel.casefold()
                if key in folded:
                    raise PackageError(f"case-insensitive path collision: {rel}")
                folded.add(key)
        if not isinstance(extension.get("dependencies"), list) or not all(
                isinstance(value, str) for value in extension["dependencies"]):
            raise PackageError("extension dependencies must be a string array")
    assets = manifest.get("assets")
    if not isinstance(assets, dict):
        raise PackageError("assets must map explicit legacy paths to package-relative paths")
    for source, target in assets.items():
        src, dst = safe_rel(source), safe_rel(target)
        if not src.startswith(ASSET_PREFIXES) or not dst.startswith(ASSET_PREFIXES) \
                or src.split("/", 1)[0] != dst.split("/", 1)[0]:
            raise PackageError("asset mappings must stay under image/ or audio/")
        key = dst.casefold()
        if key in folded:
            raise PackageError(f"case-insensitive path collision: {dst}")
        folded.add(key)


def seal(root: Path, manifest: dict) -> dict:
    validate_manifest(manifest)
    files: dict[str, str] = {}
    claims: list[tuple[str, str]] = []
    for extension in manifest["extensions"]:
        claims.append((extension["script"], "rules"))
        claims.extend((item, "rules") for item in extension["libs"])
        claims.extend((item, "presentation") for item in extension["lang"])
        claims.extend((item, "ai") for item in extension["ai"])
    claims.extend((target, "data") for target in manifest["assets"].values())
    records = []
    for relative, role in claims:
        path = contained_file(root, relative)
        folded = relative.casefold()
        if folded in files:
            raise PackageError(f"duplicate/case-colliding package path: {relative}")
        files[folded] = relative
        size, sha = digest(path)
        records.append({"path": relative, "role": role, "size": size, "sha256": sha})
    return {**manifest, "files": records}


def migrate(source: Path, mapping_path: Path, destination: Path) -> None:
    check_real_path(source)
    source = source.resolve(strict=True)
    mapping = load_json(mapping_path)
    manifest = mapping.get("manifest")
    if not isinstance(manifest, dict):
        raise PackageError("mapping JSON must contain a manifest object")
    validate_manifest(manifest)
    # Explicit source remaps target manifest paths; otherwise paths remain unchanged.
    mapping_files = mapping.get("files", {})
    if not isinstance(mapping_files, dict):
        raise PackageError("mapping files must be an explicit source-to-target object")
    source_by_target: dict[str, str] = {}
    for old, new in mapping_files.items():
        old, new = safe_rel(old), safe_rel(new)
        if new in source_by_target:
            raise PackageError(f"duplicate mapping destination: {new}")
        source_by_target[new] = old
    declared_targets: list[str] = []
    for extension in manifest["extensions"]:
        declared_targets.extend([extension["script"], *extension["libs"], *extension["lang"], *extension["ai"]])
    if any(target not in declared_targets for target in source_by_target):
        raise PackageError("files mapping contains a target unused by the manifest")
    pairs = [(source_by_target.get(target, target), target) for target in declared_targets]
    pairs.extend((safe_rel(legacy), safe_rel(target)) for legacy, target in manifest["assets"].items())
    ownership = mapping.get("asset_owners", {})
    if not isinstance(ownership, dict):
        raise PackageError("asset_owners must explicitly map every legacy asset to an extension name")
    for legacy in manifest["assets"]:
        owner = ownership.get(legacy)
        valid_extension_owner = any(ext["name"] == owner for ext in manifest["extensions"])
        valid_native_owner = not manifest["extensions"] and owner == manifest["id"]
        if not isinstance(owner, str) or not (valid_extension_owner or valid_native_owner):
            raise PackageError(f"asset ownership is missing or invalid for {legacy}")
    for legacy in ownership:
        if legacy not in manifest["assets"]:
            raise PackageError(f"asset owner supplied for unmapped asset: {legacy}")
    seen: set[str] = set()
    destination = destination.absolute()
    check_real_path(destination)
    if destination == source or source in destination.parents or destination in source.parents:
        raise PackageError("destination must be separate from source tree")
    if destination.exists() and (not destination.is_dir() or any(destination.iterdir())):
        raise PackageError("destination must be new or empty")
    for old, new in pairs:
        if new.casefold() in {"manifest.json", "package.json", "package.lock.json"}:
            raise PackageError(f"reserved package metadata path: {new}")
        if new.casefold() in seen:
            raise PackageError(f"duplicate/case-insensitive target: {new}")
        seen.add(new.casefold())
        contained_file(source, old)
    destination.mkdir(parents=True, exist_ok=True)
    try:
        for old, new in pairs:
            target = destination.joinpath(*new.split("/"))
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                raise PackageError(f"refusing to overwrite: {target}")
            shutil.copyfile(source.joinpath(*old.split("/")), target)
        sealed = seal(destination, manifest)
        (destination / "manifest.json").write_text(json.dumps(sealed, ensure_ascii=False, indent=2) + "\n",
                                                   encoding="utf-8")
    except Exception:
        # Preserve any partial output for inspection; never remove caller data.
        raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    inspect = sub.add_parser("inspect", help="list legacy declarations and unresolved assets")
    inspect.add_argument("source", type=Path)
    move = sub.add_parser("migrate", help="copy explicit mapping to a new package directory")
    move.add_argument("source", type=Path)
    move.add_argument("mapping", type=Path)
    move.add_argument("destination", type=Path)
    sealing = sub.add_parser("seal", help="recompute a package manifest file inventory")
    sealing.add_argument("package", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.operation == "inspect":
            check_real_path(args.source)
            print(json.dumps(inventory(args.source.resolve(strict=True)), ensure_ascii=False, indent=2))
        elif args.operation == "migrate":
            migrate(args.source, args.mapping, args.destination)
        else:
            check_real_path(args.package)
            path = args.package / "manifest.json"
            manifest = load_json(path)
            manifest.pop("files", None)
            sealed = seal(args.package, manifest)
            path.write_text(json.dumps(sealed, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    except (OSError, PackageError, UnicodeError) as exc:
        parser.exit(1, f"package tool: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
