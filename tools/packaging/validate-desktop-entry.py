#!/usr/bin/env python3
"""Validate a .desktop entry against the freedesktop Desktop Entry Specification.

`desktop-file-validate` is the reference implementation and CI runs it, but it
is not installed everywhere a developer works.  This covers the rules that
actually break desktop integration, so a broken entry is caught before the
package is built rather than after a player installs it:

  * the file starts with a single [Desktop Entry] group;
  * required keys are present and Type=Application implies Exec;
  * Categories and Keywords are semicolon-terminated lists, and Categories
    contains at least one registered main category;
  * Icon is a plain name (not a path, not an extension) so the icon theme can
    resolve it at every size;
  * boolean keys are exactly "true"/"false"; and
  * localised keys use the Key[locale] form and have an unlocalised default.

Usage:
    python3 tools/packaging/validate-desktop-entry.py share/applications/x.desktop
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

REQUIRED_KEYS = ("Type", "Name")
BOOLEAN_KEYS = ("Terminal", "StartupNotify", "NoDisplay", "Hidden", "DBusActivatable",
                "SingleMainWindow", "PrefersNonDefaultGPU")
LIST_KEYS = ("Categories", "Keywords", "MimeType", "OnlyShowIn", "NotShowIn",
             "Implements", "Actions")
MAIN_CATEGORIES = {
    "AudioVideo", "Audio", "Video", "Development", "Education", "Game", "Graphics",
    "Network", "Office", "Science", "Settings", "System", "Utility",
}
KNOWN_TYPES = {"Application", "Link", "Directory"}
ENTRY_RE = re.compile(r"^(?P<key>[A-Za-z0-9-]+)(?:\[(?P<locale>[^\]]+)\])?\s*=\s*(?P<value>.*)$")


def validate(path: pathlib.Path) -> list[str]:
    problems: list[str] = []
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return [f"{path}: the file is not valid UTF-8"]

    lines = text.splitlines()
    if not lines:
        return [f"{path}: the file is empty"]

    groups: list[str] = []
    current: str | None = None
    entries: dict[str, dict[str | None, str]] = {}

    for number, raw in enumerate(lines, start=1):
        line = raw.rstrip("\r")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line.startswith("["):
            if not line.endswith("]"):
                problems.append(f"{path}:{number}: malformed group header: {line}")
                continue
            current = line[1:-1]
            groups.append(current)
            continue
        if current is None:
            problems.append(f"{path}:{number}: entry before the first group header")
            continue
        match = ENTRY_RE.match(line)
        if match is None:
            problems.append(f"{path}:{number}: not a key=value entry: {line}")
            continue
        if line != line.lstrip():
            problems.append(f"{path}:{number}: leading whitespace before a key")
        entries.setdefault(current, {})
        key = match["key"]
        locale = match["locale"]
        bucket = entries[current]
        if (key, locale) in bucket:
            problems.append(f"{path}:{number}: duplicate key {key}")
        bucket[(key, locale)] = match["value"]

    if not groups or groups[0] != "Desktop Entry":
        problems.append(f"{path}: the first group must be [Desktop Entry]")
        return problems

    main = entries.get("Desktop Entry", {})
    values = {key: value for (key, locale), value in main.items() if locale is None}

    for key in REQUIRED_KEYS:
        if key not in values:
            problems.append(f"{path}: required key '{key}' is missing")

    entry_type = values.get("Type", "")
    if entry_type and entry_type not in KNOWN_TYPES:
        problems.append(f"{path}: unknown Type '{entry_type}'")
    if entry_type == "Application" and "Exec" not in values:
        problems.append(f"{path}: Type=Application requires Exec")

    for key in BOOLEAN_KEYS:
        if key in values and values[key] not in ("true", "false"):
            problems.append(
                f"{path}: {key} must be exactly 'true' or 'false', got {values[key]!r}")

    for key in LIST_KEYS:
        if key in values and values[key] and not values[key].endswith(";"):
            problems.append(f"{path}: {key} is a list and must end with ';'")

    categories = [item for item in values.get("Categories", "").split(";") if item]
    if "Categories" in values and not (set(categories) & MAIN_CATEGORIES):
        problems.append(
            f"{path}: Categories has no registered main category (got {categories})")

    icon = values.get("Icon", "")
    if icon:
        if "/" in icon:
            problems.append(
                f"{path}: Icon should be a themed name, not a path ({icon}); a path "
                "cannot be resolved per size by the icon theme")
        elif pathlib.PurePath(icon).suffix in (".png", ".svg", ".xpm", ".ico"):
            problems.append(f"{path}: Icon must not carry a file extension ({icon})")

    for (key, locale) in main:
        if locale is not None and (key, None) not in main:
            problems.append(
                f"{path}: {key}[{locale}] has no unlocalised {key} to fall back to")

    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=pathlib.Path)
    arguments = parser.parse_args(argv)

    problems: list[str] = []
    for path in arguments.files:
        if not path.is_file():
            problems.append(f"{path}: no such file")
            continue
        problems.extend(validate(path))

    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    for path in arguments.files:
        print(f"{path}: desktop entry OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
