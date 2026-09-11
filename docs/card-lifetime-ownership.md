# Card lifetime ownership ledger

This ledger is the finite inventory for the Card-only lifetime boundary. It is
kept beside the implementation so new producers and sinks must be classified
before they are exposed to Lua or a managed Room domain.

| Site | Representation | Owner | Lease/release | Affinity | Implementation |
| --- | --- | --- | --- | --- | --- |
| `Card::Clone` | native Card pointer | caller | caller destruction | caller | PR1 / PR4 |
| `WrappedCard::m_card` | native Card pointer | WrappedCard | replacement/destructor | Room thread for managed adoption | PR4 / PR5 |
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
| `RoomState::m_cards` | WrappedCard map | RoomState | reset/destructor | `Room::thread()` | PR1 / PR5 / PR7 |
| `Player::equips` | outer WrappedCard pointer | RoomState | Room mutation | `Room::thread()` | PR1 / PR4 |
| `ai-runtime` Lua callback | Lua invocation scope | runtime | pcall return | runtime owner | PR1 / PR2 / PR6 |
| `RoomInitializationThread` Lua-held Cards | observed Card objects, including parentless/pending Cards | unpublished Room domain | normal Lua/native release and domain drain | worker pushes remaining Card trees to canonical owner and refreshes manager snapshots before publication | initialization handoff |
| ordinary RoomThread | transient Card domain | Room | worker-final/shutdown | `Room::thread()` | PR1 / PR6 |
| 1v1 RoomThread | transient Card domain | Room | worker-final/shutdown | `Room::thread()` | PR1 / PR6 |
| 3v3 RoomThread | transient Card domain | Room | worker-final/shutdown | `Room::thread()` | PR1 / PR6 |
| XMode RoomThread | transient Card domain | Room | worker-final/shutdown | `Room::thread()` | PR1 / PR6 |
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

The current source scan reports legacy deletion ingress separately. These sites
remain explicitly selected by their owning boundary; the process default is ManagedReclaim after PR7, while ObserveOnly remains available for compatibility characterization; they are
not silently treated as managed reclaim.

## Mutex contention diagnostics

Set `QSAN_CARD_LIFETIME_MUTEX_TRACE=1` before process startup to instrument the
single authoritative manager mutex. When enabled, `CARD_LIFETIME_ZERO` includes
a process-cumulative `mutex_profile` with lock count, contended count, total wait
nanoseconds and maximum wait nanoseconds. The trace adds an atomic counter and an
optimistic `tryLock()` to each acquisition, so trace runs are diagnostic evidence,
not timing acceptance. The profile does not attribute contention to a Room/domain
and does not include the separate card-association mutex.

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
