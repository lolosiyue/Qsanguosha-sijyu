# Web client native-rules/WASM runtime

This document records the migration boundary for replacing the Web client's
hand-written gameplay eligibility rules with the same C++/Lua rule
implementation used by native clients.

The persistent runtime builds on PR #31. W2 adds a shared native rules identity
and mandatory WebSocket admission gate; see [rules-bundle-identity.md](rules-bundle-identity.md)
for the current contract and verification boundaries. The W3b cutover moved the
Web Worker onto the `ClientRulesIngress` streaming API (see
[native-rules-ingress.md](native-rules-ingress.md)); the PR31-era per-query
file-based evaluate is retained in C++ but rejected with
`stream_snapshot_api_disabled` once stream mode is enabled. Earlier Node/Worker
fixture results do not establish production runtime acceptance.

## Target architecture

The browser keeps the TypeScript/DOM presentation layer. Gameplay rule queries
move behind a small client-runtime API that can be compiled natively for TUI
and tests and to WebAssembly for the browser's dedicated Worker.

```text
Protocol V2 frames
      |
      v
client-visible state reducer
      |
      v
client runtime (C++/Lua)
  - Player/Card state projection
  - card availability
  - targetFilter / maxVotes
  - targetFixed / targetsFeasible
  - ViewAs card construction
  - prohibit / distance / attack range
      |
      v
presentation-neutral selection result
      |
      v
TypeScript / DOM
```

The server remains authoritative. A browser-side positive result is only a
preview; submitted replies are still validated by the server.

## First vertical slice: target evaluation

`src/client/runtime/client-target-evaluator.h` moves the engine-facing target
selection calculation out of TUI presentation code without changing TUI
behaviour.

The evaluator deliberately preserves three native-client semantics:

1. The four-argument `Card::targetFilter(..., maxVotes)` overload is the source
   of target capacity. Its boolean return value is not sufficient for cards
   such as Collateral.
2. Repeated target names are legal when `maxVotes` is greater than the number
   of votes already spent on that player.
3. `targetFixed()` short-circuits local target validation because those targets
   are owned by the current server interaction context.

Missing client-visible player state produces an **unknown** result, not a local
rejection. Incomplete projection must never silently turn into "illegal".

## Second vertical slice: shared state projection

Frontend-neutral state projection now lives under `src/client/runtime/`:

- `client-state-projection.h` applies normalized `ClientGameState` player data
  to an engine `Player`, including scalar/dynamic properties, flags, marks,
  history, card limitations, and visible skill changes.
- `client-room-context.h` owns the client-side `RoomState`, registers the
  `EngineRuntimeContext`, applies live `UPDATE_CARD` changes to `WrappedCard`,
  exposes card owner/place lookups, and carries card-use reason/pattern.
- TUI uses `ClientRoomContext` through a compatibility alias and delegates its
  player projection to the shared helper.

## Third vertical slice: explicit runtime target

`ClientPlayer` and its player-model implementation live in
`src/client/runtime/client-player-model.*` and are compiled by the dedicated
`qsanguosha_client_runtime` static library.

The exact global Qt meta-object name remains `ClientPlayer`. This is deliberate:
existing engine client paths use `inherits("ClientPlayer")` to choose
client-visible/cached rule behaviour rather than server-only evaluation.
Moving the implementation therefore must not rename the class.

`src/tui/tui-client-player.*` is only a compatibility adapter: the old
`TuiPlayerModel` spelling aliases the shared `ClientPlayerModel`, while the
implementation and AUTOMOC ownership belong to the runtime target. TUI links
that target rather than owning a second player-model implementation.

The runtime library is intentionally engine-facing but does not propagate a
normal `qsanguosha_engine` link. Final products choose the engine link policy;
TUI currently requires `WHOLE_ARCHIVE` for package registrars, and a second
normal engine link would conflict with that CMake link feature. The runtime
itself propagates `ClientCore`, Qt Core and Qt Network dependencies. Qt Network
is currently required by the engine headers' non-desktop precompiled-header
path; the runtime also exports `QSAN_ENGINE_TEST_BUILD` so consumers use that
path without supplying frontend-specific compile settings.

## Fourth vertical slice: shared selection runtime

