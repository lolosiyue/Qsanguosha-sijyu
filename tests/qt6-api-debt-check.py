#!/usr/bin/env python3
import argparse
from pathlib import Path
import re
import sys


FORBIDDEN = {
    "Qt5 random function": re.compile(r"\bqrand\b"),
    "Qt5 random seed function": re.compile(r"\bqsrand\b"),
    "Qt5-style shuffle helper": re.compile(r"\bqShuffle\b"),
    "deprecated regular expression": re.compile(r"\bQRegExp\b"),
    "legacy enum macro": re.compile(r"\bQ_ENUMS\b"),
    "deprecated text codec": re.compile(r"\bQTextCodec\b"),
    "Qt Core5Compat dependency": re.compile(r"\bCore5Compat\b"),
}


def source_files(root: Path):
    for base in (root / "src", root / "tests", root / "cmake"):
        for path in base.rglob("*"):
            if path.suffix in {".cpp", ".h", ".cmake"} or path.name == "CMakeLists.txt":
                yield path
    yield root / "CMakeLists.txt"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    root = parser.parse_args().root.resolve()
    failures = []

    for path in source_files(root):
        text = path.read_text(encoding="utf-8")
        for label, pattern in FORBIDDEN.items():
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                failures.append(f"{path.relative_to(root)}:{line}: {label}")

        if path.relative_to(root).as_posix() != "src/ui/ui-rng.cpp":
            for match in re.finditer(r"QRandomGenerator::global\s*\(", text):
                line = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{path.relative_to(root)}:{line}: UI RNG bypasses UiRng"
                )

    if failures:
        print("Qt6 API debt check failed:")
        print("\n".join(failures))
        return 1
    print("QT6_API_DEBT_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
