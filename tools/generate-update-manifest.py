#!/usr/bin/env python3
"""Build a local material-update catalog from already packaged ZIP files.

This tool only reads local package archives and atomically writes JSON. It has
no network or upload functionality. It deliberately rejects loose files rather
than guessing asset ownership or constructing package archives for the caller.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import tempfile
from urllib.parse import quote, urlsplit, urlunsplit
import zipfile
import zlib


MAX_CATALOG_BYTES = 4 * 1024 * 1024
MAX_CATALOG_ENTRIES = 1000
MAX_ARCHIVE_BYTES = 256 * 1024 * 1024
MAX_EXPANDED_BYTES = 512 * 1024 * 1024
MAX_MEMBER_BYTES = 128 * 1024 * 1024
MAX_ARCHIVE_ENTRIES = 50_000
MAX_COMPRESSION_RATIO = 200
MAX_ZIP_METADATA_BYTES = 16 * 1024 * 1024
MAX_PACKAGE_MANIFEST_BYTES = 16 * 1024 * 1024
CHUNK_BYTES = 256 * 1024

PACKAGE_ID_RE = re.compile(r"[a-z0-9][a-z0-9_-]{0,127}\Z")
PATH_RE = re.compile(r"[A-Za-z0-9_./+-]+\Z")
DIGEST_RE = re.compile(r"[0-9a-f]{64}\Z")
SEMVER_RE = re.compile(
    r"(?:0|[1-9][0-9]{0,8})\.(?:0|[1-9][0-9]{0,8})\.(?:0|[1-9][0-9]{0,8})"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?\Z"
)
ROLES = {"rules", "ai", "presentation", "data"}


class ManifestGenerationError(ValueError):
    """An input package or requested catalog setting is not safe to publish."""


def _safe_relative(value: object) -> str:
    if not isinstance(value, str) or not value or "\\" in value or "\x00" in value:
        raise ManifestGenerationError(f"invalid package-relative path: {value!r}")
    if len(value) > 4096 or not PATH_RE.fullmatch(value) or value.startswith("/"):
        raise ManifestGenerationError(f"unsafe or unsupported package path: {value!r}")
    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise ManifestGenerationError(f"unsafe package path traversal: {value!r}")
    return value


def _valid_version(value: object) -> bool:
    if not isinstance(value, str) or len(value) > 128:
        return False
    semantic = value[1:] if value.startswith("v") else value
    date_match = re.fullmatch(r"[0-9]{8}", semantic)
    if date_match:
        try:
            datetime.datetime.strptime(semantic, "%Y%m%d")
            return True
        except ValueError:
            return False
    match = SEMVER_RE.fullmatch(semantic)
    if not match:
        return False
    prerelease = match.group(1)
    if prerelease:
        for part in prerelease.split("."):
            if part.isdigit() and (len(part) > 9 or (len(part) > 1 and part.startswith("0"))):
                return False
    return True


def _validate_game_version(value: str) -> str:
    if not _valid_version(value):
        raise ManifestGenerationError(
            "game version must be a valid YYYYMMDD date or strict semantic version"
        )
    return value


def _validate_base_url(value: str) -> tuple[str, str]:
    if any(char.isspace() or ord(char) < 0x20 for char in value) \
            or re.search(r"%(?![0-9A-Fa-f]{2})", value):
        raise ManifestGenerationError("base URL contains whitespace, control characters, or malformed escapes")
    try:
        parts = urlsplit(value)
        port = parts.port
    except ValueError as exc:
        raise ManifestGenerationError(f"invalid HTTPS base URL: {exc}") from exc
    if (parts.scheme != "https" or not parts.hostname or parts.username is not None
            or parts.password is not None or parts.fragment or parts.query
            or port not in (None, 443) or "@" in parts.netloc):
        raise ManifestGenerationError(
            "base URL must be a public HTTPS URL with no credentials, query, fragment, or non-443 port"
        )
    path = parts.path
    if not path.endswith("/"):
        path += "/"
    base = urlunsplit(("https", parts.netloc, path, "", ""))
    return base, parts.netloc


def _base_url_for_file(base_url: str, filename: str) -> str:
    base, _ = _validate_base_url(base_url)
    return base + quote(filename, safe="-._~")


def _preflight_eocd(path: Path, archive_size: int) -> None:
    """Bound central-directory allocation before zipfile builds its index."""
    if archive_size < 22:
        raise ManifestGenerationError(f"not a valid ZIP archive: {path.name}")
    tail_size = min(archive_size, 22 + 65535)
    with path.open("rb") as stream:
        stream.seek(archive_size - tail_size)
        tail = stream.read(tail_size)
    marker = b"PK\x05\x06"
    pos = tail.rfind(marker)
    end = -1
    while pos >= 0:
        if pos + 22 <= len(tail):
            comment_size = struct.unpack_from("<H", tail, pos + 20)[0]
            if pos + 22 + comment_size == len(tail):
                end = pos
                break
        pos = tail.rfind(marker, 0, pos)
    if end < 0:
        raise ManifestGenerationError(f"ZIP end record is missing or has trailing data: {path.name}")

    _, disk, central_disk, disk_count, total_count, central_size, central_offset, _ = \
        struct.unpack_from("<4s4H2LH", tail, end)
    eocd_absolute = archive_size - tail_size + end
    if disk or central_disk or disk_count != total_count:
        raise ManifestGenerationError(f"multipart ZIP archives are unsupported: {path.name}")

    footer_offset = eocd_absolute
    if total_count == 0xFFFF or central_size == 0xFFFFFFFF or central_offset == 0xFFFFFFFF:
        locator_pos = end - 20
        if locator_pos < 0 or tail[locator_pos:locator_pos + 4] != b"PK\x06\x07":
            raise ManifestGenerationError(f"invalid ZIP64 locator: {path.name}")
        _, zip64_disk, zip64_offset, disk_total = struct.unpack_from("<4sIQI", tail, locator_pos)
        if zip64_disk != 0 or disk_total != 1:
            raise ManifestGenerationError(f"multipart ZIP64 archives are unsupported: {path.name}")
        with path.open("rb") as stream:
            stream.seek(zip64_offset)
            record = stream.read(56)
        if len(record) != 56 or record[:4] != b"PK\x06\x06":
            raise ManifestGenerationError(f"invalid ZIP64 end record: {path.name}")
        record_size, _made, _needed, z_disk, z_cd_disk, z_disk_count, z_count, z_cd_size, z_cd_offset = \
            struct.unpack_from("<Q2H2I4Q", record, 4)
        locator_absolute = archive_size - tail_size + locator_pos
        if (record_size < 44 or z_disk or z_cd_disk or z_disk_count != z_count
                or zip64_offset + 12 + record_size != locator_absolute):
            raise ManifestGenerationError(f"invalid ZIP64 size or multipart layout: {path.name}")
        footer_offset = zip64_offset
        total_count, central_size, central_offset = z_count, z_cd_size, z_cd_offset

    if total_count > MAX_ARCHIVE_ENTRIES:
        raise ManifestGenerationError(
            f"ZIP entry count exceeds {MAX_ARCHIVE_ENTRIES}: {path.name}"
        )
    if central_size > MAX_ZIP_METADATA_BYTES:
        raise ManifestGenerationError(
            f"ZIP central directory exceeds {MAX_ZIP_METADATA_BYTES} bytes: {path.name}"
        )
    if central_offset > footer_offset or central_size > footer_offset - central_offset:
        raise ManifestGenerationError(f"ZIP central directory is outside the archive: {path.name}")


def _package_prefix(infos: list[zipfile.ZipInfo], archive_name: str) -> str:
    files = [info.filename for info in infos if not info.is_dir()]
    manifests = [name for name in files if name == "manifest.json" or name.endswith("/manifest.json")]
    if len(manifests) != 1:
        raise ManifestGenerationError(
            f"{archive_name} must contain one package-root manifest.json"
        )
    manifest_name = manifests[0]
    if manifest_name == "manifest.json":
        return ""
    prefix, basename = manifest_name.rsplit("/", 1)
    if basename != "manifest.json" or "/" in prefix:
        raise ManifestGenerationError(
            f"{archive_name} must have manifest.json at its root or inside one wrapper directory"
        )
    for name in (info.filename for info in infos):
        if name.rstrip("/") == prefix:
            continue
        if not name.startswith(prefix + "/"):
            raise ManifestGenerationError(
                f"{archive_name} has ZIP members outside its single package wrapper directory"
            )
    return prefix + "/"


def _validate_extensions(manifest: dict) -> dict[str, str]:
    extensions = manifest.get("extensions")
    if not isinstance(extensions, list):
        raise ManifestGenerationError("package manifest extensions must be an array")
    declared: dict[str, str] = {}
    names: set[str] = set()
    modules: set[str] = set()

    def claim(path: str, role: str) -> None:
        key = path.casefold()
        if key in declared:
            raise ManifestGenerationError(f"duplicate or case-colliding Lua declaration: {path}")
        if role == "rules":
            module = "lua." + path[len("lua/"):-len(".lua")].replace("/", ".").casefold()
            if module in modules:
                raise ManifestGenerationError(f"Lua module path collision: {path}")
            modules.add(module)
        declared[key] = role

    for extension in extensions:
        if not isinstance(extension, dict):
            raise ManifestGenerationError("package extension entries must be objects")
        name = extension.get("name")
        script = extension.get("script")
        if not isinstance(name, str) or not name or name.casefold() in names:
            raise ManifestGenerationError("package extension names must be unique non-empty strings")
        names.add(name.casefold())
        if not isinstance(script, str) or not script.startswith("lua/") \
                or script.startswith("lua/ai/") or not script.endswith(".lua"):
            raise ManifestGenerationError("each extension needs a lua/*.lua rules script")
        claim(_safe_relative(script), "rules")
        for field in ("dependencies", "libs", "lang", "ai"):
            values = extension.get(field)
            if not isinstance(values, list) or any(not isinstance(value, str) or not value for value in values):
                raise ManifestGenerationError(f"extension {field} must be an array of non-empty strings")
            if field == "dependencies":
                continue
            prefix, role = {
                "libs": ("lua/", "rules"),
                "lang": ("translation/", "presentation"),
                "ai": ("lua/ai/", "ai"),
            }[field]
            for value in values:
                path = _safe_relative(value)
                if not path.startswith(prefix) or not path.endswith(".lua") \
                        or (field == "libs" and path.startswith("lua/ai/")):
                    raise ManifestGenerationError(f"invalid extension {field} path: {path}")
                claim(path, role)
    return declared


def _validate_manifest(manifest_bytes: bytes, archive_name: str) -> tuple[dict, dict[str, dict], dict[str, str]]:
    if len(manifest_bytes) > MAX_PACKAGE_MANIFEST_BYTES:
        raise ManifestGenerationError(f"package manifest exceeds {MAX_PACKAGE_MANIFEST_BYTES} bytes: {archive_name}")
    try:
        manifest = json.loads(manifest_bytes.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ManifestGenerationError(f"invalid UTF-8 package manifest in {archive_name}: {exc}") from exc
    if not isinstance(manifest, dict):
        raise ManifestGenerationError(f"package manifest must be an object: {archive_name}")
    package_id = manifest.get("id")
    version = manifest.get("version")
    if (manifest.get("schema_version") != 1 or isinstance(manifest.get("schema_version"), bool)
            or manifest.get("engine_api") != 1 or isinstance(manifest.get("engine_api"), bool)
            or not isinstance(package_id, str) or not PACKAGE_ID_RE.fullmatch(package_id)
            or package_id == "core" or not _valid_version(version)):
        raise ManifestGenerationError(
            f"package manifest needs schema_version 1, engine_api 1, a non-core ID, and a supported version: {archive_name}"
        )
    for field, maximum in (("name", 180), ("notes", 65536)):
        if field in manifest and (not isinstance(manifest[field], str) or len(manifest[field]) > maximum):
            raise ManifestGenerationError(f"package {field} must be text no longer than {maximum} characters")

    dependencies = manifest.get("dependencies")
    if not isinstance(dependencies, list) or any(
            not isinstance(value, str) or not value or not PACKAGE_ID_RE.fullmatch(value)
            for value in dependencies):
        raise ManifestGenerationError("package dependencies must be package IDs in an array")
    if len({value.casefold() for value in dependencies}) != len(dependencies):
        raise ManifestGenerationError("package dependencies contain duplicate IDs")

    declared_lua = _validate_extensions(manifest)
    assets = manifest.get("assets")
    if not isinstance(assets, dict):
        raise ManifestGenerationError("package manifest assets must be an object")
    asset_keys: set[str] = set()
    for legacy, target in assets.items():
        _safe_relative(legacy)
        _safe_relative(target)
        if not ((legacy.startswith("image/") and target.startswith("image/"))
                or (legacy.startswith("audio/") and target.startswith("audio/"))):
            raise ManifestGenerationError(f"invalid package asset mapping: {legacy}")
        folded = legacy.casefold()
        if folded in asset_keys:
            raise ManifestGenerationError(f"duplicate asset mapping: {legacy}")
        asset_keys.add(folded)

    files = manifest.get("files")
    if not isinstance(files, list):
        raise ManifestGenerationError("package manifest files must be an array")
    inventory: dict[str, dict] = {}
    folded_inventory: dict[str, str] = {}
    for entry in files:
        if not isinstance(entry, dict):
            raise ManifestGenerationError("package file inventory entries must be objects")
        path = _safe_relative(entry.get("path"))
        role = entry.get("role")
        size = entry.get("size")
        digest = entry.get("sha256")
        if role not in ROLES or not isinstance(size, int) or isinstance(size, bool) or size < 0 \
                or not isinstance(digest, str) or not DIGEST_RE.fullmatch(digest):
            raise ManifestGenerationError(f"invalid package file inventory entry: {path}")
        key = path.casefold()
        if key in folded_inventory:
            raise ManifestGenerationError(f"duplicate or case-colliding inventory path: {path}")
        folded_inventory[key] = path
        role_matches = (
            role == "rules" and path.startswith("lua/") and not path.startswith("lua/ai/") and path.endswith(".lua")
        ) or (
            role == "ai" and path.startswith("lua/ai/") and path.endswith(".lua")
        ) or (
            role == "presentation" and path.startswith("translation/") and path.endswith(".lua")
        ) or (
            role == "data" and (path.startswith("image/") or path.startswith("audio/") or path.startswith("data/"))
        )
        if not role_matches:
            raise ManifestGenerationError(f"package file role does not match path: {path}")
        if role != "data" and key not in declared_lua:
            raise ManifestGenerationError(f"Lua file is not declared by an extension: {path}")
        inventory[path] = {"size": size, "sha256": digest, "role": role}

    for key, role in declared_lua.items():
        path = folded_inventory.get(key)
        if path is None or inventory[path]["role"] != role:
            raise ManifestGenerationError(f"declared Lua file is missing or has wrong role: {path or key}")
        # Exact spelling is checked by requiring the declared string as the key below.
        if path != next(declared_path for declared_path in _declared_paths(manifest)
                        if declared_path.casefold() == key):
            raise ManifestGenerationError(f"declared Lua path casing differs from inventory: {path}")

    for legacy, target in assets.items():
        key = target.casefold()
        path = folded_inventory.get(key)
        if path is not None and (path != target or inventory[path]["role"] != "data"):
            raise ManifestGenerationError(f"asset mapping differs from package file inventory: {target}")
    return manifest, inventory, declared_lua


def _declared_paths(manifest: dict) -> list[str]:
    result: list[str] = []
    for extension in manifest.get("extensions", []):
        result.append(extension["script"])
        for field in ("libs", "lang", "ai"):
            result.extend(extension[field])
    return result


def _inspect_package(path: Path) -> tuple[dict, int, str]:
    try:
        archive_size = path.stat().st_size
    except OSError as exc:
        raise ManifestGenerationError(f"cannot stat package ZIP {path.name}: {exc}") from exc
    if archive_size <= 0 or archive_size > MAX_ARCHIVE_BYTES:
        raise ManifestGenerationError(
            f"package ZIP must be 1..{MAX_ARCHIVE_BYTES} bytes: {path.name}"
        )
    _preflight_eocd(path, archive_size)

    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            before = os.fstat(stream.fileno())
            if before.st_size != archive_size:
                raise ManifestGenerationError(f"package ZIP changed while being checked: {path.name}")
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
            after_hash = os.fstat(stream.fileno())
            if (before.st_size, before.st_mtime_ns, before.st_ino) != (
                    after_hash.st_size, after_hash.st_mtime_ns, after_hash.st_ino):
                raise ManifestGenerationError(f"package ZIP changed while being hashed: {path.name}")
            stream.seek(0)
            with zipfile.ZipFile(stream, "r") as archive:
                infos = archive.infolist()
                if not infos or len(infos) > MAX_ARCHIVE_ENTRIES:
                    raise ManifestGenerationError(
                        f"ZIP must contain 1..{MAX_ARCHIVE_ENTRIES} entries: {path.name}"
                    )
                # Reuse the same bounded structural checks after zipfile parses its index.
                _validate_zip_infos(path.name, infos)
                prefix = _package_prefix(infos, path.name)
                manifest_infos = [info for info in infos
                                  if not info.is_dir() and info.filename == prefix + "manifest.json"]
                if len(manifest_infos) != 1:
                    raise ManifestGenerationError(f"missing package manifest in {path.name}")
                manifest_info = manifest_infos[0]
                if manifest_info.file_size > MAX_PACKAGE_MANIFEST_BYTES:
                    raise ManifestGenerationError(
                        f"package manifest exceeds {MAX_PACKAGE_MANIFEST_BYTES} bytes: {path.name}"
                    )
                manifest_bytes = _read_member_bounded(archive, manifest_info, MAX_PACKAGE_MANIFEST_BYTES)
                manifest, inventory, _declared = _validate_manifest(manifest_bytes, path.name)

                actual_files: set[str] = set()
                for info in infos:
                    if info.is_dir():
                        continue
                    if not info.filename.startswith(prefix):
                        raise ManifestGenerationError(f"ZIP member is outside package root: {info.filename}")
                    relative = info.filename[len(prefix):]
                    if relative == "manifest.json":
                        continue
                    if not relative:
                        raise ManifestGenerationError(f"empty package member name in {path.name}")
                    if relative in actual_files:
                        raise ManifestGenerationError(f"duplicate package payload: {relative}")
                    actual_files.add(relative)
                    expected = inventory.get(relative)
                    if expected is None:
                        raise ManifestGenerationError(f"unlisted package file in {path.name}: {relative}")
                    if info.file_size != expected["size"]:
                        raise ManifestGenerationError(f"package file size mismatch: {relative}")
                    member_digest = hashlib.sha256()
                    _read_member_hashed(archive, info, member_digest, MAX_MEMBER_BYTES)
                    if member_digest.hexdigest() != expected["sha256"]:
                        raise ManifestGenerationError(f"package file SHA-256 mismatch: {relative}")
                if actual_files != set(inventory):
                    missing = sorted(set(inventory) - actual_files)
                    raise ManifestGenerationError(
                        "package file inventory does not match archive members"
                        + (f": missing {missing[0]}" if missing else "")
                    )
            after = os.fstat(stream.fileno())
            if (before.st_size, before.st_mtime_ns, before.st_ino) != (
                    after.st_size, after.st_mtime_ns, after.st_ino):
                raise ManifestGenerationError(f"package ZIP changed while being checked: {path.name}")
    except ManifestGenerationError:
        raise
    except (OSError, zipfile.BadZipFile, zipfile.LargeZipFile, RuntimeError, EOFError, zlib.error) as exc:
        raise ManifestGenerationError(f"invalid package ZIP {path.name}: {exc}") from exc
    return manifest, archive_size, digest.hexdigest()


def _validate_zip_infos(archive_name: str, infos: list[zipfile.ZipInfo]) -> None:
    total_expanded = 0
    folded_paths: dict[str, str] = {}
    components: dict[str, str] = {}
    file_paths: set[str] = set()
    directory_paths: set[str] = set()
    for info in infos:
        name = info.filename
        if "\x00" in getattr(info, "orig_filename", name):
            raise ManifestGenerationError(f"ZIP member contains a NUL byte: {name!r}")
        directory = info.is_dir()
        relative = name[:-1] if directory else name
        try:
            _safe_relative(relative)
        except ManifestGenerationError as exc:
            raise ManifestGenerationError(f"unsafe ZIP member in {archive_name}: {exc}") from exc
        if info.flag_bits & (0x1 | 0x20 | 0x40):
            raise ManifestGenerationError(f"encrypted or multipart ZIP member: {name}")
        if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            raise ManifestGenerationError(f"unsupported ZIP compression method: {name}")
        mode = (info.external_attr >> 16) & 0xFFFF if info.create_system == 3 else 0
        file_type = stat.S_IFMT(mode)
        if file_type not in (0, stat.S_IFREG, stat.S_IFDIR):
            raise ManifestGenerationError(f"symlink or special ZIP member is not allowed: {name}")
        if (directory and file_type == stat.S_IFREG) or (not directory and file_type == stat.S_IFDIR):
            raise ManifestGenerationError(f"ZIP file/directory type mismatch: {name}")
        if info.file_size < 0 or info.compress_size < 0:
            raise ManifestGenerationError(f"invalid ZIP member sizes: {name}")
        if info.file_size > MAX_MEMBER_BYTES:
            raise ManifestGenerationError(f"ZIP member exceeds {MAX_MEMBER_BYTES} bytes: {name}")
        if info.file_size and (not info.compress_size
                               or info.file_size > info.compress_size * MAX_COMPRESSION_RATIO):
            raise ManifestGenerationError(f"ZIP compression ratio exceeds {MAX_COMPRESSION_RATIO}: {name}")
        if directory and info.file_size:
            raise ManifestGenerationError(f"ZIP directory member has data: {name}")
        total_expanded += info.file_size
        if total_expanded > MAX_EXPANDED_BYTES:
            raise ManifestGenerationError(f"ZIP expanded size exceeds {MAX_EXPANDED_BYTES} bytes")
        folded = relative.casefold()
        if folded in folded_paths:
            raise ManifestGenerationError(f"duplicate or case-colliding ZIP path: {name}")
        folded_paths[folded] = relative
        parts = relative.split("/")
        prefix = ""
        for index, part in enumerate(parts):
            prefix = part if not prefix else prefix + "/" + part
            key = prefix.casefold()
            previous = components.get(key)
            if previous is not None and previous != prefix:
                raise ManifestGenerationError(f"case-colliding ZIP path component: {name}")
            components[key] = prefix
            is_dir = index + 1 < len(parts) or directory
            if (is_dir and key in file_paths) or (not is_dir and key in directory_paths):
                raise ManifestGenerationError(f"ZIP file/directory path collision: {name}")
            (directory_paths if is_dir else file_paths).add(key)


def _read_member_bounded(archive: zipfile.ZipFile, info: zipfile.ZipInfo, limit: int) -> bytes:
    result = bytearray()
    with archive.open(info, "r") as member:
        while True:
            block = member.read(min(CHUNK_BYTES, limit + 1 - len(result)))
            if not block:
                break
            result.extend(block)
            if len(result) > limit:
                raise ManifestGenerationError(f"ZIP member exceeds {limit} bytes: {info.filename}")
    if len(result) != info.file_size:
        raise ManifestGenerationError(f"ZIP member expanded-size mismatch: {info.filename}")
    return bytes(result)


def _read_member_hashed(archive: zipfile.ZipFile, info: zipfile.ZipInfo,
                        digest, limit: int) -> int:
    total = 0
    with archive.open(info, "r") as member:
        for block in iter(lambda: member.read(CHUNK_BYTES), b""):
            total += len(block)
            if total > limit:
                raise ManifestGenerationError(f"ZIP member exceeds {limit} bytes: {info.filename}")
            digest.update(block)
    if total != info.file_size:
        raise ManifestGenerationError(f"ZIP member expanded-size mismatch: {info.filename}")
    return total


def _package_zips(input_dir: Path) -> list[Path]:
    if input_dir.is_symlink() or not input_dir.is_dir():
        raise ManifestGenerationError("input directory must be an existing real directory, not a symlink")
    entries = sorted(input_dir.iterdir(), key=lambda path: path.name.casefold())
    if not entries:
        raise ManifestGenerationError("input directory contains no package ZIP files")
    zips: list[Path] = []
    folded_names: set[str] = set()
    for path in entries:
        if path.is_symlink():
            raise ManifestGenerationError(f"input directory contains a symlink: {path.name}")
        if not path.is_file() or path.suffix.casefold() != ".zip":
            raise ManifestGenerationError(
                f"input directory contains {path.name!r}; provide only package ZIPs. "
                "Use the existing package tools to declare and package assets; this generator will not guess ownership."
            )
        folded = path.name.casefold()
        if folded in folded_names:
            raise ManifestGenerationError(f"ZIP filenames collide case-insensitively: {path.name}")
        folded_names.add(folded)
        zips.append(path)
    if not zips:
        raise ManifestGenerationError("input directory contains no package ZIP files")
    if len(zips) > MAX_CATALOG_ENTRIES:
        raise ManifestGenerationError(f"catalog exceeds {MAX_CATALOG_ENTRIES} package entries")
    return zips


def generate(input_dir: Path, output: Path, base_url: str, game_version: str) -> dict:
    """Validate local ZIP packages and atomically write one update catalog."""
    _validate_game_version(game_version)
    _validate_base_url(base_url)
    input_dir = Path(input_dir).absolute()
    output = Path(output).absolute()
    if output.is_symlink():
        raise ManifestGenerationError("output path must not be a symlink")
    if not output.parent.is_dir():
        raise ManifestGenerationError("output parent directory must already exist")

    packages = []
    ids: set[str] = set()
    for path in _package_zips(input_dir):
        manifest, size, sha256 = _inspect_package(path)
        package_id = manifest["id"]
        if package_id in ids:
            raise ManifestGenerationError(f"duplicate package ID in input directory: {package_id}")
        ids.add(package_id)
        entry = {
            "id": package_id,
            "version": manifest["version"],
            "game_version": game_version,
            "url": _base_url_for_file(base_url, path.name),
            "size": size,
            "sha256": sha256,
        }
        if "name" in manifest:
            entry["name"] = manifest["name"]
        if "notes" in manifest:
            entry["notes"] = manifest["notes"]
        packages.append(entry)

    document = {"schema_version": 1, "packages": packages}
    output_bytes = (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if len(output_bytes) > MAX_CATALOG_BYTES:
        raise ManifestGenerationError(f"generated catalog exceeds {MAX_CATALOG_BYTES} bytes")

    temp_name = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", prefix=output.name + ".", suffix=".tmp",
                                         dir=output.parent, delete=False) as temporary:
            temp_name = temporary.name
            temporary.write(output_bytes)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temp_name, output)
    except OSError as exc:
        if temp_name:
            try:
                os.unlink(temp_name)
            except OSError:
                pass
        raise ManifestGenerationError(f"cannot atomically write output catalog: {exc}") from exc
    return document


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", required=True, type=Path,
                        help="flat local directory containing only sealed package ZIP files")
    parser.add_argument("--output", required=True, type=Path,
                        help="explicit output JSON file; parent directory must already exist")
    parser.add_argument("--base-url", required=True,
                        help="public HTTPS directory URL ending at the package objects (no credentials/query/fragment)")
    parser.add_argument("--game-version", required=True,
                        help="exact compatible game version, as YYYYMMDD or strict semantic version")
    arguments = parser.parse_args(argv)
    try:
        document = generate(arguments.input_dir, arguments.output,
                            arguments.base_url, arguments.game_version)
    except (ManifestGenerationError, OSError) as exc:
        parser.error(str(exc))
    print(f"Wrote {len(document['packages'])} package entr{'y' if len(document['packages']) == 1 else 'ies'} to {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
