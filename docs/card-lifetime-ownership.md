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
| `CardUseStruct::m_ownedCard` | native Card pointer | use struct | use completion | Room thread | PR1 / PR5 |
| `Player::ComboMovesCard` | tagged QVariant Card pointer | Player tag | overwrite/clear | Room thread | PR1 / PR5 |
| `gamerule.cpp:556 ComboMovesCard` | direct Card delete | Room game-rule cleanup | immediate legacy delete | Room thread | PR1 / PR4 |
| `gamerule.cpp:1345 judge card` | Card deferred delete | judge cleanup | deferred legacy deleteLater | Room thread | PR1 / PR4 |
| `generic-cardcontainer-ui.cpp:1208 simulated equips` | Card deferred delete | UI simulation cleanup | deferred legacy deleteLater | UI thread | PR1 / PR4 |
| `RoomState::m_cards` | WrappedCard map | RoomState | reset/destructor | `Room::thread()` | PR1 / PR5 / PR7 |
| `Player::equips` | outer WrappedCard pointer | RoomState | Room mutation | `Room::thread()` | PR1 / PR4 |
| `ai-runtime` Lua callback | Lua invocation scope | runtime | pcall return | runtime owner | PR1 / PR2 / PR6 |
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
