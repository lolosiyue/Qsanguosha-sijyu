# Experimental native / WASM rules fixture probe

This is the next verification slice after the native runner in PR #29. It is
**not a browser client**, a production Worker API, or proof that the complete
engine is already portable. Acceptance requires a real Qt/Emscripten compile,
link and fixture execution. Host-adapter self-tests and CMake syntax/graph
probes do not satisfy that gate.

The 2026-09-08 local probe built both backends with Qt 6.11.1 and executed the
WASM module with Emscripten 4.0.7 / Node 22.23.2. Four scenes / twelve queries
passed in eight fresh positive processes per backend, with byte-identical
native/WASM results and nine expected failures per backend. The checked-in CI
repeats this gate and retains the parity summary, hashes and logs.

## What is shared

`qsanguosha_rules_fixture_wasm` compiles the **unchanged**
`selection-fixture-main.cpp`, `selection-fixture.cpp`, the existing reply encoder,
and `qsanguosha_client_runtime`. Only the CLI entry symbol is renamed at target
compile time (`main=qsan_fixture_main`). A small C export invokes it once with
fixed MEMFS paths. No selection, card, skill, target or wire logic is copied to
JavaScript or Python. No raw Card/Player pointer crosses the boundary.

The CLI's existing stack lifetime still creates/destroys QCoreApplication,
flushes deferred deletes, and shuts down EngineBootstrap. Each fixture runs in a
fresh module and host process. A second invocation of the same module is an
error; this is not a reusable game-session lifecycle API.

The initial host is Node, with a modularized `.mjs` loader plus a `.wasm` binary.
It adds no DOM implementation, WebSocket transport, browser UI, pthread pool
or Node filesystem mount. A successful Node run would prove this fixture path,
not browser/Worker compatibility.

## Build

Baseline: Qt **6.11.1**, Emscripten **4.0.7**, the single-thread WASM Qt kit and a
matching native Qt host-tool installation. Qt Core/Network are still required;
the current root graph also finds Qt WebSockets. Do not mix a native Qt prefix
into the cross compiler's dependency search. The source package inventory and
final engine WHOLE_ARCHIVE policy are retained, with unresolved symbols treated
as errors. Server/platform objects still in the engine must genuinely compile;
they are not replaced with fake-success rule stubs.

Run fixtures with **Node 22+**, whose built-in `navigator.languages` is used by
Qt's system locale. Emscripten's bundled Node 20 remains its compiler helper;
it cannot host this Qt fixture. The host provides a narrow `window` timer bridge
because Qt 6.11.1's `QWasmTimer` hard-codes that name. Its only methods are
`setTimeout` and `clearTimeout`, backed by real Node timers with numeric IDs.
No document, storage, navigator replacement or other window API is supplied.

Build the native comparison binary first in a separate build directory:

```sh
cmake -S . -B build/rules-native -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$QT_NATIVE" \
  -DBUILD_TESTING=OFF -DQSAN_BUILD_GUI=OFF -DQSAN_BUILD_TUI=OFF \
  -DQSAN_BUILD_SERVER=OFF -DQSAN_BUILD_RULES_FIXTURE_RUNNER=ON
cmake --build build/rules-native --target qsanguosha_rules_fixture_runner
```

Then cross-compile (the Qt/Emscripten SDKs must already be installed):

```sh
source "$EMSDK/emsdk_env.sh"
cmake -S . -B build/rules-wasm -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$QT_WASM/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DQT_HOST_PATH="$QT_NATIVE" \
  -DBUILD_TESTING=OFF -DQSAN_BUILD_GUI=OFF -DQSAN_BUILD_TUI=OFF \
  -DQSAN_BUILD_SERVER=OFF -DQSAN_BUILD_RULES_FIXTURE_RUNNER=OFF \
  -DQSAN_BUILD_WASM_RULES_FIXTURES=ON
cmake --build build/rules-wasm --target qsanguosha_rules_fixture_wasm
```

Use the Qt kit's toolchain file: it chains Emscripten and adds the cross Qt
package roots. `emcmake` plus `Qt6_DIR` alone does not resolve Qt Core and its
dependencies. Start a fresh build directory when changing toolchains.

The fixture links Embind because Qt Core itself uses `emscripten::val`. The
shared server sources omit address enumeration when Qt defines
`QT_NO_NETWORKINTERFACE`, and use the equivalent `QDeadlineTimer` semaphore
overload on Qt 6.6+ (the static WASM kit omits the old timeout wrapper symbol).
Qt 5.6 keeps its original timeout overload. No semaphore implementation or
gameplay dependency is stubbed out for the cross build.

Only this fixture target selects a filename-based INI backend for the global
`Settings` object. The host creates `/work` and changes MEMFS CWD before static
initialization, so `config.ini` stays inside that fresh module. Qt's
organization/scope constructor probes browser cookies even with `IniFormat`;
the filename constructor avoids that browser API without DOM shims.

