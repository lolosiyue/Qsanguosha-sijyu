# Web client native-rules/WASM runtime

This document records the migration boundary for replacing the Web client's
hand-written gameplay eligibility rules with the same C++/Lua rule
implementation used by native clients.

## Target architecture

The browser keeps the TypeScript/DOM presentation layer. Gameplay rule queries
move behind a small client-runtime API that can be compiled natively for TUI
and tests and, later, to WebAssembly.

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
selection, target evaluation, and response encoding. These are native checks;
native/WASM fixture parity remains a later slice.

## Next slices

1. Add a native fixture runner that links `qsanguosha_client_runtime` directly
   and records deterministic physical-card and ViewAs selection fixtures.
2. Add the first WebAssembly build of the same runtime and compare its fixture
   output against the native runner.
3. Run that runtime in a dedicated Web Worker and replace
   `web/src/eligibility.ts` one interaction at a time.
4. Add ruleset/card-registry hashes before loading extension content.
5. Extend the selection result with remaining player-view facts such as
   explicit distance/attack-range presentation where the Web UI needs them.

The Web UI should not grow new hard-coded weapon, target, or extension tables
while this migration is in progress.
