# Managed game-state contract foundation

This branch implements an independently usable **managed value world** and a
bounded production Room adapter, including an opt-in running normal-turn executor.
A successful contract test does not make existing native packages,
legacy Lua providers, or an arbitrary running match eligible for restore. The
existing takeover snapshot format and callbacks retain their existing semantics.
The representative skill and AI are test fixtures; no production character was
migrated. Checkpoints are immutable in-memory values. A disk codec and import
bridge from `GlobalSnapshot` are not implemented here.

## Ownership and integration boundary

| Owner | Durable state / responsibility | Restore policy |
| --- | --- | --- |
| Logical game / `GameTimeline` | Root game ID, monotonic generation, branches, accepted decisions, nondeterministic inputs, anchor lineage | Never rewind generation or replace transport identity |
| `GameState::WorldStore` | Typed players, armor/HP, card identity and ordered zones, skill instances, marks, tags, phase/scopes, RNG, history and providers | Reconstruct a detached value world, validate, then publish |
| Managed Lua manager | Explicit provider declarations and per-world pure state trees | Clone, validate all providers/references, publish the prepared map |
| `RoomManagedState` | Native player/card fields, ordered zones/location index, Game/AI RNG, resolution/AI history and shared providers | Prepare detached values; retain Room/runtime/player/VM/wrapped-card identities; publish using nonthrowing swaps |
| `RoomThread` | Ordinary turn execution and GameRule's actual round-start predicate | Only an unwound outer-turn boundary; extra turns and native continuations remain unsupported |
| Transport / lobby | Logical room, sockets, connected clients | Retained across restore; state-sync is sent after publication without sampling gameplay RNG |
| Statistics | Projection keyed by root ID and generation | Consume a successful game publication; retry DB failures without undoing game generation |

The Room and its WorldStore share one GameTimeline object, the only authority
for root ID and generation. `Room::statisticsRootMatchId()` and
`statisticsGeneration()` delegate to that timeline. After successful **whole-game world**
publication the production turn executor calls
`commitStatisticsTimelineRestore(nextGeneration, branchId, anchorKind, anchorId)`
with the committed values. Its old-contribution fence and durable retry policy
belong to statistics. Do not allocate a second root ID or increment generation in
the statistics consumer, and do not invoke the hook while preparing a candidate.
The imported statistics Match retains a projection receipt, not a second game
counter. Prior control changes and takeover origins stay outside the state being undone.
The finalized hook semantics are: `true` means the durable sidecar was saved and
SQLite updating proceeds in the background; `false` leaves uncertainty and may
be retried with the same arguments. Neither result reverses an already committed
game restore/generation. Endgame replaces that root match from authoritative full
valid history. A skill-local restore must not call this hook. The pre-game
managed-setup demonstration also does not call it or claim statistics integration.
Running restores call the real Room statistics hook after native and both managed
VM roots commit. `RoomThread::TimelineCommitObserver` remains an additional
postcommit observer; statistics correctness does not depend on it.
`retryStatisticsTimelineRestore()` retries the exact pending receipt before
ordinary continued play or via the local `retry stats` command. Failure stays
quarantined and cannot reverse the game generation. If the background writer
already completed the failed immediate receipt, retry validates the durable
journal and acknowledges that same generation.

## Values, identity and capability

Schema versions and provider versions are exact-match contracts. A missing,
additional or differently versioned required provider is not silently dropped.
Player/card/skill references are logical IDs, never addresses or Lua registry
indices. A reference is represented as `{"$ref":{"kind":"player","id":"p1"}}`
(also `card` and `skill`). Resolve only against the new world's typed ID index.
An existing object in another world cannot satisfy a missing candidate reference.
Store ordered card zones as sequences; sorting them changes gameplay.

Pure state accepts explicitly supported scalar values and recursively validated
maps/lists. C++ userdata, QObject pointers, arbitrary QVariant types, Lua
functions/threads/userdata, non-finite numbers, metatables, aliases and cycles are
outside the managed tree contract. They require a deliberate typed adapter or a
capability rejection, not `tostring`, pointer copying or lossy JSON conversion.
Provider author declarations are a trust boundary; this is not a Lua sandbox or a
proof that a module has no other mutable globals/upvalues.