The WASM option defaults OFF. Existing native products and native fixture CTests
keep their previous build path. Cross builds require all native products,
the native runner and BUILD_TESTING disabled; the top-level test tree contains
native server/network tests and must not be interpreted as cross tests.

Output under `build/rules-wasm/rules-wasm/RelWithDebInfo/`:

- `qsanguosha_rules_fixture_wasm.mjs`
- `qsanguosha_rules_fixture_wasm.wasm`
- `qsanguosha_rules_fixture_wasm.assets.json`

C++ uses Emscripten's compatible exception mode, not native Wasm exception
extensions. Debug/RelWithDebInfo links reset `-g` with `-g0`, then use `-g2` to
retain function names without full DWARF; the initial full-DWARF `wasm-opt`
process used about 9 GiB locally.
Stack/initial-memory settings are probe configuration, not measured
production requirements or a promise about download size/performance.

## Matched content, not just matched card names

At configure time the Python tool calls the **existing native harness's**
`stage_builtin_assets()` function. There is no second bootstrap-closure list.
Only that staged closure is embedded at `/assets`; the complete checkout,
ignored extensions, AI scripts, configuration files and host home are not.

A generated manifest records every staged file's relative path, size and SHA-256.
Changes to source bootstrap files or the closure function trigger reconfigure
and relink. The comparison tool stages the same native closure and checks it
against the WASM sidecar **before** running either backend. The Node host also
checks the embedded manifest and each embedded file against that sidecar.
Missing/extra files or changed bytes fail. No registry/fingerprint mismatch is
normalized away.

Emscripten 4.0.7 installs embedded files during runtime initialization, after
`preRun`. Asset verification therefore runs after the module factory resolves
and before the fixture export starts EngineBootstrap. Host tests model this
ordering; eagerly populating a fake filesystem would hide startup failures.

This is the `builtin-v1` fixture profile. PR #29 identified randomized bootstrap
registration in external content; this probe does not silently weaken that
check, alter the external script or claim extension parity. These hashes are
not a complete ruleset/ABI/code-signing/security manifest.

## Run parity

```sh
module_dir="$PWD/build/rules-wasm/rules-wasm/RelWithDebInfo"
python3 tests/client_runtime/check-wasm-fixtures.py \
  --native-runner "$PWD/build/rules-native/qsanguosha_rules_fixture_runner" \
  --wasm-module "$module_dir/qsanguosha_rules_fixture_wasm.mjs" \
  --manifest "$module_dir/qsanguosha_rules_fixture_wasm.assets.json" \
  --node node --fixtures "$PWD/tests/client_runtime/fixtures" \
  --asset-root "$PWD" --artifacts "$PWD/artifacts/wasm-fixtures"
```

The gate runs the original native semantic/determinism/negative suite, then the
same four scenes/twelve queries twice in fresh WASM host processes (fixed and
random Qt hash initialization). Both WASM outputs must meet the checked-in
semantic assertions **and** match the native canonical bytes. Order, duplicate
targets/subcards, null values, full decimal uint64 strings, registry hashes and
response/wire fields all remain significant. Nine invalid/dependency fixtures
must fail with their expected diagnostic and without a result file.

A timeout, abort, nonzero C return, missing result/export, bad JSON or content
mismatch is a failure. A failed module is not reused. The host copies result
bytes unchanged and publishes atomically only after success. Logs and
`parity-summary.json` retain outcomes and executable/module/input/output hashes.

Host-only checks (do **not** execute game rules):

```sh
python3 tests/client_runtime/check-wasm-fixtures.py --self-test
node --test tests/client_runtime/run-wasm-fixture.test.mjs
```

`.github/workflows/wasm-rules-fixtures.yml` is a separate, non-optional-on-error
PR/workflow_dispatch probe. It installs the matching SDKs, builds actual native
and WASM targets, executes parity, and uploads logs/artifacts even on failure.
If compilation fails, its initial summary remains NOT_RUN, not PASS. It does not
turn previous native green CI or fake-factory tests into WASM evidence.

## Limits before Web integration

`can_confirm` still records the current shared build/target evaluator, not full
physical-card availability/ownership/pattern checks or request freshness. Existing
projection, callback-purity and arbitrary-extension limitations remain. These
are trusted local test inputs, not production snapshots or untrusted Lua APIs.

Only after compile/link/Node parity are green should the next slice investigate
browser Worker lifecycle/loading and real selection integration. A Node parity
pass alone is not browser acceptance; do not replace the Web eligibility path
based only on this probe.

Toolchain/API references:

- https://doc.qt.io/qt-6/wasm.html
- https://emscripten.org/docs/compiling/Modularized-Output.html
- https://emscripten.org/docs/api_reference/module.html
