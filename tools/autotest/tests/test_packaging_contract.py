#!/usr/bin/env python3
"""Contract tests for the Linux packaging pipeline (M3).

Packaging bugs are the ones nobody sees until a stranger downloads the file, so
the properties that make a package work on somebody else's machine are pinned
here rather than left to a reviewer noticing.  These tests read the repository:
they never build a package, so they stay fast enough for every CI run.

Run directly (``python3 tools/autotest/tests/test_packaging_contract.py``) or
through CTest as ``qsanguosha_packaging_contract``.
"""

from __future__ import annotations

import json
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[3]
WORKFLOWS = ROOT / ".github" / "workflows"
PACKAGING = ROOT / "tools" / "packaging"
DESKTOP = ROOT / "packaging" / "linux" / "qsanguosha.desktop"
ICON_DIR = ROOT / "resource" / "icon" / "linux"
CMAKELISTS = ROOT / "CMakeLists.txt"
MANIFEST_TEMPLATE = ROOT / "packaging" / "assets-manifest.json.in"
PACKAGE_WORKFLOW = WORKFLOWS / "linux-package-ci.yml"
DOCS = ROOT / "docs" / "linux-packaging.md"
RESOLVER = ROOT / "src" / "core" / "runtime-paths.cpp"

ICON_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def png_dimensions(path: pathlib.Path) -> tuple[int, int]:
    blob = path.read_bytes()
    assert blob[:8] == b"\x89PNG\r\n\x1a\n", f"{path} is not a PNG"
    width, height = struct.unpack(">II", blob[16:24])
    # A truncated or corrupt PNG still has a readable header, so verify the
    # first chunk's CRC too rather than trusting the magic bytes alone.
    length, = struct.unpack(">I", blob[8:12])
    payload = blob[12:16 + length]
    expected, = struct.unpack(">I", blob[16 + length:20 + length])
    assert zlib.crc32(payload) & 0xFFFFFFFF == expected, f"{path} has a corrupt IHDR chunk"
    return width, height


def test_linux_icons_are_real_pngs_at_every_declared_size() -> None:
    """A Windows .ico is not a Linux icon.

    The repository only ever shipped ``resource/icon/sgs.ico`` (32x32 and 16x16,
    8-bit) and an .icns whose single member is JPEG 2000.  Installing either as
    the hicolor icon gives a blurry or unreadable launcher entry, so the Linux
    set is generated at proper sizes and each one has to actually be that size.
    """
    for size in ICON_SIZES:
        path = ICON_DIR / f"qsanguosha-{size}.png"
        assert path.is_file(), f"missing generated icon: {path}"
        width, height = png_dimensions(path)
        assert (width, height) == (size, size), \
            f"{path} is {width}x{height}, not {size}x{size}"

    svg = ICON_DIR / "qsanguosha.svg"
    assert svg.is_file(), "the scalable hicolor icon is missing"
    assert "<svg" in read(svg), "the scalable icon is not SVG"

    installed = read(CMAKELISTS)
    assert "icons/hicolor/scalable/apps" in installed, \
        "the scalable icon is not installed into the hicolor theme"
    sizes = re.search(r"foreach\(qsan_icon_size ([0-9 ]+)\)", installed)
    assert sizes, "the hicolor PNG sizes are not installed from a size list"
    assert tuple(int(size) for size in sizes.group(1).split()) == ICON_SIZES, \
        "the installed icon sizes do not match the generated set"
    assert '${qsan_icon_size}x${qsan_icon_size}/apps' in installed, \
        "the PNG icons are not installed into per-size hicolor directories"
    assert 'RENAME "qsanguosha.png"' in installed, \
        "the installed icons must all be named after the desktop entry's Icon key"
    print("PASS test_linux_icons_are_real_pngs_at_every_declared_size")


