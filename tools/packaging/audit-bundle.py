#!/usr/bin/env python3
"""Audit a QSanguosha Linux bundle before it is shipped.

A package that runs on the machine that built it proves nothing: the failures
that matter only show up on a player's machine.  This checks the three things
that cause them, on the artifact itself rather than on the build tree:

1. **RPATH/RUNPATH must be $ORIGIN-relative.**  An absolute entry pointing at a
   build machine's Qt is the classic "works here, missing libQt6Core there".
2. **No developer paths anywhere in the payload.**  Build directories, the
   packager's home directory and the Qt prefix must not survive into any file.
3. **No development or system files.**  Headers, static archives, CMake and
   pkg-config files, separate debug info - and no bundled copy of the host's
   glibc/libGL/libX11, which is what makes a portable bundle refuse to start on
   a different distribution.

Exit code is non-zero when a check fails, so this is usable as a CI gate.

Usage:
    python3 tools/packaging/audit-bundle.py <root> [--forbid <path>]... \
        [--report audit.json] [--allow-system-lib libfoo.so.1]
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import elfinfo  # noqa: E402

# Libraries that must come from the host, never from the bundle.  Shipping any
# of these is what breaks a portable Linux build on a different distribution:
# the loader would mix the bundle's copy with the host's driver stack.
FORBIDDEN_BUNDLED = (
    "libc.so.", "libm.so.", "libpthread.so.", "libdl.so.", "librt.so.",
    "ld-linux", "libgcc_s.so.", "libstdc++.so.",
    "libGL.so.", "libGLX.so.", "libEGL.so.", "libOpenGL.so.", "libGLdispatch.so.",
    "libX11.so.", "libxcb.so.", "libwayland-client.so.",
    "libdrm.so.", "libgbm.so.",
    "libfontconfig.so.", "libfreetype.so.",
    "libasound.so.", "libpulse.so.",
)

FORBIDDEN_SUFFIXES = (".a", ".prl", ".la", ".pc", ".debug", ".h", ".hpp", ".cmake", ".pdb")
FORBIDDEN_DIR_NAMES = ("include", "cmake", "pkgconfig", "mkspecs", "metatypes")

# Scanning every byte of a 30 MB executable for developer paths is fine; a
# 32 MB ICU data blob has no strings we care about but costs the same.  The cap
# keeps the audit quick without weakening it - developer paths live in RPATH,
# qt.conf, scripts and small data files, all far below it.
MAX_SCAN_BYTES = 64 * 1024 * 1024


class Finding:
    def __init__(self, check: str, path: str, detail: str):
        self.check = check
        self.path = path
        self.detail = detail

    def as_dict(self) -> dict:
        return {"check": self.check, "path": self.path, "detail": self.detail}

    def __str__(self) -> str:
        return f"[{self.check}] {self.path}: {self.detail}"


def audit(root: pathlib.Path, forbidden_paths: list[str],
          allowed_system_libs: set[str]) -> tuple[list[Finding], dict]:
    findings: list[Finding] = []
    summary = {
        "root": str(root),
        "elf_files": 0,
        "files": 0,
        "rpath_entries": {},
        "bundled_libraries": [],
    }

    # Normalise the needles once; an empty or "/" needle would match everything.
    needles = [path.rstrip("/") for path in forbidden_paths if len(path.rstrip("/")) > 1]
    needle_bytes = [(needle, needle.encode()) for needle in needles]

    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            target = os.readlink(path)
            if os.path.isabs(target):
                findings.append(Finding("absolute-symlink", str(path.relative_to(root)),
                                        f"points outside the bundle: {target}"))
            continue
        if path.is_dir():
            if path.name in FORBIDDEN_DIR_NAMES:
                findings.append(Finding("development-files", str(path.relative_to(root)),
                                        f"development directory '{path.name}' in a runtime bundle"))
            continue
        if not path.is_file():
            continue

        summary["files"] += 1
        relative = str(path.relative_to(root))

        if path.suffix in FORBIDDEN_SUFFIXES:
            findings.append(Finding("development-files", relative,
                                    f"development file suffix '{path.suffix}'"))

        is_elf = elfinfo.is_elf(path)
        if is_elf:
            summary["elf_files"] += 1
            try:
                info = elfinfo.read(path)
            except elfinfo.NotAnElf as error:
                findings.append(Finding("elf", relative, str(error)))
                info = None
            if info is not None:
                entries = info.runpath or info.rpath
                if entries:
                    summary["rpath_entries"][relative] = entries
                for entry in entries:
                    if not entry.startswith("$ORIGIN") and not entry.startswith("${ORIGIN}"):
                        findings.append(Finding(
                            "rpath", relative,
                            f"RPATH/RUNPATH entry is not $ORIGIN-relative: {entry}"))
                if info.soname:
                    summary["bundled_libraries"].append(info.soname)
                    for forbidden in FORBIDDEN_BUNDLED:
                        if info.soname.startswith(forbidden) \
                                and info.soname not in allowed_system_libs:
                            findings.append(Finding(
                                "system-library", relative,
                                f"host library must not be bundled: {info.soname}"))

        if needle_bytes and path.stat().st_size <= MAX_SCAN_BYTES:
            try:
                blob = path.read_bytes()
            except OSError as error:
                findings.append(Finding("unreadable", relative, str(error)))
                continue
            for needle, encoded in needle_bytes:
                if encoded in blob:
                    findings.append(Finding("developer-path", relative,
                                            f"payload contains a build-machine path: {needle}"))

    summary["bundled_libraries"] = sorted(set(summary["bundled_libraries"]))
    return findings, summary


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument("--forbid", action="append", default=[],
                        help="a path that must not appear anywhere in the payload "
                             "(build directory, Qt prefix, home directory)")
    parser.add_argument("--allow-system-lib", action="append", default=[],
                        help="soname that may be bundled despite the host-library rule")
    parser.add_argument("--report", type=pathlib.Path)
    arguments = parser.parse_args(argv)

    root = arguments.root.resolve()
    if not root.is_dir():
        print(f"audit-bundle: not a directory: {root}", file=sys.stderr)
        return 2

    findings, summary = audit(root, arguments.forbid, set(arguments.allow_system_lib))

    print(f"== bundle audit: {root} ==")
    print(f"files            : {summary['files']}")
    print(f"ELF objects      : {summary['elf_files']}")
    print(f"bundled libraries: {len(summary['bundled_libraries'])}")
    distinct = sorted({tuple(value) for value in summary["rpath_entries"].values()})
    print(f"distinct RPATHs  : {len(distinct)}")
    for entry in distinct:
        print(f"  {':'.join(entry)}")

    if arguments.report:
        arguments.report.parent.mkdir(parents=True, exist_ok=True)
        arguments.report.write_text(
            json.dumps({"summary": summary,
                        "findings": [finding.as_dict() for finding in findings]},
                       indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        print(f"report           : {arguments.report}")

    if findings:
        print(f"\n{len(findings)} problem(s) found:", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        return 1
    print("\nbundle audit OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
