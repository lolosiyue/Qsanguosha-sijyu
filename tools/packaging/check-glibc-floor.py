#!/usr/bin/env python3
"""Fail when a Linux executable needs a newer glibc than the release floor.

    check-glibc-floor.py --max 2.38 <elf>...

Linking on a new distribution silently binds the newest symbol version of whatever
the code calls: glibc 2.43 re-versioned acosf, atan2f and sqrtf, so a GUI linked
there refuses to start on Ubuntu 24.04 with "version `GLIBC_2.43' not found" although
nothing else needs more than 2.38.  src/util/linux-glibc-compat.c pins the known
cases; this check names the next symbol that needs pinning.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys

SYMBOL_VERSION = re.compile(r"\(?GLIBC_([0-9]+(?:\.[0-9]+)+)\)?\s+(\S+)\s*$")


def version_key(text: str) -> tuple[int, ...]:
    return tuple(int(part) for part in text.split("."))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--max", required=True, help="newest allowed GLIBC_x.y, e.g. 2.38")
    parser.add_argument("binaries", nargs="+")
    arguments = parser.parse_args(argv)

    objdump = shutil.which("objdump")
    if objdump is None:
        print("check-glibc-floor: objdump not found (binutils)", file=sys.stderr)
        return 2

    limit = version_key(arguments.max)
    failed = False
    for binary in arguments.binaries:
        output = subprocess.run([objdump, "-T", binary], check=True,
                                capture_output=True, text=True).stdout
        newest = (0,)
        offenders: dict[str, set[str]] = {}
        for line in output.splitlines():
            match = SYMBOL_VERSION.search(line)
            if not match:
                continue
            version = version_key(match.group(1))
            newest = max(newest, version)
            if version > limit:
                offenders.setdefault(match.group(1), set()).add(match.group(2))
        shown = ".".join(str(part) for part in newest)
        if offenders:
            failed = True
            print("%s: needs GLIBC_%s, above the %s floor" % (binary, shown, arguments.max))
            for version in sorted(offenders, key=version_key):
                print("  GLIBC_%s: %s" % (version, " ".join(sorted(offenders[version]))))
        else:
            print("%s: GLIBC_%s (floor %s) OK" % (binary, shown, arguments.max))
    if failed:
        print("pin the listed symbols in src/util/linux-glibc-compat.c", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
