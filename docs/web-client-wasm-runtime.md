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

## First vertical slice

The first extraction is `src/client/runtime/client-target-evaluator.h`.
It moves the engine-facing target-selection calculation out of TUI-specific
presentation code without changing TUI behaviour.

The evaluator deliberately preserves three native-client semantics:

1. The four-argument `Card::targetFilter(..., maxVotes)` overload is the source
   of target capacity. Its boolean return value is not sufficient for cards
   such as Collateral.
2. Repeated target names are legal when `maxVotes` is greater than the number
   of votes already spent on that player.
3. `targetFixed()` short-circuits local target validation because those targets
   are owned by the current server interaction context.

Missing client-visible player state produces an **unknown** result, not a local
rejection. This rule is important for the future WASM bridge: incomplete
projection must never silently turn into "illegal".

`src/tui/tui-target-advice.*` now adapts this presentation-neutral result to TUI
localized error text. The engine regression suite covers multi-vote,
target-fixed, incomplete, and unknown-state cases.

## Next slices

The next changes should keep the same direction and avoid exporting raw
`Player*`, `Card*`, or `Room*` pointers to JavaScript:

1. Extract TUI's client-side Player/RoomState projection into
   `src/client/runtime/`.
2. Add a selection evaluator that combines card selection, ViewAs construction,
   target evaluation, and canonical reply encoding.
3. Add a native fixture runner and a WebAssembly build of the same runtime.
4. Run that runtime in a dedicated Web Worker and replace
   `web/src/eligibility.ts` one interaction at a time.
5. Add ruleset/card-registry hashes before loading extension content.

The Web UI should not grow new hard-coded weapon, target, or extension tables
while this migration is in progress.