`src/client/runtime/client-selection-runtime.h` now owns the engine-facing
selection helpers that were previously embedded in `tui-play-skills.cpp`.
TUI remains a localized adapter over these presentation-neutral results.

The shared API provides:

- prompt-pattern to ViewAs-skill resolution;
- interaction/handling-method to native `CardUseReason` mapping;
- visible ViewAs skill candidate discovery;
- native activation availability checks for legacy ViewAs and ViewAsSkillV2;
- ordered subcard validation and native ViewAs card construction;
- target-step and finished-target evaluation through the shared target
  evaluator;
- canonical `InteractionResponse::CardSelectionData` construction. The existing
  `InteractionReplyEncoder` remains the single Protocol V2 wire encoder.

ViewAsSkillV2 construction no longer invents `CARD_USE_REASON_PLAY`. The build
request consumes the current `ClientRoomContext` reason and pattern, so a
response, response-use, named skill prompt and play-phase request reach
`canActivate`, `canSelectCard`, `cardSelectionFeasible` and `createCard` with the
same context the native client is currently answering.

Set `ClientRoomContext::setCardUseContext()` before each selection query; there
is no separate per-request override that could disagree with legacy callbacks
reading the engine context. Ordered selections reject duplicate physical IDs,
and zero-card skills reject supplied subcards. V2 activation is checked again
with the completed selection before construction. Server-named legacy prompts
retain the desktop's borrowed-skill behaviour without changing player marks.

Legacy ViewAs subcards also resolve through `Engine::getCard()` rather than the
printed engine-card table. That means an `UPDATE_CARD`/WrappedCard change seen by
the client remains visible during selection instead of silently reverting to
the card's original catalog face.

The result type contains a transient native `Card*` only for native callers that
must finish rule evaluation in the same event handler. A JS/WASM binding must
never export that pointer; it copies the canonical card text and structured
selection result before crossing the boundary.

The `tui-play-skills` regression suite also calls the shared API directly to
cover borrowed prompts, wrapped-card filtering, V2 context and ordered
selection, target evaluation, and response encoding. Separate native/WASM
fixture consumers cover their recorded scenes; neither suite establishes the
production browser session's acceptance.

## Persistent Web runtime

The fixture targets remain separate consumers. `qsanguosha_client_wasm` links
the existing `qsanguosha_client_runtime`, whole engine/package registrations,
`InteractionReplyEncoder`, and the production `ClientRulesSession`/WASM entry
sources. It does not link `qsanguosha_rules_fixture_support`, the fixture
evaluator, or a renamed fixture CLI main. Native product source inventories are
unchanged by this opt-in product.

The WASM entry keeps one QCoreApplication/engine alive across requests.
`ClientRulesSession` creates a fresh projected scene from the client-visible
snapshot for each selection query, so removed properties and stale wrapped
cards cannot leak between snapshots. C++ owns physical-card,
ViewAs and target evaluation, then copies JSON results across the boundary;
Card/Player pointers remain inside the module. Decimal request IDs remain
strings. Browser request identity, state revision and selection revision prevent
late results from confirming a newer prompt or selection.

The dedicated Worker loads one module per session and verifies the build
deployment bundle (`.bundle.json`: schema, bridge schema and per-file SHA-256
pairing of the `.mjs` and `.wasm`) before initializing the engine. Its
module URL is fixed to the application origin, not supplied by game packets.
The TypeScript/DOM frontend continues to own Protocol V2 transport and display.
Native confirmation uses the existing C++ reply encoder's payload.

The deployed content profile is `declared-v2`: the embedded rules identity and
`rules-content-manifest` cover only the declared bootstrap/translation and
declared extension content; they do not copy ignored extensions, AI, local
configuration or the checkout into MEMFS. The Web side never supplies a
package or content manifest — the identity is exported by C++. A matching
bundle does not prove arbitrary server
extension compatibility or a complete ruleset/ABI agreement. Unknown or
unsupported content must not enable confirmation through guessed TS rules.

## Production build and packaging

The existing baseline is Qt **6.11.1**, Emscripten **4.0.7**, the single-thread
WASM Qt kit and a matching native Qt host-tool installation. This uses Qt Core
and Network in a Worker; it does not ship a Qt Widgets/Quick browser UI. The
root dependency graph also finds Qt WebSockets. Configure and build are explicit
follow-up commands, not steps executed for this source implementation:

