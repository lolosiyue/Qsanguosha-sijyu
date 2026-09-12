# Card lifetime ownership ledger

This ledger is the finite inventory for the Card-only lifetime boundary. It is
kept beside the implementation so new producers and sinks must be classified
before they are exposed to Lua or a managed Room domain.

| Site | Representation | Owner | Lease/release | Affinity | Implementation |
| --- | --- | --- | --- | --- | --- |
| `Card::Clone` | native Card pointer | caller | caller destruction | caller | PR1 / PR4 |
| `WrappedCard::m_card` | native Card pointer | WrappedCard | destructor; replacement retires the old generation through `retireAdopted()` (lease-gated drain, never inline delete) | canonical owner while adopted; a retired card returns to the registered turn worker of its domain | PR4 / PR5 / adoption retirement |
| SWIG Card exposure | generation token | Card owner | wrapper release | invoking runtime | PR2 |
| `Card::tag` | QVariant matrix | containing Card | overwrite/remove/destructor | containing Card | PR5 |
| `Card::change_cards` | generation sidecar edge | source Card | source destruction | containing Card | PR4 / PR5 |
| `Card::Parse` temporary cards | native Card pointer | parser scope | scope exit | caller thread | PR1 / PR3 |
| `DummyCard` decision paths | native Card pointer | decision boundary | safe point | Room thread | PR1 / PR6 |
| `CardMoveReason::m_extraData` | QVariant Card payload | move reason | dispatch/copy end | Room thread | PR1 / PR5 |
| `CardUseStruct::m_ownedCard` | owned smart pointer `OwnedCardPtr` | use struct | use completion | Room thread | PR1 / PR5 |
| `Player::ComboMovesCard` | tagged QVariant `CardTagOwner` payload | Player tag | overwrite/clear | Room thread | PR1 / PR5 |
| `gamerule.cpp:556 ComboMovesCard` | `CardTagOwner` tag payload (`QVariant::fromValue(CardTagOwner{...})`) | Room game-rule cleanup | deferred legacy deleteLater (`owner.card->deleteLater()`) plus tag remove | Room thread | PR1 / PR4 |
| `gamerule.cpp:1345 judge card` | Card deferred delete | judge cleanup | deferred legacy deleteLater | Room thread | PR1 / PR4 |
| `generic-cardcontainer-ui.cpp:990-1231 simulated equips` | Card deferred delete | UI simulation cleanup | deferred legacy deleteLater | UI thread | PR1 / PR4 |
| `ClientRulesSession::applyDeclaration` | temporary declaration Card and projected Player tag | synchronous query Scene | exact QObject deferred delete, delivered by Scene destruction after JSON evaluation | client rules worker | client declaration boundary |
| `RoomState::m_cards` | WrappedCard map | RoomState | reset/destructor | `Room::thread()` | PR1 / PR5 / PR7 |
| `Player::equips` | outer WrappedCard pointer | RoomState | Room mutation | `Room::thread()` | PR1 / PR4 |
| `ai-runtime` Lua callback | Lua invocation scope | runtime | pcall return | runtime owner | PR1 / PR2 / PR6 |
| `RoomInitializationThread` Lua-held Cards | observed Card objects, including parentless/pending Cards | unpublished Room domain | normal Lua/native release and domain drain | worker pushes remaining Card trees to canonical owner and refreshes manager snapshots before publication | initialization handoff |
| ordinary RoomThread | transient Card domain | Room | turn-end / worker-final / shutdown | game worker for its transients; canonical owner for the remainder | PR1 / PR6 / turn-end |
| 1v1 RoomThread | transient Card domain | Room | turn-end / worker-final / shutdown | game worker for its transients; canonical owner for the remainder | PR1 / PR6 / turn-end |
| 3v3 RoomThread | transient Card domain | Room | turn-end / worker-final / shutdown | game worker for its transients; canonical owner for the remainder | PR1 / PR6 / turn-end |
| XMode RoomThread | transient Card domain | Room | turn-end / worker-final / shutdown | game worker for its transients; canonical owner for the remainder | PR1 / PR6 / turn-end |
| `SkillContext` QVariant payload | extracted `use_card`/`updated_card` plus nested `extra_data`/`interceptor_data` | tag or payload container | tag overwrite/remove or payload release | `Room::thread()` | PR8 |
| `CorrectSkillContext` QVariant payload | extracted `card` | tag or payload container | tag overwrite/remove or payload release | `Room::thread()` | PR8 |

