# Native client-rules fixture runner

This is the next verification slice after the shared player model and selection
runtime. `qsanguosha_rules_fixture_runner` links `qsanguosha_client_runtime`
directly, with the existing engine package registrars and reply encoder. It
starts no terminal frontend, GUI, socket, server Room or AI game.

## Build and run

The runner defaults to `BUILD_TESTING`, or can be enabled independently:

```sh
cmake -S . -B build/rules-fixtures -G Ninja \
  -DQSAN_BUILD_GUI=OFF -DQSAN_BUILD_TUI=OFF -DQSAN_BUILD_SERVER=OFF \
  -DBUILD_TESTING=OFF -DQSAN_BUILD_RULES_FIXTURE_RUNNER=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/rules-fixtures --target qsanguosha_rules_fixture_runner
repo="$PWD"
scratch="$(mktemp -d)"
(cd "$scratch" && XDG_CONFIG_HOME="$scratch/config" \
  "$repo/build/rules-fixtures/qsanguosha_rules_fixture_runner" \
  --asset-root "$repo" \
  --fixture "$repo/tests/client_runtime/fixtures/wrapped-wusheng.json" \
  --output "$repo/build/rules-fixtures/wrapped-wusheng.result.json")
```

Use the normal Qt 6 native toolchain and the repository's existing SWIG/Lua
requirements. Multi-config generators place the executable below the chosen
configuration directory. XP and WASM are not supported by this build target yet.

With `BUILD_TESTING=ON` (and the existing required server product enabled), run:

```sh
cmake --build build/debug --target qsanguosha_rules_fixture_runner
ctest --test-dir build/debug -R '^qsanguosha_client_rules_fixture' --output-on-failure
```

`qsanguosha_client_rules_fixtures` runs every checked-in fixture twice in fresh
processes, once with `QT_HASH_SEED=0` and once with the default randomized seed.
It verifies explicit semantic expectations and byte-identical JSON, then checks
six malformed-input/error cases. A mismatch, crash, timeout, missing executable,
missing package, missing output or wrong error diagnostic fails the test; none
is converted into a skip. Outputs and separate stdout/stderr logs are retained
under `<build>/client-rules-fixtures/`.

Each child starts in a temporary working directory with private XDG/AppData
paths. This must happen **before process startup**, because global `Settings
Config` binds its QSettings store before `main`. The runner also isolates its
engine user-data root. For manual Windows runs, likewise launch from an empty
working directory and use absolute fixture/output/asset paths; `--asset-root`
alone does not isolate pre-existing preferences.

The stdlib-only comparator also has nine self-tests, independent of Qt:

```sh
python tests/client_runtime/check-fixtures.py --self-test
python tests/client_runtime/check-fixtures.py --validate-fixtures \
  --fixtures tests/client_runtime/fixtures
```

Those commands test the harness and input/expectation structure, **not** C++
compilation or native game-rule behavior.

## Fixture contract (schema 1)

One file describes one isolated client-visible scene. Required top-level fields
are `schema_version: 1`, `name`, `self`, `setup`, `players`, `cards`, and `queries`.
See the four checked-in fixtures for complete examples.

- `setup` uses the reduced-state representation (`mode`, not a wire packet).
- `players` is an ordered array of `{name, properties}`. Names and positive seat
  numbers must be unique. `self` must appear in this array. `properties` contains
  the existing reduced Player data: seat, general, hp, max_hp, alive, role,
  hand_count, hand_max, phase, skills, flags, marks and dynamic distance values.
  Skills/flags are explicitly converted to QStringList before projection.
  Player identities cannot be overwritten through the property map.
- `cards` maps fixture keys to real registered cards. A selector has `name` and
  optional `suit` and `number`. The first as-yet-unbound match in numeric registry
  order is selected. A missing match fails; the runner never guesses an ID or
  silently substitutes another card. `owner` and `place` populate client-visible
  location data. Supported places are hand, equip, table and discard. Only cards
  described by the fixture get location data; the registry is not another
  player's hidden hand.