```sh
source "$EMSDK/emsdk_env.sh"
cmake -S . -B build/web-wasm -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$QT_WASM/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQT_HOST_PATH="$QT_NATIVE" \
  -DBUILD_TESTING=OFF -DQSAN_BUILD_GUI=OFF -DQSAN_BUILD_TUI=OFF \
  -DQSAN_BUILD_SERVER=OFF -DQSAN_BUILD_RULES_FIXTURE_RUNNER=OFF \
  -DQSAN_BUILD_WASM_RULES_FIXTURES=OFF -DQSAN_BUILD_WASM_WEB_CLIENT=ON
cmake --build build/web-wasm --target qsanguosha_client_wasm
```

`QSAN_BUILD_WASM_WEB_CLIENT` defaults OFF and works independently of
`QSAN_BUILD_WASM_RULES_FIXTURES`. Both may be ON in the same cross build; they
reuse one asset recipe and exception model while retaining distinct targets,
entry points and output directories. Native products, the native fixture runner
and `BUILD_TESTING` must be OFF for that cross build. The production session
sources are compiled only by the production WASM target.

Generated artifacts under `build/web-wasm/web-wasm/RelWithDebInfo/`:

| Artifact | Contract |
|---|---|
| `qsanguosha_client_wasm.mjs` | ES module factory `createQSanguoshaClient`, Worker environment |
| `qsanguosha_client_wasm.wasm` | Persistent C++/Lua runtime |
| `qsanguosha_client_wasm.bundle.json` | Deployment pairing manifest (schema/bridge schema/SHA-256 of the two files above); generated by the build, re-verified by the packager and the Worker |

Exports are `_qsan_client_bridge_schema` and `_qsan_client_code_identity`
(identity probes), `_qsan_client_initialize`, `_qsan_client_stream` and
`_qsan_client_shutdown`; `_qsan_client_evaluate` is still compiled but the
Worker requires the stream entry and C++ rejects the file-based snapshot query
with `stream_snapshot_api_disabled` in stream mode. Emscripten exposes `FS`
and `ENV` to the host. The module
has no `main` entry, permits memory growth, starts with 128 MiB memory and an
8 MiB stack, and preserves the fixture's exception mode. These inherited sizes
are configuration, not browser memory/performance acceptance.

| Export | MEMFS/JSON contract |
|---|---|
| `_qsan_client_initialize` | Initializes once and writes `/work/init.json`: bridge schema 2, native `rules_bundle`, `card_count`, and numeric-ID registry entries with object name, integer suit, number, class and package |
| `_qsan_client_stream` | Reads `/work/stream.json` (at most 4 MiB; `schema_version`, `action`, `generation`, plus action fields) and writes `/work/stream-result.json`; the `ClientRulesIngress` actions cover snapshot/interaction/query operations — see [native-rules-ingress.md](native-rules-ingress.md) |
| `_qsan_client_shutdown` | Ends the engine lifetime; the closed module cannot initialize again |

The host must establish isolated MEMFS configuration in `preRun`, then verify
embedded assets after the Emscripten factory resolves and before initialization.
Queries run serially. The main-thread controller discards stale generation or
revision results. Failure disposes the Worker; explicit rule reload or a new
connection creates a fresh Worker.

Builds do not modify `web/public`. To package already-built artifacts:

```sh
python3 tools/package-web-runtime.py \
  --module build/web-wasm/web-wasm/RelWithDebInfo/qsanguosha_client_wasm.mjs \
  --destination web/public/rules
```

Alternatively, `cmake --build build/web-wasm --target package-web-runtime` first
builds the runtime dependency, then runs the same packaging command. The tool
requires the three artifacts (`.mjs`, `.wasm` and the build-generated
`.bundle.json`), checks the WASM header and the bundle's SHA-256 pairing, and
publishes only those fixed generated names. It does not execute the module
or provide runtime acceptance. The Worker verifies the bundle and embedded
asset bytes when the application starts.

