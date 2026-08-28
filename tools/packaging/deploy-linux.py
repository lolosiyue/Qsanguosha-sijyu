#!/usr/bin/env python3
"""Deploy a private Qt runtime into a QSanguosha install tree.

Qt 6.11 has no generic-Linux equivalent of ``windeployqt``: there is no
``qt_generate_deploy_app_script`` support for desktop Linux and no
``linuxdeployqt`` in the Qt release, so the collection is done here.

What it collects, and why:

* Qt libraries reached transitively from the installed executables and from
  every plugin/QML plugin that is copied, restricted to libraries that live
  inside the Qt prefix.  That rule alone keeps the host's glibc, libGL, libX11,
  libfreetype and friends out of the bundle - only Qt's own libraries (plus the
  ICU and FFmpeg copies Qt ships inside its own prefix) come along.
* Qt plugins for the groups the game actually needs.  ``ldd`` cannot see these:
  platform, image format, TLS and multimedia plugins are all dlopen'd.
* QML modules, resolved with Qt's own ``qmlimportscanner`` over the project's
  QML sources, plus the Quick Controls style the game selects at runtime.

The private runtime deliberately mirrors Qt's own prefix layout
(``lib/``, ``plugins/<type>/``, ``qml/<Module>/``).  Every Qt binary already
carries an $ORIGIN-relative RUNPATH that assumes exactly that layout, so
reproducing it means nothing has to be rewritten with patchelf.

Never copied: headers, CMake/pkg-config files, .prl files, static archives,
separate debug info, or the Qt tools.

Usage:
    python3 tools/packaging/deploy-linux.py \
        --staging <cmake --install prefix> --qt-prefix <qt> \
        --source-dir <repo> --report deploy-report.json
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import elfinfo  # noqa: E402

# dlopen'd at runtime, so no amount of dependency walking finds them.
PLUGIN_GROUPS = (
    "platforms",
    "platformthemes",
    "platforminputcontexts",
    "xcbglintegrations",
    "imageformats",
    "iconengines",
    "multimedia",
    "tls",
    "networkinformation",
    "generic",
    "wayland-decoration-client",
    "wayland-graphics-integration-client",
    "wayland-shell-integration",
)

# Within those groups, plugins that would only add weight.
PLUGIN_SKIP = {
    "libqvnc.so",
    "libqlinuxfb.so",
    "libqeglfs.so",
    "libqdirectfb.so",
    "libqtuiotouchplugin.so",
}

# QML modules the scanner cannot see: QQuickStyle::setStyle("Basic") is a
# runtime string, and the controls implementation is loaded through it.
EXTRA_QML_MODULES = (
    "QtQuick/Controls",
    "QtQuick/Controls/Basic",
    "QtQuick/Controls/Fusion",
    "QtQuick/Templates",
    "QtQuick/Window",
    "QtQuick/Layouts",
    "QtQml/WorkerScript",
)

# Quick Controls styles the game can never select: main.cpp calls
# QQuickStyle::setStyle("Basic") unconditionally, and Fusion is kept as the one
# sensible manual override (QT_QUICK_CONTROLS_STYLE).  The scanner pulls every
# style in because QtQuick.Controls advertises them all; carrying them would add
# roughly ten megabytes of unreachable QML and shared objects.
QML_MODULE_SKIP_PREFIXES = (
    "QtQuick/Controls/FluentWinUI3",
    "QtQuick/Controls/Imagine",
    "QtQuick/Controls/Material",
    "QtQuick/Controls/Universal",
    "QtQuick/Controls/iOS",
    "QtQuick/Controls/macOS",
    "QtQuick/Controls/Windows",
)

# Files that must never end up in a runtime bundle.
EXCLUDED_SUFFIXES = (".a", ".prl", ".la", ".pc", ".debug", ".h", ".hpp", ".cmake")
EXCLUDED_NAMES = ("objects-Debug", "objects-Release", "objects-RelWithDebInfo",
                  "cmake", "pkgconfig", "include")


class DeployError(Exception):
    pass


def log(message: str) -> None:
    print(f"[deploy] {message}", flush=True)


def find_qmlimportscanner(qt_prefix: pathlib.Path) -> pathlib.Path | None:
    for candidate in (qt_prefix / "libexec" / "qmlimportscanner",
                      qt_prefix / "bin" / "qmlimportscanner"):
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    return None


def scan_qml_imports(qt_prefix: pathlib.Path, roots: list[pathlib.Path]) -> list[str]:
    """Ask Qt which QML modules the project's .qml files import."""
    scanner = find_qmlimportscanner(qt_prefix)
    existing = [root for root in roots if root.exists()]
    if scanner is None or not existing:
        if scanner is None:
            log("qmlimportscanner not found; falling back to the curated module list")
        return []
    command = [str(scanner)]
    for root in existing:
        command += ["-rootPath", str(root)]
    command += ["-importPath", str(qt_prefix / "qml")]
    try:
        output = subprocess.run(command, check=True, capture_output=True, text=True).stdout
    except (subprocess.CalledProcessError, OSError) as error:
        log(f"qmlimportscanner failed ({error}); falling back to the curated module list")
        return []
    modules: list[str] = []
    for entry in json.loads(output or "[]"):
        # Only modules that resolve inside the Qt prefix are ours to deploy;
        # the project's own QML lives in the data directory already.
        path = entry.get("path")
        name = entry.get("name")
        if not path or not name:
            continue
        resolved = pathlib.Path(path).resolve()
        try:
            relative = resolved.relative_to((qt_prefix / "qml").resolve())
        except ValueError:
            continue
        modules.append(str(relative))
    return sorted(set(modules))


