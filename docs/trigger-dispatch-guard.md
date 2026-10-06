# RoomThread cascade break

RoomThread bounds synchronous V1 and V2 event dispatch. A limit cancels the
runaway cascade with `TriggerCascadeBreak`, rather than ending the room or
requesting a turn change. Completed effects remain committed. Unexecuted
effects in that cascade are discarded after native cleanup, and the owning
operation returns so the remaining turn can continue.

For example, `draw A -> judge B -> draw C -> judge D -> ...` unwinds to the
initial draw A operation. Nested draws and judgements do not independently
consume cancellation. `judge()` never returns a fabricated successful result
from an interrupted judgement. Native TurnStart and phase drivers have separate
recovery ownership, since TurnStart encloses the whole turn.

## Settlement exhaustion policy

The first recovery continuation receives one fixed settlement allowance under
its original cancelled cascade owner. All retained dying/death frames, new
continuation contexts and native rescue retries share those counters. Resetting
a local dispatch budget or changing callback identities cannot reset that
allowance. Ordinary original rescue and conversion remain available until it
is exhausted.

The user selected forced death for an unfinished dying decision at exhaustion.
After the offending callback unwinds and any canonical physical commit finishes,
the current original victim is killed through the existing native mode/death
flow only if alive, still at HP zero or below, still dying, and unresolved.
Remaining optional rescue/death callbacks in that cascade are cut. Native seat,
role, victory and burial work completes; the owning operation then returns so
the room continues, unless ordinary victory ends the game. Already dead,
recovered and completed death-prevention victims are not killed again, and
unrelated players are not selected by a room-wide HP-zero scan. An already
started original death cursor completes its remaining native work once.

This fallback deliberately ends remaining rescue eligibility only at the total
settlement ceiling. It is a gameplay policy for runaway exhaustion, not a claim
that interrupted arbitrary skill mutations can be rolled back.

## Configuration

Settings are read once when each RoomThread is constructed.

| Setting | Default | Meaning |
| --- | ---: | --- |
| `RoomThreadTriggerGuardEnabled` | `true` | Enable dispatch ceilings. |
| `RoomThreadTriggerMaxDepth` | `64` | Active guarded dispatch/scope depth; clamped to 512. |
| `RoomThreadTriggerMaxEvents` | `1000000` | Event entries within one cascade. |
| `RoomThreadTriggerMaxSteps` | `10000000` | Dispatch scan, retry and expansion steps. |
| `RoomThreadTriggerMaxContexts` | `65536` | V2 contexts materialized in one dispatch scan. |
| `RoomThreadTriggerMaxSettlementEvents` | `1000000` | Total recovery event entries under the original cancelled owner. |
| `RoomThreadTriggerMaxSettlementSteps` | `10000000` | Total recovery dispatch, native stage and rescue-retry work under that owner. |

Zero, negative and invalid numeric limits use the defaults. Disabling the
guard removes its ceilings; it does not revoke cancellation already in
progress. Raising the depth above the platform's native/Lua stack capacity
can exhaust that stack before the configured ceiling. Default depth 64 is
verified by the focused Linux Lua recursion fixture; other platforms need
their own validation.

There is no wall-clock watchdog. Human prompts, pauses and network waits do
not consume a time allowance. The high defaults accommodate large rooms and
long legitimate chains. They bound dispatch work, not elapsed seconds or
arbitrary allocations performed inside a callback. Repeated identical event
names are not treated as proof of a loop: legitimate state changes can produce
the same event repeatedly.

## Recovery contracts

`RoomThread::CascadeScope` establishes a recoverable logical operation or
joins an existing one. Nested judge and deck-reservation scopes join their
owner. Root draws, card use, damage and movement APIs catch only their own
cancellation receipt after child scopes unwind. A completed child operation
promotes outstanding resource/deferred receipts to its enclosing cascade.
Author callbacks also retain an ambient cumulative budget, so a shallow loop
that repeatedly starts and finishes draws cannot reset every allowance.

Native cleanup preserves already committed card moves, costs and HP changes.
Uncommitted draw reservations return to their original end/order. Abandoned
cascade-owned judgement/table cards are discarded, including held
`throw_card=false` judgement cards awaiting an unexecuted award. Cards already
awarded or moved into a stable zone remain there. Cleanup tracks physical
receipts rather than trusting a mutable JudgeStruct pointer.

