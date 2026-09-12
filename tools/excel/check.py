#!/usr/bin/env python3
"""Static checks for Excel VBA source and a portable release manifest."""
from __future__ import annotations
import argparse, hashlib, json, re, zipfile
from pathlib import Path

REQUIRED = ("WinHttp.WinHttpRequest.5.1", "/v1/commands", "/v1/updates", "Authorization", "X-QSan-Session", "Application.OnTime")
FIELDS = ("schema", "status", "capabilities", "ipc_version", "stop_entry", "runtime_tiers", "runtime_tier", "max_players", "binary_targets", "files", "dependencies", "source_identity", "main_sha256", "extensions_sha256", "xlsm", "trust_access")
EXPECTED_BINARIES = {
    "modern": {"QSanguoshaExcelBridge.exe", "QSanguoshaExcelServer.exe"},
    "legacy": {"QSanguoshaExcelBridge.exe", "QSanguoshaXPServer.exe"},
}

def tree_digest(root: Path, exclude_manifest: bool = False) -> str:
    digest = hashlib.sha256()
    for path in sorted(p for p in root.rglob("*") if p.is_file() and ".git" not in p.parts and (not exclude_manifest or p.name != "release-manifest.json")):
        digest.update(path.relative_to(root).as_posix().encode("utf-8") + b"\0" + hashlib.sha256(path.read_bytes()).digest() + b"\0")
    return digest.hexdigest()

def contained_path(root: Path, relative: object) -> Path | None:
    if not isinstance(relative, str) or not relative:
        return None
    path = Path(relative)
    if path.is_absolute() or path.drive or ".." in path.parts:
        return None
    candidate = (root / path).resolve()
    return candidate if candidate.is_relative_to(root.resolve()) else None

def main() -> int:
    p = argparse.ArgumentParser(description="Check VBA/API strings and manifest coverage without running Excel.")
    p.add_argument("--vba-source", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--tier", choices=sorted(EXPECTED_BINARIES), required=True)
    p.add_argument("--api-contract", type=Path, help="defaults to docs/excel-ipc.md")
    a = p.parse_args()
    sources = [x for x in a.vba_source.rglob("*") if x.is_file() and x.suffix.lower() in (".bas", ".cls", ".frm")]
    text = "\n".join(x.read_text(encoding="utf-8", errors="ignore") for x in sources)
    missing = [item for item in REQUIRED if item.lower() not in text.lower()]
    manifest = json.loads(a.manifest.read_text(encoding="utf-8"))
    absent = [field for field in FIELDS if field not in manifest]
    errors = []
    if missing: errors.append("missing VBA/API markers: " + ", ".join(missing))
    if absent: errors.append("missing manifest fields: " + ", ".join(absent))
    if not sources: errors.append("VBA source directory contains no .bas/.cls/.frm exports")
    if manifest.get("runtime_tiers") != [a.tier]: errors.append(f"manifest must contain only {a.tier} runtime tier")
    if manifest.get("runtime_tier") != a.tier: errors.append(f"manifest runtime_tier must be {a.tier}")
    if manifest.get("ipc_version") != 1: errors.append("manifest ipc_version must be 1")
    if manifest.get("stop_entry") != "/v1/shutdown": errors.append("manifest stop_entry must be /v1/shutdown")
    expected_max = 10 if a.tier == "legacy" else None
    if manifest.get("max_players") != expected_max: errors.append(f"manifest max_players must be {expected_max!r} for {a.tier}")
    if manifest.get("trust_access") != "unchanged": errors.append("TrustAccess must remain unchanged")
    if set(manifest.get("binary_targets", [])) != EXPECTED_BINARIES[a.tier]: errors.append(f"manifest binary_targets do not match {a.tier} bundle")
    if not isinstance(manifest.get("dependencies"), list) or not manifest["dependencies"]: errors.append("manifest dependencies are absent")
    identity = manifest.get("source_identity")
    if not isinstance(identity, dict) or identity.get("schema") != "qsanguosha.excel.inventory.v1" or not identity.get("head") or not identity.get("sha256"):
        errors.append("manifest lacks verified source inventory identity")
    for key in ("main_sha256", "extensions_sha256"):
        if not isinstance(manifest.get(key), str) or not re.fullmatch(r"[0-9a-f]{64}", manifest[key]): errors.append(f"manifest {key} is absent or invalid")
    xlsm = manifest.get("xlsm")
    if not isinstance(xlsm, dict) or xlsm.get("vbaProject.bin") != "present" or not xlsm.get("module_hashes_verified"):
        errors.append("manifest lacks verified actual .xlsm/VBE module evidence")
    manifest_root = a.manifest.parent
    if manifest.get("main_sha256") != tree_digest(manifest_root, exclude_manifest=True): errors.append("manifest main_sha256 mismatch")
    extensions_root = manifest_root / "extensions"
    if not extensions_root.is_dir() or manifest.get("extensions_sha256") != tree_digest(extensions_root): errors.append("manifest extensions_sha256 mismatch")
    for entry in manifest.get("files", []):
        relative = entry.get("path") if isinstance(entry, dict) else None
        path = contained_path(manifest_root, relative)
        if path is None:
            errors.append("manifest contains an unsafe file path")
            continue
        if not path.is_file():
            errors.append(f"manifest file missing: {relative}")
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != entry.get("sha256"):
            errors.append(f"manifest hash mismatch: {relative}")
    if isinstance(xlsm, dict):
        workbook = contained_path(manifest_root, xlsm.get("path"))
        if workbook is None or not workbook.is_file():
            errors.append("manifest workbook is missing")
        else:
            try:
                with zipfile.ZipFile(workbook) as archive:
                    if "xl/vbaProject.bin" not in archive.namelist() or archive.getinfo("xl/vbaProject.bin").file_size == 0:
                        errors.append("manifest workbook has no non-empty xl/vbaProject.bin")
            except (OSError, zipfile.BadZipFile):
                errors.append("manifest workbook is not a valid .xlsm archive")
            if xlsm.get("sha256") != hashlib.sha256(workbook.read_bytes()).hexdigest():
                errors.append("manifest workbook hash mismatch")
        module_hashes = xlsm.get("module_hashes")
        if not isinstance(module_hashes, dict) or not module_hashes:
            errors.append("manifest VBE module hash map is absent")
        else:
            for relative, expected in module_hashes.items():
                module = contained_path(a.vba_source, relative)
                if module is None or not module.is_file():
                    errors.append(f"VBE export module missing: {relative}")
                elif hashlib.sha256(module.read_bytes()).hexdigest() != expected:
                    errors.append(f"VBE export module hash mismatch: {relative}")
    if errors:
        for error in errors: print("ERROR: " + error)
        return 1
    print("PASS: static VBA/API and manifest coverage checks")
    return 0

if __name__ == "__main__": raise SystemExit(main())