Package before the Web frontend's normal Vite build so `public/rules` is copied
into `dist/rules`. Deploy all three artifacts together at `/rules/`, serve
`.mjs` as JavaScript and `.wasm` as `application/wasm`, and configure the server's
SPA fallback after the static `/rules/` route. Missing artifacts must return a
visible runtime failure rather than an HTML application shell masquerading as
the module. Use HTTPS or localhost for the Worker's Web Crypto asset checks.

## Structured interaction contract

`ClientRulesIngress` already builds the shared ClientCore `InteractionRequest`
through `ProtocolInteractionRequestBuilder`. `prepareQuery` now forwards that
built request as `interaction` beside the raw `command`/`payload`, and
`ClientRulesSession` consumes it. Enumerated prompts therefore take their
selectable set, counts and reply shape from one implementation rather than a
second reading of the wire payload in the session, and the browser renders from
the same structured object it is echoed in `evaluate`'s result.

`S_COMMAND_SKILL_GUANXING`, `S_COMMAND_SKILL_GONGXIN` and `S_COMMAND_SKILL_YIJI`
are answered on that enumerated path. The session validates the draft against
the typed payload — a rearrangement must partition the whole set inside its
top/bottom bounds, gongxin names exactly one selectable card, yiji stays inside
`min_cards`/`max_cards` and names one offered recipient — and then encodes the
reply with `InteractionCommandRegistry`'s own encoder for that command. No skill
effect is evaluated: those prompts resolve on the Room side.

`guhuo`, `juguan` and `tiansuan` declarations keep their existing native
enumeration — each candidate is probed through `applyDeclaration`, so the
offered list is the one the desktop dialog would allow rather than any string
that happens to clone a card. The evaluation also carries the skill's
`SkillDialogInfo` as `declaration_dialog`, so a shell implements the three
dialog shapes once instead of one branch per general.

The card-use path additionally reports, per ViewAs candidate, the declared
subcard amount (`ViewAsSkillV2::getN`, or the zero/one-card base classes),
committed usage read from the projected limit-scope mark, instance
invalidation, `isResponseOrUse` and the expand pile. These are display and
sizing hints; `canActivate`, `canSelectCard` and `cardSelectionFeasible` remain
the only legality decisions. Selectable and selected card ids are also reported
with the zone they occupy — hand, equip, hand pile, expand pile or a sibling
player's pile.

### Preview purity

A query is a preview, never a move:

- The projected `Scene` (state, room context, players) is constructed and
  destroyed per query, so declaration tags, marks and history changes cannot
  reach the next query or the committed client state. A `guhuo`/`juguan`
  probe's tag is removed before the player's actual choice is applied.
- Usage is only read. Nothing calls `Skill::addUsage`, so opening a skill,
  enumerating its declarations or previewing a card never spends a use.
- `evaluate` binds a throwaway `GameRng` for its whole body, so a rule callback
  that draws randomness cannot advance the process-wide fallback stream that a
  later query would observe.

## Remaining acceptance and scope

Production compile/link, repeated-query and lifecycle execution, native/WASM
parity for live snapshots, actual browser interaction/reconnect acceptance and
deployment checks remain unperformed. Fixture probes continue to document their
own scope in [wasm-rules-fixtures.md](wasm-rules-fixtures.md).

Arbitrary extension loading and complete server/runtime ruleset negotiation
remain outside the `declared-v2` manifest profile; no claim of
arbitrary-extension parity follows from this integration. The server remains
authoritative for every reply.

The Web UI should not grow new hard-coded weapon, target, or extension tables
while this migration is in progress.

## W2 rules identity and the W3b streaming cutover

The follow-up contract is defined in [rules-bundle-identity.md](rules-bundle-identity.md).
Initialization uses bridge schema 2 with the shared native `rules_bundle`.
Package the `.bundle.json` deployment manifest before building the Web
frontend. WebSocket signup requires this identity, including reconnect; legacy
TCP clients may still omit it.

Since the W3b cutover ([native-rules-ingress.md](native-rules-ingress.md)) the
Worker drives the runtime through `_qsan_client_stream` instead of the
per-query `request.json`/`result.json` evaluate files; the recipe above
describes the build/packaging layout, while the PR31-era `.assets.json`
manifest and the standalone file-based evaluate no longer exist on the
production path.
