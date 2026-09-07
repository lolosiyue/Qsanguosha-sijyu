# Browser Dedicated Worker rules-fixture probe

This is the next verification slice after the native runner (#29) and Node-hosted
WASM parity (#30). It is **not** the production Web client or a persistent game
runtime. No `web/src/eligibility.ts` path is replaced.

## Build and execution

Use the same separate Qt 6.11.1 / Emscripten 4.0.7 single-thread cross build
specified in `wasm-rules-fixtures.md`, with `QSAN_BUILD_WASM_RULES_FIXTURES=ON` and
native products, native runner and `BUILD_TESTING` disabled. Then explicitly build:

```sh
cmake --build build/rules-wasm --target qsanguosha_rules_fixture_worker
```

The Worker target is `EXCLUDE_FROM_ALL`. It takes the Node target's source/link
recipe, changing only the Emscripten host environment to `worker`. The existing
CLI, C entry, fixture evaluator, engine, Lua and reply encoder are unchanged.
There is no second package inventory or second implementation of game rules.

Build the native runner as documented in `native-rules-fixtures.md`. On Linux
with Chromium or Google Chrome installed, run:

```sh
python3 tests/client_runtime/check-browser-fixtures.py \
  --native-runner "$PWD/build/rules-native/qsanguosha_rules_fixture_runner" \
  --wasm-module "$PWD/build/rules-wasm/rules-wasm/RelWithDebInfo/qsanguosha_rules_fixture_worker.mjs" \
  --manifest "$PWD/build/rules-wasm/rules-wasm/RelWithDebInfo/qsanguosha_rules_fixture_worker.assets.json" \
  --fixtures "$PWD/tests/client_runtime/fixtures" \
  --asset-root "$PWD" --artifacts "$PWD/artifacts/browser-rules"
```

The harness detects `google-chrome` or `chromium`; `--browser /absolute/path` can
override it. The outer deadline defaults to 360 seconds. A missing browser,
startup failure, missing report, compile failure or parity mismatch is not a pass.
Browser sandboxing is not disabled by default. In an isolated root test container
that cannot run its browser sandbox, an explicit `--browser-arg=--no-sandbox` is
available; this is not a production deployment setting.

No Playwright, Selenium, npm dependency or Python package is added. The harness
uses Chromium's command line and a Python standard-library loopback HTTP server.
It serves an exact allowlist under a random per-run URL, not the whole checkout;
POST requires the expected origin and accepts only one bounded report. The browser
uses a private profile and is terminated/reaped, including its Linux process group,
on completion and timeout. Node remains a separate required parity gate in CI.

## Host boundary and lifecycle

`wasm-fixture-host.mjs` is the shared Node/Worker initialization and content-check
implementation. Its inputs/outputs are bytes, and its hash function is supplied
by Node crypto or browser Web Crypto. It verifies the exact embedded inventory,
file sizes and SHA-256 values **after** Emscripten initialization and **before**
EngineBootstrap. It preserves the canonical native output bytes unchanged.

The Dedicated Worker loads a fixed sibling `.mjs` and `.wasm` pair, never a URL
supplied by a fixture. The only `window` bridge is the same narrow Qt timer
contract identified in #30, forwarding to actual Worker `setTimeout` and
`clearTimeout`. No document/storage/navigator emulation or gameplay stubs exist.
This is not a claim that arbitrary Qt APIs run in a Worker.

Each query batch runs in a fresh Worker/module/MEMFS. The main-thread client:

- copies input buffers before transferring, and accepts only the current run ID;
- terminates on success, native error, script error, message error or timeout;
- rejects cancelled/superseded operations and ignores their queued late messages;
- permanently rejects new work after disposal.

The Worker consumes at most one message and closes after responding. Successful
output crosses the boundary as an owned ArrayBuffer, never a Card/Player pointer
or view into live WASM memory. Input is limited to 1 MiB, result to 8 MiB, and
captured diagnostics are bounded. Forced cancellation kills the VM: it is not
represented as successful native destructor execution or an application logout.

## Acceptance gate

The Python harness runs the original native semantic/determinism/negative suite
under the exact compiled `builtin-v1` content closure. It then runs the same four
scenes/twelve queries twice in fresh browser Workers, plus one successful
recovery run. Every positive browser output must pass the **existing native
semantic assertions** and byte-for-byte comparison to its native baseline,
including card-registry hash, uint64 strings, array order and duplicate votes.

All nine existing malformed-input/dependency cases must fail in the browser with
the expected diagnostics and no result. Host faults additionally exercise missing
and corrupt WASM downloads, mismatched manifest bytes, an intentionally stalled
HTTP download (timeout), immediate cancellation, and disposal. A successful fresh
run after the failure/cancellation cases checks that a destroyed VM is not reused.

`browser-parity-summary.json` becomes `PASS` only after Python checks the complete
report, exact case inventory and all semantic/byte comparisons. Browser return of
`COMPLETE` means only that execution finished, not that parity passed. CI retains
raw reports, output bytes, HTTP requests, browser logs/version and artifact hashes.
A Node pass alone is not browser acceptance. No binary is checked into the repo.

Local host-unit tests (not engine execution):

```sh
node --test tests/client_runtime/run-wasm-fixture.test.mjs \
  tests/client_runtime/browser/fixture-worker-client.test.mjs
python3 tests/client_runtime/check-browser-fixtures.py --self-test
```

These use explicit doubles. They cannot replace the real Chromium/Qt/WASM gate.

## Still outside this slice

No live WebSocket/session/reconnect integration, complete physical-card legality,
request freshness, arbitrary extension compatibility, production content signing
or callback-purity guarantee is added. `can_confirm` retains the shared evaluator's
current build/target meaning. Browser support beyond the tested Chromium build
is unverified; Safari/Firefox are not silently counted. Workers are isolated per
fixture, so startup cost and memory use are not a production performance result.

After real browser parity is green, introduce a persistent runtime lifecycle and
versioned live client-visible state ingestion before replacing a Web interaction.

Primary platform references: Emscripten settings (`ENVIRONMENT`, `MODULARIZE`,
`EXPORT_ES6`) at https://emscripten.org/docs/tools_reference/settings_reference.html,
Qt WASM at https://doc.qt.io/qt-6/wasm.html, and the Worker lifecycle at
https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API/Using_web_workers.
