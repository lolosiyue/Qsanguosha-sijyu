# Native Protocol V2 rules ingress — W3 first slice

Base: `debug@c74d3581f3ae8d65d69d5bd3257f34a74bcc27b9`, after the independently
implemented W2 server/runtime bundle admission and verified deployment loader.
This work does not resurrect the closed PR33 `rules_identity` contract.

## Scope

`ClientRulesIngress` receives exact incoming **and successfully sent outgoing**
Protocol V2 frames, uses the existing native codec/session controller/reducer/
request builder, and owns the committed client-visible state. A query supplies
only its revision/request correlation and selection; it cannot supply replacement
players, cards, setup, patterns, or a snapshot.

`ClientRulesHost::stream()` and the additive `_qsan_client_stream` export put the
same implementation in the native probe and the actual production WASM product.
No separate test evaluator or JavaScript gameplay rules are introduced.

**W3b has since moved the real Web client onto this entry.** `rules-worker.ts`
and `rules-client.ts` no longer call the external-snapshot bridge at all; see
"Web client cutover" below. The stream entry is still exercised by a focused
browser ABI probe, not by a live WebSocket/DOM game acceptance test. W4
projection and W5 submission work remain.

## Stream ABI

Initialize the existing host first. Write a JSON object to `/work/stream.json`,
invoke `_qsan_client_stream`, and read `/work/stream-result.json`. Paths are fixed
by the existing host; this is not a caller-selectable filesystem interface.

Every operation has exactly `schema_version: 1`, `action`, `generation`, and the
fields shown below. Schema and generation/revision numbers must be integers;
request IDs remain canonical decimal strings. Unknown fields are rejected.

| Action | Additional fields | Meaning |
|---|---|---|
| `reset` | none | Bind the real initialized Engine's W2 identity and start a strictly newer connection generation |
| `frame` | `direction`, `frame` | Observe `incoming` or `outgoing` raw UTF-8 protocol text through the existing native decoder |
| `query` | `revision`, `request_id`, `selection` | Evaluate the current request against committed native state |
| `view` | none | Read committed state only, never pending STATE_SYNC state |

The result carries `success`, `reason` and `status`. Status contains generation,
revision, active/failed/synchronizing flags, current request ID and W2 bundle ID.
A successful query also contains the existing production `evaluation` result.
An invalid operation never returns a usable wire reply. Output is replaced
atomically and old successful output is removed before every call and on shutdown.

A nonzero ABI status is a host I/O/JSON/lifetime failure, not a game-rule answer.
Semantic/stream rejections return zero with `success: false`; a decoder or current
stream failure makes later queries unavailable until a newer reset. An obsolete
generation cannot mutate or poison the newer stream. Runtime shutdown is terminal.

Once a host successfully opts into `reset`, its old external-snapshot `evaluate`
entry rejects with `stream_snapshot_api_disabled`. The Web host now opts in
during initialization, so that entry is dead for every shipped browser session.

## Native state and request contracts

- Reuse `ProtocolCodecRouter`, `ClientSessionController`,
  `ClientGameStateReducer`, `ProtocolInteractionRequestBuilder` and the existing
  production `ClientRulesSession`/reply encoder. No card-name rules are copied.
- Require the existing W2 `rules_bundle` comparison for both incoming Hello and
  outgoing Signup. Do not accept card count as a replacement for bundle identity.
- Incoming and outgoing IDs must increase independently. Observe outgoing Signup
  and Ready before following replies/application traffic. Track pending request
  replies by the existing payload registry's reply command, not a second table.
- Reject frames larger than the native protocol maximum. Failed reduction of a
  copied candidate never partially commits into visible state.
- STATE_SYNC begin cancels the old selection, resets a private gameplay
  accumulator, and preserves the previously committed view. Matching end commits
  once. Overlap, unmatched end, or an interaction arriving during sync fails the
  stream. No query may see the private accumulator.
- Every accepted frame advances revision, including an in-progress sync frame.
  Queries must match the exact current generation/revision/request. Query failures
  do not consume requests or mutate committed state. A corresponding outgoing
  reply, new request, game transition or control-context change invalidates the
  previous selection.
- Per-query ServerInfo restoration and native Scene/Card cleanup are the existing
  production host contract. This is not a proof that arbitrary extension callbacks
  are pure, or that the reducer/projection has every feature needed by W4.

The internal adapter to the existing evaluator uses `ClientGameState::toJson()`
plus the native roster. It is not a browser-supplied snapshot or a second reducer.
The native request builder validates and materializes the request, but the existing
production evaluator still interprets its own card-query prompt. W5 must
consolidate that final prompt adaptation, expiry and final submission validation.
Observing an already sent reply verifies correlation, **not server game legality**.

