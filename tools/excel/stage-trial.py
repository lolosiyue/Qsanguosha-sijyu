#!/usr/bin/env python3
"""Stage an offline, runtime-only Excel trial bundle.

This tool never creates or modifies an .xlsm.  The default operation stages
the binaries, runtime content, FMOD and VBE source copies; ``--finalize``
seals the resulting file inventory and current source identity.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

VBA_FILES = (
    "QsanBootstrap.bas", "QsanCatalog.bas", "QsanInteraction.bas",
    "QsanJson.bas", "QsanTests.bas", "QsanTransport.bas", "QsanUi.bas",
    "ThisWorkbook.cls",
)
BINARY_FILES = ("QSanguoshaExcelBridge.exe", "QSanguoshaExcelServer.exe")
FMOD_NAME = "fmodex64.dll"


def _hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _copy_file(source: Path, target: Path) -> None:
    if not source.is_file() or source.is_symlink():
        raise ValueError(f"missing or unsafe file: {source}")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def _copy_tree(source: Path, target: Path, *, relative_files: set[str] | None = None) -> None:
    if not source.is_dir() or source.is_symlink():
        raise ValueError(f"missing or unsafe directory: {source}")
    for path in sorted(source.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"runtime tree contains symlink: {path}")
        if not path.is_file():
            continue
        relative = path.relative_to(source).as_posix()
        if relative_files is not None and relative not in relative_files:
            continue
        _copy_file(path, target / relative)


def _load_web_packager(root: Path):
    source = root / "tools" / "package-web-solo.py"
    spec = importlib.util.spec_from_file_location("qsan_web_packager", source)
    if spec is None or spec.loader is None:
        raise ValueError(f"cannot load declared-content helper: {source}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def _manifest_files(dest: Path) -> list[dict[str, object]]:
    result = []
    for path in sorted(p for p in dest.rglob("*") if p.is_file() and p != dest / "release-manifest.json"):
        result.append({"path": path.relative_to(dest).as_posix(), "sha256": _hash(path), "bytes": path.stat().st_size})
    return result


def _base_manifest(root: Path) -> dict[str, object]:
    try:
        head = _git(root, "rev-parse", "HEAD")
        dirty = _git(root, "status", "--porcelain", "--untracked-files=all").splitlines()
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError(f"cannot read Git source identity: {error}") from error
    return {
        "schema": "qsanguosha.excel.portable-manifest.v1",
        "status": "runtime-only-trial",
        "runtime_tier": "modern",
        "binary_targets": list(BINARY_FILES),
        "workbook": {"status": "missing", "reason": "Office activation required; no xlsm staged"},
        "vba": {"source_original_utf8": "vba-source/original-utf8", "import_copy_cp950_crlf": "vba-source/import-cp950-crlf", "modules": list(VBA_FILES)},
        "content": {"declared_lua": True, "ai_collector": "tools/package-web-solo.py:_collect_ai", "excluded_interactions": ["qml_interact", "qsanguosha.qml"], "excluded_content": ["undeclared Lua", "extensions/temp", "etc custom scenarios", "lua/ai/.git", "lua/ai/logs", "lua/ai/data", "lua/ai/temp", "lua/ai/runtime", "lua/ai/backup", "lua/ai/backups"]},
        "trust_access": "unchanged",
        "source": {"head": head, "dirty": dirty, "dirty_count": len(dirty)},
        "files": [],
    }


def stage(root: Path, binary_dir: Path, dest: Path) -> None:
    if dest.exists() and (not dest.is_dir() or any(dest.iterdir())):
        raise ValueError(f"destination must be new or empty: {dest}")
    # Preflight encodings before publishing any part of a new destination.
    for name in VBA_FILES:
        (root / "excel" / "vba" / name).read_text(encoding="utf-8").encode("cp950")
    for name in BINARY_FILES:
        _copy_file(binary_dir / name, dest / name)
    _copy_file(root / "docs" / "excel-trial-readme.txt", dest / "00-先讀我.txt")
    _copy_file(root / "LICENSE", dest / "LICENSE")
    _copy_file(root / "tools" / "excel" / "LaunchExcel.vbs", dest / "LaunchExcel.vbs")
    _copy_file(root / "TODO" / "anime" / FMOD_NAME, dest / FMOD_NAME)

    vba_root = root / "excel" / "vba"
    for name in VBA_FILES:
        source = vba_root / name
        original = dest / "vba-source" / "original-utf8" / name
        _copy_file(source, original)
        try:
            text = source.read_text(encoding="utf-8")
            encoded = text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n").encode("cp950")
        except UnicodeEncodeError as error:
            raise ValueError(f"VBA module cannot be encoded as CP950: {source}: {error}") from error
        import_copy = dest / "vba-source" / "import-cp950-crlf" / name
        import_copy.parent.mkdir(parents=True, exist_ok=True)
        import_copy.write_bytes(encoded)

    packager = _load_web_packager(root)
    with tempfile.TemporaryDirectory(prefix="excel-declared-") as temporary:
        declared = Path(temporary) / "content"
        packager.prepare_content(root, declared)
        _copy_tree(declared, dest)
    for relative, source in packager._collect_ai(root):
        _copy_file(source, dest / relative)
    _copy_tree(root / "image", dest / "image")
    _copy_tree(root / "audio", dest / "audio")
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "release-manifest.json").write_text(json.dumps(_base_manifest(root), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def finalize(root: Path, dest: Path) -> None:
    manifest_path = dest / "release-manifest.json"
    if not manifest_path.is_file():
        raise ValueError(f"runtime trial manifest is missing: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("status") != "runtime-only-trial":
        raise ValueError("refusing to finalize a non-runtime-only trial manifest")
    manifest["source"] = _base_manifest(root)["source"]
    manifest["files"] = _manifest_files(dest)
    manifest["file_count"] = len(manifest["files"])
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--binary-dir", type=Path)
    parser.add_argument("--dest", type=Path, required=True)
    parser.add_argument("--finalize", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    dest = args.dest.resolve()
    try:
        if args.finalize:
            finalize(root, dest)
        else:
            if args.binary_dir is None:
                parser.error("staging requires --binary-dir")
            stage(root, args.binary_dir.resolve(), dest)
    except (OSError, UnicodeError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Excel runtime trial staging failed: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