Ordinary managed skills and AI use the same per-provider/owner/key store and do
not need individual export/restore callbacks. A provider has an explicit ID,
version and audit scope. The representative tests use one managed skill and one
managed AI state. They do not migrate the native character library. Unmanaged
closures/upvalues remain unsupported; whole-script/global coverage is unknown.
Legacy `RegisterTakeoverStateProvider` callbacks are not enrolled automatically.

For example, a managed skill can use the module directly (AI uses the same store
with its player's stable ID and a separate provider ID):

```lua
local Contract = dofile("lua/game-state-contract.lua")
local manager = Contract.new()
assert(manager:registerManagedProvider {
    id = "example.skill", version = 1,
    audit = {
        author = "package author", scope = "per-instance usage counter only",
        mode = "managed-only", scriptCoverage = "unknown",
        unmanagedClosures = "unsupported", mutableUpvalues = "unsupported",
        externalSideEffects = "unsupported"
    }
})
local world = assert(manager:newWorld("logical-room-1"))
assert(world:setManaged("example.skill", "skill", "instance-1", "uses", 2))
local saved = assert(world:capture())
assert(world:setManaged("example.skill", "skill", "instance-1", "uses", 3))
local candidate = assert(manager:prepareRestore(world, saved, {
    player = {}, card = {}, skill = { ["instance-1"] = true }
}))
assert(manager:activate(world, candidate))
assert(world:getManaged("example.skill", "skill", "instance-1", "uses") == 2)
```

The candidate ID index above is illustrative. In a real adapter it must be built
from the reconstructed C++ world, never from a provider's own claimed references.
The common owner namespace is `player`, `card` or `skill`; an AI provider owns
state under a `player` ID. Getters and exported snapshots return detached copies.

## Candidate reconstruction and success

1. Freeze the expected live world and timeline revision at a quiescent boundary.
2. Check schema/provider versions, declared coverage, continuation capability and
   anchor provenance. Build the candidate object-ID universe.
3. Copy and validate player/card/skill data, ordered locations and all references;
   reconstruct only detached provider state, RNG and history.
4. Validate the completed candidate again. An exception or false result discards
   the candidate. A provider must never retain or mutate live objects.
5. Recheck live revision and candidate ownership. Publish the already prepared
   world and timeline without fallible restore callbacks in the commit path.
6. Only then notify projections and clients. New requests use the new generation.

Success means the complete **declared managed world** was published, generation
advanced once, and previous request/timeout tokens cannot act on it. Failed
prepare or stale/cross-world commit leaves both world and timeline unchanged.
This guarantee does not cover a malicious C++ callback that captures arbitrary
external pointers. Such callbacks violate the provider contract.

The standalone Lua module's map publication and C++ value-world publication are
separate APIs. They must not be called sequentially against a Room and described
as atomic. The production setup adapter instead installs `ManagedStateLuaBridge`
in the Room's Game and AI VMs: both read/write the same C++ provider root. Its
publication has no second Lua activation step. This preserves VM identity and
`LuaCallbackRef` bindings; it does not restore mutable closures or globals.

## Bounded production Room demonstration

`Room::managedState()` owns a `RoomManagedState` whose lifetime outlasts both Lua
VMs. Enrollment is explicit: initialize both VMs, register audited providers,
initialize the adapter, then install one provider namespace into each VM. Lua
uses `sgs.ManagedState.get/set/remove(ownerKind, stableOwnerId, key, ...)`.
`sgs.ManagedState.array()` represents an empty array distinctly from `{}`. Native
bridge calls are restricted to the bound VM's current owner thread and lifetime
generation. Explicit game-worker ownership handoff is supported; both VMs and
the core store must remain serialized on that owner, not accessed concurrently.

The admitted Room has not started, has no worker/continuation/request/card state
to resume, and has not loaded room rules definitions. Its native slice covers
real ServerPlayer HP/max HP, marks (including `@HuJia` armor), pure tags, phase and
fixed skill-instance data. Roster object identities, skill topology, gameplay/AI
RNG and the clean resolution-history journal are invariants: changes reject this
path rather than pretending those native domains were restored. It is useful
for verifying the live publication mechanism, not for restoring an ordinary
running match.

At checkpoint, production native values are captured into the same managed
world that holds both Lua/AI provider namespaces. Candidate construction performs
all fallible copying and validation first. Publication locks both VMs, checks
native fingerprint/revision and candidate identity, publishes the shared root,
then swaps prepared native values without setters, signals, Lua callbacks or
socket dispatch. A stale or rejected candidate publishes nothing. Arbitrary
callbacks that capture and mutate live external objects remain outside the audit
contract.

Setup checkpoints use the distinct `ManagedSetup` kind. They cannot be selected
by `previousPlayerTurn()` or `previousFullRound()`.

The running path is configured before worker start with
`RoomThread::enableManagedTurns(registry)` and `setManagedLuaProviders(game, ai)`.
`actionNormal()` and the focused live test use the same `stepNormalTurn()` executor:
capture at an idle boundary, or apply an already queued restore, then dispatch
the complete real `GameRule::TurnStart`, player phases, and next-player transition.
`requestManagedRestore(PlayerTurn|FullRound)` queues work on the game owner thread;
it never tries to resume a captured native stack. A rejected restore in the
production loop reports its error and keeps playing the current world.
The target anchor is fixed when the request is accepted. During an active turn,
the preceding ordinary turn/round is selected; between turns, the last completed
player turn is preceding, and at GameRule's next-round boundary the just-completed
round is preceding. Waiting for the stack to unwind cannot move that target.

The initial supported running envelope is explicitly audited `02p` normal
GameRule with deferred/unloaded room rules definitions, fixed living
roster/skill/general topology, stateless native TrustAI,
and managed Game/AI provider state. Active card definitions are a deliberately
small audited whitelist; dynamic definition changes, arbitrary Card subclasses,
unmanaged Lua/SmartAI state, controller redirection, races, external agents,
pending extra turns/requests, dying and nested continuations are not admitted.
Inactive card catalogue state is frozen and checked. The fixture exercises
Slash/Jink/Peach; whitelist membership alone is not an audit of every surrounding
skill or package.

At the outer `NotActive` boundary, the native `Suijiyingbian` property must be
absent or an empty QString; every nonempty value is unsupported. Integer
`distanceTo_<enrolled player>` QObject properties are derived client sync caches,
never the server's distance authority. They remain attached to their objects
and are invalidated/recomputed by the postcommit RoomThread path. All other
dynamic player properties remain unsupported.

The reserved `engine.native` provider carries native bookkeeping absent from the
public typed projection: phase schedules, flags, card metadata, location indexes,
full resolution history and AI event records. It cannot be exposed as a Lua
namespace or rewritten by a provider restore hook. This is part of the same
checkpoint root, not a separate native save store. Existing `RoomRuntime` and
services remain alive; prepared native values are swapped after the shared root
publishes, with no callbacks or allocation in the publication section.
The native `ComboMovesCard` cache has a narrow owned-clone contract: only audited
concrete card classes with exclusive same-world ownership are rebuilt. Its
prepared lifetime-lease map is published alongside native tags; failed candidates
release only their staged clones. This does not admit arbitrary userdata or
escaped/shared card references.
The retained `HpChangedData` tag supports the fixture's `RecoverStruct` through
an explicit typed descriptor: amount, reason, logical player/card references,
and physical wrapper-versus-inner identity. Reconstruction resolves only current
enrolled objects and prepares native payload leases before publication. Other
HP event payload types, retired card objects and foreign-world pointers reject.
`UseHistory` has a corresponding typed contract for ordinary physical
`CardUseStruct` values, preserving targets, flags, response lists and history
metadata. Owned skill cards, active skill continuations and unaudited target-mod
references reject; these fields are never silently discarded.
Transport sequence numbers, connection identity and diagnostic execution IDs are
monotonic and are not rewound. Decoded reply buffers are transient transport
state; persistent skill memory must use managed values instead of reading a
previous request's leftover buffer. Presentation caches are invalidated/rebuilt
after commit, then the existing state-sync protocol replaces client projections.

## Anchors, accepted decisions and external inputs

`previous_player_turn` means the prior ordinary player-turn start, excluding
extra turns and nested continuations. `full_round` uses the preceding actual
GameRule round-start scope. No seat-count arithmetic can substitute for this:
`GameRule::TurnStart` starts a round when the first alive player's ordinary turn
advances `TurnLengthCount`, then runs the `RoundStart` hooks.
`GameRule::beginsNormalRound()` is shared by that rule and the outer-turn executor.
The checkpoint is taken before either effect, and paired round/turn anchors are
published atomically and refer to the same immutable checkpoint. Restoring that
round retains its paired first-player anchor. A rejected restore does not first
publish a checkpoint for the abandoned upcoming turn. Extra turns and continuations have distinct
anchor kinds; their existence does not imply that their C++ stack is restorable.

The timeline records an accepted request's identity, generation, command, player,
selected value and source. Late replies, duplicate replies and timeout races
must use that same token. Record nondeterministic values (clock reads, external AI
responses, external seeds) at the point they are accepted, before dependent game
effects. Replaying a rejected proposal or sampling the clock again is incorrect.
`RequestCoordinator` binds wire message IDs to timeline request tokens, accepts
replies on the I/O side only for the current generation, and journals consumption
or timeout on the game owner thread. `ChoiceMade` records the rule-accepted
decision separately from a merely decoded transport reply. This distinction
matters because an otherwise valid reply may still be rejected by game rules.
Race/controller paths invalidate this bounded capability until their acceptance
semantics are migrated. Native waits cannot overlap a restore boundary.
The new journal is a usable recording primitive; neither it nor the existing
outbound Replay stream is yet a complete command replay engine.

`NosRendeCard::use` currently reads wall time and conditionally consumes gameplay
RNG for audio selection (`src/package/standard-generals.cpp`). Consequently even
the ordinary native package needs an audit or migration. No native package is
implicitly declared restore-safe by this branch.

## Remaining integration work

* Extend the audited running envelope with explicit skill/AI/provider migrations,
  dynamic topology, additional Card subclasses and service-owned state. Merely
  replacing `RoomRuntime` remains unsafe: its shutdown touches Room/player tags.
* Existing `GlobalSnapshot`
  schema 3 is not upgraded by relabeling it as this schema; explicit armor,
  complete scope/phase/continuation and provider coverage must be verified.
* Extend resource accounting beyond the bounded checkpoint count when admitting
  larger managed games. The default store retains eight player-turn checkpoints,
  four full-round checkpoints and two other anchors. Paired round/turn anchors
  share a payload; a round's structural turn alias outside the turn window is
  not independently rewindable. Successful restore prunes abandoned future
  payloads atomically. Failed restore retains the original payload set. Timeline
  journal records remain available to consumers and are not count-pruned.
* Migrate remaining semantic decision sites, SmartAI, controller/race acceptance,
  and asynchronous sources. `ChoiceMade` is not proof that all existing decision
  APIs have been fully journaled. SnapshotService/replay files and external
  observers are not rewound by this core; skills consuming those sidecars need
  separate audit and branch-aware migration.
* Route all gameplay-affecting nondeterminism through accepted-value recording,
  and implement a verified dispatcher before promising deterministic replay.
* External effects such as sent messages, files and third-party actions cannot
  automatically be undone; define suppression/idempotency/compensation per effect.

There is no default enablement or ordinary-package support. The local console
and the separately explicit native GUI/network debug entry below expose the same
bounded native executor; ordinary shipped packages are not declared supported
by these fixtures' success.

## Private local rewind console

Configure the main engine with `-DQSAN_BUILD_REWIND_LAB=ON` and build
`qsanguosha_rewind_lab`. This option is off by default and requires a POSIX
desktop platform. Start it with the ordinary staged engine assets:

```sh
./qsanguosha_rewind_lab --asset-root /path/to/runtime --user-data-root /path/to/local-data --seed 1
```

The process creates one private `02p` Room, two living players without generals
or skills, stateless native TrustAI, and a fixed physical Slash/Jink/Peach deck.
It starts a real `RoomThread` and executes the normal `GameRule`. The first JSON
`ready` response follows the first completed player turn. Enter `step`,
`rewind turn`, `rewind round`, `retry stats`, `status`, or `quit` on standard input. Input is a
bounded, serialized local-operator mailbox with a generation check; it is never
a remote player command. EOF completes queued commands and exits.

`rewind turn` returns to the start of the preceding player's turn. `rewind round`
uses the actual GameRule full-round anchor. Successful rewind pauses before that
turn's effects; `step` continues it. Repeated rewinds move further back, and an
unavailable retained anchor is rejected without mutation. Room/runtime/VM/player
identities stay intact. History hashes, both players' hands, draw/discard order,
HP/armor/phase/turn counters, gameplay RNG position and monotonic generation are
reported for this deliberately omniscient local debug session. Hidden information
already seen cannot be unlearned. This process has no listener, lobby registration,
spectator path or online clients. Its production statistics lifecycle carries
`restricted_rewind_lab` as an exclusion reason and cannot enter ordinary datasets.
An unfinished lab only leaves a timeline fence; it is never submitted as a
completed match. The console reports the statistics path, shared root/generation
and pending durable-receipt status. Its application data location is separate
from the normal client.

The console permits at most 128 turn advances and 128 successful restores per
process, independent of undo. These bounds also limit retained journal growth in
this profile. It does not resume arbitrary native stacks, migrate SmartAI or
Lua closures, restore general/skill topology, or make unsupported card-effect
tags serializable. Unknown state still rejects capability validation. In
particular, the legacy GlobalSnapshot `TrickEffectData`/`NullifyingEffect`
CardEffectStruct problem is not claimed fixed by this basic-card profile.


## Explicit native GUI / TCP debug entry

The native **Start Server** dialog now offers a default-off restricted rewind
checkbox. It requires 02p, cheats enabled, SmartAI disabled, no second general,
hegemony, melee, takeover or scenario work. The dialog passes a one-shot application
property to the Server constructor; the constructor consumes it and retains the
choice only for that server's rooms. It is not persisted in QSettings. The QML
home server-setup page does not expose this checkbox.

This profile assigns two connected seats to fixed `sujiang` presentation and
stateless native TrustAI, with 24 physical Slash/Jink/Peach cards and no skills.
Both clients remain connected to their original Room and player objects. They
observe their own private hands while the owner advances whole ordinary turns.
It is **not a human-choice game** and does not enable rewind for ordinary shipped
skill packages. Equipment/judgment/special zones are empty in this admitted
inventory; wider equipment/provider support has not been demonstrated here.

After both seats join, the cheat-enabled room owner starts the room. The game
control panel and controller menu expose advance, previous-player-turn rewind,
full-round rewind and cancellation of a locally pending control. Existing Qt
focus/navigation makes the same actions reachable by mouse, keyboard and the
controller menu. The visible reason disables unavailable/unauthorized controls.
No consent voting or new permission scheme is introduced. Only the bound room
owner may advance/restore; the other seat may request its own view resynchronization.
Rewind cannot erase information a human already saw in the abandoned future, so
this entry remains an explicit debug profile with local statistics excluded.

`S_COMMAND_MANAGED_REWIND` (137) is a typed Client-to-Room notification, with a
server result on `S_COMMAND_MANAGED_REWIND_STATE` (138). It does not use the legacy
cheat payload or a generic successful request receipt. Every operation carries
rootGameId, worldId, generation, an unguessable per-connection token and a strictly
increasing decimal sequence. Status supports an empty-identity bootstrap.
Malformed/extra fields fail protocol validation. Foreign/stale identities,
reused sequences and unauthorized actions never enter the mutation mailbox.
Status/control tokens are excluded from replay recording.

The mailbox holds one operation, expires unclaimed mutations after five seconds,
and validates the connection/ownership again at the worker boundary. Cancellation
or disconnect can revoke an unclaimed operation; once execution has begun, the
client waits for its final authoritative result instead of pretending to undo it.
Restore still uses the existing validate/prepare/publish transaction and request
coordinator generation fence. `STATE_SYNC` carries root/world/generation and the
absolute round/current-player state. All snapshots use recipient-filtered marshal;
the console's omniscient report never goes onto the network. Read-only resync
marshals only its requester and has a 250 ms per-peer cooldown. Idle acknowledgment
is sent after the snapshot end, and the GUI clears its old mutable player
projection before applying the committed replacement.

Disconnected seats keep their player objects and native TrustAI state. The room's
existing owner succession chooses a connected seat. Reconnect rotates the token,
clears unsent frames from the old transport, and schedules a fresh private snapshot
on the game worker. Socket availability and owner authority are not game-undo
state. The room retains the existing lobby/reconnect identity model; this feature
does not introduce new spectator access or authenticate a different external user.