`NativeCommitScope` and `invokeStructuralCallback()` unwind an offending
equipment/filter callback, then finish the canonical physical move without
more optional dispatch. Cancellation remains latched until the caller's
checkpoint after commit. Interrupted equipment slot choices re-read physical
owner/place/slot state and finish only remaining overflow in stable native
order, preserving any award committed during the choice. `MandatoryCleanupScope` suppresses optional skills,
deferred work and author-backed presentation while structural cleanup runs.
Only registered native GameRule handling for GameOverJudge and BuryVictim is
routed through this suppression boundary.

Committed HP at or below zero continues through the existing `enterDying`,
GameRule AskForPeaches/AskForPeachesDone, `askForSinglePeach` and `useCard` flow.
Normal validation, virtual/view-as conversion and rescue skills remain available.
There is no separate emergency rescue algorithm or physical-card restriction.
The cancelled ancestor stays latched while original native stages continue; an
interrupted stage advances its existing observer/rescuer cursor instead of replaying
completed stages. If a physical move must finish first, that original cursor is
retained until its canonical checkpoint. Genuine victory reached by original
death settlement follows normal game completion; the guard does not manufacture
a room-abort result.

Only interrupted authoritative HP commits are candidates for unfinished native
dying settlement. An unrelated cancellation does not re-enter a previously
completed HP-zero death-prevention result. Original cursors retain observer/saver
position, cause-card lifetime and history attribution across physical commits.

Quarantine records actual causal callback identities under the original cancelled
owner. V1 and V2 query/dispatch identities include the definition, callback site,
event and target; selected V2 activations also retain complete source/activation
and physical-equipment provenance. Equipment callback identity includes its card
lifetime generation. Exact failed activations are discarded; healthy sibling
sources and ordinary conversion/rescue callbacks remain eligible. The quarantine
is retired with its original owner and never inferred from repeated event names
or the diagnostic ring.

Cancelled cascade-owned summon, anytime and pending reveal work is removed.
Unrelated prior/network requests and committed visibility changes are
preserved. Consumed or cancelled anytime requests receive their native completion
acknowledgement, allowing later requests in the continuing room. Interrupted TurnBroken cleanup retries share one bounded iterative
budget. If that cleanup itself runs away, mandatory cleanup finishes the
already requested turn boundary before the existing mode driver resumes;
ordinary cancellation does not introduce TurnBroken.

## Lua transport and diagnostics

The vendored Lua VM uses C setjmp protection. A foreign C++ exception must not
escape a protected callback before Lua restores its error/call bookkeeping.
The optional per-VM host bridge catches it inside `luaD_rawrunprotected`, lets
normal Lua error cleanup restore the VM, and transports the exception back to
`LuaRuntime::protectedCall`. Protected-call completion and coroutine recovery
also propagate pending cancellation, so author `pcall`/`xpcall` retry loops
cannot consume it. The caller stack is restored before C++ propagation, and
the same game VM remains usable afterward.

On the first limit in a cascade, `ROOMTHREAD_CASCADE_BREAK` emits a server-only
JSON diagnostic with room/cascade ID, numeric event and phase, bounded player
and skill names, configured ceilings, counters, and the latest 32 entries.
`ROOMTHREAD_SETTLEMENT_LIMIT` reports the original owner and cumulative recovery
counters once at exhaustion; `ROOMTHREAD_SETTLEMENT_FORCED_DEATH` records only
the affected room, victim and phase when the fallback actually kills. These
diagnostics contain no event payload, private hand/card identities, roles or hidden
general names, and are not sent to other players. Successful dispatch adds
bounded counter/ring bookkeeping and callback scope overhead; no wall-time
performance improvement or numerical overhead claim is implied.

The guard is cooperative. It cannot preempt a Lua/native function's own
infinite loop if that function never re-enters a guarded operation. It cannot
automatically identify or roll back arbitrary author-owned temporary tags,
marks, external side effects or heap allocations whose cleanup was written
after the interrupted call. Such skills still need their own cancellation-safe
cleanup. This is a bounded event-cascade mechanism, not general hang protection.

Focused temporary fixtures cover native direct/mutual recursion, shallow
redispatch and repeated-root resets, V2 retries/context expansion, ordinary
exception cleanup, TurnBroken retry cleanup, disabled/invalid settings, a
50-seat 50,000-event chain, another live room, and reusable Lua after recursive
and protected-call cancellation. Native lifecycle fixtures exercise Draw/Judge,
reservation conservation, preserved awards and committed draws, physical move
commit, deferred work, original virtual/skill rescue, and rescue/death. Settlement
ceiling cases check current-victim death, same-room continuation, recovered and
completed HP-zero prevention, and completion of already-started death without
a second kill. These are short synthetic fixtures,
not a concurrent-network stress test or a complete 50-player match.
