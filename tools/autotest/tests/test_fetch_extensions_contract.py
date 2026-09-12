#!/usr/bin/env python3
"""fetch-extensions.sh must copy the AI tree whole and the extension tree flat.

The repository keeps AI content in subdirectories (ai/isolated, ai/temp) and a copy
that only takes the top level loses it; the loss shows up much later as an engine
bootstrap failure:

    Lua script error: lua/sanguosha.lua extensions/gaoda.lua:19598:
    attempt to concatenate a nil value (field '?')

Extensions are the opposite case.  lua/config.lua's extension_names is the only
thing the loader reads, and the Web admission gate (declared-v1) rejects the whole
bundle when any undeclared .lua sits under extensions/ or lua/ - see
contentScanIsDeclared() in src/core/rules-bundle-exporter.cpp.  Copying
extensions/temp therefore does not add content the engine can use, it only makes
the server answer rules_bundle={"error_code": "rules_content_unsupported"} and
refuse every Web client.  So this checks the fetch itself against a fixture
repository, in both directions.
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
    "extensions/temp/extraheg.lua": "-- undeclared nested extension\n",
    "lua/luaoldenemy_lib.lua": "-- lua lib\n",
    "lua/nested/extra.lua": "-- undeclared nested lua\n",
}

# Where each fixture file has to land under the checkout root.
EXPECTED = {
    "ai/smart-ai.lua": "lua/ai/smart-ai.lua",
    "ai/isolated-bootstrap.lua": "lua/ai/isolated-bootstrap.lua",
    "ai/isolated-facades.lua": "lua/ai/isolated-facades.lua",
    "ai/isolated/ask-for-use-card.lua": "lua/ai/isolated/ask-for-use-card.lua",
    "ai/temp/glory-ai.lua": "lua/ai/temp/glory-ai.lua",
    "extensions/standard.lua": "extensions/standard.lua",
    "lua/luaoldenemy_lib.lua": "lua/luaoldenemy_lib.lua",
}

# Undeclared content the declared-v1 scan would reject; it must stay out of the tree.
# lua/ai/ is the one subtree the scan exempts, which is why ai/ is copied whole.
FORBIDDEN = {
    "extensions/temp/extraheg.lua": "extensions/temp/extraheg.lua",
    "lua/nested/extra.lua": "lua/nested/extra.lua",
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
        for source, destination in FORBIDDEN.items():
            if (checkout / destination).exists():
                problems.append("%s was copied to %s; declared-v1 rejects it" % (source, destination))
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