def test_desktop_entry_is_valid_and_matches_the_installed_icon_name() -> None:
    sys.path.insert(0, str(PACKAGING))
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "validate_desktop_entry", PACKAGING / "validate-desktop-entry.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    problems = module.validate(DESKTOP)
    assert not problems, f"the shipped desktop entry does not validate: {problems}"

    entries = dict(
        line.split("=", 1) for line in read(DESKTOP).splitlines()
        if "=" in line and not line.startswith("[")
    )
    # Icon must be the themed name the install rules rename every size to;
    # any other value and the launcher shows a generic placeholder.
    assert entries["Icon"] == "qsanguosha", \
        f"Icon={entries['Icon']} does not match the installed icon name 'qsanguosha'"
    assert entries["Exec"].split()[0] == "QSanguosha", \
        "Exec must name the installed binary"
    print("PASS test_desktop_entry_is_valid_and_matches_the_installed_icon_name")


def test_manifest_template_and_cmake_agree() -> None:
    """The manifest has to describe what was installed, not a wish list.

    The path lists live in CMakeLists.txt precisely so a server-only install
    cannot claim to ship GUI assets; the template must therefore stay a pure
    substitution and never grow a hard-coded list of its own.
    """
    template = read(MANIFEST_TEMPLATE)
    assert "@QSAN_ASSET_REQUIRED_JSON@" in template
    assert "@QSAN_ASSET_OPTIONAL_JSON@" in template
    assert "@QSAN_GAME_VERSION@" in template
    assert "@QSAN_ASSET_PACK_VERSION@" in template

    rendered = (template
                .replace("@QSAN_GAME_VERSION@", "20251231")
                .replace("@QSAN_ASSET_PACK_VERSION@", "core-1")
                .replace("@QSAN_ASSET_REQUIRED_JSON@", '"lua/config.lua"')
                .replace("@QSAN_ASSET_OPTIONAL_JSON@", '"image"'))
    payload = json.loads(rendered)
    assert payload["schema_version"] == 1

    cmake = read(CMAKELISTS)
    required = re.search(r"set\(QSAN_ASSET_REQUIRED\n(.*?)\n    \)", cmake, re.S)
    optional = re.search(r"set\(QSAN_ASSET_OPTIONAL\n(.*?)\n    \)", cmake, re.S)
    assert required and optional, "the manifest path lists are not in CMakeLists.txt"
    required_paths = required.group(1).split()
    optional_paths = optional.group(1).split()

    # The engine exits from its own constructor without these, so calling them
    # optional would produce a package that installs cleanly and never starts.
    for path in ("lua/config.lua", "lua/sanguosha.lua"):
        assert path in required_paths, f"{path} must be a required asset"
    # These are gigabytes that are deliberately not in the repository; a
    # packaging job must never fail because they are absent.
    for path in ("image", "audio", "font"):
        assert path in optional_paths, f"{path} must stay optional"
        assert path not in required_paths, f"{path} must not be required"

    gui_only = re.search(r"if\(QSAN_BUILD_GUI\)\n        list\(APPEND QSAN_ASSET_REQUIRED\n(.*?)\n        \)",
                         cmake, re.S)
    assert gui_only, "GUI-only assets are not gated on QSAN_BUILD_GUI"
    for path in gui_only.group(1).split():
        assert path not in required_paths, \
            f"{path} is GUI-only but is also required for a server-only install"
    print("PASS test_manifest_template_and_cmake_agree")


def test_installed_layout_beats_the_working_directory() -> None:
    """The whole point of M3: a package must not use whatever is in the CWD.

    Before M3 the game required `CWD == repository root`.  If the working
    directory were tried before the installed layout, an installed build started
    from inside an old checkout would silently load that checkout's data.
    """
    resolver = read(RESOLVER)
    order = [
        resolver.index('QStringLiteral("installed-prefix")'),
        resolver.index('QStringLiteral("portable-bundle")'),
        resolver.index('QStringLiteral("working-directory")'),
    ]
    assert order == sorted(order), \
        "the installed and portable layouts must be resolved before the working directory"
    assert "QDir::setCurrent(g_resolution.assetRoot)" in resolver, \
        "the resolver must move the working directory to the resolved asset root"
    print("PASS test_installed_layout_beats_the_working_directory")


