#!/usr/bin/env python3
"""Build the Linux release artifacts for QSanguosha.

Takes a configured CMake build directory and produces, in one pass:

    <output>/QSanguosha-<version>-linux-x86_64.tar.zst   portable bundle
    <output>/QSanguosha-<version>-x86_64.AppImage        AppImage
    <output>/build-info.json                             provenance
    <output>/SHA256SUMS                                  checksums

Stages: ``cmake --install`` into a staging prefix, deploy a private Qt runtime
into it, audit the result, then wrap the same staging tree twice.  Both
artifacts therefore ship *identical* payloads - a bug reproduced in one is a bug
in the other, and there is no second definition of "what goes in the package".

The AppImage is assembled directly (type2 runtime + squashfs) rather than
through ``appimagetool``: appimagetool is itself an AppImage and wants FUSE,
which CI containers do not have.  The resulting file is a normal type2 AppImage
and still supports ``--appimage-extract``, which is how it gets smoke-tested.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import urllib.request

APPIMAGE_RUNTIME_URL = (
    "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"
)


class BuildError(Exception):
    pass


def log(message: str) -> None:
    print(f"[package] {message}", flush=True)


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess:
    log("$ " + " ".join(str(part) for part in command))
    return subprocess.run(command, check=True, **kwargs)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_game_version(source_dir: pathlib.Path) -> str:
    header = (source_dir / "src" / "core" / "version.h").read_text(encoding="utf-8")
    for line in header.splitlines():
        if "Number[]" in line and '"' in line:
            return line.split('"')[1]
    raise BuildError("unable to read QSanVersion::Number from src/core/version.h")


def cmake_cache_value(build_dir: pathlib.Path, key: str) -> str:
    """Read one CMakeCache entry by name (entries are `NAME:TYPE=value`)."""
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return ""
    prefix = f"{key}:"
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix) and "=" in line:
            return line.split("=", 1)[1]
    return ""


def qt_version(qt_prefix: pathlib.Path) -> str:
    """Read the Qt version from the Qt that is actually being deployed.

    CMake does not cache it, and the prefix path itself must never reach
    build-info.json - only the version string does.
    """
    for name in ("Qt6ConfigVersion.cmake", "Qt6ConfigVersionImpl.cmake"):
        path = qt_prefix / "lib" / "cmake" / "Qt6" / name
        if not path.is_file():
            continue
        match = re.search(r'set\(PACKAGE_VERSION "([^"]+)"\)',
                          path.read_text(encoding="utf-8", errors="replace"))
        if match:
            return match.group(1)
    return ""


def compiler_identity(build_dir: pathlib.Path) -> dict:
    """The compiler id and version, which CMake records but does not cache."""
    identity = {"id": "", "version": ""}
    for path in sorted(build_dir.glob("CMakeFiles/*/CMakeCXXCompiler.cmake")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for key, field in (("CMAKE_CXX_COMPILER_ID", "id"),
                           ("CMAKE_CXX_COMPILER_VERSION", "version")):
            match = re.search(rf'set\({key} "([^"]*)"\)', text)
            if match:
                identity[field] = match.group(1)
        if identity["id"]:
            break
    return identity


def git_describe(source_dir: pathlib.Path) -> dict:
    def capture(args: list[str]) -> str:
        try:
            return subprocess.run(["git", "-C", str(source_dir)] + args,
                                  check=True, capture_output=True,
                                  text=True).stdout.strip()
        except (subprocess.CalledProcessError, OSError):
            return ""

    return {
        "commit": capture(["rev-parse", "HEAD"]),
        "short_commit": capture(["rev-parse", "--short", "HEAD"]),
        "branch": capture(["rev-parse", "--abbrev-ref", "HEAD"]),
        "dirty": bool(capture(["status", "--porcelain"])),
    }


def stage(arguments: argparse.Namespace, staging: pathlib.Path) -> None:
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    install = ["cmake", "--install", str(arguments.build_dir), "--prefix", str(staging)]
    if not arguments.no_strip:
        # Unstripped RelWithDebInfo binaries are ~400 MB each; debug info does
        # not belong in a player-facing download.
        install.append("--strip")
    run(install, stdout=subprocess.DEVNULL)

    run([sys.executable, str(arguments.tools_dir / "deploy-linux.py"),
         "--staging", str(staging),
         "--qt-prefix", str(arguments.qt_prefix),
         "--source-dir", str(arguments.source_dir),
         "--qt-version", arguments.qt_version,
         "--report", str(arguments.output_dir / "deploy-report.json")])


def audit(arguments: argparse.Namespace, root: pathlib.Path, report: pathlib.Path) -> None:
    command = [sys.executable, str(arguments.tools_dir / "audit-bundle.py"), str(root),
               "--report", str(report)]
    for forbidden in arguments.forbid:
        command += ["--forbid", forbidden]
    run(command)


def build_portable(arguments: argparse.Namespace, staging: pathlib.Path,
                   name: str) -> pathlib.Path:
    """Wrap the staging tree so that extracting gives one self-contained directory."""
    layout = arguments.work_dir / name
    if layout.exists():
        shutil.rmtree(layout)
    shutil.copytree(staging, layout, symlinks=True)

    # Top-level launchers so the documented `./QSanguosha` works straight after
    # extraction, with no environment variable set by hand.
    for launcher, target in (("QSanguosha", "qsanguosha-launcher.sh"),
                             ("qsanguosha-server", "qsanguosha-server-launcher.sh")):
        source = arguments.source_dir / "packaging" / "linux" / target
        destination = layout / launcher
        shutil.copy2(source, destination)
        destination.chmod(0o755)

    readme = layout / "README.txt"
    readme.write_text(
        "QSanguosha portable bundle\n"
        "==========================\n\n"
        "Run ./QSanguosha (GUI) or ./qsanguosha-server (dedicated server).\n"
        "Nothing has to be installed and no environment variable has to be set:\n"
        "the private Qt runtime under lib/qsanguosha/qt is found through the\n"
        "executables' $ORIGIN-relative RUNPATH and bin/qt.conf.\n\n"
        "Game data lives in share/qsanguosha.  Large artwork and voice packs are\n"
        "not part of this download; point the game at an external copy with\n"
        "  ./QSanguosha --asset-root /path/to/assets\n"
        "or by setting QSAN_ASSET_ROOT.\n\n"
        "Settings, replays and logs are written to ~/.local/share/QSanguosha and\n"
        "~/.config/QSanguosha.org - never into this directory.\n\n"
        "  ./QSanguosha --asset-report    what the game found, and what is missing\n",
        encoding="utf-8")

    archive = arguments.output_dir / f"{name}.tar.zst"
    run([sys.executable, str(arguments.tools_dir / "tar-zst.py"), "create",
         "--source", str(layout), "--output", str(archive), "--top-level", name])
    return archive


def fetch_appimage_runtime(arguments: argparse.Namespace) -> pathlib.Path:
    if arguments.appimage_runtime:
        runtime = arguments.appimage_runtime
        if not runtime.is_file():
            raise BuildError(f"--appimage-runtime does not exist: {runtime}")
    else:
        runtime = arguments.work_dir / "runtime-x86_64"
        if not runtime.is_file():
            log(f"downloading the AppImage runtime from {APPIMAGE_RUNTIME_URL}")
            runtime.parent.mkdir(parents=True, exist_ok=True)
            with urllib.request.urlopen(APPIMAGE_RUNTIME_URL, timeout=120) as response:
                runtime.write_bytes(response.read())
    digest = sha256(runtime)
    log(f"AppImage runtime sha256: {digest}")
    if arguments.appimage_runtime_sha256 \
            and digest != arguments.appimage_runtime_sha256:
        raise BuildError(
            "AppImage runtime checksum mismatch\n"
            f"  expected {arguments.appimage_runtime_sha256}\n"
            f"  actual   {digest}\n"
            "The upstream 'continuous' release was rebuilt.  Verify the new runtime "
            "and update the pin.")
    return runtime


def build_appimage(arguments: argparse.Namespace, staging: pathlib.Path,
                   name: str) -> tuple[pathlib.Path, str]:
    appdir = arguments.work_dir / f"{name}.AppDir"
    if appdir.exists():
        shutil.rmtree(appdir)
    (appdir / "usr").mkdir(parents=True)

    # The payload keeps the install layout verbatim under usr/, which is what
    # lets the runtime layout resolver find usr/share/qsanguosha from usr/bin.
    for entry in sorted(staging.iterdir()):
        destination = appdir / "usr" / entry.name
        if entry.is_dir():
            shutil.copytree(entry, destination, symlinks=True)
        else:
            shutil.copy2(entry, destination)

    apprun = appdir / "AppRun"
    shutil.copy2(arguments.source_dir / "packaging" / "linux" / "AppRun", apprun)
    apprun.chmod(0o755)

    # A type2 AppImage needs the desktop file and the icon at the root of the
    # image, next to AppRun, or desktop integration finds nothing.
    desktop_source = staging / "share" / "applications" / "qsanguosha.desktop"
    if not desktop_source.is_file():
        raise BuildError(f"missing desktop entry in the install tree: {desktop_source}")
    shutil.copy2(desktop_source, appdir / "qsanguosha.desktop")

    icon_source = staging / "share" / "icons" / "hicolor" / "256x256" / "apps" / "qsanguosha.png"
    if not icon_source.is_file():
        raise BuildError(f"missing application icon in the install tree: {icon_source}")
    shutil.copy2(icon_source, appdir / "qsanguosha.png")
    (appdir / ".DirIcon").write_bytes(icon_source.read_bytes())

    squashfs = arguments.work_dir / f"{name}.squashfs"
    if squashfs.exists():
        squashfs.unlink()
    mksquashfs = shutil.which("mksquashfs")
    if mksquashfs is None:
        raise BuildError("mksquashfs is required to build an AppImage (squashfs-tools)")
    run([mksquashfs, str(appdir), str(squashfs),
         "-comp", arguments.squashfs_comp, "-root-owned", "-noappend", "-no-progress",
         "-quiet", "-mkfs-time", "0", "-all-time", "0"])

    runtime = fetch_appimage_runtime(arguments)
    appimage = arguments.output_dir / f"{name}.AppImage"
    with open(appimage, "wb") as output:
        output.write(runtime.read_bytes())
        output.write(squashfs.read_bytes())
    appimage.chmod(0o755)
    return appimage, sha256(runtime)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    parser.add_argument("--source-dir", default=pathlib.Path("."), type=pathlib.Path)
    parser.add_argument("--qt-prefix", required=True, type=pathlib.Path)
    parser.add_argument("--qt-version", default="")
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--work-dir", type=pathlib.Path,
                        help="scratch directory (default: <output-dir>/work)")
    parser.add_argument("--version", help="release version (default: QSanVersion::Number)")
    parser.add_argument("--squashfs-comp", default="zstd")
    parser.add_argument("--appimage-runtime", type=pathlib.Path)
    parser.add_argument("--appimage-runtime-sha256", default="")
    parser.add_argument("--forbid", action="append", default=[],
                        help="path that must not appear in the payload (passed to the audit)")
    parser.add_argument("--no-strip", action="store_true")
    parser.add_argument("--skip-appimage", action="store_true")
    arguments = parser.parse_args(argv)

    arguments.source_dir = arguments.source_dir.resolve()
    arguments.build_dir = arguments.build_dir.resolve()
    arguments.qt_prefix = arguments.qt_prefix.resolve()
    arguments.output_dir = arguments.output_dir.resolve()
    arguments.work_dir = (arguments.work_dir or arguments.output_dir / "work").resolve()
    arguments.tools_dir = pathlib.Path(__file__).resolve().parent
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    arguments.work_dir.mkdir(parents=True, exist_ok=True)

    try:
        version = arguments.version or read_game_version(arguments.source_dir)
        if not arguments.qt_version:
            arguments.qt_version = qt_version(arguments.qt_prefix)

        staging = arguments.work_dir / "install"
        log(f"version {version}, Qt {arguments.qt_version or 'unknown'}")
        stage(arguments, staging)
        audit(arguments, staging, arguments.output_dir / "audit-install.json")

        portable_name = f"QSanguosha-{version}-linux-x86_64"
        portable = build_portable(arguments, staging, portable_name)

        appimage = None
        runtime_digest = ""
        if not arguments.skip_appimage:
            appimage, runtime_digest = build_appimage(
                arguments, staging, f"QSanguosha-{version}-x86_64")

        artifacts = [path for path in (portable, appimage) if path is not None]

        build_info = {
            "schema_version": 1,
            "product": "QSanguosha",
            "game_version": version,
            "platform": "linux-x86_64",
            "build_type": cmake_cache_value(arguments.build_dir, "CMAKE_BUILD_TYPE"),
            "compiler": compiler_identity(arguments.build_dir),
            "qt_version": arguments.qt_version,
            "asset_manifest": json.loads(
                (staging / "share" / "qsanguosha" / "assets-manifest.json")
                .read_text(encoding="utf-8")),
            "git": git_describe(arguments.source_dir),
            "created": datetime.datetime.now(datetime.timezone.utc)
                        .replace(microsecond=0).isoformat(),
            "appimage_runtime_sha256": runtime_digest,
            "artifacts": [
                {"name": path.name, "size": path.stat().st_size, "sha256": sha256(path)}
                for path in artifacts
            ],
        }
        # Deliberately absent: working directories, the packager's user name, the
        # Qt prefix and the build directory.  Those identify the build machine and
        # have no meaning to whoever downloads the artifact.
        build_info["asset_manifest"].pop("_comment", None)

        info_path = arguments.output_dir / "build-info.json"
        info_path.write_text(json.dumps(build_info, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")

        checksums = arguments.output_dir / "SHA256SUMS"
        lines = [f"{entry['sha256']}  {entry['name']}" for entry in build_info["artifacts"]]
        lines.append(f"{sha256(info_path)}  build-info.json")
        checksums.write_text("\n".join(lines) + "\n", encoding="utf-8")

        log("")
        for entry in build_info["artifacts"]:
            log(f"{entry['name']}  {entry['size']} bytes")
            log(f"  sha256 {entry['sha256']}")
        log(f"build-info: {info_path}")
        log(f"checksums : {checksums}")
    except (BuildError, subprocess.CalledProcessError) as error:
        print(f"build-linux-packages: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