Card-bearing `QVariant` metatypes are classified in three ways. *Lease-bearing by
extraction*: `CardEffectStruct`, `CardTagOwner`, bare `Card *` / `const Card *`,
`SkillContext` and `CorrectSkillContext` have their Card pointers pulled out and leased by
`CardLifetimeManager::retainVariantPayload`. *Lease-bearing by self-registration*:
`CardUseStruct`, `CardResponseStruct`, `DamageStruct`, `SlashEffectStruct`, `RecoverStruct`,
`CardsMoveStruct` and `CardsMoveOneTimeStruct` already retain and release their own native
leases from their copy/assign/destructor hooks, so a `QVariant` copy carries its own leases
and is accepted as-is; `ShownCardChangedStruct` joins that list as an id-only payload whose
name merely happens to contain "Card". *Rejected opaque*: anything else whose type name
contains "Card"/"card" returns the exact error `Card lifetime error: rejected opaque QVariant
Card payload`. A new Card-bearing metatype must be added to one of those three classes;
leaving it unclassified means either a silently dropped tag or an unleased raw Card pointer.

Unknown rows are a gate failure. The checker is intentionally static and is
combined with runtime counters; a clean scan alone is not an ownership proof.

`ClientRulesSession::applyDeclaration` follows the client selection runtime's
existing temporary-card boundary. It queues each clone for QObject deletion
before any rejection can return. Successful declarations remain available through
the projected Player tag until the synchronous query has built its JSON result;
`Scene::~Scene()` delivers their deferred deletes while those Players still exist.
No native pointer escapes the query. The exact upcast-delete site is classified
separately because calling `Card::deleteLater()` would also drain unrelated Cards
through the global manager; this exception does not permit a Room-owned Card to
bypass managed reclamation.

The current source scan reports legacy deletion ingress separately. These sites
remain explicitly selected by their owning boundary; the process default is ManagedReclaim after PR7, while ObserveOnly remains available for compatibility characterization; they are
not silently treated as managed reclaim.

## Turn-end reclamation

The main gameplay `RoomThread` registers its Room domain for turn-end reclamation
after binding the Lua runtime and before dispatching gameplay. While registered,
ordinary `drain()` / `drainDomain()` calls skip that domain, including global
drains caused by another Card's `deleteLater()`. Initialization, client-side Cards
and independently owned `RoomState` / `WrappedCard` objects retain their existing
ownership paths. Registration ends after worker-final cleanup and before the
worker exits; canonical-owner shutdown can then complete its remaining drains.

- Successful outer `TurnStart` return: drain after trigger locals,
  `CardLifetimeScope`, AI event filtering and deferred UI/anytime work have
  finished. Covers normal, 1v1, 3v3 and Hulao Pass gameplay.
- `TurnBroken` / Hulao `StageChange`: drain after the mode's phase cleanup,
  before resuming the turn loop; never from the throwing trigger's unwind.
- Nested immediate or scheduled extra turn: keep the outer invocation's
  protection. Drain the accumulated eligible Cards when the outer turn returns.
- Still retained at turn end: keep pending until a later safe boundary; never
  clear a tag, wrapper, lease, pin or change edge merely to reclaim a Card.
- Game finish / worker exit: keep the existing Lua-close, worker-final and
  canonical-owner shutdown ordering.

`drainTurnDomain()` accepts only the registered worker and its current domain.
It preserves generation, baseline, ownership, invocation, lease and affinity
checks, and selects only pending, live, unowned transient Cards. It dispatches
`DeferredDelete` for each selected receiver on that same thread because the game
worker has no `exec()` event loop. It does not process unrelated queued events or
delete foreign-affinity Cards from the worker.

