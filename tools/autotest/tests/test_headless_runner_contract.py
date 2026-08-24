from __future__ import annotations

import csv
import json
import os
import subprocess
import sys
from pathlib import Path


RUNNER = Path(__file__).parents[1] / "headless_runner.py"
SCHEMA = [
    "mode",
    "game",
    "ok",
    "exit",
    "exit_name",
    "timeout",
    "crashed",
    "finished",
    "expected",
    "failed_games",
    "done_marker",
    "marker_count",
    "marker_status",
    "marker_fields",
    "marker_failure",
    "log",
]

GAUGE_FIELDS = (
    "managed_live",
    "pending_delete",
    "adoption_reserved",
    "wrapper_leases",
    "native_leases",
    "lua_pins",
    "sidecar_edges",
    "entries",
    "active_scopes",
)


def write_fixture(
    directory: Path,
    *,
    games: int,
    marker_mode: str,
    exit_code: int,
) -> Path:
    child = directory / "fake_headless.py"
    child.write_text(
        (
            "from pathlib import Path\n"
            "import json\n"
            "import os\n"
            "import sys\n"
            "args = sys.argv[1:]\n"
            "log = Path(args[args.index('--headless-log') + 1])\n"
            "log.parent.mkdir(parents=True, exist_ok=True)\n"
            "games = int(args[args.index('--games') + 1])\n"
            "mode = os.environ.get('FAKE_HEADLESS_MODE', 'valid')\n"
            "fields = {name: 0 for name in "
            f"{GAUGE_FIELDS!r}"
            "}\n"
            "lines = ['>>> Headless stress test started - fake\\n']\n"
            "for game in range(1, games + 1):\n"
            "    lines.append(f'>>> Starting headless game {game} <<<\\n')\n"
            "    marker = {'event': '[CardLifetime] FINAL_GAUGE', 'game': game, "
            "'domain': f'room-{game}', **fields}\n"
            "    if mode == 'nonzero':\n"
            "        marker['managed_live'] = 1\n"
            "    if mode == 'runtime-delta':\n"
            "        marker['runtime_delta'] = {'managed_live': 1}\n"
            "    if mode == 'wrong-game':\n"
            "        marker['game'] = game + 1\n"
            "    if mode == 'early':\n"
            "        lines.append('CARD_LIFETIME_ZERO ' + json.dumps(marker) + '\\n')\n"
            "    lines.append(f'>>> Game {game} finished. Winner: fake <<<\\n')\n"
            "    if mode == 'malformed':\n"
            "        lines.append('CARD_LIFETIME_ZERO {not-json\\n')\n"
            "    elif mode != 'none':\n"
            "        lines.append('CARD_LIFETIME_ZERO ' + json.dumps(marker) + '\\n')\n"
            "        if mode == 'duplicate':\n"
            "            lines.append('CARD_LIFETIME_ZERO ' + json.dumps(marker) + '\\n')\n"
            "log.write_text(''.join(lines) + '>>> All games completed. Exiting. <<<\\n', encoding='utf-8')\n"
            "Path(log.parent / 'argv.txt').write_text('\\n'.join(args), encoding='utf-8')\n"
            f"raise SystemExit({exit_code})\n"
        ),
        encoding="utf-8",
    )
    launcher = directory / "fake_headless.cmd"
    launcher.write_text(
        f'@"{sys.executable}" "%~dp0fake_headless.py" %*\n',
        encoding="utf-8",
    )
    return launcher


def invoke(
    tmp_path: Path,
    *extra: str,
    games: int = 1,
    marker_mode: str = "valid",
    exit_code: int = 0,
) -> subprocess.CompletedProcess[str]:
    exe = write_fixture(
        tmp_path,
        games=games,
        marker_mode=marker_mode,
        exit_code=exit_code,
    )
    output = tmp_path / "summary.csv"
    output.write_text("stale output that must be replaced\n", encoding="utf-8")
    command = [
        sys.executable,
        str(RUNNER),
        "--exe",
        str(exe),
        "--mode",
        "20p",
        "--seed",
        "4294967295",
        "--repeat",
        str(games),
        "--output",
        str(output),
        *extra,
    ]
    environment = dict(os.environ)
    environment["FAKE_HEADLESS_MODE"] = marker_mode
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        check=False,
        env=environment,
    )


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        assert reader.fieldnames == SCHEMA
        return list(reader)