- `queries` is an ordered sequence. Each has a unique `name`, `request_id` as a
  positive **decimal string** (up to `18446744073709551615`), `reason` (`play`,
  `response`, `response_use`), optional `pattern`, and exactly one of `card` (a
  fixture key) or `skill` (`name`, optional `instance_id`, ordered `subcards`,
  optional `user_string`). `targets` and `target_pool` preserve their given order
  and repeated names. An omitted target pool uses the fixture player order.
- A query's optional `updates` runs before evaluation. Each entry names a fixture
  `card` and either `reset: true`, or a replacement `name`, `suit`, `number`, with
  optional `skill_name` and `flags`. These go through the actual
  `ClientRoomContext::applyMessage(UPDATE_CARD)` path, not a fixture-specific
  imitation of WrappedCard. Updates persist into subsequent queries.
- Checked-in tests add `expect`, an object subset checked by the Python harness.
  Scalars and types must match exactly; arrays are ordered and exact, including
  duplicate votes. Expectations are not automatically regenerated from actual
  results. The native evaluator ignores this test-only field.

Initial coverage is physical Slash (unfinished, self-target, valid, missing
player state, repeated target), Kongcheng prohibition, Wusheng's black/red/reset
WrappedCard transitions and duplicate subcards, and a borrowed named Tuxi prompt
including zero-card rejection. V2 semantics remain covered by the existing
shared-selection regression suite; this initial fixture set does not claim V2
extension coverage.

## Output and lifecycle

`selection-fixture.cpp` contains a JSON-in/JSON-out function; `main` owns only
file I/O, engine bootstrap and process lifetime. It bounds the input to 1 MiB,
resolves input/output paths before the asset resolver changes CWD, isolates the
engine user-data directory, and writes results using QSaveFile. Bootstrap/Lua
logging may use stdout, so **the output file**, not stdout, is the machine
contract. A failed run does not replace an existing output. Always check exit
status; never accept an old file after a failed run.

Exit codes: 0 success, 2 CLI/JSON/version/input errors, 3 bootstrap/assets errors,
4 fixture/evaluation-contract errors, 5 output errors. Engine faults still
produce a nonzero process status and fail the harness.

The result contains a registry SHA-256, the resolved card-key mapping, and one
entry per query with build status, card text, next target candidates/maxVotes,
known/valid/incomplete target validation and `can_confirm`. A native card is
copied into `preview_card`; allocation-dependent negative virtual IDs are
represented as null. No pointer, memory address, timestamp, localized text or
absolute path appears in the result. Object keys are sorted recursively;
semantically ordered arrays are never sorted or deduplicated.

Only confirmable evaluations produce canonical response data and an encoded
wire payload through the existing InteractionReplyEncoder. Request/reply IDs
stay decimal strings throughout the JSON bridge, including values beyond the
JavaScript safe-integer range. **No reply is sent.** Transient cards are consumed
and serialized before deferred deletion; players/Self are cleared before the
room's wrapped cards, and the engine outlives the scene.

## Deliberate limits

This is a trusted local fixture format, not a new network protocol, production
snapshot import API, or a sandbox for arbitrary third-party scripts. It uses
only the state explicitly supplied to the scene. Its `can_confirm` records the
current shared evaluator's target/build result; it is **not** full server
legality, ownership validation, pattern/availability validation for physical
cards, request freshness, or proof that every player property is projected.
Known projection gaps (including equipment face refresh and structured skill
instance state) are not silently repaired in this PR.

The card-registry hash detects mismatched numeric registries; it is **not** a
complete ruleset, Lua-content, ABI or extension-compatibility hash. Two identical
native outputs do not prove desktop equivalence or query purity. The fixtures
exercise the real runtime, but their adapter still needs comparison against the
desktop client and a real WASM build.

Next: compile this same JSON-in/JSON-out evaluator for WASM and compare fixture
outputs under a matched content build. Worker/session revision handling, Web UI
integration, extension manifests and a complete differential fixture set remain
separate work. No `.wasm` artifact or browser support is claimed here.