def test_packaged_installs_never_write_into_themselves() -> None:
    """/usr/share and an AppImage's squashfs are read-only.

    Replays, AI data and custom scenarios have to go to the user's own
    directory, or they are lost silently - the write simply fails.
    """
    resolver = read(RESOLVER)
    packaged = re.search(r"bool assetRootIsPackaged\(AssetRootSource source\)\n\{(.*?)\n\}",
                         resolver, re.S)
    assert packaged, "the packaged-root classification is missing"
    body = packaged.group(1)
    for source in ("CommandLine", "Environment", "InstalledPrefix", "PortableBundle"):
        assert source in body.split("return false")[0], \
            f"{source} must be classified as a packaged root"

    for path, needle in (
        (ROOT / "src" / "server" / "room.cpp", "QSanRuntimePaths::recordDir()"),
        (ROOT / "src" / "client" / "client.cpp", "QSanRuntimePaths::recordDir()"),
        (ROOT / "src" / "ui" / "roomscene.cpp", "QSanRuntimePaths::recordDir()"),
        (ROOT / "src" / "core" / "ai-data-store.cpp", "aiDataWritePath()"),
    ):
        assert needle in read(path), f"{path.name} still writes replays/AI data by hand"
        assert 'QDir::currentPath()+"/record"' not in read(path), \
            f"{path.name} still writes into the working directory"
    print("PASS test_packaged_installs_never_write_into_themselves")


def test_install_directory_rules_tolerate_an_incomplete_build_context() -> None:
    """`install(DIRECTORY)` is fatal when the source directory is absent.

    The Docker server image's .dockerignore deliberately strips the GUI/client
    asset directories (lang/, qss/, skins/, ui-script/) from the build context,
    and extensions/ is fetched during the build rather than copied in.  Naming
    such a directory directly in an install rule turns a deliberately slim
    context into a hard CMake error halfway through installing.  Directories
    that may be absent must be collected behind an EXISTS check, so a missing
    one is reported by the asset manifest - loudly, and at a useful moment -
    instead of aborting the install.
    """
    cmake = read(CMAKELISTS)
    guarded_lists = {"${QSAN_DATA_DIRECTORIES}", "${QSAN_GUI_DATA_DIRECTORIES}"}
    for match in re.finditer(r"install\(DIRECTORY ([^\n]+)", cmake):
        argument = match.group(1).strip()
        assert argument in guarded_lists, (
            f"install(DIRECTORY {argument} ...) names directories directly; collect "
            "them behind an EXISTS check instead, or a build context without one "
            "of them fails the install"
        )

    # Each collector must actually test for existence before appending.
    for variable in ("QSAN_DATA_DIRECTORIES", "QSAN_GUI_DATA_DIRECTORIES"):
        block = re.search(
            rf"foreach\((\w+) [^)]*\)\s*\n\s*if\(EXISTS [^)]*\)\s*\n\s*"
            rf"list\(APPEND {variable} ", cmake)
        assert block, f"{variable} is not built from an EXISTS check"
    print("PASS test_install_directory_rules_tolerate_an_incomplete_build_context")


def test_deploy_never_bundles_host_libraries() -> None:
    """Bundling the host's glibc/libGL/libX11 is what breaks portable builds.

    The deploy tool's rule is 'only what lives inside the Qt prefix'; the audit
    then re-checks the artifact independently, so a mistake has to get past both.
    """
    deploy = read(PACKAGING / "deploy-linux.py")
    assert "resolve_library(soname, qt_lib)" in deploy, \
        "the deploy tool must resolve dependencies inside the Qt prefix only"
    assert 'report["system_libraries"] = sorted(system)' in deploy, \
        "host libraries must be recorded rather than silently dropped"

    audit = read(PACKAGING / "audit-bundle.py")
    for library in ("libc.so.", "libstdc++.so.", "libGL.so.", "libX11.so.", "libfreetype.so."):
        assert library in audit, f"the audit does not reject a bundled {library}"
    assert "not entry.startswith(\"$ORIGIN\")" in audit, \
        "the audit does not reject absolute RPATH entries"
    print("PASS test_deploy_never_bundles_host_libraries")