## Web client cutover

`LiveSession` exposes one frame sink. It publishes each incoming frame's exact
bytes on arrival, before its own decoder forms any opinion of them, and each
outgoing frame only after `send()` has actually put it on the socket, so the
runtime observes the true transport order including Signup and Ready. The sink
is registered while the rules Worker loads, which already precedes the socket,
so no frame of a connection can be missed. Frames are tagged with the session
generation; a frame from an obsolete connection is dropped by the controller.

`rules-worker.ts` performs `reset` as the last step of initialization, so its
`ready` message means ingress is armed and the external snapshot entry is
already locked out for that Engine's life. Its remaining message carries only
`frame` and `query` operations; a `reset` is never accepted from a message,
because replaying one would silently discard committed native state. Operations
are applied one per native call, in order, and a rejected operation ends the
batch.

`RulesController` no longer composes players, cards, setup or card id space for
the runtime. It sends the observed frames and, once they are all acknowledged,
a selection plus the generation/revision/request the runtime itself reported.
Reducer revisions are never used for correlation: the two counters advance on
different events, and the reducer runs ahead of the Worker. A query is issued
only when the queue is drained, the reported state is active, not failed, not
synchronizing, and its request matches the one the UI is showing; a refused
query is not reissued at the same revision. A result stops being displayable as
soon as a newer frame is observed.

A refused frame is preview-only: the controller marks itself failed, releases
the Engine and leaves the session running on the TS reducer, which keeps driving
the UI as a shadow. There is no retry, because an Engine created after the fact
cannot be given the frames the failed one already consumed; recovery is a new
connection.

## Deployment compatibility

The additive stream API has its own envelope schema; BridgeSchema remains 2 and
all existing exports remain. W2 source identity now includes the shared root-level
client request/encoder adapters and the session/product build recipes in addition
to the already fingerprinted runtime sources. Rebuild and package the matched
server/runtime/frontend set. No W2 admission, artifact-hash, or content restriction
is disabled. No arbitrary extension/module download is introduced.

## Verification

Build `qsanguosha_rules_ingress_probe` with `QSAN_BUILD_RULES_SESSION_TESTS=ON`
using the normal native Qt 6 toolchain. Its input is a private builtin asset root,
not a full mutable user data tree. The Python wrapper stages the existing shared
five-file closure, runs two native processes with different Qt hash initialization,
and compares their complete records and native semantic checks.

```sh
python3 tests/client_runtime/check-rules-ingress.py --self-test
python3 tests/client_runtime/check-rules-ingress.py \
  --native-runner build/rules-native/qsanguosha_rules_ingress_probe \
  --asset-root . --artifacts artifacts/rules-ingress
```

For real browser parity, also pass `--wasm-module`, `--manifest`, and `--browser`.
The module must be the freshly linked **qsanguosha_client_wasm**, not a fixture
module. Two fresh Dedicated Workers replay the same operation sequence through
its real stream export. They receive operations and pinned artifact hashes, never
expected outputs. The tester verifies embedded content with the existing host
helper and compares registry identity and all result bytes without normalization.
This test host is not a replacement for W2's production verified deployment loader.

The Web controller itself is covered by `tests/client_runtime/rules-controller.test.mjs`,
which links the real controller against a Worker/timer/catalog double. It proves
frame order, native correlation, sync and backlog gating and the preview-only
failure policy; it executes no WASM and is not browser evidence.

Checks cover native Hello/Signup/Ready, actual card movement and Slash selection,
mark updates and stale revision, sync isolation and commit, old request rejection,
external snapshot rejection, decode failure/rollback, new generation recovery,
max-uint64 request/reply correlation, snapshot API lockout and terminal shutdown.
The report's `checks` array is verified along with independently asserted positive
and negative query results; two agreeing implementations alone are not the oracle.

The existing WASM workflow explicitly builds the native probe and production
WASM target, then runs this gate before its unchanged fixture/W1/W2 gates. Evidence
starts at NOT_RUN; missing compiler/browser, crashes, wrong diagnostics or parity
failures do not count as PASS. Logs, raw outputs, hashes and `ingress-summary.json`
are retained even on failure. Native CTest registers the same probe, with Windows
output placed in the existing `tests/<CONFIG>` handoff directory.

## Validation for the submitted Draft

Local Python verifier self-tests: 8/8 pass. JavaScript/Python syntax, original-file
Git blob identity and diff checks are separate from real rule execution. This
session has no Qt development kit or Emscripten, and container GitHub DNS failed;
source reads/publication use the connected GitHub tool. Actual C++ compilation,
native probe execution, WASM linking, browser parity and full repository CI must
be verified before marking ready. No earlier PR's green results prove this slice.
