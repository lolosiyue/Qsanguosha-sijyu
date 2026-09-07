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

The TUI `ClientPlayer` class declaration intentionally remains in the TUI
adapter for this slice. Its `Q_OBJECT` meta-object name is part of existing
engine behaviour: client rule paths rely on `inherits("ClientPlayer")`. Moving
that class is deferred until the build graph can explicitly own its AUTOMOC
source in a shared runtime target.

This boundary lets a future native fixture runner and WASM frontend reuse
room/card projection without depending on terminal presentation code while the
current TUI keeps the same class identity and public surface.

## Next slices

1. Promote the client-player/model implementation into an explicit
   `qsanguosha_client_runtime` target while preserving the `ClientPlayer`
   meta-object contract.
2. Add a selection evaluator that combines card selection, ViewAs construction,
   target evaluation, and canonical reply encoding.
3. Add a native fixture runner and a WebAssembly build of the same runtime.
4. Run that runtime in a dedicated Web Worker and replace
   `web/src/eligibility.ts` one interaction at a time.
5. Add ruleset/card-registry hashes before loading extension content.

The Web UI should not grow new hard-coded weapon, target, or extension tables
while this migration is in progress.