def test_requires_explicit_exe_and_seed(tmp_path: Path) -> None:
    command = [sys.executable, str(RUNNER), "--mode", "20p"]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    assert result.returncode != 0
    assert "--exe" in result.stderr


def test_rejects_signed_or_overflow_seed(tmp_path: Path) -> None:
    for seed in ("-1", "4294967296"):
        result = invoke(tmp_path, "--seed", seed)
        assert result.returncode != 0
        assert "unsigned" in result.stderr.lower()


def test_rejects_malformed_seed_before_starting_child(tmp_path: Path) -> None:
    result = invoke(tmp_path, "--seed", "not-a-seed")

    assert result.returncode != 0
    assert "argument --seed" in result.stderr
    assert "unsigned 32-bit integer" in result.stderr
    assert not (tmp_path / "argv.txt").exists()
    assert (tmp_path / "summary.csv").read_text(encoding="utf-8").replace(
        "\r\n", "\n"
    ) == "stale output that must be replaced\n"


def test_rejects_synthetic_actor_counts_as_product_modes(tmp_path: Path) -> None:
    for mode in ("30p", "50p"):
        result = invoke(tmp_path, "--mode", mode)
        assert result.returncode != 0
        assert mode in result.stderr


def test_validates_one_final_gauge_per_game_and_persists_zero_fields(
    tmp_path: Path,
) -> None:
    result = invoke(tmp_path, games=5)
    assert result.returncode == 0, result.stderr
    rows = read_rows(tmp_path / "summary.csv")
    assert len(rows) == 5
    assert [row["game"] for row in rows] == ["1", "2", "3", "4", "5"]
    for row in rows:
        assert row["ok"] == "True"
        assert row["marker_count"] == "1"
        assert row["marker_status"] == "valid"
        assert row["marker_failure"] == ""
        assert all(
            value == 0
            for value in json.loads(row["marker_fields"]).values()
        )


def test_rejects_invalid_final_gauge_cases_with_csv_reason(tmp_path: Path) -> None:
    for mode, reason in (
        ("none", "missing"),
        ("duplicate", "duplicate"),
        ("malformed", "malformed"),
        ("nonzero", "nonzero"),
        ("runtime-delta", "nonzero"),
        ("early", "early"),
        ("wrong-game", "wrong-game"),
    ):
        result = invoke(tmp_path, marker_mode=mode)
        assert result.returncode != 0
        rows = read_rows(tmp_path / "summary.csv")
        assert len(rows) == 1
        assert rows[0]["ok"] == "False"
        assert reason in rows[0]["marker_failure"]


def test_child_exit_failure_remains_a_csv_failure(tmp_path: Path) -> None:
    result = invoke(tmp_path, exit_code=7)
    assert result.returncode != 0
    rows = read_rows(tmp_path / "summary.csv")
    assert len(rows) == 1
    assert rows[0]["ok"] == "False"
    assert rows[0]["exit"] == "7"
    assert rows[0]["marker_status"] == "valid"


def test_forwards_explicit_exe_and_max_seed(tmp_path: Path) -> None:
    result = invoke(tmp_path)
    assert result.returncode == 0, result.stderr
    rows = read_rows(tmp_path / "summary.csv")
    assert str((tmp_path / "fake_headless.cmd").resolve()) in result.stdout
    argv = (Path(rows[0]["log"]).parent / "argv.txt").read_text(
        encoding="utf-8"
    ).splitlines()
    assert "--seed" in argv
    assert argv[argv.index("--seed") + 1] == "4294967295"