def resolve_library(soname: str, qt_lib: pathlib.Path) -> pathlib.Path | None:
    candidate = qt_lib / soname
    if candidate.exists():
        return candidate.resolve()
    return None


def collect_libraries(seeds: list[pathlib.Path], qt_lib: pathlib.Path,
                      report: dict) -> dict[str, pathlib.Path]:
    """Transitive DT_NEEDED closure, keeping only libraries inside the Qt prefix."""
    wanted: dict[str, pathlib.Path] = {}
    system: set[str] = set()
    pending = list(seeds)
    seen: set[pathlib.Path] = set()
    while pending:
        current = pending.pop()
        if current in seen or not current.exists():
            continue
        seen.add(current)
        try:
            info = elfinfo.read(current)
        except elfinfo.NotAnElf:
            continue
        for soname in info.needed:
            if soname in wanted or soname in system:
                continue
            resolved = resolve_library(soname, qt_lib)
            if resolved is None:
                # Not inside the Qt prefix: the host provides it (glibc, libGL,
                # libX11, libfreetype, ...).  Bundling those is what breaks
                # portable Linux builds, so they are recorded, not copied.
                system.add(soname)
                continue
            wanted[soname] = resolved
            pending.append(resolved)
    report["system_libraries"] = sorted(system)
    return wanted