`CARD_LIFETIME_TURN_END room=<id> retired=<n> pending=<n> live=<n>` reports the Room,
retirement count for that boundary and the remaining domain gauges. These are Card counts,
not process memory measurements. Long nested extra-turn chains or references
retained across turns can still accumulate memory; there is no forced size/age
eviction and no claim of a hard per-turn memory bound.

The asset-free `--suite card-lifetime-turn-reclaim` selector in the existing
runtime test executable covers pending lifetime across boundaries, real worker
destruction, retained references and domain/affinity isolation. The existing
RoomThread deferred-state suite covers the production trigger boundary. These
fixtures do not substitute for full-game crash or memory acceptance.

## Mutex contention diagnostics

Set `QSAN_CARD_LIFETIME_MUTEX_TRACE=1` before process startup to instrument the
single authoritative manager mutex. When enabled, `CARD_LIFETIME_ZERO` includes
a process-cumulative `mutex_profile` with lock count, contended count, total wait
nanoseconds and maximum wait nanoseconds. The trace adds an atomic counter and an
optimistic `tryLock()` to each acquisition, so trace runs are diagnostic evidence,
not timing acceptance. The profile does not attribute contention to a Room/domain
and does not include the separate card-association mutex.

## Adopted inner card retirement

`WrappedCard::takeOver()` / `copyEverythingFrom()` replace the adopted inner card
(view-as delayed tricks, filter skills, `Room::resetCard` when a modified card enters
the discard pile). The replaced generation may still be the running receiver
(`DelayedTrick::onNullified()` throws itself and thereby resets its own wrapper), and
payloads such as `CardMoveReason::m_extraData` hold native leases on its raw pointer.
Deleting it inline freed it under those holders; the next payload copy dereferenced
it in `retainVariantPayload()`.

`CardLifetimeManager::retireAdopted()` therefore turns the `Adopted` generation into
`PendingDelete` instead. On the domain's registered turn worker the card is first moved
back to that worker, so `drainTurnDomain()` frees it at the next quiescent turn end once
leases, wrappers, reservations and change edges are gone. Elsewhere it keeps its owner
and is reclaimed by `drain()` or the shutdown `drainDomain()`. Unmanaged cards and
`ObserveOnly` mode keep the previous destruction. `card-lifetime-wrapped-adoption`
covers both the canonical-owner and the turn-worker paths.

## Initialization worker handoff

Before publishing an asynchronously initialized Room, the worker returns the
definition QObject tree, then calls `handoffInitializedDomain()`. That second
step moves remaining live domain-owned Card roots and refreshes the manager's affinity
snapshots, including Cards already moved with definitions. It does not retire
Cards, drop wrappers/leases, or close the Lua states. The unpublished domain must
have no active invocation scopes or Lua pins; foreign domains and baseline Cards
are excluded. Retired objects keep their existing deferred-deletion cleanup path.
QObject operations run outside the manager mutex.
The completion callback joins the worker before publishing the Room, so deferred
deletions left on that worker finish before the owner can close the domain.

`CARD_LIFETIME_INITIALIZATION_HANDOFF` reports the number of objects still on the
initialization worker, the roots moved, and the tracked snapshots refreshed.
Shutdown domain entries include cached affinity addresses and all drain blockers.
An initialization QThread may already be destroyed by shutdown, so its diagnostic
address must not be dereferenced. A live, pending entry has not passed the manager
retirement gate; repeating DeferredDelete dispatch cannot advance that entry.

The runtime test executable exposes two direct focused suites:

- `--suite card-lifetime-initialization-handoff`: no Engine assets; covers detached
  Cards, definition-tree affinity snapshots, wrapper/native lease retention, scope
  rejection, baseline/domain isolation, and reclamation after the worker dies.
- `--suite card-lifetime-initial-room-close`: uses production async Room creation
  and immediately destroys the waiting Room, without gameplay or network clients.
  Use the matching Qt runtime PATH and enforce a 60-second external timeout locally.
  This tests core Room shutdown, not the GUI return-home event sequence.