def test_private_qt_mirrors_the_qt_prefix_layout() -> None:
    """Qt's own libraries carry $ORIGIN-relative RUNPATHs that assume it.

    libqxcb.so has RUNPATH `$ORIGIN/../../lib`, a nested QML plugin has
    `$ORIGIN/../../../../lib`, and so on.  Reproducing Qt's prefix layout makes
    all of those correct for free; any other layout would need patchelf to
    rewrite every Qt binary.
    """
    cmake = read(CMAKELISTS)
    assert 'set(QSAN_PRIVATE_QT_DIR "${CMAKE_INSTALL_LIBDIR}/qsanguosha/qt")' in cmake
    assert "qsanguosha/qt/lib" in cmake, "the RPATH does not point at the private Qt lib dir"
    assert 'INSTALL_RPATH "$ORIGIN/${QSAN_BIN_TO_QT_LIB_RELATIVE}"' in cmake, \
        "the installed binaries must use an $ORIGIN-relative RPATH"

    deploy = read(PACKAGING / "deploy-linux.py")
    assert 'default="lib/qsanguosha/qt"' in deploy
    for subdirectory in ('private / "lib"', 'private / "plugins"', 'private / "qml"'):
        assert subdirectory in deploy, f"the private runtime is missing {subdirectory}"
    print("PASS test_private_qt_mirrors_the_qt_prefix_layout")


def test_qml_and_plugins_are_deployed_explicitly() -> None:
    """`ldd` cannot see a dlopen.

    Platform, image-format, TLS and multimedia plugins plus every QML module are
    loaded by name at runtime.  A bundle built from dependency walking alone
    starts and then fails the moment the home scene or audio is touched.
    """
    deploy = read(PACKAGING / "deploy-linux.py")
    for group in ("platforms", "imageformats", "multimedia", "tls", "iconengines"):
        assert f'"{group}"' in deploy, f"the {group} plugin group is not deployed"
    assert "qmlimportscanner" in deploy, "QML modules must be resolved with Qt's own scanner"
    assert "EXTRA_QML_MODULES" in deploy, \
        "modules selected at runtime (QQuickStyle) need an explicit list"
    print("PASS test_qml_and_plugins_are_deployed_explicitly")


def test_release_metadata_carries_no_developer_identity() -> None:
    builder = read(PACKAGING / "build-linux-packages.py")
    assert '"sha256": sha256(path)' in builder, "artifacts must be checksummed"
    assert 'checksums.write_text' in builder, "SHA256SUMS must be produced"
    for field in ("game_version", "qt_version", "build_type", "compiler", "created",
                  "asset_manifest"):
        assert f'"{field}"' in builder, f"build-info.json is missing {field}"
    # Whatever else changes, these must never be written into a published file.
    for leak in ("getpass", "os.getlogin", "expanduser", "str(arguments.qt_prefix)",
                 "str(arguments.build_dir)"):
        assert leak not in builder.split("build_info = {")[1].split("info_path")[0], \
            f"build-info.json must not record {leak}"
    print("PASS test_release_metadata_carries_no_developer_identity")