def deploy(arguments: argparse.Namespace) -> dict:
    staging = arguments.staging.resolve()
    qt_prefix = arguments.qt_prefix.resolve()
    qt_lib = qt_prefix / "lib"
    if not qt_lib.is_dir():
        raise DeployError(f"no lib/ directory under the Qt prefix: {qt_prefix}")

    bindir = staging / arguments.bindir
    if not bindir.is_dir():
        raise DeployError(f"no {arguments.bindir}/ in the staging tree: {staging}")
    binaries = sorted(item for item in bindir.iterdir()
                      if item.is_file() and elfinfo.is_elf(item))
    if not binaries:
        raise DeployError(f"no executables found in {bindir}")

    private = staging / arguments.private_dir
    private_lib = private / "lib"
    private_plugins = private / "plugins"
    private_qml = private / "qml"
    for directory in (private_lib, private_plugins, private_qml):
        directory.mkdir(parents=True, exist_ok=True)

    report: dict = {
        "schema_version": 1,
        "qt_version": arguments.qt_version,
        "binaries": [str(item.relative_to(staging)) for item in binaries],
        "private_dir": str(private.relative_to(staging)),
    }

    # --- QML modules -------------------------------------------------------
    qml_roots = [arguments.source_dir / name for name in ("qml", "ui-script")]
    modules = scan_qml_imports(qt_prefix, qml_roots)
    for extra in EXTRA_QML_MODULES:
        if extra not in modules:
            modules.append(extra)
    # A module directory implies its parents (QtQuick/Controls/Basic needs the
    # qmldir of QtQuick/Controls above it).
    expanded: set[str] = set()
    for module in modules:
        parts = pathlib.PurePath(module).parts
        for index in range(1, len(parts) + 1):
            expanded.add(str(pathlib.PurePath(*parts[:index])))
    qml_copied: list[str] = []
    qml_skipped: list[str] = []
    qml_plugins: list[pathlib.Path] = []
    for module in sorted(expanded):
        if module.startswith(QML_MODULE_SKIP_PREFIXES):
            qml_skipped.append(module)
            continue
        source = qt_prefix / "qml" / module
        if not source.is_dir():
            continue
        destination = private_qml / module
        destination.mkdir(parents=True, exist_ok=True)
        # Only this level's own files; nested modules are separate entries.
        for item in sorted(source.iterdir()):
            if item.is_dir() or item.suffix in EXCLUDED_SUFFIXES:
                continue
            target = destination / item.name
            shutil.copy2(item, target, follow_symlinks=True)
            if elfinfo.is_elf(target):
                qml_plugins.append(target)
        qml_copied.append(module)
    report["qml_modules"] = qml_copied
    report["qml_modules_skipped"] = qml_skipped

    # --- plugins -----------------------------------------------------------
    plugins_copied: list[str] = []
    plugin_files: list[pathlib.Path] = []
    for group in PLUGIN_GROUPS:
        source = qt_prefix / "plugins" / group
        if not source.is_dir():
            continue
        for item in sorted(source.iterdir()):
            if not item.is_file() or item.suffix != ".so" or item.name in PLUGIN_SKIP:
                continue
            destination = private_plugins / group / item.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, destination, follow_symlinks=True)
            plugin_files.append(destination)
            plugins_copied.append(f"{group}/{item.name}")
    report["plugins"] = plugins_copied

    # --- libraries ---------------------------------------------------------
    libraries = collect_libraries(binaries + plugin_files + qml_plugins, qt_lib, report)
    copied_libraries: list[str] = []
    for soname, real in sorted(libraries.items()):
        target = private_lib / real.name
        if not target.exists():
            shutil.copy2(real, target, follow_symlinks=True)
        if soname != real.name:
            link = private_lib / soname
            if not link.exists():
                link.symlink_to(real.name)
        copied_libraries.append(soname)
    report["libraries"] = copied_libraries

    # --- qt.conf -----------------------------------------------------------
    # Relative to the executable's own directory, so the whole tree stays
    # relocatable.  Without this Qt would look for plugins next to the binary
    # and then in the Qt prefix baked in at build time - a developer path that
    # does not exist on the player's machine.
    prefix_relative = os.path.relpath(private, bindir)
    qt_conf = bindir / "qt.conf"
    qt_conf.write_text(
        "[Paths]\n"
        f"Prefix = {prefix_relative}\n"
        "Libraries = lib\n"
        "Plugins = plugins\n"
        "Qml2Imports = qml\n"
        "Data = .\n"
        "Translations = translations\n",
        encoding="utf-8",
    )
    report["qt_conf"] = str(qt_conf.relative_to(staging))

    # Qt's own translations (dialog buttons and the like) are small and make the
    # bundle behave the same as a distro install.
    qt_translations = qt_prefix / "translations"
    if qt_translations.is_dir():
        target = private / "translations"
        target.mkdir(parents=True, exist_ok=True)
        wanted = [item for item in sorted(qt_translations.glob("qt*_zh_CN.qm"))]
        for item in wanted:
            shutil.copy2(item, target / item.name)
        report["qt_translations"] = [item.name for item in wanted]

    return report


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staging", required=True, type=pathlib.Path,
                        help="the tree produced by cmake --install")
    parser.add_argument("--qt-prefix", required=True, type=pathlib.Path)
    parser.add_argument("--source-dir", required=True, type=pathlib.Path,
                        help="repository root, used to scan QML imports")
    parser.add_argument("--bindir", default="bin")
    parser.add_argument("--private-dir", default="lib/qsanguosha/qt",
                        help="must mirror Qt's prefix layout; see the module docstring")
    parser.add_argument("--qt-version", default="")
    parser.add_argument("--report", type=pathlib.Path)
    arguments = parser.parse_args(argv)

    try:
        report = deploy(arguments)
    except DeployError as error:
        print(f"deploy-linux: {error}", file=sys.stderr)
        return 1

    log(f"libraries    : {len(report['libraries'])}")
    log(f"plugins      : {len(report['plugins'])}")
    log(f"QML modules  : {len(report['qml_modules'])}")
    log(f"system libs  : {len(report['system_libraries'])} (left to the host)")
    if arguments.report:
        arguments.report.parent.mkdir(parents=True, exist_ok=True)
        arguments.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                                    encoding="utf-8")
        log(f"report       : {arguments.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
