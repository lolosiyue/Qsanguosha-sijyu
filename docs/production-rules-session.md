# W1: production rules session lifecycle gate

This slice follows #31. It validates the production `ClientRulesSession` and
`web/src/rules-worker.ts`, not another copy of the one-shot fixture evaluator.
It does not implement the later ruleset handshake or live protocol ingestion.

## Shared implementation

`qsanguosha_rules_session` owns the existing session evaluator/reply encoder
and `ClientRulesHost`. The unchanged three-function WASM ABI forwards to this
host. The native probe links the same library with the final WHOLE_ARCHIVE
engine policy, without GUI or TUI code.

The host owns one QCoreApplication/Engine/Lua bootstrap. Initialize is idempotent
while ready; shutdown is idempotent and terminal. A rejected ownership check
must not drain or destroy somebody else's application. Normal input rejection
is recoverable at the native host; the Worker treats nonzero ABI status as a
fatal transport error and is replaced before browser recovery.

Each evaluation restores the whole `ServerInfoStruct`, even when loading the
scene fails after writing setup fields. Successful output is removed on
shutdown; failed calls explicitly publish known=false/can_confirm=false and
wire=null rather than leaving a previous successful reply. This is not a
complete Lua purity or global-state sandbox guarantee.

## Native gate

Enable `QSAN_BUILD_RULES_SESSION_TESTS=ON` (defaults on for normal BUILD_TESTING).
It is independently available with GUI/TUI/server/BUILD_TESTING/fixtures OFF.

```sh
cmake --build build/native --target qsanguosha_rules_session_probe
python3 tests/client_runtime/check-rules-session.py --native-only \
  --native-runner build/native/qsanguosha_rules_session_probe \
  --asset-root . --artifacts artifacts/rules-session-native
```

The harness stages the original builtin asset closure and starts two isolated
processes with fixed/random Qt hashing. The probe derives card IDs from the
real registry, checks twelve lifecycle conditions and ten sequential queries:
A -> incomplete B -> A; scene rejection -> A; invalid ID/schema; Wusheng
response -> A; malformed/oversized input -> A; terminal shutdown.

Engine, Lua and application pointers must remain the same across queries;
projected Self/RoomContext must be gone when a query returns. An actual queued
QObject verifies deferred deletion. These checks are not an allocation soak
benchmark or exhaustive extension/callback-purity coverage.

## Production browser gate

Build `qsanguosha_client_wasm` with the existing Qt 6.11.1/Emscripten 4.0.7
cross-build, `QSAN_BUILD_WASM_WEB_CLIENT=ON`, native products/tests OFF. Bundle
`web/src/rules-worker.ts` with `web/scripts/build-rules-worker.mjs`, passing the
build's `qsanguosha_client_wasm.bundle.json` so the Worker is bound to the
deployment it is tested against, exactly as the shipped loader is.
Then run the same Python harness without `--native-only`, adding `--wasm-module`,
`--manifest`, `--worker-script` and `--browser`.

The page uses the real compiled production Worker, running ten queries without
recreating it. It repeats the sequence in a fresh Worker, checks graceful
shutdown acknowledgement, exercises a real malformed-JSON ABI failure, and
requires successful recovery in another fresh Worker. Expected outputs are
never served to the browser. The Python verifier requires native/Worker result
bytes to match exactly, as well as explicit native semantic expectations.

`session-summary.json` starts NOT_RUN. Only actual native and browser execution
can change it to PASS with browser=PASS; missing artifacts, compilation errors,
wrong errors, mismatches and missing reports cannot pass. The existing fixture
Node/browser gates remain separate and unchanged.

## Controller tests

```sh
npm ci --prefix web
node --experimental-vm-modules --test tests/client_runtime/rules-controller.test.mjs
python3 tests/client_runtime/check-rules-session.py --self-test
```

Controller tests transpile the actual production TypeScript; Worker, timers,
catalog and protocol utility surfaces are explicit test doubles. This tests
coalescing, correlation, invalidation, retries and disposal, not native rules.
Full TypeScript/Qt/WASM builds remain separate gates.

## Acceptance and limits

Do not count previous PR evidence or doubles as production runtime execution.
Require the new production summary, existing Node/browser fixture parity and
normal native CI before marking ready. This slice does not prove complete DOM
play, remote multiplayer, all skills/extensions, memory limits, ruleset identity
or freshness checked by the server. W2 remains the rules-bundle handshake;
W3 replaces the transitional TS snapshot with native protocol ingestion.