def test_linux_packaging_rules_stay_out_of_the_windows_build() -> None:
    """Windows deploys with windeployqt and has no $ORIGIN.

    Every install rule, RPATH and relative-data-path computation added for M3
    has to sit inside an `if(UNIX ...)` block, or a Windows configure would try
    to apply GNUInstallDirs semantics that mean nothing there.
    """
    lines = read(CMAKELISTS).splitlines()
    guarded: list[tuple[int, str]] = []
    depth_stack: list[bool] = []
    unix_depth = 0
    for number, line in enumerate(lines, start=1):
        stripped = line.strip()
        if stripped.startswith("if("):
            is_unix = stripped.startswith("if(UNIX")
            depth_stack.append(is_unix)
            if is_unix:
                unix_depth += 1
        elif stripped.startswith("endif()") and depth_stack:
            if depth_stack.pop():
                unix_depth -= 1
        if unix_depth == 0:
            if not stripped.startswith("#") and any(
                    needle in line for needle in
                    ("INSTALL_RPATH", "$ORIGIN", "QSAN_BIN_TO_DATA_RELATIVE",
                     "QSAN_PRIVATE_QT_DIR", "CMAKE_INSTALL_FULL_")):
                guarded.append((number, stripped))
    assert not guarded, \
        f"Linux packaging rules must be inside if(UNIX ...): {guarded}"

    # The Windows deployment path must stay exactly as it was.
    cmake = read(CMAKELISTS)
    assert "QSAN_WINDEPLOYQT" in cmake and "cmake/Deploy.cmake" in cmake, \
        "the Windows windeployqt deployment target was removed"
    assert "VS_DEBUGGER_WORKING_DIRECTORY" in cmake, \
        "the Visual Studio debugger working directory was removed"
    assert "QSAN_FMOD_RUNTIME" in cmake, "the Windows FMOD deployment was removed"
    print("PASS test_linux_packaging_rules_stay_out_of_the_windows_build")


def test_packaging_workflow_pins_actions_and_smokes_the_artifact() -> None:
    assert PACKAGE_WORKFLOW.is_file(), "the packaging workflow is missing"
    workflow = read(PACKAGE_WORKFLOW)

    for uses in re.findall(r"uses:\s*(\S+)", workflow):
        assert "@" in uses, f"unpinned action: {uses}"
        ref = uses.split("@", 1)[1]
        assert re.fullmatch(r"[0-9a-f]{40}", ref), \
            f"actions must be pinned to a full commit SHA, got {uses}"

    # The point of the workflow is testing the artifact, not the build tree.
    assert "linux-package-smoke.sh" in workflow, \
        "the workflow must smoke-test the package artifacts"
    assert "--appimage-extract" in workflow, \
        "the AppImage must be verified without FUSE, which CI containers lack"
    assert "audit-bundle.py" in workflow, "the workflow must audit the bundle"
    assert "SHA256SUMS" in workflow, "the workflow must publish checksums"
    # Packaging is expensive; it must not run on every source edit.
    assert "workflow_dispatch:" in workflow, "the workflow must be dispatchable"
    assert "paths:" in workflow, "the workflow must be path-filtered"
    print("PASS test_packaging_workflow_pins_actions_and_smokes_the_artifact")


def test_packaging_documentation_exists_and_states_the_deb_decision() -> None:
    """A deferred `.deb` has to say so in writing.

    Ubuntu 24.04 ships Qt 6.4.2 and the GUI baseline is 6.11.1; a .deb that
    claims to support it and then cannot start is worse than no .deb at all.
    """
    docs = read(DOCS)
    for needle in ("--asset-root", "QSAN_ASSET_ROOT", "tar.zst", "AppImage",
                   "assets-manifest.json", "SHA256SUMS"):
        assert needle in docs, f"the packaging docs do not mention {needle}"
    assert "DEB STATUS" in docs, "the .deb decision must be recorded"
    assert "M3.1" in docs, "a deferred .deb must name its follow-up milestone"
    print("PASS test_packaging_documentation_exists_and_states_the_deb_decision")


def main() -> int:
    tests = (
        test_linux_icons_are_real_pngs_at_every_declared_size,
        test_desktop_entry_is_valid_and_matches_the_installed_icon_name,
        test_manifest_template_and_cmake_agree,
        test_installed_layout_beats_the_working_directory,
        test_packaged_installs_never_write_into_themselves,
        test_install_directory_rules_tolerate_an_incomplete_build_context,
        test_deploy_never_bundles_host_libraries,
        test_private_qt_mirrors_the_qt_prefix_layout,
        test_qml_and_plugins_are_deployed_explicitly,
        test_release_metadata_carries_no_developer_identity,
        test_linux_packaging_rules_stay_out_of_the_windows_build,
        test_packaging_workflow_pins_actions_and_smokes_the_artifact,
        test_packaging_documentation_exists_and_states_the_deb_decision,
    )
    for test in tests:
        test()
    return 0


if __name__ == "__main__":
    sys.exit(main())
