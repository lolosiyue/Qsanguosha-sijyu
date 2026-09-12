#!/usr/bin/env python3
"""fetch-extensions.sh must copy the extensions repository's whole .lua tree.

The repository keeps runtime content in subdirectories (extensions/temp, ai/isolated,
ai/temp).  A copy that only takes the top level still reports success, and the loss shows
up much later as an engine bootstrap failure:

    Lua script error: lua/sanguosha.lua extensions/gaoda.lua:19598:
    attempt to concatenate a nil value (field '?')

so this checks the fetch itself against a fixture repository.
"""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "tools" / "ci" / "fetch-extensions.sh"

# Fixture repository content; the script has to bring every one of these across.
FIXTURE_FILES = {
    "ai/smart-ai.lua": 'local sl = "x"\ndofile("lua/ai/"..sl)\n',
    "ai/isolated-bootstrap.lua": "-- bootstrap\n",
    "ai/isolated-facades.lua": "-- facades\n",
    "ai/isolated/ask-for-use-card.lua": "-- isolated\n",
    "ai/temp/glory-ai.lua": "-- nested ai\n",
    "extensions/standard.lua": "-- extension\n",
    "extensions/temp/extraheg.lua": "-- nested extension\n",
    "lua/luaoldenemy_lib.lua": "-- lua lib\n",
}

# Where each fixture file has to land under the checkout root.
EXPECTED = {
    "ai/smart-ai.lua": "lua/ai/smart-ai.lua",
    "ai/isolated-bootstrap.lua": "lua/ai/isolated-bootstrap.lua",
    "ai/isolated-facades.lua": "lua/ai/isolated-facades.lua",
    "ai/isolated/ask-for-use-card.lua": "lua/ai/isolated/ask-for-use-card.lua",
    "ai/temp/glory-ai.lua": "lua/ai/temp/glory-ai.lua",
    "extensions/standard.lua": "extensions/standard.lua",
    "extensions/temp/extraheg.lua": "extensions/temp/extraheg.lua",
    "lua/luaoldenemy_lib.lua": "lua/luaoldenemy_lib.lua",
}


def git(*arguments: str, cwd: pathlib.Path) -> None:
    subprocess.run(["git", *arguments], cwd=cwd, check=True, capture_output=True, text=True)


def build_fixture_repository(path: pathlib.Path) -> None:
    path.mkdir(parents=True)
    git("init", "--quiet", "--initial-branch", "main", cwd=path)
    git("config", "user.email", "fixture@example.invalid", cwd=path)
    git("config", "user.name", "fixture", cwd=path)
    for relative, content in FIXTURE_FILES.items():
        target = path / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")
    git("add", "-A", cwd=path)
    git("commit", "--quiet", "-m", "fixture", cwd=path)


def main() -> int:
    if not SCRIPT.is_file():
        print("missing %s" % SCRIPT, file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory() as directory:
        base = pathlib.Path(directory)
        repository = base / "extensions-repo"
        checkout = base / "checkout"
        checkout.mkdir()
        build_fixture_repository(repository)

        environment = dict(os.environ)
        environment["QSAN_EXTENSIONS_REPO"] = repository.as_uri()
        environment["QSAN_EXTENSIONS_REF"] = "main"
        environment.pop("GITHUB_ENV", None)
        result = subprocess.run(["bash", str(SCRIPT), str(checkout)], env=environment,
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stdout + result.stderr, file=sys.stderr)
            print("FETCH_EXTENSIONS_RESULT FAIL (script exited %d)" % result.returncode)
            return 1

        problems = []
        for source, destination in EXPECTED.items():
            if not (checkout / destination).is_file():
                problems.append("%s did not reach %s" % (source, destination))
        # The AI loader patch has to survive the copy, or mixed-case packages break on
        # case-sensitive filesystems.
        smart_ai = checkout / "lua" / "ai" / "smart-ai.lua"
        if smart_ai.is_file() and '"lua/ai/"..ai_file' not in smart_ai.read_text(encoding="utf-8"):
            problems.append("smart-ai.lua lost the mixed-case AI filename patch")

        for problem in problems:
            print("  - " + problem)
        print("FETCH_EXTENSIONS_RESULT %s" % ("FAIL" if problems else "PASS"))
        return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
