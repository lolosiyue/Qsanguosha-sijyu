#!/usr/bin/env python3
"""Stage a candidate Excel portable package without overwriting a destination."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import shutil
import tempfile
import zipfile
from pathlib import Path

EXPECTED_BINARIES = {
    "modern": ("QSanguoshaExcelBridge.exe", "QSanguoshaExcelServer.exe"),
    "legacy": ("QSanguoshaExcelBridge.exe", "QSanguoshaXPServer.exe"),
}
REQUIRED_TREES = {"lua", "extensions", "lang", "image", "audio"}


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def validate_payload(path: Path, machine: int | None = None) -> None:
    """Require a non-empty PE payload with the expected COFF machine."""
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"missing or empty package input: {path}")
    if path.suffix.lower() in (".exe", ".dll"):
        with path.open("rb") as stream:
            header = stream.read()
        if len(header) < 64 or header[:2] != b"MZ":
            raise ValueError(f"unverified PE payload (missing MZ header): {path}")
        pe_offset = struct.unpack_from("<I", header, 0x3c)[0]
        if pe_offset + 6 > len(header) or header[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise ValueError(f"unverified PE payload (invalid PE header): {path}")
        actual_machine = struct.unpack_from("<H", header, pe_offset + 4)[0]
        if machine is not None and actual_machine != machine:
            raise ValueError(f"wrong PE machine 0x{actual_machine:04x}, expected 0x{machine:04x}: {path}")


def tree_digest(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(p for p in root.rglob("*") if p.is_file() and ".git" not in p.parts):
        relative = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(relative + b"\0" + bytes.fromhex(digest_file(path)) + b"\0")
    return digest.hexdigest()


def digest_file(path: Path) -> str:
    return digest(path)


def validate_inventory(path: Path, source_root: Path) -> dict[str, object]:
    try:
        inventory = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid source inventory: {path}") from error
    if inventory.get("schema") != "qsanguosha.excel.inventory.v1" or inventory.get("status") != "static-inventory-only":
        raise ValueError("source inventory is missing verified static-inventory identity")
    if Path(str(inventory.get("source_root", ""))).resolve() != source_root:
        raise ValueError("source inventory root does not match package source root")
    if not inventory.get("git", {}).get("head"):
        raise ValueError("source inventory has no Git HEAD identity")
    return {"schema": inventory["schema"], "head": inventory["git"]["head"], "branch": inventory["git"].get("branch", ""), "dirty_count": inventory["git"].get("dirty_count", 0), "sha256": digest(path)}


def validate_xlsm(path: Path, vba_source: Path | None, hashes: Path | None) -> dict[str, object]:
    if not path.is_file():
        raise ValueError(f"existing .xlsm not found: {path}")
    validate_payload(path)
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as error:
        raise ValueError(f"invalid .xlsm archive: {path}") from error
    with archive:
        names = set(archive.namelist())
        if "xl/vbaProject.bin" not in names:
            raise ValueError("existing .xlsm has no xl/vbaProject.bin; refusing a fake empty-VBA workbook")
        if archive.getinfo("xl/vbaProject.bin").file_size == 0:
            raise ValueError("existing .xlsm has an empty xl/vbaProject.bin")
    if not vba_source or not hashes:
        raise ValueError("an actual .xlsm, --vba-source, and --vba-hashes are required")
    try:
        expected = json.loads(hashes.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid VBE hash manifest: {hashes}") from error
    if not isinstance(expected, dict) or not expected:
        raise ValueError("VBE hash manifest must be a non-empty JSON object")
    normalized_expected: dict[str, str] = {}
    actual = {}
    for relative, expected_hash in expected.items():
        if not isinstance(relative, str):
            raise ValueError("VBE module paths must be strings")
        relative_path = Path(relative)
        if relative_path.is_absolute() or relative_path.drive or ".." in relative_path.parts:
            raise ValueError(f"unsafe VBE module path: {relative}")
        if not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            raise ValueError(f"invalid SHA-256 for VBE module: {relative}")
        module = vba_source / relative_path
        if not module.resolve().is_relative_to(vba_source.resolve()):
            raise ValueError(f"VBE module escapes source directory: {relative}")
        validate_payload(module)
        normalized_expected[relative_path.as_posix()] = expected_hash
        actual[relative_path.as_posix()] = digest(module)
    if actual != normalized_expected:
        raise ValueError("VBE export module hashes do not match the release manifest")
    source_modules = {p.relative_to(vba_source).as_posix() for p in vba_source.rglob("*")
                      if p.is_file() and p.suffix.lower() in (".bas", ".cls", ".frm")}
    if not source_modules or source_modules != set(actual):
        raise ValueError("VBE hash manifest must cover every exported source module exactly")
    result: dict[str, object] = {"path": str(path), "sha256": digest(path), "vbaProject.bin": "present", "module_hashes": actual, "module_hashes_verified": True}
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Stage a candidate portable Excel package; never overwrite a destination.")
    parser.add_argument("--dest", type=Path, required=True, help="new or empty destination directory")
    parser.add_argument("--source-root", type=Path, required=True, help="source root represented by the inventory")
    parser.add_argument("--inventory", type=Path, required=True, help="static inventory JSON for source identity")
    parser.add_argument("--existing-xlsm", type=Path, required=True, help="actual existing workbook to validate, never modified")
    parser.add_argument("--vba-source", type=Path, required=True, help="VBE-exported .bas/.cls source directory")
    parser.add_argument("--vba-hashes", type=Path, required=True, help="JSON map of module relative paths to SHA-256")
    parser.add_argument("--tier", choices=sorted(EXPECTED_BINARIES), required=True)
    parser.add_argument("--bridge", type=Path, required=True, help="built bridge executable for this tier")
    parser.add_argument("--server", type=Path, required=True, help="built server executable for this tier")
    parser.add_argument("--dll", type=Path, action="append", default=[])
    parser.add_argument("--tree", type=Path, action="append", default=[], help="runtime tree(s): Lua, AI, extensions, lang, image, audio")
    args = parser.parse_args()
    dest = args.dest.resolve()
    if dest.exists() and not dest.is_dir():
        raise SystemExit(f"destination is not a directory: {dest}")
    if dest.exists() and any(dest.iterdir()):
        raise SystemExit(f"refusing non-empty destination: {dest}")
    source_root = args.source_root.resolve()
    source_identity = validate_inventory(args.inventory.resolve(), source_root)
    xlsm_path = args.existing_xlsm.resolve()
    xlsm = validate_xlsm(xlsm_path, args.vba_source.resolve(), args.vba_hashes.resolve())
    # The release manifest must address the staged workbook, never the source path.
    xlsm["path"] = xlsm_path.name
    bridge_target, server_target = EXPECTED_BINARIES[args.tier]
    if args.bridge.resolve() == args.server.resolve():
        raise ValueError("bridge and server must be separate executables")
    tree_names = {p.resolve().name for p in args.tree}
    if not REQUIRED_TREES.issubset(tree_names):
        raise ValueError("package is missing one or more required runtime dependency trees: " + ", ".join(sorted(REQUIRED_TREES - tree_names)))
    if len(tree_names) != len(args.tree):
        raise ValueError("duplicate runtime tree destinations")
    lua_root = next(p.resolve() for p in args.tree if p.resolve().name == "lua")
    if not (lua_root / "ai").is_dir() or not any((lua_root / "ai").glob("*.lua")):
        raise ValueError("the lua runtime tree must include lua/ai scripts")
    dest.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix="excel-package-", dir=str(dest.parent)))
    machine = 0x8664 if args.tier == "modern" else 0x014c
    manifest: dict[str, object] = {"schema": "qsanguosha.excel.portable-manifest.v1", "status": "candidate-static-only", "capabilities": ["excel-ipc-v1", "static-catalog", "bootstrap-auth", "typed-interactions"], "ipc_version": 1, "stop_entry": "/v1/shutdown", "runtime_tiers": [args.tier], "runtime_tier": args.tier, "max_players": 10 if args.tier == "legacy" else None, "binary_targets": [bridge_target, server_target], "bootstrap_fields": ["api_version", "nonce", "session", "token", "port", "pid", "parent_pid", "parent_created", "runtime_tier", "max_players", "status"], "files": [], "dependencies": [], "source_identity": source_identity, "xlsm": xlsm, "trust_access": "unchanged"}
    try:
        sources = [(args.bridge.resolve(), staging / bridge_target), (args.server.resolve(), staging / server_target)]
        sources.extend((dll.resolve(), staging / dll.name) for dll in args.dll)
        sources.append((xlsm_path, staging / xlsm_path.name))
        launcher = Path(__file__).with_name("LaunchExcel.vbs")
        if not launcher.is_file():
            raise ValueError("private launcher is missing")
        sources.append((launcher, staging / launcher.name))
        for tree in args.tree:
            sources.append((tree.resolve(), staging / tree.name))
        for source, target in sources:
            if not source.exists():
                raise ValueError(f"missing package input: {source}")
            if source.is_file():
                validate_payload(source, machine if source.suffix.lower() in (".exe", ".dll") else None)
            if source.is_dir():
                shutil.copytree(source, target, ignore=shutil.ignore_patterns(".git", ".stignore", "*.bak", "*.bak-*", "logs", "temp"))
            else:
                if target.exists():
                    raise ValueError(f"duplicate package destination: {target.name}")
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
        for path in sorted(p for p in staging.rglob("*") if p.is_file()):
            if path.suffix.lower() in (".exe", ".dll"):
                validate_payload(path, machine)
            manifest["files"].append({"path": path.relative_to(staging).as_posix(), "sha256": digest(path), "bytes": path.stat().st_size})
        manifest["dependencies"] = [{"kind": "binary" if source.suffix.lower() in (".exe", ".dll") else "runtime-tree", "packaged": target.name, "sha256": digest(source) if source.is_file() else tree_digest(source)} for source, target in sources]
        manifest["main_sha256"] = tree_digest(staging)
        manifest["extensions_sha256"] = tree_digest(staging / "extensions")
        (staging / "release-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if dest.exists():
            dest.rmdir()
        staging.rename(dest)
    except Exception:
        if staging.resolve().parent == dest.parent.resolve() and staging.name.startswith("excel-package-"):
            shutil.rmtree(staging, ignore_errors=True)
        raise
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
