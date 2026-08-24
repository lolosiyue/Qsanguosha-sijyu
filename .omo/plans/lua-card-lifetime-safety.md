# lua-card-lifetime-safety - Work Plan

## TL;DR (For humans)

**What you'll get:** Lua 即使保留已被 C++ 回收的 Card userdata，後續使用也只會收到穩定錯誤，不再造成 UAF、double delete 或位址重用誤判；既有 Lua/C++ `deleteLater()` 拼法與擴充腳本維持相容。

**Why this approach:** 只為實際進入 Lua 或生命週期管理的 Card 延遲建立世代 token，再以明確 owner、lease、adoption 與安全回收點決定實體析構；這同時避免「全量接管所有牌」與「只修 Lua、留下 C++ 懸空指標」兩個極端。

**What it will NOT do:** 不重寫一般 QObject／Engine 實體牌 ownership，不修改 legacy extensions，不把公開 Card 指標全面改成智慧指標，也不新增 30／50 人產品模式。

**Effort:** XL
**Risk:** High - 同時跨越 SWIG userdata、Lua GC、Qt thread affinity、原生 raw-pointer escape 與 Room shutdown 次序。
**Decisions to sanity-check:** 未知 ownership 的 Lua 刪除會報錯；已知定義卡／已收養卡採相容 no-op；20 人使用真實對局，30／50 僅做生命週期壓測；正式回退依相依順序，只有預設行為開關可單獨回退。

Your next move: 高精度審查通過後，再由獨立執行工作階段依此計畫實作。Full execution detail follows below.

---

> TL;DR (machine): XL/high-risk, eight dependency-ordered Card-only PRs implementing lazy generation tokens, checked SWIG aliases/GC, provenance-aware release, native leases/adoption, owner-thread drains, reversible default enablement, and deterministic 20/30/50-scale verification.

## Scope
### Must have

- Introduce a Card-only lifetime control plane with lazy generation tokens, origin-aware delete requests, managed transient ownership states, keyed pending queues, native leases, owner-thread destruction, and bounded counters.
- Preserve the public Lua spelling `card:deleteLater()`, the public C++ Card spelling `Card::deleteLater()`, `WrappedCard::takeOver/copyEverythingFrom` `void` signatures, and the public mutable `Card::change_cards` type.
- Attach one shared token to every SWIG wrapper of an observed Card, including derived/static-base aliases, through a Lua-invisible side table; validate before conversion/dereference, make Card GC idempotent, distinguish allocator address reuse, and return the exact stale error `Lua error: attempt to use deleted Card`.
- Keep token allocation lazy: known factories register transients; every SWIG exposure calls `observeLive` while an owner, borrow, or invocation scope still proves the Card live; every Card destructor only calls `invalidateIfObserved(this)`. If the destructor wins that linearization, wrapper creation is refused. Observation never transfers Engine/Package ownership. The plan does not claim that an untracked raw pointer can safely create the first wrapper after destruction has begun.
- Store the authoritative token pointer, generation, and original SWIG ownership bit in a Lua-invisible side table / C++ token object. Conversion pins are held by the outermost protected Lua invocation and released only after that `lua_pcall` returns.
- Route Card-as-QObject `QObject::deleteLater()` to Card native-origin policy by dynamic type, while ordinary non-Card QObject deletion stays unchanged.
- Treat Lua delete of unknown `ObservedExternal` Cards as the exact error `Lua error: attempt to delete Card with unknown ownership`; treat known definition/adopted Cards as a compatibility no-op with counters.
- Migrate every in-tree gameplay Card raw delete/default smart-pointer deleter to manager-aware release, while retaining only audited quiescent Engine/Package/RoomState/WrappedCard/test physical owner destruction.
- Close all persistent native sinks before enabling reclamation: Player equipment stored as outer `WrappedCard *` with query-time `getRealCard()`, generation-keyed `change_cards` sidecar edges, frozen QVariant metatype matrix, keyed Player/Room/Card tags, and explicit `ComboMovesCard` ownership.
- Make WrappedCard adoption transactional to the T4 canonical owner (`Room::thread()`): reserve/cancel the exact generation, acquire target lifecycle lease before move, snapshot on incoming affinity, move the parentless Card to `Room::thread()`, commit on the canonical owner thread with no-fail post-move, preserve the old Card on pre-move failure only, and retire replacement exactly once; normalize RoomState outers/inners to the canonical owner after setup worker and before gameplay worker on ordinary/1v1/3v3/XMode, with PR1 dispatcher-liveness characterization that blocks on failure.
- Reclaim only at named canonical-owner safe points after active Card/Lua scopes, native leases, persistent owners, adoption reservations, and compatibility roots are clear; implement worker-final plus preclose/`lua_close`/postclose drains on `Room::thread()`.
- Use one internal `CardLifetimeMode::{ObserveOnly, ManagedReclaim}` selector. PR1-6 production domains use `ObserveOnly`; PR7 changes only `defaultCardLifetimeMode()`. Except the single selector-default regression in PR7, every test explicitly constructs `ObserveOnly` or `ManagedReclaim`; that unique default-mode test is the only one that reads `defaultCardLifetimeMode()` with no override.
- Deliver eight dependency-ordered, regression-first PRs, deterministic counters, focused CTest/Lua regressions, a real fixed-seed `20p` headless run, and synthetic 30/50-actor lifetime stress.

### Must NOT have (guardrails, anti-slop, scope boundaries)

- Do not redesign lifetime/ownership for Package, General, Skill, Player, Room, or general QObject; do not make the manager own Engine prototypes/physical Cards merely because Lua observed them.
- Do not replace the public Card pointer surface with global smart pointers/handles, privatize `change_cards`, change legacy extension source, or add production 30p/50p modes.
- Do not rely on `CardFinished`, a decision return, an empty trigger stack, RoomThread `deleteLater()`, GameOver quarantine, or Lua GC as proof that a Card is physically deletable.
- Do not edit, sync, stage, or commit tracked `swig/sanguosha_wrap.cxx`; regenerate and inspect only `builds/cmake-vs2026/generated/sanguosha_wrap.cxx` from `swig/*.i`.
- Do not guess inside opaque custom QVariant metatypes, persist `QVariant<const Card*>`, accept Card lightuserdata, resurrect a token after destruction begins, invoke Card/Lua/Qt callbacks under the manager lock, or delete a QObject on the wrong affinity thread.
- Do not claim to intercept arbitrary cross-thread direct `change_cards` mutation, and do not re-resolve public-list addresses into live generations on drain.
- Do not add all-Card eager tokens or a permanent raw-address tombstone; do not treat a post-destruction first wrapper from an untracked raw pointer as a supported safe path.
- Do not implement conversion pins as SWIG-local C++ RAII that `lua_error` longjmp can skip; do not store authoritative generation or original-own bits in Lua-writable uservalue tables.
- Do not add a public configuration surface, external dependency, arbitrary middle-PR revert compatibility layer, or hidden fallback that retains all Cards until shutdown.
- Do not copy files between L and H or commit/push unless the later execution invocation explicitly authorizes branch/PR work; preserve unrelated dirty/runtime files.
- Do not treat any historical draft table, formula, mode rule, or reclaim condition as executable. Historical drafts are background only and have no normative force.

## Normative contracts

**Normative authority:** This file is the sole execution specification. Implementers and reviewers must use the N1–N14 tables below; no historical draft table, formula, or pointer is normative. Historical drafts are non-normative background only.

**Locked facts that constrain implementation:**

| Fact | Bound |
|---|---|
| `RoomThread::run` does not call `exec()` | worker-affinity `QObject::deleteLater()` delivery cannot be assumed; ObserveOnly must not invent a new drain |
| `CardFinished`, decision return, and `event_stack.isEmpty()` have downstream Card consumers | they are never reclaim proofs |
| Generated SWIG 4.3.1 creates a fresh userdata per exposure, compares by raw pointer, and uses `SWIG_MustGetPtr` for `CardList` | all three of `SWIG_NewPointerObj` / `SWIG_ConvertPtr` / `SWIG_MustGetPtr` must be wrapped; no wrapper cache |
| An unobserved Card has no token, so `invalidateIfObserved` is a no-op | post-destruction first wrapper from an untracked raw pointer is out of contract; every production SWIG exposure must `observeLive` while an owner/borrow/invocation scope still holds the live object; destructor-won linearization refuses wrapper creation; no all-Card eager tokens and no permanent raw-address tombstone |
| `lua_error` longjmps over C++ automatic destructors inside SWIG wrappers | conversion pins belong to the outermost protected `lua_pcall`/ledgered binding frame and are released only after that call returns, including `LUA_ERR*`; SWIG-local RAII pins are forbidden |
| `QObject::deleteLater()` is not virtual | Card-as-QObject still reaches Card policy by dynamic type (`Card::event` intercepts `QEvent::DeferredDelete`); the Card public slot covers `Card*` static calls; ordinary non-Card QObject timing stays unchanged |
| Lua can replace userdata uservalues | authoritative token pointer, generation, and original SWIG `own` bit live in a Lua-invisible side table plus the C++ token; ConvertPtr/`__eq`/`__gc` must not trust a Lua-writable table |
| Project `%{...%}` block is after stock helpers and `SWIGTYPE_p_Card`, before wrapper bodies | checked macros may redefine the three helpers in `swig/*.i` without editing generated output |
| `%extend Card::deleteLater` already shadows QObject at the Lua surface | it must call the Lua-origin bridge, never the native-origin `Card::deleteLater()` |
| Engine clone wrappers are `own=0`; `sgs.DummyCard()` is `own=1` | conversion safety and Card `__gc` must ship together |
| Only `20p` is a registered product mode | 30/50 exist only as synthetic actor rows; do not add production modes |
| `Player::equips` is `QList<const EquipCard *>` filled from `getRealCard()` (`src/core/player.h:507`, `src/core/player.cpp:882-895,948-1073`) | internal store becomes `QList<const WrappedCard *>`; public EquipCard*/Card* getters keep their current shapes and resolve `getRealCard()` at query; returned raw pointers are valid only until the next Room mutation or named safe point |
| `Card::change_cards` is a public mutable `QList<const Card *>` | preserve type and access; authoritative reclaim edges are `(source_generation, target_generation, duplicate_count)`; compatible mutation is defined only on the Room domain control thread; this plan does not intercept arbitrary cross-thread direct writes |
| `ComboMovesCard` stores a raw clone in a Player tag as `QVariant::fromValue((const Card*)…)` and manual-deletes on replacement | convert to one explicit managed owner; persistent `QVariant<const Card*>` is forbidden |
| Card-bearing event structs and tags copy through `QVariant` | frozen metatype matrix (`lease-bearing` / `scoped borrow` / `rejected opaque`); leases, not empty trigger stacks, block deletion; opaque payload is an explicit error and blocks PR7 |
| `RoomState` outers are main-owned; later `takeOver` can install worker-created inners | adoption must `moveToThread` before commit; worker-final must not delete Adopted Cards |
| `Card` currently has no C++ `deleteLater()` of its own | add a same-name public slot; ordinary QObject timing stays unchanged |
| Gameplay Card raw `delete` / default smart-pointer deleters exist alongside non-Card deletes | classify by static type; no blanket text replacement |
| `Card::Parse` returns a pointer after already scheduling deletion | same-stack use and later adoption must remain defined |
| `safeTurnCardToEquip` has four clone failure exits before adoption | first explicit orphan fixture in PR3 |

### N1 Card domain / owner map

Known fresh results are registered at factory chokepoints. `Card::Clone` is registered briefly, then the two current `RoomState` consumers transition it to `Adopted`; it is not prematurely removed from tracking. Numeric `Card::Parse` remains borrowed. A `Card` destructor always marks the matching generation `Dead`, regardless of who physically deleted it. Parentlessness alone never proves ownership. Dynamic class name alone is never used to decide ownership.

| Category | Entry points | Initial state | Physical deletion authority |
|---|---|---|---|
| A. Engine-owned prototype / runtime template | registered Card definitions selected by `Engine::cloneCard`, Package children, named Lua definitions | `ObservedDefinition` | existing Engine/Package/RoomRuntime owner; never the manager merely because Lua observed it |
| B. WrappedCard-owned inner Card | `WrappedCard` constructor, `takeOver`, `copyEverythingFrom` | `Adopted` | current `WrappedCard`; on replacement, authority transfers once to the retirement queue |
| C. Known-factory temporary | `Engine::cloneCard`, `cloneSkillCard`, `Card::Clone`, every `Lua*Card::clone` | `UnclaimedTransient` plus birth transaction | explicit delete/adoption/managed owner may claim it; after that transaction, only a zero-wrapper/zero-owner/zero-pin orphan is implicitly retired |
| D. Lua-owned transient | Card constructor wrapper with SWIG `own=1`, e.g. `sgs.DummyCard()` | `LuaOwnedTransient` with owning-wrapper lease | manager-aware Card `__gc` or explicit delete request; not orphan-reaped while a live owning wrapper remains, and never stock-direct deleted after adoption/death |
| E. Borrowed / non-owning | numeric `Card::Parse`, `getCard`, `getRealCard`, struct fields | source token state plus a non-owning C++/Lua lease | existing C++ owner |
| F. Unknown | Card pointer not attributable to a registered factory/owner | `ObservedExternal` plus diagnostic | Lua delete fails closed; native delete may claim only after definition/borrowed/adopted exclusions and records the native provenance |

### N2 Complete state transitions

```text
Unclassified -- definition/parent/registry ---> ObservedDefinition
Unclassified -- known factory ----------------> UnclaimedTransient
Unclassified -- owning wrapper ---------------> LuaOwnedTransient
Unclassified -- borrowed exposure ------------> ObservedExternal
Unclassified -- native Card::deleteLater -----> PendingDelete  (only after owner exclusions)
UnclaimedTransient -- Lua/C++ delete request -> PendingDelete
UnclaimedTransient -- explicit owner/lease ---> ClaimedTransient
UnclaimedTransient -- birth scope ended,
                      zero wrappers/owners/pins -> Retired ------> Dead
UnclaimedTransient -- last non-owning wrapper GC,
                      scope ended/no owner/pin -> Retired -------> Dead
LuaOwnedTransient --- Lua/C++/last-owner-GC ---> PendingDelete
*Transient ---------- WrappedCard reserves ---> Adopting ------> Adopted
PendingDelete ---- WrappedCard reserves ------> Adopting -----> Adopted   (cancel queue)
Adopting --------- affinity-transfer failure -> prior state    (restore/requeue)
PendingDelete ---- safe-point + zero C++ pins -> Retired --> Dead
Adopted ---------- replace/destruct ----------> Retired --> Dead
Any live state -- approved quiescent owner destroy -> Dead
Any live state -- unapproved runtime raw delete ----> Dead + Debug/CI gate failure
```

| From | Event / guard | To |
|---|---|---|
| `Unclassified` | definition/parent/registry | `ObservedDefinition` |
| `Unclassified` | known factory | `UnclaimedTransient` |
| `Unclassified` | owning SWIG wrapper `own=1` | `LuaOwnedTransient` |
| `Unclassified` | borrowed exposure | `ObservedExternal` |
| `Unclassified` | native `Card::deleteLater()` after definition/borrowed/parent/adopted exclusions | `PendingDelete` |
| `UnclaimedTransient` | Lua or native delete request | `PendingDelete` |
| `UnclaimedTransient` | explicit owner/lease | `ClaimedTransient` |
| `UnclaimedTransient` | birth scope ended AND zero wrappers AND zero owners AND zero pins; ManagedReclaim only for production orphan reap | `Retired` |
| `UnclaimedTransient` | last non-owning wrapper GC AND scope ended AND no owner/pin; ManagedReclaim only for production orphan reap | `Retired` |
| `LuaOwnedTransient` | Lua/C++ delete request or last owning `__gc` | `PendingDelete` |
| `ClaimedTransient` | owner release / authorized delete | `PendingDelete` |
| `*Transient` / `PendingDelete` | WrappedCard reserve of exact generation | `Adopting` (pending queue cancelled if present) |
| `Adopting` | owner-thread commit | `Adopted` |
| `Adopting` | affinity-transfer or invariant failure | restored prior state; incoming Card queued on current affinity as specified in N9 |
| `PendingDelete` | named safe point AND all N4 blockers clear AND mode is ManagedReclaim | `Retired` |
| `Adopted` | WrappedCard replace/destruct through manager | `Retired` |
| any live | approved quiescent Engine/Package/RoomState/WrappedCard/test owner destroy | `Dead` |
| any live | unapproved runtime raw delete | `Dead` plus Debug/CI gate failure |
| `Retired` | owner-thread physical destroy completes | `Dead` |

| State | Lua dereference | Queue membership | Meaning |
|---|---:|---:|---|
| `ObservedDefinition` | allowed while token live | no | Engine/Package/Room definition; Lua delete is never allowed to take ownership |
| `ObservedExternal` | allowed while token live | no | external/unknown owner seen by Lua |
| `UnclaimedTransient` | allowed while its birth scope or a live wrapper/lease remains | orphan candidate | known fresh non-owning factory result awaiting disposal, explicit owner/lease, adoption, or wrapper release |
| `LuaOwnedTransient` | allowed while owning wrapper/lease lives | no | constructor result whose SWIG owner is managed by Card-aware GC |
| `ClaimedTransient` | allowed | no | explicit native persistent owner/lease; owner release returns it to pending/retirable state |
| `PendingDelete` | allowed | yes, cancelable | grace period preserves same-call-stack use after `deleteLater()` |
| `Adopting` | allowed only to the adoption transaction | no | generation-reserved transfer; safe-point retirement is blocked |
| `Adopted` | allowed | no | `WrappedCard` owns the live Card; owning Lua GC cannot delete it |
| `Retired` | rejected with `Lua error: attempt to use deleted Card` | owner-thread destruction queue | logically dead; native object may still exist briefly |
| `Dead` | rejected with the same Lua error | no | destructor completed; token can survive only through stale wrappers |

`PendingDelete` remains dereferenceable during the grace period. Only `Retired`/`Dead` fail conversion. ObserveOnly production domains do not take the `PendingDelete -> Retired` managed safe-point edge; they preserve each migrated call site's original immediate-versus-deferred physical timing, including the current worker non-delivery of `QObject::deleteLater()` when `RoomThread` has no event loop.

### N3 Provenance × delete origin × mode

Delete origins: Lua = public `card:deleteLater()` via `%extend` Lua-origin bridge; Native = public-slot `Card::deleteLater()`; GC = Card-aware owning `__gc`; OwnerDestroy = direct/native owner destruction; Adopt = WrappedCard adoption transaction.

**Physical overlay** (applied by mode whenever a cell enters `PendingDelete`, becomes orphan-retirable, or retires a replaced inner Card):

| Outcome | ObserveOnly | ManagedReclaim |
|---|---|---|
| `PendingDelete` | keep this call site's PR1-recorded immediate vs deferred physical timing; do not add managed drains on production domains; destructor still `invalidateIfObserved` | stay queued until a named owner-thread safe point with all N4 blockers clear, then `Retired -> Dead` exactly once on `card->thread()` |
| orphan-retirable `UnclaimedTransient` | production domains do not reap; explicit ManagedReclaim tests may | reap at the next advancing hook only if `unknown_unclaimed == 0` |
| WrappedCard replacement retirement | original `m_card->deleteLater()` / destructor timing of that site | manager owner-thread retirement of the exact generation, once |
| compatibility no-op / exact Lua error | no physical change | no physical change |
| approved quiescent owner destroy | owner-thread physical destroy through the audited path | same path, manager-validated affinity/zero-lease/generation |
| unapproved raw delete | already physical; Debug/CI gate failure; destructor marks `Dead` | forbidden leftover; same gate failure |

Policy strings are mode-independent. Mode only changes physical timing after a cell names an overlay outcome.

| Provenance | Origin | ObserveOnly | ManagedReclaim |
|---|---|---|---|
| `Unclassified` | Lua | exact `Lua error: attempt to delete Card with unknown ownership`; state/queue unchanged; no physical delete | same policy; no physical delete |
| `Unclassified` | Native | after definition/borrowed/parent/adopted exclusions, record native provenance and enter `PendingDelete`; else reject. Physical: overlay PendingDelete | same policy. Physical: overlay PendingDelete |
| `Unclassified` | GC | missing owning-wrapper classification is a Debug/CI failure; no stock-direct delete | same |
| `Unclassified` | OwnerDestroy | unapproved runtime direct delete is a gate failure; destructor still marks `Dead` | same; leftover sites fail the PR7 zero gate |
| `Unclassified` | Adopt | reserve only if parentless/live and ownership checks pass; else N9 failure | same policy; physical of displaced inner uses overlay replacement |
| `ObservedDefinition` | Lua | compatibility no-op + `definition_delete_ignored`; Card remains usable | same |
| `ObservedDefinition` | Native | Debug ownership failure; production no-op + diagnostic; no manager physical | same |
| `ObservedDefinition` | GC | disown wrapper / no-op | same |
| `ObservedDefinition` | OwnerDestroy | only quiescent Engine/Package/RoomRuntime owner teardown | same, manager-validated |
| `ObservedDefinition` | Adopt | reject | reject |
| `ObservedExternal` | Lua | exact `Lua error: attempt to delete Card with unknown ownership`; `unknown_card_delete +1`; state/queue unchanged | same |
| `ObservedExternal` | Native | if no definition/adopted/parent owner remains, explicit native relinquishment enters `PendingDelete`; otherwise reject. Physical: overlay PendingDelete | same policy. Physical: overlay PendingDelete |
| `ObservedExternal` | GC | non-owning no-op | same |
| `ObservedExternal` | OwnerDestroy | approved external owner teardown only; active lease/owner is a failure | same |
| `ObservedExternal` | Adopt | reserve only with explicit native transfer authority and owner checks | same |
| `UnclaimedTransient` | Lua | enter `PendingDelete`. Physical: overlay PendingDelete | same policy. Physical: overlay PendingDelete |
| `UnclaimedTransient` | Native | enter `PendingDelete`. Physical: overlay PendingDelete | same policy. Physical: overlay PendingDelete |
| `UnclaimedTransient` | GC | decrement wrapper count; if last wrapper after birth scope and zero owner/pin, mark orphan-retirable. Physical: overlay orphan | same policy. Physical: overlay orphan |
| `UnclaimedTransient` | OwnerDestroy | migrate to manager; raw runtime delete forbidden | same |
| `UnclaimedTransient` | Adopt | reserve; cancel orphan eligibility | same |
| `LuaOwnedTransient` | Lua | enter `PendingDelete`. Physical: overlay PendingDelete | same policy. Physical: overlay PendingDelete |
| `LuaOwnedTransient` | Native | enter `PendingDelete`. Physical: overlay PendingDelete | same policy. Physical: overlay PendingDelete |
| `LuaOwnedTransient` | GC | last owning wrapper enters `PendingDelete`. Physical: overlay PendingDelete | same policy. Physical: overlay PendingDelete |
| `LuaOwnedTransient` | OwnerDestroy | raw runtime delete forbidden | same |
| `LuaOwnedTransient` | Adopt | reserve and disown the owning wrapper | same |
| `ClaimedTransient` | Lua | owner-policy error unless the caller holds delete authority; no silent physical | same |
| `ClaimedTransient` | Native | owner release/delete request enters `PendingDelete`. Physical: overlay PendingDelete | same policy. Physical: overlay PendingDelete |
| `ClaimedTransient` | GC | non-owning no-op | same |
| `ClaimedTransient` | OwnerDestroy | explicit owner releases through manager | same |
| `ClaimedTransient` | Adopt | reserve only after owner transfer | same |
| `PendingDelete` | Lua | idempotent counter / no-op | same |
| `PendingDelete` | Native | idempotent counter / no-op | same |
| `PendingDelete` | GC | clear wrapper ownership / no-op | same |
| `PendingDelete` | OwnerDestroy | manager-only destruction after zero pins/owners. Physical: overlay PendingDelete | same policy. Physical: overlay PendingDelete |
| `PendingDelete` | Adopt | reserve exact generation and cancel queue | same |
| `Adopting` | Lua | reject / retry after transaction | same |
| `Adopting` | Native | reject / retry after transaction | same |
| `Adopting` | GC | no-op | same |
| `Adopting` | OwnerDestroy | forbidden | forbidden |
| `Adopting` | Adopt | current generation transaction only | same |
| `Adopted` | Lua | compatibility no-op + `adopted_delete_ignored`; never delete inner | same |
| `Adopted` | Native | Debug owner conflict; production no-op | same |
| `Adopted` | GC | disown / no-op | same |
| `Adopted` | OwnerDestroy | WrappedCard replacement/destructor through manager. Physical: overlay replacement | same policy. Physical: overlay replacement |
| `Adopted` | Adopt | same-owner idempotent only | same |
| `Retired` / `Dead` | Lua | stale-card error `Lua error: attempt to use deleted Card` | same |
| `Retired` / `Dead` | Native | diagnostic / no-op | same |
| `Retired` / `Dead` | GC | no-op | same |
| `Retired` / `Dead` | OwnerDestroy | duplicate-destroy failure | same |
| `Retired` / `Dead` | Adopt | reject | reject |

### N4 Reclaim blockers

A named safe point is only a scheduling opportunity. Physical deletion additionally requires every row below to be clear. An explicit delete request may retire the C++ Card even if wrappers remain; a still-live wrapper keeps an unrequested clone active and counted.

| Blocker | What it is | Blocks | Does not block | Clear condition |
|---|---|---|---|---|
| Native lease | compact non-owning `CardLifetimeLeaseSet` on registered event/QVariant/tag payloads, keyed by `(container, key, generation)` | `PendingDelete` drain and `Retired -> Dead` for that generation | Lua stale error after `Dead`; delete of a different generation | last exact-generation copy released by destructor, assignment, overwrite, or remove |
| Explicit owner | `ClaimedTransient`, `CardUseStruct::m_ownedCard`, `ComboMovesCard` managed tag, `Adopted` WrappedCard link | physical delete and orphan reap of that generation | Lua definition/adopted no-op policy | owner release through manager; WrappedCard replacement transfers once |
| Finite root / `change_cards` | sidecar edge `(source_generation, target_generation, duplicate_count)` from N8.2 | physical delete of a target generation while any edge still names it; any non-self cycle of length ≥ 2 | claiming that arbitrary memory or cross-thread direct list writes are intercepted; re-resolving public-list addresses after reuse | every naming edge dropped (source destructor clears outgoing; incoming Dead generations dropped and never rebound); `change_list_cycles == 0` and `change_list_self_cycle == 0` before PR7 |
| Lua invocation | per-runtime `luaInvocationDepth` / closing epoch plus conversion pins held by the outermost protected `lua_pcall`/ledgered binding frame for any runtime that holds a wrapper for the token | physical delete until that outermost call returns | token invalidation after `Dead`; conversion while `PendingDelete` | outermost depth 0, conversion pin count 0, and epoch not closing-against-this-call |
| Adoption reservation | `Adopting` plus the exact-generation reserve | safe-point retirement of that generation | other generations | N9 commit to `Adopted` or failure restore/requeue |
| Wrapper count (owning) | side-table original-own bit from first exposure (`own=1`), not a Lua-writable field | orphan reap; stock `__gc` double-delete | explicit Lua/native delete request (may enter `PendingDelete` while wrappers remain) | last owning `__gc` or disown on adoption |
| Wrapper count (non-owning) | side-table original-own `0` aliases | orphan reap of `UnclaimedTransient` | explicit delete request | last wrapper `__gc` after birth scope |
| Birth scope / epoch | `CardLifetimeScope` depth and factory birth epoch | orphan eligibility | explicit delete request | outermost audited scope close; exception unwind releases the same scope; AI/network wait keeps it active |
| ObserveOnly mode | production domain selector before PR7, or an explicit ObserveOnly test | managed safe-point physical reclaim and production orphan reap | token/diagnostics/Lua errors/N3 policy; legacy physical timing | explicit `ManagedReclaim` test, or PR7 `defaultCardLifetimeMode()` |
| Worker-final Adopted skip | inner Card already moved to canonical owner `Room::thread()` affinity (T4) | worker-final drain of that Card | canonical-owner later replacement/dtor | N9 success; remaining worker-affinity Adopted is an invariant failure |
| Runtime `Closing` | Lua shutdown epoch | new calls and new transients | `__gc` count decrement | N5 preclose / `lua_close` / postclose protocol |

`CardUseStruct::m_ownedCard` remains the one existing explicit C++ owner; it is not duplicated by a second owning field. The new lease is non-owning and only blocks retirement. Ordinary stack locals need no per-pointer registration because deletion happens only after the enclosing transaction boundary.

### N5 Safe points, worker-final, terminal shutdown (C5/C6 — T5)

**N5.1 Binding vs invocation separation (C5):** 本計畫強制分離兩個 RAII 維度，禁止用同一個 Binding 兼作呼叫保護。

| 類型 | 生命週期 (Lifetime) | 職責 | 是否改動 invocation/pin |
|---|---|---|---|
| `LuaRuntime::Binding` | 長生命 access／affinity binding。外層持有，確保當前執行緒可存取 Lua 狀態、正確 affinity 及 `m_executionMutex`。 | 將指定 `LuaRuntime` 設為 `currentRuntime`、必要時 `adoptCurrentThread()`、鎖住執行互斥；提供 `state()`、`isCurrentThreadOwner()` 檢查；**不**觸及 `luaInvocationDepth` 或轉換 pin。 | 否 |
| `LuaInvocationScope` | 只包實際 `lua_pcall`／callback。短生命，每次受控 Lua 呼叫前建構、呼叫返回（含 `LUA_ERRRUN/LUA_ERRMEM/LUA_ERRERR`）後才解構。 | 原子遞增該 runtime `luaInvocationDepth`、在最外層 frame 建立轉換 pin（per-token pin count）、快照 closing epoch；巢狀 pcall 共用最外層 frame，內層 `lua_error` longjmp 不可卸除最外層 pin。 | 是，僅最外層 frame |

* 禁止在 SWIG wrapper 內以 C++ RAII 持有 pin（會被 `lua_error` 跳過）；pin 必須由呼叫端的 `LuaInvocationScope`（最外層 protected `lua_pcall`）持有。
* `LuaRuntime::Binding` 可在 `RoomThread::run` 外層、`RoomRuntime::initialize` 全程等長期範圍持有；`LuaInvocationScope` 僅在每次 trigger/AI/skill 回呼的 `lua_pcall` 臨界區內存在。

Each Room/runtime domain owns a monotonically increasing lifetime epoch plus `activeCardScopeDepth` and `luaInvocationDepth`. A known factory records its birth epoch. RAII `CardLifetimeScope` opens at `Room::useCard`, every public PlayerDecisionService ask/response from the PR1 ledger, and outer `RoomThread::trigger`. `LuaInvocationScope` (not SWIG-local RAII) increments invocation depth and pins immediately before each ledgered `lua_pcall`/protected binding and decrements only after that call returns; nested pcalls share the outermost frame. Before a Card pointer escapes a scope as a return value, event payload, tag, `change_cards` edge, or WrappedCard input, that boundary must promote it to a wrapper count, lease, explicit owner, or adoption transaction. Exception/control-flow unwinds release the same C++ `CardLifetimeScope`s; an AI/network wait keeps the current scope active. `LuaRuntime::Binding` unwind does not count as invocation exit.

Closing the outermost `CardLifetimeScope` only marks birth/orphan candidates; it never directly deletes. Engine/Package construction outside a Room domain has no implicit orphan drain; registration must classify definitions before gameplay exposure. The factory-caller characterization gate must show `unknown_unclaimed == 0` at every advancing hook before production orphan reap.

**N5.2 Drain proposal vs destructive drain (C5):**

* phase／turn hook 只可提出 drain attempt。位於 phase 迴圈頂部、turn 迴圈頂部的鉤點（`tryDrainIfEligible()`）僅做「可回收性檢查並嘗試排程」；若仍處於外層 trigger 內部或 `luaInvocationDepth > 0`／N4 blocker 未清，則僅計數 `boundary_reap_blocked_*` 並返回，不做破壞性刪除。
* destructive drain（`PendingDelete -> Retired -> Dead` 及 orphan reap）只能在最外層 trigger、control cleanup、exception cleanup 完整返回後執行。判斷條件：該 domain `activeCardScopeDepth == 0`、對應 runtime `luaInvocationDepth == 0` 且 closing epoch 未處於 `Closing` 對抗呼叫、所有 N4 blockers（native lease / explicit owner / `change_cards` sidecar / adoption reserve / wrapper count / birth scope）皆 clear，且當前執行緒為 `card->thread()`（canonical owner）。

| Boundary | Verdict | Rule |
|---|---|---|
| `CardFinished` | reject | `Room::useCard` and exception cleanup still read `use.card` afterward |
| Lua callback / decision return | reject | callers still consume returned `Card*` or dispatch response events |
| `event_stack.isEmpty()` | reject alone | trigger cleanup, UI/cache, summons, and anytime work follow the pop |
| GUI next event-loop turn | conditional pass (proposal) | queue on the Card affinity context and recheck generation/state/adoption before deletion; destructive only after outermost return |
| Room phase boundary | conditional pass (proposal only) | at top of next phase iteration call `tryDrainIfEligible()`; destructive drain deferred until outermost trigger/control/exception cleanup fully returned and N4 clear; do not use exception-unwind RAII destructor to delete |
| Turn/control-exception boundary | conditional pass fallback (proposal only) | same: recheck after `TurnBroken`/`StageChange` cleanup; destructive only after that cleanup fully returned |
| Active Lua invocation (`LuaInvocationScope` depth > 0) | reject | checked conversion is protected by outermost `LuaInvocationScope` until that call returns; SWIG-local RAII is not the pin |
| Worker final unwind | required | see N5.4 worker-final order below (destructive, after quiescence) |
| Room/runtime shutdown | required | see N5.5 `shutdownFinal()` below |

**N5.3 Enumerated safe paths (C5 — 明列路徑):** 以下路徑皆經 PR1 盤點，任何新增路徑須更新本表並加測試；未列路徑不得執行 destructive drain。

| 路徑 | 進入點 | 允許的動作 | 破壞性回收點 | 備註 |
|---|---|---|---|---|
| normal phase loop | `RoomThread::trigger` 外層 phase iteration 頂部 | proposal only | 最外層 `trigger` 返回且 `activeCardScopeDepth==0`、`luaInvocationDepth==0`、N4 clear 後的下一輪 drain | 正常回合內 phase 切換 |
| normal turn loop | `Room::useCard` / turn iteration 頂部 | proposal only | 同上，外層返回後 | 正常回合間 |
| TurnBroken | `TurnBroken` 控制例外拋出 → catch 清理 → 返回 | proposal during catch 禁止；cleanup 返回後 proposal | cleanup 完整返回後，若 N4 clear 則 destructive | 需等待 Room/Player 狀態回滾完成 |
| StageChange | `StageChange` / phase 跳轉控制例外 → 清理 | 同 TurnBroken | 同 TurnBroken | 階段變更 |
| extra-turn | `ExtraTurnScheduler::scheduleExtraTurn` / `Room::executeExtraTurn` / `processScheduledExtraTurns` 邊界 | proposal at extra-turn setup 頂部 | 前一 turn 的 control cleanup 完整返回後，且 extra-turn 上下文建立完成後的下一個 outermost drain | 不可在 extra-turn 調度中途刪除 |
| worker-final ordinary | `RoomThread::run` 外層 `qScopeGuard` (Binding 之後宣告) | destructive (terminal) | 見 N5.4 嚴格順序，於 worker return/join 前 | ordinary 模式 |
| worker-final 1v1 setup | `RoomThread1v1::run` setup worker 外層 guard | destructive | 同 N5.4 | 1v1 setup |
| worker-final 1v1 gameplay | `RoomThread1v1::run` gameplay worker 外層 guard | destructive | 同 N5.4 | 1v1 gameplay |
| worker-final 3v3 setup | `RoomThread3v3::run` setup worker 外層 guard | destructive | 同 N5.4 | 3v3 setup |
| worker-final 3v3 gameplay | `RoomThread3v3::run` gameplay worker 外層 guard | destructive | 同 N5.4 | 3v3 gameplay |
| worker-final XMode | `RoomThreadXMode::run` 外層 guard(s) | destructive | 同 N5.4 | XMode |
| GUI remainder | `Room::thread()` 事件循環下一個 turn | proposal → destructive if still outermost | 需在 canonical owner 上重檢 generation/state/adoption | 僅處理 GUI 親和性殘留 |

所有 phase/turn 相關列皆為 proposal only；真正的 `Retired->Dead` 僅在該列「破壞性回收點」欄所述「最外層返回後」條件滿足時執行。

**Canonical owner setup normalization (T4):** immediately after the setup worker completes and before any gameplay worker starts, normalize all RoomState outer/inner Cards to `Room::thread()` (canonical owner per N9). This fixup runs once per Room domain on the setup completion path and is required on ordinary, 1v1, 3v3, and XMode. Gameplay adoption thereafter always moves to the same canonical owner.

**N5.4 Worker-final order (strict):** stop new transient creation -> enter quiescence -> clear `ComboMovesCard` and every worker-affinity explicit managed owner -> destroy/release copied event payload leases -> apply `change_cards` sidecar and tag releases (do not re-resolve public-list addresses) -> terminally drain every matching unadopted worker transient/pending generation, even if Lua wrappers remain (they become stale before Lua close) -> assert zero worker-affinity managed transients -> return/join. `Adopted`/observed definition entries are skipped. Any remaining worker-affinity adopted Card is a failed invariant (canonical owner fixup per N9 must have already moved it). After join, `shutdownFinal()` handles only canonical-owner pending entries and persistent owners. Required on ordinary, 1v1, 3v3, and XMode `run()` paths. Each worker path has its own outer `qScopeGuard` declared **after** the outer `LuaRuntime::Binding` so the guard fires before the binding leaves scope on normal return, `GameFinished`, early return, and control exception.

**N5.5 Lua two-drain protocol (shared):** `Running -> Closing` rejects new calls and snapshots runtime-owned/unclaimed generations; pre-close drain retires/deletes eligible Cards while wrapper metadata is still inspectable; Card `__gc` during `lua_close` is deletion-idempotent and only releases wrapper counts/ownership; after `Closed`, a final owner-thread drain processes any last zero-wrapper orphan and requires zero runtime-owned pending/unclaimed tokens. No GC/destructor hook calls back into Lua or schedules work onto an exited worker. Standalone `LuaRuntime::shutdown()` for non-Room domains uses the same two-drain shape, but the runtime owner must provide a domain finalizer or assert that it has no Card tokens; it does not run the full Room 8-step sequence.

**N5.6 Terminal shutdown — unique idempotent `RoomRuntime::shutdownFinal()` (C6):** 全進程唯一終局關閉入口，冪等（idempotent，首個呼叫原子置位 `Closing`，重入為 no-op），順序固定、與解構次序無關。僅由 `Room::~Room()`（及等價的 Room 擁有者析構路徑）呼叫；`RoomRuntime::~RoomRuntime()` 僅委託至此函式，不另做清理。

| 順序 | 動作 | 具體語意與不變量 |
|---:|---|---|
| 1 | join／handoff workers | 呼叫 `Room::stopGameThreads()` 並等待每個 worker 的外層 `qScopeGuard`（N5.4）完成；失敗為 fatal，不可在錯誤執行緒執行 fallback drain；確保無 worker 再產生新 transient |
| 2 | 原子進入 Closing | 對 game 與 AI 兩個 runtime 執行 `Running -> Closing` CAS；此後 `LuaInvocationScope` 拒絕新 `lua_pcall`，已快照 runtime-owned/unclaimed generations |
| 3 | 清除 persistent roots | 在兩個 Lua runtime 及 process registry 仍存活時，執行冪等 Room owner-cleanup helper：delete/release 已 join 的 thread-owned 物件、已拷貝的 event payload leases、Player/Room tags（含 `ComboMovesCard`）、players；套用 keyed tag/sidecar releases（不做位址 rescan）；斷言零 worker-affinity entries |
| 4 | preclose drain | 在 canonical owner 上執行 preclose drain：僅處理未被採納（unadopted）且符合 N4 的 GUI/runtime transients，wrapper metadata 仍可檢查；採納後（Adopted）及定義卡保留 |
| 5 | AI Lua 及 game Lua 執行 `lua_close`，此時 registry/domain 仍有效 | 依序 `AiLuaRuntime::shutdown()` → `LuaRuntime::shutdown()`；要求 `luaInvocationDepth==0`；Card `__gc` 僅遞減 wrapper/ownership counts，不可 enqueue 刪除或呼叫 Lua；registry 與 Card domain 在 `lua_close` 期間保持有效，使 destructor 的 `invalidateIfObserved` 安全 |
| 6 | unregister／postclose | 從 process registry 移除 runtime 關聯；在 owner thread 執行 postclose drain：處理最後零 wrapper orphan，校驗 per-runtime pending/unclaimed/wrapper/lease counters 回到 baseline，並輸出每 domain evidence snapshot |
| 7 | physical-owner destruction | 正常 C++ 成員析構：`RoomState` / `WrappedCard` / definition 擁有者在其 affinity（canonical owner）上銷毀所屬 Cards；因 process registry 仍存活，析構期 `invalidateIfObserved` 安全，即使 Lua 已關閉 |
| 8 | final-zero assertion | 終局斷言：process registry 仍存活期間，校驗所有 managed/token/wrapper/unclaimed/lease/explicit-owner/sidecar-edge 及 per-runtime invocation/closing counters 為 baseline；`actually_destroyed` 等單調計數保持一致；無 manager 鎖內 Lua/Qt/Card 呼叫 |

* 任何非 `shutdownFinal()` 路徑不得重排或省略上述順序；解構次序推斷不可替代顯式呼叫。
* `RoomRuntime::shutdownFinal()` 冪等：以原子 flag 保護，重入直接返回；`RoomRuntime::~RoomRuntime()` 僅 `if (!closed) shutdownFinal()`。

**N5.7 AI init-failure／restart shutdown 與 terminal shutdown 分離 (C6):** 以下兩者與 N5.6 終局路徑嚴格分離，不得共用同一函式或重排步驟：

* `AI init-failure`：`RoomRuntime::initialize()` 內 `m_ai.initialize()` 失敗時，僅走 `AiLuaRuntime::shutdownForInitFailure()`（或等價的局部回滾）：關閉已部分建立的 AI Lua 狀態、釋放 AI-side 臨時資源、保留 game Lua 及 Room 生命週期；**不**執行 N5.6 的步驟 1/3/4/6/7/8，不清除尚未建立的 persistent roots，不對 game domain 做 pre/postclose drain。若 game Lua 本身初始化失敗，則走 `RoomRuntime::shutdownForInitFailure()` 的 game 失敗分支：僅清理已建立的 game Lua 及已配置的 registry 條目，不觸及 worker join（workers 尚未啟動）。
* `restart`：任何對局重啟／Room 重建請求必須先完成當前 Room 的 N5.6 `shutdownFinal()`（若 Room 已進入 gameplay），或若重啟發生在 init 階段則走上述 init-failure 局部路徑；禁止在 `Running` domain 上直接重建 Lua 而跳過 `Closing` 原子轉換與 pre/postclose drains。重啟路徑與終局路徑共用同一原子 `Closing` 語意，但為獨立函式入口（`shutdownForRestart()`），以避免與析構期 terminal shutdown 競爭或重入混淆。

Manager locking is metadata-only under a short mutex. It must never call Lua, emit signals, move/delete a QObject, or invoke Card methods while holding the mutex. The manager never acquires `SafeLuaMutex`. Physical deletion occurs outside the lock on `card->thread()`. A cross-thread request only marks `PendingDelete`; marshalling to the affinity thread happens after the requester reaches a proven safe point (N5.2/N5.3), not immediately while its call stack can still use the Card.

### N6 Counters, zero gates, bounded formulas

The live registry contains one token per currently observed/managed Card. A destroyed Card is removed from the address map; stale userdata retains only its small token. A keyed pending set removes entries on adoption/destruction. Native leases and sidecar edges exist only for current event payloads, explicit persistent owners, or live `change_cards` generations. Retained memory is proportional to live Cards + current pending deletions + current native leases/sidecar edges + intentionally retained stale wrappers, never cumulative historical clone count. A leftover `change_cards` sidecar edge that is never cleared intentionally blocks physical deletion and is reported as a live blocker.

**Required counters/gauges:** `clone_created`, `factory_unclaimed`, `factory_unpaired_exit`, `unknown_unclaimed`, `unknown_escape`, `boundary_reap_candidate`, `boundary_reap_blocked_by_wrapper`, `boundary_reap_blocked_by_lua_invocation`, `native_delete_requested`, `lua_delete_requested`, `definition_delete_ignored`, `adopted_delete_ignored`, `pending_delete`, `adoption_reserved`, `adopted`, `adopted_after_delete_request`, `adoption_failed`, `affinity_transfer_failed`, `retired`, `actually_destroyed`, `external_direct_destroy`, `unapproved_card_raw_delete`, `stale_access`, `double_delete_request`, `unknown_card_delete`, `card_delete_bypass`, `blocked_by_legacy_change_list`, `change_list_self_cycle`, `change_list_cycles`, `change_list_reuse_reconnect`, `unknown_qvariant_card_payload`, `preclose_retired`, `postclose_orphan_retired`, `peak_managed_cards`, `peak_unclaimed_transient`, `peak_pending_delete`, `peak_wrapper_count`. Also expose current managed/token/wrapper/unclaimed/lease/explicit-owner/sidecar-edge and per-runtime invocation/closing counts plus cross-thread/owner-mismatch diagnostics. Detailed per-object traces are compile-time disabled in production; Debug/CI gets aggregate counters and optional traces.

**Reset rules:** `unknown_unclaimed`, current managed/token/wrapper/unclaimed/lease/owner/edge counts, pending counts, and invocation depths are per-domain gauges that must return to baseline at the matching finalizer. Request/destroy/error counters are monotonic per process; only a test-only reset is allowed when no lifetime domain or token is live. Peak gauges reset at the start of each isolated scenario. Production startup does not silently clear zero-gate counters.

**PR7 default-on zero gates** (all must be machine-asserted before changing `defaultCardLifetimeMode()`):

| Gate | Required value |
|---|---|
| `unknown_unclaimed` | `== 0` |
| `unknown_qvariant_card_payload` | `== 0` |
| `change_list_self_cycle` | `== 0` |
| `change_list_cycles` | `== 0` |
| `change_list_reuse_reconnect` | `== 0` |
| `unapproved_card_raw_delete` | `== 0` |
| `card_delete_bypass` | `== 0` |
| `adoption_failed` | `== 0` |
| `affinity_transfer_failed` | `== 0` |
| unlisted persistent tag/root/lease rows | `== 0` |
| worker/runtime final gauges | baseline / zero |
| source checker | clean |
| focused / Lua / cumulative gates | green |

**Bounded formulas:**

Ring stress: `iterations = 10000`, `batch = 64`, `ring = 64`. After every batch, current pending/lease/explicit-owner/sidecar-edge counts equal the captured baseline.

```text
peak_live_tokens <= baseline + batch + ring + explicitly_retained_tag_or_lease_entries
actually_destroyed == eligible_created_count
```

Peaks follow batch/ring size, never iteration count. A token retained across iterations violates the peak formula.

Synthetic 30/50-actor rows: `actor_count ∈ {30, 50}`, `epochs = 200`, `transient_ops_per_actor_per_epoch = 4`, seed `2026082201`，**單一獨立 `CardLifetimeManager` domain（`ManagedReclaim`）、單一 canonical owner thread（`Room::thread()`／測試 owner thread）、每 epoch 生產者 barrier 對齊所有 actor、每 actor 每 epoch 四項操作固定順序 `create transient -> expose/alias via observeLive -> request delete (Lua/Native 交替) -> tryDrainIfEligible`**，唯一 CTest（`qsanguosha_card_lifetime:synthetic-30`／`qsanguosha_card_lifetime:synthetic-50` 參數化）。每 epoch 內 `managed_live <= actor_count` 且 `pending_delete <= actor_count` 且 `wrapper_leases <= 2*actor_count`；每 epoch 末 `drain` 後 `managed_live==0 && pending_delete==0 && wrapper_leases==0 && lease/explicit-owner/sidecar-edge 回到 baseline`。After every epoch, current gauges equal the captured baseline.

```text
peak_live_tokens <= baseline + (actor_count * transient_ops_per_actor_per_epoch) + explicitly_retained_tag_or_lease_or_wrapper_ring_entries
actually_destroyed == eligible_created_count
```

Per-epoch 斷言（synthetic 30/50）：`managed_live <= actor_count`、`pending_delete <= actor_count`、`wrapper_leases <= 2*actor_count`；每個 epoch 末 `drain` 後 `managed_live==0 && pending_delete==0 && wrapper_leases==0` 且 `lease/explicit-owner/sidecar-edge` 回 baseline。違反即為 gate 失敗。

Peaks must not scale with epoch count. These rows are not product 30p/50p modes.

RSS is supporting evidence only, never a brittle CI threshold. Deterministic gauges are the merge gate.

### N7 SWIG bridge

This is the reduced lazy-token design. It does **not** add all-Card eager tokens and does **not** add a permanent raw-address tombstone. It also **retracts** any earlier claim that an untracked raw pointer can safely create the first wrapper after the Card destructor has begun or completed. That path is out of contract.

1. Every Card-convertible SWIG exposure, including derived and static-base aliases, must call atomic `observeLive/getOrCreateToken` **before** stock `SWIG_Lua_NewPointerObj`, and only while an owner, borrow, or invocation scope still proves the pointer live. A static `QObject*` path may attach only when the address is already registered or an audited producer supplies safe Card recognition under the same live scope. Dynamic class name alone is never used to decide ownership.
2. `observeLive` and `Card::~Card()->invalidateIfObserved(this)` share one registry mutex. An existing live generation is reused. If the destructor already holds lifetime control, wrapper creation is refused and the address is never resurrected. An unobserved Card still has no token; therefore a dangling untracked pointer after destruction is not a supported safe first-wrapper path and must not be papered over with eager tokens or an address tombstone.
3. After a live observation wins, call stock `SWIG_Lua_NewPointerObj`, copy the original SWIG `own` bit into the Lua-invisible side table / C++ token, then clear stock userdata ownership so Card `__gc` cannot double-delete. Bind the wrapper to the token through that side table (optional sealed uservalue is a non-authoritative cache only). `SWIG_ConvertPtr`, `SWIG_MustGetPtr`, `__eq`, and `__gc` read token pointer, generation, and original-own from the side table and C++ token, never from a Lua-writable table. Forged or stripped uservalues fail closed.
4. Checked `SWIG_ConvertPtr` and `SWIG_MustGetPtr` inspect the wrapper token before any cast or native dereference. `Retired`/`Dead` raises `Lua error: attempt to use deleted Card`; `PendingDelete` remains valid. Card lightuserdata is rejected.
5. Conversion pins are held by the **outermost** `LuaInvocationScope` (outermost protected Lua invocation) for that runtime. The C++ caller (via `LuaInvocationScope`) increments invocation depth and the token pin immediately before the ledgered `lua_pcall`/protected callback and decrements them only after that call returns, including `LUA_ERRRUN`/`LUA_ERRMEM`/`LUA_ERRERR`. Nested pcalls share the outer `LuaInvocationScope` frame; inner `lua_error` longjmp cannot drop the outer pin. SWIG-wrapper local C++ RAII and `LuaRuntime::Binding` must not own these pins: `lua_error` longjmp skips SWIG destructors and Binding is long-life. Retirement checks the same outermost depth, so a converted raw pointer stays pinned until the outer protected call returns.
6. A dispatching `__eq` compares token identity/generation when both operands are Card-backed. The same stale aliases remain equal; an address-reused new Card is unequal. Tokenless userdata delegates to stock SWIG behavior; one tokenized and one tokenless operand is unequal and counted.
7. A dispatching `__gc` delegates for non-Card/tokenless userdata. For a Card token, it decrements that token's wrapper count, removes the side-table row for that wrapper, and clears only that wrapper's original-own bit once without dereferencing a dead pointer: last owning GC requests managed deletion for `LuaOwnedTransient`; last non-owning GC may expose a post-birth-scope `UnclaimedTransient` as an orphan; `PendingDelete` is already queued, `Adopted` belongs to `WrappedCard`, and `Retired`/`Dead` is a no-op.
8. The dispatcher must be installed wherever a tokenized Card can appear, including derived Card metatables and static-`QObject` exposures; CardList/other `SWIG_MustGetPtr` paths are explicit regressions.
9. `%extend Card::deleteLater` invokes the Lua-origin deletion bridge directly; it does not call `Card::deleteLater()`, which is reserved for native-origin policy. Both preserve their existing public spelling.
10. Only `swig/*.i` and C++ helpers are edited. The generated wrapper is inspected/regenerated, never hand-modified or synchronized. Tracked `swig/sanguosha_wrap.cxx` remains untouched.

Token wrapper counts are partitioned by runtime. Conversion safety spans the whole outermost protected Lua execution, not only `SWIG_ConvertPtr`.

### N8 Native escape closure and sink ledger

The manager must not attempt to discover every stack-local raw pointer. It closes the finite sinks that can survive the current synchronous transaction. N8.1–N8.3 close C3/H1/H2; N8.5–N8.6 close M3.

#### N8.1 Player equipment

Internal storage becomes `QList<const WrappedCard *>`. `setEquip` stores the RoomState outer `WrappedCard` for that id, never `equip->getRealCard()`. A caller that passes a non-outer inner `EquipCard` is a classified failure unless an audited fake-equip path first supplies the outer.

Public getters keep their current shapes and resolve the current inner at query time:

| API | Keep returning | Query-time resolution |
|---|---|---|
| `getEquip`, `getWeapon`, `getArmor`, `getDefensiveHorse`, `getOffensiveHorse`, `getTreasure` | `const EquipCard *` | `qobject_cast<const EquipCard *>(outer->getRealCard())` |
| `getWeapons`, `getArmors`, `getDefensiveHorses`, `getOffensiveHorses`, `getTreasures` | `QList<const EquipCard *>` | same per element |
| `getEquips` | `QList<const Card *>` | current real cards of matching slots |
| `getEquipsId` / `hasEquip` / counts | existing id/bool/int shapes | compare ids of current outers/reals; do not cache inner pointers |

Returned raw `EquipCard*`/`Card*` values are **scoped borrows**. They are valid only until the next Room mutation or named safe point (`takeOver`, `copyEverythingFrom`, `RoomState::reset`/`resetCard`, `moveCards*`, `setEquip`/`removeEquip`, `Player::copyFrom`, or a drain). Stashing a getter result across those boundaries is UAF. `CompareByLocation` must sort by the current real card's `location()`, not a retained inner pointer.

Direct consumers of the `equips` member, not only of the getters, must be listed in PR1 and migrated in PR4. Starting set (PR1 completes the rest; do not confuse with AI/snapshot id lists named `equips`):

| Consumer | File | Required migration |
|---|---|---|
| `setEquip` | `src/core/player.cpp:882-895` | store outer `WrappedCard *`; sort by current real `location()` |
| `removeEquip`, `hasEquip`, `hasEquip()` | `src/core/player.cpp:898-934` | identify by id against outers |
| `getWeapon`/`getArmor`/`getDefensiveHorse`/`getOffensiveHorse`/`getTreasure` and list/count variants | `src/core/player.cpp:948-1088` | query-time `getRealCard()` |
| `CompareByLocation` | `src/core/player.cpp:877-880` | compare current reals |
| `Player::copyFrom` | `src/core/player.cpp:2495-2530` | copy the outer `WrappedCard *` list; tag map follows N8.3 |
| `Player::addCard` / `ServerPlayer::addCard` | `src/core/player.cpp:2921`, `src/server/serverplayer.cpp:586` | keep passing `Sanguosha->getCard(id)` / existing outer |
| `tenyear2` Huashang resync `setEquip` | `src/package/tenyear2.cpp:28466-28474` | becomes redundant once storage is outer; remove only that now-dead resync, do not broaden package cleanup |

`Player::copyFrom` currently copies `equips = p->equips` and `tag = QVariantMap(p->tag)`. After N8.1 the equip copy is stable identity; the tag copy is a whole-map QVariant copy and must follow N8.3.

#### N8.2 `change_cards` sidecar

Preserve the public `QList<const Card *>` type and read/write spelling. The public list is a compatibility surface, **not** the reclaim source of truth.

**Room domain control thread** (ticket phrase “Room owner thread”): the gameplay worker while ordinary/`1v1`/`3v3`/`XMode` `run()` owns the domain; the Room QObject thread after those workers have joined. It is **not** the Qt Room QObject thread during `run()`. Compatible mutation of `change_cards` is defined **only** on that thread, through `addChange` / clear-replace helpers (and the one dedicated compatibility fixture, which must call the same sidecar snapshot API at mutation time). `CardUseStruct::changeCard` / `CardResponseStruct::changeCard` already run on the worker during `run()`; they stay legal. This plan does **not** claim to intercept arbitrary cross-thread direct mutation of the public list. Production in-tree bypasses must be zero by the source checker.

Each sidecar edge is `(source_generation, target_generation, duplicate_count)`:

| Event | Sidecar effect |
|---|---|
| append live target T from source S | increment `duplicate_count` of `(S.gen, T.gen)` |
| `S.gen == T.gen` (self-cycle, including S appending S) | gate failure: `change_list_self_cycle +1`, Debug/CI fail, PR7 blocked; do not install the edge |
| remove one matching pointer / decrement | decrement; drop the edge at 0 |
| clear | drop every outgoing edge of S.gen |
| source destructor / `invalidateIfObserved(S)` | drop every outgoing edge of S.gen |
| target generation becomes `Dead` | drop incoming edges that name it; never rebind them |
| new Card allocated at a reused address | new generation; old edges do not reconnect (`change_list_reuse_reconnect` must stay 0) |

Drain and orphan eligibility consult the sidecar only. They must **not** re-resolve public-list addresses into the current live map; that would reconnect reused addresses.

Other cycles (`S → T → … → S` with length ≥ 2) block reclaim of every generation on the cycle. Gauge `change_list_cycles`. Production must show `change_list_cycles == 0` before PR7. A leftover cycle is a live blocker plus a default-on failure, not silent breakage.

Finite `ChangeListRootSet` enumerates who may carry a public list: manager-observed/managed Cards, Engine/RoomRuntime definition and physical-card collections, RoomState outer/inner Cards, current event-lease roots, and explicit managed tag owners. It does not allocate tokens for otherwise unobserved Cards. An un-tokenized RoomState/definition root mutated through the compatibility fixture still snapshots generations at mutation time.

#### N8.3 Frozen QVariant / event metatype matrix

Every Card-bearing `Q_DECLARE_METATYPE` and every `QVariant::fromValue` producer is frozen into exactly one class. A missing class is a PR1 failure. PR5 implements only classified rows. An opaque or unclassified payload returns the exact diagnostic `Card lifetime error: rejected opaque QVariant Card payload`, increments `unknown_qvariant_card_payload`, leaves destination state unchanged, and blocks PR7. Production never guesses inside custom metatypes and never installs a room-wide fallback blocker.

| Class | Meaning | Persistent store allowed? |
|---|---|---|
| `lease-bearing` | copyable value or registered wrapper whose copy/move/assign/dtor acquire and release exact generation leases; nested standard `QVariantList`/`QVariantMap` recurse | yes; **required** for any payload that outlives the producing scope |
| `scoped borrow` | non-owning pointer/view valid only inside the producing `CardLifetimeScope` / askFor dispatch | no |
| `rejected opaque` | custom metatype without an extraction/copy/release policy | no; explicit error, PR7 blocked |

Direct `QVariant<const Card*>` (`Q_DECLARE_METATYPE(const Card*)` in `src/core/card.h:335`) is **not** a persistent representation. It is a scoped borrow only inside an audited dispatch. Persistent Player/Room/Card tags, `CardMoveReason::m_extraData`, detached QVariants, and `ComboMovesCard` must use a copyable lease-bearing wrapper (explicit managed owner for ComboMoves; keyed lease otherwise).

Per-class operation matrix (every classified type has these cells in the PR1 ledger; PR5 implements them):

| Operation | `lease-bearing` | `scoped borrow` | `rejected opaque` |
|---|---|---|---|
| copy | acquire leases for every nested Card generation | legal only while the producer scope is live; a copy that escapes is a gate failure | error, no lease |
| move | transfer leases; source left empty | same as copy | error |
| overwrite / `setTag` / map insert | release old keyed leases, acquire new | reject if the destination is persistent | error; destination unchanged |
| destructor / `removeTag` / container dtor | release exact generations | leftover persist after scope end is a failure | error diagnostic |
| nested `QVariantList`/`QVariantMap` | recurse; any opaque child fails the parent | recurse; child persist fails | fail closed |
| detached QVariant (copied out of event/`data`/tag) | the detached copy is a new lease-bearing value and must itself be released | detach beyond the producer scope is forbidden | fail closed |

Starting classifications (PR1 completes against current `structs.h` / `card.h` / `skill.h` / `skill-instance-types.h` / `skill-dialog-info.h`):

| Metatype | Class | Notes |
|---|---|---|
| `CardUseStruct`, `CardResponseStruct`, `DamageStruct`, `CardEffectStruct`, `SlashEffectStruct`, `RecoverStruct` | lease-bearing | every Card pointer named in the current declaration; `change_cards` generations ride with the Card; `CardUseStruct::m_ownedCard` stays the single explicit owner and uses a manager-aware deleter |
| `CardsMoveStruct`, `CardsMoveOneTimeStruct` | lease-bearing iff they embed Card pointers or a `CardMoveReason`; else non-Card | nested `m_extraData` follows this matrix |
| `JudgeStruct*`, `PindianStruct*`, `YishiStruct*` | scoped borrow | pointer metatype; Card fields inside the live pointed object are borrows of that object |
| `const Card*` | scoped borrow only; persist forbidden | every `setTag`/`m_extraData`/`ComboMovesCard` producer of this type migrates to lease-bearing |
| `ServerPlayer*`, `SkillAmountChangeStruct`, skill-instance/dialog metatypes | non-Card unless a Card pointer is smuggled | smuggled Card* without a row is rejected opaque |
| any other `Q_DECLARE_METATYPE` | PR1 classify; default rejected opaque if Card-bearing | missing row is a gate failure |

Known structs that are themselves copied as QVariant values (`QVariant::fromValue(card_use)` in `Card::onUse`) are lease-bearing. `QVariant::fromValue(judge)` passed into `askForCard` is a scoped borrow for that ask.

Whole-map copies are first-class ledger rows, not an afterthought: `Card::tag = …` (`WrappedCard::takeOver`/`copyEverythingFrom`, `CardUseStruct::changeCard`), `Player::copyFrom`’s `tag = QVariantMap(p->tag)`, and any `QVariantMap` assignment of Card/Player/Room tags. Each copied child is classified by this matrix.

#### N8.4 Remaining sinks

| Sink | Planned representation | Why it closes the gap |
|---|---|---|
| `ComboMovesCard` tag | one explicit managed owner value; not `QVariant<const Card*>` | overwrite/removal retires exactly once and cannot race the transient queue |
| arbitrary Card/Player/Room tags | mechanical audit plus allowlist: IDs/definitions allowed; a transient Card requires a lease-bearing owner/lease value | a persistent `QVariant<const Card*>` or opaque payload cannot silently defeat the safe-point proof |
| native Card deletion ingress | Card-specific public-slot `deleteLater()` routes Card-typed C++ and name-based meta-object calls to native-origin policy. Card-as-QObject `QObject::deleteLater()` is not virtual, so `Card::event` intercepts `QEvent::DeferredDelete` and routes that instance to the same Card policy by dynamic type. Ordinary non-Card QObject `deleteLater()` is unchanged. Source audit still rejects qualified/member-pointer `QObject::deleteLater` on Card* as defense in depth; the base destructor always invalidates aliases for unavoidable direct `delete` owners | preserves current C++ spelling; Card-as-QObject deferred delete cannot bypass Card policy; non-Card QObject timing stays unchanged |
| direct `delete`, smart-pointer deleter, and QObject-parent destruction | migrate every gameplay Card raw delete to a managed request; give `CardUseStruct::m_ownedCard` a manager-aware final deleter; restrict direct physical destruction to manager-validated quiescent WrappedCard/RoomState/Engine/Package/test owners and destructor invalidation | a checked destroy API enforces affinity/zero-lease/owner generation before `delete`; `external_direct_destroy` with an active lease/owner fails Debug/CI |
| worker-affinity Card adopted by a main-owned `WrappedCard` (T4) | transactional adoption moves the parentless Card to canonical owner `Room::thread()` before committing `Adopted`; the owner link remains until replacement/destruction on canonical owner | worker-final skips it |

#### N8.5 PR1 ledger mandatory rows

PR1 creates `docs/card-lifetime-ownership.md` as the checked finite sink ledger, and PR5 may implement only rows present there. In addition to every N8.3 classified metatype, these rows are mandatory:

| Representation / sink | Card fields or payload | Acquire/copy hook | Release hook / unknown policy |
|---|---|---|---|
| `CardUseStruct` | `card`, `m_ownedCard`, nested `change_cards` generations | constructors, copy/move/assignment, `changeCard` | destructor/assignment; `m_ownedCard` keeps its single explicit owner and uses a manager-aware deleter |
| `CardResponseStruct` | `m_card` and replaced-card history | constructors, copy/move/assignment, `changeCard` | destructor/assignment |
| `DamageStruct`, `CardEffectStruct`, `SlashEffectStruct`, `RecoverStruct` | every Card pointer named by the current `src/core/structs.h` declaration | constructors, copy/move/assignment and QVariant registration | destructor/assignment |
| `JudgeStruct*` / `PindianStruct*` / `YishiStruct*` | Card fields of the pointed live object | scoped borrow at ask/dispatch | producer-scope exit; persist/detach is a failure |
| every other registered Card-bearing struct | exhaustive `Q_DECLARE_METATYPE` plus `QVariant::fromValue` producer inventory | row-specific copy/move hook before PR5 begins | row-specific destructor/dispatch-scope release; missing row is a gate failure |
| `CardMoveReason::m_extraData` | registered lease-bearing nested values only; no persistent `const Card*` | reason construction/copy/assignment recursively applies the matrix | overwrite/destructor/dispatch end; opaque custom type is the exact opaque error |
| Player/Room generic tags | registered lease-bearing values, including `ComboMovesCard` | `setTag`/equivalent stores `(container,key,generation)` keyed lease or explicit owner | overwrite, `removeTag`, container/player destruction |
| Card-owned `Card::tag` | same matrix as Player/Room tags | `setTag`, whole-map `tag = …` in `takeOver`/`copyEverythingFrom`/`changeCard` | `removeTag`, Card destructor, whole-map overwrite |
| Whole-map copy | `QVariantMap` assignment of Card/Player/Room tags | `Player::copyFrom`, `CardUseStruct::changeCard` tag merge, WrappedCard inner tag copy | each child released/acquired per class; opaque child fails the copy |
| `Player::copyFrom` | `equips` outers plus `tag` map | copy outer `WrappedCard *` list; classify every tag child | destination player destruction / later overwrite |
| `ComboMovesCard` | current `QVariant::fromValue((const Card*)clone)` plus `delete` on replace | one explicit managed owner | overwrite/`removeTag`/player destruction retires once |
| QObject parent-child destruction | Package/Engine/Room parented Cards; `QObject` tree delete of a Card child | parent dtor / `delete parent` | manager-validated quiescent owner path; active lease/owner is `external_direct_destroy` |
| `RoomState::reset` | deletes every outer `WrappedCard` then recreates | `RoomState::reset` | quiescent owner destroy of prior outers/inners, then new Adopted inners |
| `RoomState::resetCard` | `Card::Clone` + `copyEverythingFrom` (old inner `deleteLater`) | `resetCard`/`filterCards` | adoption transaction of the clone; old inner generation retired once |
| `CardUseStruct::m_ownedCard` | `QSharedPointer<Card>` default deleter | `setOwnedCard`, copy/move of the struct | manager-aware final deleter; not a second owning field |

The ledger records representation, producer paths, copy/move/overwrite/destructor behavior, owner versus lease versus scoped-borrow semantics, affinity, implementation PR, and focused test. Default-on gates include `unknown_qvariant_card_payload == 0`, `change_list_self_cycle == 0`, `change_list_cycles == 0`, `change_list_reuse_reconnect == 0`, and zero unlisted persistent Card-bearing tag keys. This does not alter Player, Skill, Package, or Room object lifetime.

**Roots** are manager-observed/managed Cards, Engine/RoomRuntime definition/physical collections, RoomState outer/inner Cards, current event roots, and explicit managed tags.

#### N8.6 Checker blind spots

`tools/check-card-lifetime.py` must classify these patterns even when a text search for `delete card` misses them. An unlisted hit is a PR1/PR7 failure. Self-tests feed one fixture of each kind:

| Blind spot | What it hides | Required checker behavior |
|---|---|---|
| `qDeleteAll` on a Card/QObject container | bulk `delete` without a per-element `delete` token | treat as N raw deletes of the container’s static element type |
| `QObject*` / `Card*` upcast then `delete`/`deleteLater()` | static type is no longer Card | already a bypass if the known pointee is Card; require an allowlist row or fail |
| pointer-to-member `QObject::deleteLater` / `operator delete` | qualified call hides the Card slot | reject on Card* as today; fixture must be in `--self-test` |
| macro / inline wrapper around `delete`/`deleteLater` | the expansion site is the real ingress | expand or match known project macros; unexpanded unknown macro that takes a Card* fails closed |
| default `QSharedPointer`/`std::unique_ptr` deleter | `m_ownedCard` and similar | require a manager-aware deleter row |
| QObject parent-child | no `delete` at the child site | N8.5 parent-child row; Package/Engine/RoomState owners only |

Checker 能力與盲區：`tools/check-card-lifetime.py` 以 Python AST／正則＋型別模式匹配靜態原始碼（文本 `delete`／`deleteLater`／`qDeleteAll`／`QSharedPointer`／`std::unique_ptr` 預設 deleter／成員指標／宏／QObject parent-child 等），覆蓋上表盲點並在 `--self-test` 以具名 fixture 驗證；盲區為宏內間接展開、跨翻譯單元別名、執行期動態型別與條件編譯，**不得以 scan clean 單獨宣稱閉合**，須與 N6 計數器及 `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` 運行期 gate 共同判定。

This is still Card-lifetime scope: it does not alter Player, Skill, Package, or Room object lifetime. It only changes how those surfaces retain a temporary/inner Card reference.

### N9 Adoption sequence (T4 canonical owner)

`WrappedCard::takeOver/copyEverythingFrom` keep public `void` signatures. All legitimate incoming factory Cards must be parentless and the call must execute on the incoming Card affinity thread. Internally a boolean adoption transaction preserves the old `m_card` until commit. The executor may not improvise a cross-thread pointer swap.

**Canonical owner (T4):** the sole canonical owner for every RoomState outer and inner Card is `Room::thread()` — the long-lived Room QObject thread that has an event dispatcher. This choice is locked by the PR1 characterization test below; if that test cannot prove the dispatcher survives until `Room` teardown (`Room::~Room()` / `Room::stopGameThreads()` join point), implementation stops and does not silently substitute another owner. Ordinary, 1v1, 3v3, and XMode all obey the same canonical-owner rule.

**Setup normalization:** after the setup worker completes and before any gameplay worker starts, normalize every RoomState outer/inner Card to the canonical owner (`Room::thread()`). This is a one-time affinity fixup on the setup domain; gameplay adoption thereafter always targets the canonical owner.

| Step | Execution context | Required action |
|---|---|---|
| Reserve | incoming Card affinity thread | verify parentless/live/generation and caller thread, reserve `Adopting`, cancel only the matching pending generation, then release manager lock |
| Lease | target-affinity check (still before move) | acquire the target lifecycle lease for the incoming generation on the canonical owner domain. This lease blocks reclamation of the target while adoption is in flight. Failure here is a recoverable pre-move failure |
| Snapshot | incoming Card affinity thread | copy every field/tag/flag needed by the outer `WrappedCard`; this is the final source-thread Card access |
| Transfer | incoming Card affinity thread | call Qt 6.11 `moveToThread(Room::thread())` (canonical owner); no manager/Room/outer lock is held. All recoverable failures must occur before this move |
| Commit | canonical owner thread (`Room::thread()`) | invoke directly when same-thread, otherwise `QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection)` with no lock held; apply the value snapshot, swap `m_card`, transition `Adopted`, and enqueue the old inner generation for canonical-owner retirement. After a successful `moveToThread`, commit is no-fail: it must not re-validate in a way that can fail or require rollback |
| Return | original caller thread | the public `void` call returns only after commit or a terminal pre-move failure; after a successful transfer the source thread never calls an incoming Card method |
| Failure | current Card affinity (pre-move only) | before transfer, restore the prior pending/live state and release the target lease; queue the incoming generation for retirement on its current affinity. Increment `adoption_failed`/`affinity_transfer_failed`; Debug/CI fails and production emits a hard diagnostic. Post-move failure is not a defined path |

Cross-thread commit is permitted only while the canonical owner event dispatcher is running and outside shutdown/join. The blocking invoke holds neither manager nor Lua/Room locks. Post-move commit must succeed; any invariant that would have caused failure must have been checked before the move.

### N10 Token / registry

Preferred binding remains the reduced lazy token: one shared generation token per observed Card, created only on first **live** SWIG exposure or managed-factory registration. There is no all-Card eager registration and no permanent raw-address tombstone.

Every Card destructor calls non-owning `invalidateIfObserved(this)`; an unobserved Card has no token/sidecar and returns immediately. That no-op is **not** a guarantee that a later untracked raw pointer can safely create the first wrapper. Invalidation flips an existing token and frees the C++ Card independently of stale userdata lifetime.

**Authority (Lua-invisible):**

| Field | Where it lives | Lua-writable? |
|---|---|---|
| token pointer, unique generation/identity, atomic state | C++ token object | no |
| original SWIG `own` bit at first exposure | C++ token / per-wrapper side-table row | no |
| live `Card*` association | process map `Card* -> token`, only while the Card is live | no |
| per-wrapper binding | Lua registry side table keyed by wrapper lightuserdata, not present in `_G`; optional sealed C uservalue is cache only | no |

Each token also stores affinity thread, non-owning C++ pin count (held by the outermost protected invocation, not SWIG-local RAII), optional WrappedCard owner link, and compact counters. After destruction, the address map drops the Card; stale wrappers may keep the dead token via the side table without retaining the Card. `__gc` removes that wrapper's side-table row. No strong Lua registry reference to the Card object is used. Scripts cannot forge generation or original-own by `lua_setuservalue` or by writing a table; ConvertPtr then fails closed.

The registry service intentionally outlives Engine/Room/Card teardown and releases dead tokens after their final wrapper/reference disappears.

Observation and `invalidateIfObserved(this)` linearize under the same registry mutex: observation that wins creates one generation; destruction that wins forbids later resurrection and forbids creating a wrapper. Cards that are never observed/managed allocate no token or ownership sidecar.

### N11 Mode selection

`CardLifetimeMode::{ObserveOnly, ManagedReclaim}` is stored per manager/runtime domain. No public config, extension flag, or second behavior switch is added.

| Rule | Binding |
|---|---|
| Production default PR1-6 | `defaultCardLifetimeMode() == ObserveOnly` |
| Production default PR7+ | change only `defaultCardLifetimeMode()` to `ManagedReclaim` |
| ObserveOnly semantics | token invalidation/diagnostics/N3 policy active; each migrated call site's original immediate-versus-deferred physical timing preserved; production orphan reap disabled |
| ManagedReclaim semantics | N2/N3/N4/N5 physical reclaim; orphan reap enabled only after `unknown_unclaimed == 0` |
| Tests | except the single selector-default regression, every test explicitly constructs `ObserveOnly` or `ManagedReclaim` |
| Unique selector-default regression | PR7 only. One test reads `defaultCardLifetimeMode()` with no override and asserts managed orphan/delete/safe-point behavior. It is red before the switch and green after. It is the only test allowed to omit an explicit mode argument |
| Emergency rollback | revert/disable PR7 only; explicit-mode tests remain valid |
| A/B evidence | same source SHA apart from the switch; run identical fixtures in explicit ObserveOnly, explicit ManagedReclaim, and the unique default test |

### N12 Owner decisions

| ID | Decision | Status | Rationale |
|---:|---|---|---|
| 1 | Add a Card-specific public-slot C++ `Card::deleteLater()` with the existing spelling; route Card-typed/name-based meta-object calls through the manager; route Card-as-QObject `QObject::deleteLater()` to the same native-origin policy by dynamic type (`Card::event` intercepts `QEvent::DeferredDelete`); leave ordinary non-Card QObject deletion unchanged. | approved 2026-08-22 | A Lua-only bridge cannot cover the large native Card deletion surface. `QObject::deleteLater()` is not virtual, so Card-as-QObject must still hit Card policy. |
| 2 | Migrate every in-tree gameplay raw `delete Card*` and default smart-pointer Card deleter to manager-aware release; permit physical destruction only through audited quiescent Engine/Package/RoomState/WrappedCard/test owner paths. | approved 2026-08-22 | Destructor-only alias invalidation cannot prevent native UAF when a Card-bearing lease or payload still exists. |
| 3 | Implicitly reap only known-factory `UnclaimedTransient` Cards after their birth transaction and only when Lua wrapper count, native leases, and persistent owners are all zero; require `unknown_unclaimed == 0` before enabling the behavior. | approved 2026-08-22 | This repairs provable orphan leaks while preserving legacy Lua that intentionally retains a clone for later use. |
| 4 | Include the minimal native escape closure for `Player::equips`, `Card::change_cards`, Card-bearing event leases, and `ComboMovesCard`, while preserving existing public APIs. Equipment internals become `QList<const WrappedCard *>`; public EquipCard* getters resolve `getRealCard()` at query and return borrows valid only until the next Room mutation/safe point. | approved 2026-08-22 | Lua token checks alone cannot prevent native UAF after physical Card reclamation. |
| 5 | Preserve the public mutable `Card::change_cards` surface; add a generation sidecar of `(source_generation, target_generation, duplicate_count)`; route every in-tree mutation through managed methods on the Room domain control thread. Self-cycle is a gate failure; other cycles block reclaim and must be zero before PR7. Source destruction clears outgoing edges; address reuse must not reconnect old edges. Do not claim to intercept arbitrary cross-thread direct mutation. | approved 2026-08-22 | This keeps source/ABI compatibility while making reclamation safe; API privatization is deferred to a future major version. |
| 6 | Keep Lua `card:deleteLater()` as a managed release request for proven transients, but fail closed with a stable ownership error for `ObservedExternal` Cards whose deletion authority remains unknown after provenance characterization. | approved 2026-08-22 | Lua may request retirement but cannot infer ownership from a raw or parentless Card pointer; rejecting unknown authority prevents caller-owned native UAF. |
| 7 | Use a real existing `20p` headless game plus synthetic 30/50-actor Card-lifetime stress fixtures; do not add production 30p/50p game modes. | approved 2026-08-22 | This covers the requested scale for lifetime, alias, lease, adoption, and shutdown behavior without expanding into unrelated rules, UI, networking, or AI mode work. |
| 8 | Use regression-first sequencing inside every behavioral PR: capture a failing test/evidence first, implement the minimum behavior, then land the PR only with its focused and cumulative gates green. | approved 2026-08-22 | This proves each regression test detects the pre-fix defect while keeping every dependency prefix buildable, testable, and rollback-safe. |
| 9 | Use the reduced liveness-observation design: every Card destructor exposes `invalidateIfObserved(this)`, but generation tokens and sidecar state are allocated lazily only for SWIG-observed or manager-registered Cards; every exposure `observeLive`s while an owner/borrow/invocation scope still holds the live Card; destructor-won control refuses wrapper creation; physical deletion authority remains with existing owners or proven transients. | approved 2026-08-22 | This invalidates every observed stale Lua alias without pre-registering Engine/Package Cards. It does not claim an untracked post-destruction raw pointer can safely create a first wrapper, and it does not require eager tokens or a permanent address tombstone. |
| 10 | Define PR revertibility as dependency-ordered rollback with every applied prefix buildable/testable; keep PR7 as the independently reversible default-behavior switch rather than supporting arbitrary removal of a middle dependency. | approved 2026-08-22 | Arbitrary middle removal would require permanent compatibility stubs and combinatorial flag coverage, while the isolated switch restores legacy deletion timing without dismantling dormant safety layers. |
| 11 | Treat Lua `card:deleteLater()` on known Engine/Package definitions or already-adopted inner Cards as a production compatibility no-op with `definition_delete_ignored`/`adopted_delete_ignored` counters and owner diagnostics. | approved 2026-08-22 | The manager knows Lua lacks deletion authority, so ignoring the request prevents corruption without aborting legacy callbacks; tests still expose unexpected calls through counters. |
| 12 | Keep a conservative keyed lease for every recognized transient stored in a persistent generic `QVariant`/tag until overwrite, remove, or container destruction. Freeze every Card-bearing metatype as `lease-bearing`, `scoped borrow`, or `rejected opaque`. Persistent payloads must use a copyable lease-bearing representation. Direct `QVariant<const Card*>` must not persist. Opaque payload returns the exact rejected-opaque error and blocks PR7. | approved 2026-08-22 | This preserves existing gameplay while preventing native UAF and keeps retained memory proportional to currently stored tags rather than historical Cards. |

### N13 Regression and stress matrix

| Surface | Required observable test |
|---|---|
| Same-stack compatibility | clone -> `deleteLater()` -> method access before flush remains valid |
| Native compatibility ingress | representative `Card*`, `SkillCard*`, and derived-pointer C++ `deleteLater()` calls enter one pending generation; `QObject* q = card; q->deleteLater()` also enters Card native-origin policy by dynamic type; ordinary non-Card QObject `deleteLater()` is unchanged |
| Native bypass guard | qualified/member-pointer `QObject::deleteLater` on Card* is still rejected by the source audit/Debug diagnostic as defense in depth; name-based meta-object invocation resolves the Card slot; Card-as-QObject `DeferredDelete` is routed to Card policy rather than treated as a silent bypass; an injected external owner destruction still invalidates all Lua aliases and fails if active leases remain |
| Raw-delete gate | every confirmed gameplay `delete Card*` and default smart-pointer Card deleter is migrated; only fully qualified quiescent owner/test sites remain, and `external_direct_destroy` with an active lease/owner fails |
| Multiple aliases | expose one Card through two wrappers; after flush both `pcall` accesses return the exact Lua error, never a native crash |
| Owning wrapper GC | `sgs.DummyCard()` -> `deleteLater()` -> flush -> two GC cycles; exactly one native destruction |
| Adoption cancellation | clone -> delete request -> `WrappedCard::takeOver` -> flush; Card survives and counter records `adopted_after_delete_request` |
| Replacement | adopt new Card, replace it, then every old alias fails safely while the new Card remains usable |
| SWIG bypasses | stale derived/base alias, static QObject method call, CardList/`SWIG_MustGetPtr`, and Card lightuserdata rejection |
| Conversion/use race | pause a generated Card method after checked conversion while another affinity requests/drains deletion; the outermost protected `lua_pcall` pin blocks destruction until that call returns, including when an inner `lua_error` longjmps; SWIG-local RAII must not be the pin; then the next drain retires it |
| Lua-invisible token authority | after exposure, Lua `lua_setuservalue` / forged table cannot change generation or original-own; ConvertPtr still uses the side table and either keeps the real token or fails closed |
| Destruction-won first wrapper | under the linearization lock, let destruction win before wrapper creation; `observeLive` refuses the wrapper; do not add an untracked post-free dereference test, eager tokens, or an address tombstone to make that unsupported path appear safe |
| Address reuse | deterministic allocator fixture reuses an address; old/new generations are unequal and old wrapper stays rejected |
| Borrowed Cards | numeric Parse/engine physical Card remains live across Lua GC/delete-policy test |
| Native equip stability | equip -> `filterCards/resetCard/takeOver` -> every `Player::getEquip(s)` query resolves the new inner Card; stashed pre-takeOver `EquipCard*` is defined as invalid; no manual resync dependency |
| Equip borrow lifetime | getter raw pointer remains usable until the next Room mutation/safe point and must not be used after it |
| Native change history | validate/response replacement pins old Card until `change_cards` sidecar count reaches 0, then permits retirement |
| Legacy change history mutation | owner-thread compatibility-fixture append/clear snapshots generations into the sidecar; self-cycle fails the gate; a length≥2 cycle blocks reclaim; source destructor drops outgoing edges; reused address does not reconnect; cross-thread direct writes are out of intercept contract and forbidden in production |
| Persistent tag owner | repeated `ComboMovesCard` replacement destroys each clone exactly once and returns managed counts to baseline; storage is not `QVariant<const Card*>` |
| Event lease | copies of CardUse/Response/Damage/Judge payloads in `QVariant` block a flush; destroying the last copy allows the next flush |
| Frozen QVariant matrix | lease-bearing copy/move/overwrite/dtor, scoped-borrow persist/detach failure, nested list/map recursion, and detached QVariant each have a focused test |
| Unknown QVariant payload | opaque custom metatype returns exact `Card lifetime error: rejected opaque QVariant Card payload`, increments `unknown_qvariant_card_payload`, leaves destination unchanged, and fails Debug/CI with no fallback blocker |
| Persistent `QVariant<const Card*>` | storing `QVariant::fromValue((const Card*)…)` in a Player/Room/Card tag or `m_extraData` is rejected or migrated; a leftover persist fails PR7 |
| Whole-map tag copy | `Player::copyFrom` and `Card::tag = …` acquire/release per child class; an opaque child fails the copy |
| RoomState reset/resetCard | `reset` quiescent-destroys prior outers; `resetCard` adopts the clone and retires the old inner once |
| QObject parent-child Card destroy | Package/Engine parent delete of a Card child is a classified quiescent owner path or `external_direct_destroy` |
| Checker blind spots | `--self-test` feeds `qDeleteAll`, Card-as-QObject upcast delete, member-pointer `deleteLater`, and a delete macro and requires nonzero exit with path/line/category |
| Unpaired factory exit | each failure branch of `safeTurnCardToEquip` releases its unclaimed clone; a deliberate missing-request/no-wrapper loop is orphan-reaped at the proven boundary, while a live Lua wrapper blocks implicit reaping until GC or explicit delete |
| Thread/control flow | normal/skip phase, `TurnBroken`, `StageChange`, extra-turn path after verified boundary, and final Room teardown destroy only on affinity thread |
| Shutdown topology | normal/1v1/3v3/XMode early return and `GameFinished` all leave zero worker-affinity transients before thread exit; GUI remainder drains before Lua close |
| Cross-thread adoption shutdown (T4) | main-created outer WrappedCard transactionally adopts a worker-created parentless inner Card; adoption cancels pending deletion, acquires target lease before move, moves the inner to canonical owner `Room::thread()`, worker-final skips it, and later RoomState destruction deletes it exactly once on that affinity |
| Adoption invariant failure | inject parented/wrong-current-thread incoming Card; public void API leaves the old `m_card` unchanged, retires/diagnoses the incoming Card without leaking, and trips Debug/CI `adoption_failed` |
| Worker persistent-owner final | the last operation stores a managed `ComboMovesCard`, then raises `GameFinished`; final guard clears the owner/leases before transient drain and returns every worker counter to baseline |
| Lua close finalization | live owning/non-owning Card wrappers survive until runtime shutdown; pre-close retirement runs, `lua_close` GC creates no new delete work, post-close drain returns runtime pending/unclaimed/wrapper counters to baseline |
| Bounded loop | N6 ring formula |
| Load | real fixed-seed `20p` headless run plus N6 synthetic 30/50-actor formulas; no claim of nonexistent 30p/50p product-mode coverage |
| Selector-default | unique PR7 default-mode test with no explicit override; all other tests pass an explicit mode |

All focused lifetime tests are registered as `qsanguosha_card_lifetime` in `qsanguosha_runtime_tests` with implementation in `tests/card-lifetime-manager-test.cpp`. Except the unique selector-default regression, each test constructs its `CardLifetimeMode` explicitly.

### N14 Rejected alternatives

| Alternative | Verdict |
|---|---|
| Mutate every wrapper `ptr = nullptr` via weak wrapper registry | reject |
| Raw-address tombstone checked in conversion | reject |
| All-Card eager token registration | reject |
| Post-destruction first wrapper from an untracked raw pointer as a supported safe path | reject |
| Conversion pin via SWIG-local C++ RAII that `lua_error` can skip | reject |
| Authoritative generation / original-own in a Lua-writable uservalue table | reject |
| Shared generation-token sidecar + checked conversion/GC only, without native escape closure | reject as incomplete |
| Replace public `Card*` with owning handles/smart pointers | reject as out of scope |
| GameOver quarantine that retains all Cards until shutdown | reject |

## Finding → 修正對照表 (C1–C6, H1–H6, M1–M3)

> 每個 finding 均有唯一規範章節、實作 PR 與關鍵檔案行號定位；未列即視為未閉合。此表為正式計畫之追溯依據，實作與審查必須逐項勾稽。

| ID | 嚴重度 | 問題簡述 | 修正位置（規範章節 / 實作 PR / 關鍵檔案） |
|---|---|---|---|
| C1 | Critical | SWIG alias 在 C++ Card 析構後仍可轉換並解引用（stale UAF） | N7-1/2/4、N10 觀察/失效線性化；PR2；`swig/sanguosha.i:47-85` 三 helper 重定義、`src/core/card.cpp:43-49` 析構 `invalidateIfObserved`、`builds/cmake-vs2026/generated/sanguosha_wrap.cxx` 檢查 |
| C2 | Critical | `sgs.DummyCard()` 等 `own=1` 擁有型 userdata 在 `__gc` 二次刪除 | N7-3/7、N3 `LuaOwnedTransient`、N10 original-own 位；PR2；`swig/sanguosha.i` `__gc` 分發、`src/core/card-lifetime-manager.{h,cpp}` owning wrapper 計數 |
| C3 | Critical | `Player::equips` 存 `getRealCard()` 內指針，`WrappedCard::takeOver` 後懸空 | N8.1 內存改 `QList<const WrappedCard *>`、查詢時 `getRealCard()`；PR4；`src/core/player.h:507`、`src/core/player.cpp:882-895,948-1088` 全遷移 |
| C4 | Critical | `WrappedCard::takeOver` 無法撤銷已排程的 pending delete，舊隊列誤刪新 `m_card` | N9 交易式收養、N2 `Adopting` 撤銷隊列；PR5；`src/core/wrapped-card.cpp:3-83` reserve/cancel |
| C5 | Critical | Lua 可經 `lua_setuservalue` 偽造 token 世代/original-own | N7-3/4/6、N10 權威欄位在 Lua 不可見 side table + C++ token；PR2；`src/core/lua-runtime.*` side table、`swig/sanguosha.i` ConvertPtr 校驗 |
| C6 | Critical | 析構與 `observeLive` 競爭：未追蹤裸指針在析構後首次建 wrapper 復活舊世代 | N10 單一 registry mutex 線性化、N7-2 `observeLive` 僅在活域內且 destructor-won 拒絕；PR2；`src/core/card-lifetime-manager.{h,cpp}` |
| H1 | High | `RoomThread::run` 無 `exec()`，worker 親和 `deleteLater` 堆積至線程退出 | N5.4 worker-final 嚴格 8 步順序、N5.6 `shutdownFinal()`；PR6；`src/server/roomthread.cpp:603-685`、`src/server/roomthread1v1.cpp:20-113`、`roomthread3v3.cpp:64-116`、`roomthreadxmode.cpp:19-115` |
| H2 | High | 原生 `Card::deleteLater()` 為主流量入口，僅攔 Lua 仍被繞過 | N12-1、N3 來源區分、N6 `native_delete_requested`；PR3；`src/core/card.h` 新增 `Card::deleteLater()` slot、`src/core/card.cpp` `Card::event` 攔 `DeferredDelete` |
| H3 | High | `delete Card*` / `qDeleteAll` / 預設智能指針 deleter 繞過 lease/owner 檢查 | N8.4/8.5/8.6、`docs/card-lifetime-ownership.md` 陵記；PR4 遷移；`tools/check-card-lifetime.py --self-test` 含 qDeleteAll/upcast/member-pointer/macro 盲點 |
| H4 | High | `safeTurnCardToEquip` 四失敗分支孤兒泄漏（已知工廠未配對退出） | N2 `UnclaimedTransient -> Retired` 孤兒邊、N4 `unknown_unclaimed` gate；PR3 首個孤兒 fixture、PR5 `ManagedReclaim` 回收；`src/package/yjcm2023.cpp:29-44` |
| H5 | High | `Card::Parse` 克隆後已排 `deleteLater` 仍回傳裸指針（同棧使用/收養語意不清） | N1-C/E 分類、N3 `PendingDelete` grace、N2 same-stack 可用；PR3；`src/core/card.cpp:565-680` Parse 註冊 |
| H6 | High | `CardFinished` / decision return / `event_stack.isEmpty()` 被誤作回收證明 | N5.2/N5.3 列舉安全路徑、N5.1 `LuaInvocationScope`；PR6；`src/server/room.cpp:223-294` phase/turn hook 僅 proposal |
| M1 | Medium | 原始指標地址重用導致 stale 與新 Card 相等性混淆 | N7-6 `__eq` token 世代比對、N6 `change_list_reuse_reconnect`；PR2；`swig/sanguosha.i` `__eq` 分發、確定性 allocator 重用 fixture |
| M2 | Medium | `WrappedCard` 替換/析構缺親和性與空值契約（泄漏/double delete/跨線程） | N9 步驟表、N5.4 adopt 跳過、N5 setup normalization；PR5/PR6；`src/core/room-state.cpp:5-58`、`src/core/wrapped-card.cpp` |
| M3 | Medium | `QVariant`/`m_extraData`/tag 內 Card 指針經通用容器逃逸，無提取/釋放策略 | N8.3 凍結矩陣、N8.5 ledger 必列、N6 `unknown_qvariant_card_payload==0`；PR5；`src/core/structs.h:50-177,473-606` 每型別複製/覆寫/析構鉤 |

> 上表每列對應審查 gate：C/H/M 任一未在指定章節實作或 PR 驗收即為阻擋項；PR7 default-on 零 gate 須同時滿足 `unknown_unclaimed==0` 等 N6 指標。

## Verification strategy
> Zero human intervention - all verification is agent-executed.
- Test decision: regression-first TDD with the existing QtTest/CTest harness, generated SWIG runtime tests,具名 Lua Card lifetime CTest（`qsanguosha_card_lifetime` 及其 `lua` 子集）, source-ledger checks, and headless runner. For each behavioral PR: write the failing scenario, capture red output, implement the minimum change, capture focused green output, then run the cumulative gate before committing. Except the unique PR7 selector-default regression, every new test explicitly selects `ObserveOnly` or `ManagedReclaim`. Never use `debug\QSanguosha.exe --lua-test`（該 CLI 不存在，已自 `src/main.cpp` 移除）.
- Evidence: `<attemptDir>/task-<N>-lua-card-lifetime-safety.{txt,json,log,csv}` where `<attemptDir>` is `currentAttemptDir` from `omo ulw-loop status --json`; outside ulw-loop use `.omo/evidence/lua-card-lifetime-safety/`. Red evidence is retained but never committed as a failing final state.
- First configure after adding new C++ sources: `cmake --preset vs2026-x64`（僅在新增/刪除 `.cpp`、修改 `CMakeLists.txt`／`cmake/QSanguoshaSources.cmake`、新增 Qt module 或修改 Qt 路徑時）. Incremental build thereafter: `cmake --build --preset debug --parallel 8`.
- Focused gate（全 PR 統一）: `ctest --test-dir builds/cmake-vs2026 -C Debug -R <test> --output-on-failure`，其中 `<test>` 為該 PR 對應的已註冊 CTest 正則（lifetime 套件為 `^qsanguosha_card_lifetime$`，Lua 子集為 `^qsanguosha_card_lifetime:lua$` 或等價具名測試；PR1 為其 diagnostics／ledger 子集）. Cumulative gate 亦統一為 `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure`. 若回傳 `0xc0000135`，先 `set PATH=H:\Qt6111\6.11.1\msvc2022_64\bin;%PATH%` 重跑一次再判定語意失敗。
- SWIG gate after any `.i` edit: incremental build 後執行 `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure`（涵蓋具名 Lua CTest），再檢查 `builds/cmake-vs2026/generated/sanguosha_wrap.cxx` 是否含三個受檢分發（`SWIG_NewPointerObj`／`SWIG_ConvertPtr`／`SWIG_MustGetPtr` 之重定義）且 `git diff -- swig/sanguosha_wrap.cxx` 為空。禁止使用 `debug\QSanguosha.exe --lua-test` 作為驗收。
- Source/ownership gate: `python tools/check-card-lifetime.py --root . --ledger docs/card-lifetime-ownership.md --allowlist tools/card-lifetime-allowlist.json`; before PR7 it must report zero unclassified sites, and at PR7 it must additionally report zero legacy gameplay Card raw deletes, bypasses, opaque Card payloads, persistent `QVariant<const Card*>`, `change_list_self_cycle`/`change_list_cycles`/`change_list_reuse_reconnect`, and direct production `change_cards` mutations. Checker 能力須在 `tools/check-card-lifetime.py` 註解與 `docs/card-lifetime-ownership.md` 中說明：以 Python AST／正則＋型別模式匹配靜態原始碼（`delete`／`deleteLater`／`qDeleteAll`／`QSharedPointer` 預設 deleter／成員指標／宏展開列表／QObject parent-child 等），可覆蓋 N8.6 Blind spots；盲區為宏內間接展開、跨翻譯單元別名、執行期動態型別，**不得以 scan clean 單獨宣稱閉合**，須與 N6 計數器及執行期測試共同作為 gate。
- Deterministic stress: apply N6. Ring: 10,000 create/alias/delete/drain iterations, batch 64, stale-wrapper ring 64. 30/50 actor synthetic rows（非產品模式）: 單一獨立 `CardLifetimeManager` domain（`ManagedReclaim`）、單一 canonical owner thread（`Room::thread()`／測試用 owner thread）、每 epoch 生產者 barrier 對齊所有 actor、每 actor 每 epoch 固定四項操作依序 `create transient (known factory) -> expose/alias via observeLive -> request delete (Lua/Native 交替) -> tryDrainIfEligible`、seed `2026082201`、epochs `200`、actors `30` 與 `50` 各一唯一 CTest（`qsanguosha_card_lifetime:synthetic-30`／`qsanguosha_card_lifetime:synthetic-50` 或等價具名 `qsanguosha_card_lifetime_synthetic` 參數化）. 每 epoch 內不變量：`managed_live <= actor_count` 且 `pending_delete <= actor_count` 且 `wrapper_leases <= 2*actor_count`；每個 epoch 末 `drain` 後 `managed_live==0 && pending_delete==0 && wrapper_leases==0 && lease/explicit-owner/sidecar-edge 回到 baseline`. Peaks obey `peak_live_tokens <= baseline + batch + ring + explicitly_retained_tag_or_lease_entries` for the ring, and `peak_live_tokens <= baseline + (actor_count * 4) + explicitly_retained_tag_or_lease_or_wrapper_ring_entries` for actor rows, never iteration/epoch count. `actually_destroyed == eligible_created_count`.
- Real load gate after PR8: `python tools/autotest/headless_runner.py --exe <Debug exe> --modes 20p --games 5 --parallel 1 --seed 2026082201`，其中 `<Debug exe>` 必須為明確 Debug 執行檔路徑（`L:\finaldebug\QSanguosha-v2\debug\QSanguosha.exe` 或 `builds/cmake-vs2026/debug/QSanguosha.exe`），PR8 須為 runner 新增 `--exe` 參數並固定使用 Debug executable，禁止僅以 `--exe-root` 隱式尋找。Require exit 0, done marker, 5/5 finished, zero failed games, 且 PR6 輸出的每局唯一 JSON final-gauge marker 通過 PR8 校驗（見 PR6/PR8），並檢查最終 per-Room lifetime gauges at baseline。
- Prefix diagnostics vs reclaim 分界: PR1–PR4 僅驗證該 prefix 已具備的 diagnostics／classification（token 失效與 Lua exact error、N3 策略計數、ledger 分類完整性、equip/sidecar 整型），**不**對實體 `Retired->Dead` 回收做斷言；實體 reclaim／`actually_destroyed`／drain 後置零斷言自 PR5（state-machine／lease／adoption）與 PR6（safe-point／worker-final／shutdown drains）開始。
- Legacy red oracle（用於 PR2 之前的紅態特徵）: 以固定 `ObserveOnly` child mode、單進程 timeout `15s`、`--suite card-lifetime-legacy-red`、退出碼 `64`（未註冊 suite 按 `runtime-tests-main.cpp` 返回 64）對比；在具名 Lua Card lifetime CTest 更新前，child 以 exact `Lua error: attempt to use deleted Card`（stale alias）與 `Lua error: attempt to delete Card with unknown ownership`（unknown ownership delete）失敗，並驗證 `actually_destroyed==1`（單一析構）與 `stale_access==1`，不要求 allocator 必然 crash 或 address reuse 觸發 AV。該 oracle 僅作紅態基線，特徵通過後即被 PR2 綠態覆蓋。
## Execution strategy
### Parallel execution waves
> Target 5-8 todos per wave. Fewer than 3 (except the final) means you under-split.

- One eight-todo implementation wave is intentionally serialized as PR1 -> PR8 because each safety proof consumes the previous prefix. Do not manufacture parallel branches across ownership/state-machine boundaries.
- Within a todo, independent inventory/test-file work may be delegated read-only or implemented in parallel only when file ownership is disjoint; the owning worker integrates and runs the focused+cumulative gates before the PR boundary.
- After todo 8, F1-F4 run in parallel against the same final SHA. All four must approve and the user must explicitly accept their surfaced evidence before execution is declared complete.

### Owner-decision traceability

| Decision | Implemented in | Focused proof / default-on gate |
|---:|---|---|
| 1 Card-specific C++ slot plus Card-as-QObject dynamic dispatch | PR3 | Card/derived/meta-object ingress and `QObject* q = card; q->deleteLater()`; ordinary non-Card QObject timing unchanged |
| 2 migrate every raw/default Card deleter | PR1 inventory, PR4 migration, PR7 gate | allowlist has zero legacy gameplay Card delete/deleter entries |
| 3 known-factory orphan reap | PR3 provenance, PR5 state machine, PR6 drains | wrapper/lease/owner-zero fixture and `unknown_unclaimed == 0` |
| 4 native escape closure | PR4-5 | equip outer storage, history sidecar, event matrix, tag, ComboMoves scenarios |
| 5 preserve mutable `change_cards` | PR4-5 | owner-thread generation snapshot, self-cycle fail, length≥2 cycle block, source-dtor edge drop, reuse non-reconnect |
| 6 unknown Lua delete fails closed | PR3 and PR7 | exact `pcall` error, state unchanged, `unknown_card_delete +1` |
| 7 real 20p plus synthetic 30/50 | PR8 | fixed-seed headless evidence plus parameterized actor rows |
| 8 regression-first | every PR | red/focused-green/cumulative evidence triplet |
| 9 reduced lazy observation | PR2 | unobserved Card allocates no token; live-scope `observeLive` shares one token; destructor-won lock refuses wrapper creation; Lua cannot forge generation/original-own; no post-dtor untracked first-wrapper guarantee |
| 10 dependency rollback | all PR boundaries | every prefix green; full rollback 8->1; PR7-only emergency behavior rollback |
| 11 definition/adopted delete no-op | PR3/5 | exact ignored counters; callback continues; Card remains usable |
| 12 keyed persistent QVariant/tag lease | PR1 ledger and PR5 | frozen matrix copy/overwrite/remove/destructor; `const Card*` persist forbidden; opaque exact error; unknown gauge zero |

### Dependency matrix
| Todo | Depends on | Blocks | Can parallelize with |
| --- | --- | --- | --- |
| 1 | none | 2-8 | none |
| 2 | 1 | 3-8 | none |
| 3 | 2 | 4-8 | none |
| 4 | 3 | 5-8 | none |
| 5 | 4 | 6-8 | none |
| 6 | 5 | 7-8 | none |
| 7 | 6 | 8 | none |
| 8 | 7 | F1-F4 | none |
| F1-F4 | 8 | completion | each other |

## Todos
- [ ] 1. PR1 - Characterize hazards and freeze finite ownership ledgers
  - What to do: add `CardLifetimeMode::{ObserveOnly,ManagedReclaim}` and a diagnostics-only `CardLifetimeManager` shell in new `src/core/card-lifetime-manager.{h,cpp}`, **correctly wired through root CMake source list** (`CMakeLists.txt` -> `cmake/QSanguoshaSources.cmake` `QSAN_SOURCES` for `qsanguosha_engine`); first configure `cmake --preset vs2026-x64`, thereafter `cmake --build --preset debug --parallel 8` incremental. Add `tests/card-lifetime-manager-test.cpp` to `qsanguosha_runtime_tests` in `tests/CMakeLists.txt` and register named CTest `qsanguosha_card_lifetime` via `qsan_add_ctest(qsanguosha_card_lifetime qsanguosha_runtime_tests SUITE card-lifetime)`; extend `tests/runtime-tests-main.cpp` dispatcher for `card-lifetime` and `card-lifetime-legacy-red` suites (unknown suite returns 64). Production default remains `ObserveOnly` and no new path may change physical deletion timing. Add `docs/card-lifetime-ownership.md`, `tools/check-card-lifetime.py`, and `tools/card-lifetime-allowlist.json`. Except the later unique PR7 selector-default regression, every test in this file constructs mode explicitly (PR1-PR4 diagnostics/classification only, no physical reclaim assertions).
  - What to inventory: every known factory/constructor/definition/adoption source; every C++ `deleteLater`/raw delete/default Card deleter; N8.6 checker blind spots (`qDeleteAll`, upcast, member-pointer, macro, default smart-pointer deleter, QObject parent-child); every direct `change_cards` mutation; every `lua_pcall`/`LuaRuntime::Binding`/`LuaInvocationScope` entry with N5.1 separation; every Card-bearing `Q_DECLARE_METATYPE`/`QVariant::fromValue` classified as `lease-bearing` / `scoped borrow` / `rejected opaque`; every persistent Player/Room/Card tag key including whole-map copies; `Player::copyFrom`; `ComboMovesCard`; `RoomState::reset`/`resetCard`; `CardUseStruct::m_ownedCard`; every direct `Player::equips` consumer from N8.1; every quiescent owner-destruction site; and the canonical owner per N9 (`Room::thread()`). Each ledger row records provenance, owner/lease/scoped-borrow semantics, copy/overwrite/remove/destructor hooks, affinity (canonical owner where applicable), legacy immediate/deferred timing, implementation PR, and focused scenario. Unknown rows fail; known legacy sites may remain explicitly classified until PR4.
  - Regression-first proof: add child-process characterization for stale alias UAF, owning-wrapper double-delete risk, adoption-after-delete request, worker deferred-delete accumulation, and direct owner destruction — **legacy red oracle uses fixed parameters**: child in `ObserveOnly` single-process, `timeout 15s`, `--suite card-lifetime-legacy-red`, expected exit `64` for unknown suite (per `runtime-tests-main.cpp`), exact stderr `Lua error: attempt to use deleted Card` (stale alias) and `Lua error: attempt to delete Card with unknown ownership` (unknown ownership delete) with `actually_destroyed==1` and `stale_access==1`, **does not require allocator must crash**. Add dedicated characterization test proving canonical owner `Room::thread()` event dispatcher remains alive until Room teardown (`Room::stopGameThreads()` join / `Room::~Room()` entry) on ordinary/1v1/3v3/XMode paths — e.g. posted `BlockingQueuedConnection` probe and pending `deleteLater` delivery probe before join. Capture pre-instrumentation missing-counter/ledger red evidence; committed state may encode expected legacy child failure but parent CTest must pass deterministically. If dispatcher-liveness proof fails, stop and do not substitute another owner; blocking gate.
  - Must NOT do: no SWIG interception, Card destructor hook, delete ingress change, owner migration, physical managed reclaim, blanket text replacement, edits to generated wrapper/legacy extensions, or silent canonical-owner substitution on characterization failure.
  - Parallelization: Wave 1 | Blocked by: none | Blocks: 2-8.
  - References: this plan N1, N5, N8, N9 (T4), N11, N12; `src/core/engine.cpp:1286-1370`; `src/core/card.cpp:565-680`; `src/core/lua-wrapper.cpp:276-631`; `src/core/wrapped-card.cpp:3-83`; `src/core/structs.h:50-177,473-606`; `src/server/room.cpp:6828-6841`; `src/server/roomthread.cpp:603-685,989-1160`; `src/server/roomthread1v1.cpp:20-113`; `src/server/roomthread3v3.cpp:64-116`; `src/server/roomthreadxmode.cpp:19-115`; `src/core/room-state.cpp:5-58`; `src/server/gamerule.cpp:554-560`; `tests/CMakeLists.txt:1-90`; `builds/cmake-vs2026/generated/sanguosha_wrap.cxx` inspection only.
  - Acceptance criteria: first `cmake --preset vs2026-x64` succeeds then incremental Debug build; `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` (PR1 diagnostics subset) and `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure` both exit 0; checker `--self-test` passes and repo scan reports zero unclassified factories/delete sites/persistent sinks/Lua entries while preserving explicit legacy-site count; canonical-owner dispatcher-liveness characterization passes on all four Room variants (ordinary/1v1/3v3/XMode) and records evidence, otherwise PR1 blocked; test-only counter reset refuses with live domain/token; `git diff -- swig/sanguosha_wrap.cxx extensions lua/ai` empty; **PR1 contains no `Retired->Dead` physical reclaim or `actually_destroyed` assertions (diagnostics/classification only), physical reclaim assertions start at PR5**; all gates use unified `ctest --test-dir builds/cmake-vs2026 -C Debug -R <test> --output-on-failure` and never `debug\QSanguosha.exe --lua-test`.
  - QA scenarios: happy - run checker, diagnostics suite, and assert `ObserveOnly` plus zero active manager gauges at teardown; failure - checker self-test feeds an unlisted raw delete, opaque QVariant producer, persistent `QVariant<const Card*>`, direct `change_cards` mutation, `qDeleteAll`/upcast/member-pointer/macro fixture, and malformed allowlist row and must exit nonzero with path/line/category. Evidence: `<attemptDir>/task-1-lua-card-lifetime-safety-{red,focused,cumulative,inventory}.{txt,json}`.
  - Commit: Y | `test: characterize card lifetime hazards`.

- [ ] 2. PR2 - Make every observed SWIG Card alias generation-safe
  - What to do: implement lazy `observeLive/getOrCreateToken` and `Card::~Card()->invalidateIfObserved(this)` linearized by one registry mutex; allocate no token for unobserved/unmanaged Cards. Every SWIG exposure must `observeLive` while an owner/borrow/invocation scope still holds the live Card; destructor-won control refuses wrapper creation. In `swig/sanguosha.i`, source-order wrap `SWIG_NewPointerObj`, `SWIG_ConvertPtr`, and `SWIG_MustGetPtr`; bind wrappers through the Lua-invisible side table (token pointer, generation, original-own); clear stock Card ownership; dispatch Card-specific `__eq`/idempotent `__gc`; reject Card lightuserdata; delegate non-Card/tokenless userdata to stock SWIG. Keep `LuaRuntime::Binding` as long-life access/affinity binding (N5.1); add `LuaInvocationScope` (outermost protected-invocation depth and conversion pins) around every ledgered `lua_pcall`/callback in `src/core/lua-runtime.*`, `src/core/lua-wrapper.cpp`, `src/server/room.cpp`, and any additional PR1 ledger row: `LuaInvocationScope` increments immediately before `lua_pcall`, decrements only after it returns (including error statuses); do not use SWIG-local RAII pins and do not let `LuaRuntime::Binding` own pins. **Create the named Lua Card lifetime CTest** in `tests/CMakeLists.txt` (e.g. `qsanguosha_card_lifetime` with `SUITE card-lifetime` covering Lua alias/GC/equality/reuse suites, or `qsanguosha_card_lifetime_lua` sub-suite) and extend `tests/runtime-tests-main.cpp` dispatcher accordingly; this CTest is the sole Lua验收, never `debug\QSanguosha.exe --lua-test`. PR2 remains diagnostics/classification only for token/alias correctness (no physical `Retired->Dead` reclaim assertions).
  - Required behavior: `PendingDelete` remains callable; only `Retired/Dead` returns exact `Lua error: attempt to use deleted Card`. Same-generation stale aliases remain equal, reused-address generations differ, owning/non-owning aliases cannot double-delete, and a converted raw pointer remains pinned through the outermost protected invocation even if inner `lua_error` longjmps. Lua cannot change generation or original-own.
  - Regression-first proof: using fixed `ObserveOnly` child `timeout 15s` and exit `64` oracle, change the PR1 child characterization expectation from native crash/double-delete to safe exact `Lua error: attempt to use deleted Card`/one native destruction (`actually_destroyed==1`) with `stale_access==1` before implementing; it must fail red (exact error + single destruction, not allocator crash) on legacy runtime, then pass after SWIG/token layer. Parent CTest `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` is the green gate.
  - Must NOT do: no eager all-Card token registration, no permanent raw-address tombstone, no post-destruction first-wrapper-from-untracked-pointer safety claim, no Lua-state wrapper traversal/cache, no Lua-writable authoritative uservalue table, no SWIG-local RAII conversion pins, no manager acquisition of `SafeLuaMutex`, no Card method/Qt signal under registry lock, no manual generated-wrapper edit, no managed physical reclamation.
  - Parallelization: Wave 1 | Blocked by: 1 | Blocks: 3-8.
  - References: this plan N7, N10, N12 decision 9; `src/core/card.h:22-263`; `src/core/card.cpp:43-49`; `src/core/lua-runtime.cpp:43-82,152-163`; `src/core/lua-wrapper.cpp:276-286`; `src/server/room.cpp:6828-6841`; `swig/sanguosha.i:47-56,68-85,1385-1436`; generated runtime `builds/cmake-vs2026/generated/sanguosha_wrap.cxx:1062-1082,1803-1863,2526-2592,2756,2971+` inspection only.
  - Acceptance criteria: `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` and `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure` both exit 0; named Lua Card lifetime CTest passes; generated `builds/cmake-vs2026/generated/sanguosha_wrap.cxx` contains all three checked dispatches after the project block and `git diff -- swig/sanguosha_wrap.cxx` empty; tests cover owner=0/1, derived/base/static-`QObject` aliases, `CardList`/MustGetPtr, lightuserdata, deterministic address reuse, two runtimes, destruction racing first observation (destructor wins => no wrapper), Lua-invisible side-table authority, outermost-pcall pin surviving inner `lua_error`, and paused conversion/use; `ObserveOnly` physical timing unchanged and unobserved-card token count stays zero; **no `debug\\QSanguosha.exe --lua-test` gate**; PR2 still diagnostics/classification only, physical reclaim assertions remain at PR5/PR6. Every PR2 test constructs mode explicitly.
  - QA scenarios: happy - expose one Card through multiple static/dynamic types, destroy it through an injected native owner, and require both wrappers to return exact `Lua error: attempt to use deleted Card` with `actually_destroyed==1`; failure - let destruction win before wrapper creation, pass lightuserdata, forge uservalue, or drain during paused invocation/`lua_error` and require rejection/blocking without resurrection/UAF or pin loss (single destruction, no allocator crash requirement). Evidence: `<attemptDir>/task-2-lua-card-lifetime-safety-{red,focused,cumulative,lua,generated}.{txt,log}`.
  - Commit: Y | `fix: add generation-safe Lua card aliases`.

- [ ] 3. PR3 - Route deletion ingress through provenance-aware legacy-exact APIs
  - What to do: declare the same-name public slot `Card::deleteLater()` for native-origin requests and keep `%extend Card::deleteLater` routed directly to a separate Lua-origin bridge. Intercept Card-as-QObject `QObject::deleteLater()` by dynamic type: `Card::event` consumes `QEvent::DeferredDelete` and enters the same native-origin policy; ordinary non-Card QObject deletion is unchanged. Register/classify Engine clones, Lua Card clones/constructors, `Card::Parse`, `Card::Clone`, definitions, numeric borrowed Cards, and unclaimed birth transactions. Implement manager-aware release descriptors that preserve every PR1 call site's original immediate/deferred timing in `ObserveOnly`; repair all `safeTurnCardToEquip` early exits in `src/package/yjcm2023.cpp` as the first explicit/orphan fixture.
  - Required policy: N3. Lua known transient -> pending request; unknown `ObservedExternal` -> exact unknown-ownership error, `unknown_card_delete +1`, state/queue unchanged; known definition/adopted -> compatibility no-op with `definition_delete_ignored`/`adopted_delete_ignored`; native unknown parentless -> claim only after definition/borrowed/adopted/parent exclusions. Orphan reaping remains production-disabled until PR7, but explicit `ManagedReclaim` tests may exercise it.
  - Regression-first proof: add native Card/SkillCard/derived/meta-object ingress, `QObject* q = card; q->deleteLater()` dynamic-type ingress, ordinary non-Card QObject control, Parse return-after-request, definition/adopted no-op, exact unknown error, and four yjcm2023 failure-branch tests before implementation and retain red/green evidence.
  - Must NOT do: no QObject-wide override, parentlessness-as-ownership heuristic, unknown Lua fail-open, immediate invalidation at request time, production managed reclaim, legacy call-site spelling changes, or hidden definition deletion.
  - Parallelization: Wave 1 | Blocked by: 2 | Blocks: 4-8.
  - References: this plan N1–N3, N11, N12 decisions 1,3,6,11; `src/core/card.h:22-263`; `src/core/card.cpp:565-680`; `src/core/engine.cpp:1286-1370`; `src/core/lua-wrapper.cpp:308-631`; `src/core/room-state.cpp:37-58`; `swig/sanguosha.i:1385-1436`; `src/package/yjcm2023.cpp:29-44`; `docs/lua-ext-spec.md:1409-1421`.
  - Acceptance criteria: exact-policy tests and same-stack `clone -> deleteLater -> method before flush` pass; `QObject* q = card; q->deleteLater()` enters Card native-origin policy; ordinary non-Card QObject test proves unchanged behavior; source checker reports every ingress/factory classified and zero remaining qualified/member-pointer Card bypass; in `ObserveOnly`, fixture-recorded physical timing matches PR1; `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` and `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure` both exit 0 (unified form, no `debug\QSanguosha.exe --lua-test`); **PR3 still diagnostics/classification only, no `Retired->Dead` physical reclaim or `actually_destroyed` drain assertions (those start at PR5)**. Every PR3 test constructs mode explicitly.
  - QA scenarios: happy - known clone/delete/adopt/definition paths produce specified state/counter (`definition_delete_ignored`/`adopted_delete_ignored`/`unknown_card_delete`) and remain usable through current stack; `QObject* q = card; q->deleteLater()` counts as native-origin Card policy; ordinary non-Card QObject unchanged. Failure - delete unknown borrowed/external Card and require exact `pcall` `Lua error: attempt to delete Card with unknown ownership` plus unchanged generation/queue and `unknown_card_delete==1`, while deliberately unpaired zero-wrapper factory result is eligible only in explicit `ManagedReclaim` test mode; no allocator crash required. Evidence: `<attemptDir>/task-3-lua-card-lifetime-safety-{red,focused,cumulative,lua,policy}.{txt,json}`.
  - Commit: Y | `refactor: route card deletion through managed provenance`.

- [ ] 4. PR4 - Stabilize native owner surfaces and migrate every gameplay Card delete
  - What to do: change `Player::equips` to `QList<const WrappedCard *>` and migrate every N8.1 direct consumer; public EquipCard*/Card* getters keep their current shapes and resolve `getRealCard()` at query; document returned raw pointers as valid only until the next Room mutation/safe point. Preserve public `Card::change_cards` while adding change helpers and the generation sidecar `(source_generation, target_generation, duplicate_count)` plus the finite non-token `ChangeListRootSet`. Convert `ComboMovesCard` off `QVariant<const Card*>` to one explicit managed tag owner. Complete the PR1 sink ledger, including Card tags, whole-map copies, `Player::copyFrom`, parent-child destroy, `RoomState::reset`/`resetCard`, and `m_ownedCard`. Migrate every allowlisted gameplay Card raw delete/default smart-pointer deleter, including `CardUseStruct::m_ownedCard`, to the PR3 release/owner APIs with its legacy timing descriptor. Keep only fully qualified quiescent Engine/Package/RoomState/WrappedCard/test physical-destroy allowlist rows. **PR4 remains diagnostics/classification and storage migration only; physical reclaim timing unchanged (`ObserveOnly`) and no `Retired->Dead` drain assertions.**
  - Root/sidecar contract: N4 and N8.1–N8.2. Compatible mutation is defined only on the Room domain control thread. Route every in-tree append/clear through helpers that snapshot generations; the dedicated compatibility fixture must use the same snapshot API. Do not re-resolve public-list addresses on drain. Self-cycle is a gate failure; other cycles are reclaim blockers.
  - Regression-first proof: add equip-reset/takeOver stale-inner, stashed getter-after-mutation, owner-thread sidecar append/clear, self-cycle, length≥2 cycle, source-dtor edge drop, address-reuse non-reconnect, repeated ComboMoves replacement, representative immediate/deferred raw-delete, parent-child destroy, RoomState reset/resetCard, and allowlist rejection scenarios before changing storage/deleters.
  - Must NOT do: no `change_cards` type/access removal, global smart-pointer conversion, blanket delete replacement, event/QVariant lease semantics yet, physical managed reclaim in production, claim of cross-thread intercept, address-rescan reconciliation, or migration of non-Card deletes.
  - Parallelization: Wave 1 | Blocked by: 3 | Blocks: 5-8.
  - References: this plan N4, N8, N12 decisions 2,4,5; `src/core/player.h:507`; `src/core/player.cpp:877-1088,2495-2530,2921`; `src/package/tenyear2.cpp:28466-28474`; `src/core/card.h:136,237-238,335`; `src/core/card.cpp:709-729,921-932`; `src/server/roomthread.cpp:281-318`; `src/server/skill-runtime-coordinator.cpp:893`; `src/server/gamerule.cpp:554-560`; `src/core/room-state.cpp:5-58`; every candidate recorded in `tools/card-lifetime-allowlist.json`.
  - Acceptance criteria: equip queries always resolve new inner Card after reset/takeOver; public getter shapes remain; stashed pre-mutation `EquipCard*` not used by migrated consumers; sidecar fixture blocks/releases referenced generation without address rescan; self-cycle and reuse-reconnect counters stay at specified test values; ComboMoves replacement/clear destroys each clone once and is not `QVariant<const Card*>`; checker reports zero production direct `change_cards` mutations, zero legacy gameplay Card raw/default-deleter rows, and only approved quiescent owners; `ObserveOnly` timing fixtures, `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` and `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure` pass (unified, no `debug\QSanguosha.exe --lua-test`); **no physical reclaim/drain-zero assertions in PR4 (those start at PR5)**. Every PR4 test constructs mode explicitly.
  - QA scenarios: happy - exercise equip replacement, owner-thread compatibility append/clear on an un-tokenized finite root, ComboMoves overwrite/remove, copyFrom outer copy, and representative migrated deletes; failure - inject an unlisted delete or persistent Card root and require checker failure; inject self-cycle or reuse reconnect and require the matching counter; a cross-thread direct write is not treated as intercepted safety. Evidence: `<attemptDir>/task-4-lua-card-lifetime-safety-{red,focused,cumulative,audit}.{txt,json}`.
  - Commit: Y | `refactor: stabilize native card ownership sinks`.

- [ ] 5. PR5 - Implement dormant reclamation, native leases, and transactional WrappedCard adoption (T4)
  - What to do: implement N2/N3 states/transitions, generation-keyed pending/adoption sets, wrapper/runtime/native lease counts, owner links, N6 bounded counters, orphan eligibility, and canonical-owner destroy API (`Room::thread()`). Add lease ownership/copy/move/assignment/destruction to every closed `docs/card-lifetime-ownership.md` row in the same PR, following the N8.3 frozen matrix (`lease-bearing` / `scoped borrow` / `rejected opaque`); recursively extract only registered nested standard QVariant payloads; implement `(container,key,generation)` tag leases; consult the `change_cards` sidecar (do not re-resolve public-list addresses) and count cycles. Implement the exact N9 (T4) adoption table for `WrappedCard` constructor/takeOver/copyEverythingFrom/replacement/destructor: source-thread reserve, target-lease acquisition before move, snapshot, `moveToThread(Room::thread())`, then no-fail canonical-owner blocking commit. Every adoption and subsequent retirement targets the canonical owner `Room::thread()` (ordinary/1v1/3v3/XMode identical).
  - Required adoption failure: keep old `m_card` unchanged; all recoverable failures occur before `moveToThread`; before-move failure restores prior state/requeue on source affinity and releases the target lease; after a successful move, commit is no-fail and must not require rollback; no source-thread Card call after move; no manager/Lua/Room lock during `moveToThread` or blocking invoke; `void` returns only after terminal commit or pre-move failure. Target lifecycle lease is acquired before move.
  - Regression-first proof: add state-table tests, event-copy lease lifetime, tag overwrite/remove/container teardown, whole-map copy, detached QVariant, nested list/map, scoped-borrow persist failure, opaque custom metatype rejection with the exact error string, change-list cycle blocker, delete-then-adopt cancellation, address/generation replacement, cross-thread canonical-owner commit, parented/wrong-thread/no-dispatcher failures, target-lease-before-move coverage, and post-move no-fail commit assertion before implementation.
  - Must NOT do: no arbitrary QVariant memory introspection, room-wide fallback blocker, unknown opaque fail-open, persistent `QVariant<const Card*>`, address-rescan reconnect, asynchronous public adoption completion, cross-thread delete, production default switch, second owning field beside `CardUseStruct::m_ownedCard`, or post-move recoverable failure path.
  - Parallelization: Wave 1 | Blocked by: 4 | Blocks: 6-8.
  - References: this plan N2–N6, N8, N9 (T4), N12 decisions 3-6,11-12; `src/core/wrapped-card.cpp:3-83`; `src/core/room-state.cpp:5-58`; `src/core/structs.h:50-177,473-606,871-892`; `src/core/card.h:335`; `src/core/structs.cpp`; `src/server/card-movement-service.cpp:332,446,497`; `src/server/player-decision-service.cpp:1349`; `src/server/room.cpp:4765`; Player/Room/Card tag setters/removers discovered in PR1 ledger.
  - Acceptance criteria: all N2 state transitions match N2; event/tag/change-list copies block retirement until last exact-generation release; opaque custom type returns exact `Card lifetime error: rejected opaque QVariant Card payload`, increments `unknown_qvariant_card_payload`, and fails without fallback; persistent `const Card*` absent from production tags/`m_extraData`; adoption after pending request records `adopted_after_delete_request`, survives drain, moves affinity to `Room::thread()`, acquires target lease before move, and destroys once later on canonical owner; pre-move failure preserves old Card and ends with no leaked reservation/lease; post-move commit is no-fail; **explicit `ManagedReclaim` tests assert physical `PendingDelete->Retired->Dead` reclaim, `actually_destroyed==eligible_created_count`, and post-drain `managed_live==0 && pending_delete==0 && wrapper_leases==0`** while production/default remains `ObserveOnly`; `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` and `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure` pass. Every PR5 test constructs mode explicitly.
  - QA scenarios: happy - copy CardUse/Response/Damage/Effect/Judge and CardMoveReason/tag payloads, drain blocked then release and drain; adopt worker Card into canonical-owner WrappedCard and replace it safely with lease-before-move. Failure - inject opaque metatype, persistent `const Card*`, scoped-borrow detach, stale generation overwrite, parented Card, wrong current thread, missing canonical dispatcher, or target-lease failure and require exact counters/old-card preservation/no UAF; inject post-move failure and require no-fail violation detection. Evidence: `<attemptDir>/task-5-lua-card-lifetime-safety-{red,focused,cumulative,leases,adoption}.{txt,json}`.
  - Commit: Y | `fix: manage card leases and wrapped adoption`.

- [ ] 6. PR6 - Drain only at proven gameplay, worker-final, and Lua shutdown boundaries (T4 normalized)
  - What to do: add RAII `CardLifetimeScope`/domain epoch guards at `Room::useCard`, every public PlayerDecisionService ask/response boundary from the PR1 ledger, and outer `RoomThread::trigger`. Lua invocation depth and conversion pins stay with `LuaInvocationScope` (PR2/N5.1), not SWIG-local RAII nor `LuaRuntime::Binding`. Exceptions release the same C++ `CardLifetimeScope`s and waits keep them active. Add GUI next-turn, top-of-next-phase, post-turn/control-exception safe points per N5.2/N5.3 (phase/turn hooks only propose drain attempt; destructive drain only after outermost trigger/control/exception cleanup fully returned and N5.3 enumerated paths); never drain at `CardFinished`, decision return, or raw empty event stack. Insert the T4 setup-normalization step (N5/N9) that moves all RoomState outer/inner Cards to canonical owner `Room::thread()` after setup worker completes and before gameplay worker starts — identically on ordinary/1v1/3v3/XMode. Add outer final guards (`qScopeGuard` after `LuaRuntime::Binding`) to ordinary/1v1/3v3/XMode runs and implement the exact N5.6 `RoomRuntime::shutdownFinal()` 8-step order (1 join/handoff -> 2 atomic Closing -> 3 clear persistent roots -> 4 preclose drain -> 5 AI/game `lua_close` with registry/domain still valid -> 6 unregister/postclose -> 7 physical-owner destruction -> 8 final-zero assertion), with AI init-failure/restart separate per N5.7. **Emit per-game unique JSON final-gauge marker** from Room teardown/PR6 (e.g. `[CardLifetime] FINAL_GAUGE {"game":N,"managed_live":0,"pending_delete":0,"wrapper_leases":0,"lease_count":0,"explicit_owner":0,"sidecar_edges":0,"unknown_unclaimed":0}`) on canonical owner thread after postclose drain; each game exactly one marker, values are baseline/zero gauges for headless validation (see Verification strategy and PR8).
  - Worker-final order: N5 (T4). Stop creation, quiesce, clear ComboMoves/explicit owners, destroy event leases, apply tag/sidecar releases without address rescan, terminally retire unadopted worker transients even with wrappers, assert zero worker-affinity managed entries, then return/join. Adopted Cards must already be on canonical owner affinity (`Room::thread()`) via N9 setup normalization/adoption and are skipped; any remaining worker-affinity adopted Card is an invariant failure not repaired here.
  - Regression-first proof: add normal/skip phase, TurnBroken, StageChange, extra turn, GUI nested callback, active Lua invocation race, GameFinished/early-return final guards for all variants, last-operation persistent tag, live wrappers through shutdown, and fatal join/no-wrong-thread fallback scenarios before installing hooks; add marker validation test that fails when JSON marker missing/duplicate or any gauge nonzero.
  - Must NOT do: no RoomThread event-loop assumption, exception-scope destructor deletion, post-join worker deletion, GC-created work, destructor-to-Lua callback, silent final counter leak, or deletion of adopted/definition Cards in worker final.
  - Parallelization: Wave 1 | Blocked by: 5 | Blocks: 7-8.
  - References: this plan N4, N5; `src/core/card.cpp:707-761`; `src/server/room.cpp:223-294,3635-4000`; `src/server/roomthread.cpp:603-685,989-1160`; `src/server/roomthread1v1.cpp:20-113`; `src/server/roomthread3v3.cpp:64-116`; `src/server/roomthreadxmode.cpp:19-115`; `src/server/player-decision-service.cpp:1139-1274,1448-1485,1553-1559,1728-1794`; `src/server/room-runtime.cpp:8-19`; `src/core/lua-runtime.cpp:43-56`; `src/server/ai-runtime.cpp:412-415`.
  - Acceptance criteria: every named normal/exception/final path drains on recorded affinity only after depth/leases/owners clear; active invocation prevents destruction until return; all worker variants report zero worker-affinity managed entries before join; preclose/close/postclose leaves per-runtime gauges at baseline and GC enqueues zero work; explicit `ManagedReclaim` `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` and cumulative `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure` pass, default remains `ObserveOnly`; **PR6 emits per-game unique JSON final-gauge marker and asserts within test harness that marker count==games, no duplicate/missing, and every gauge value==0 (otherwise test fails); production code does not claim allocator must crash, only `actually_destroyed` and counter checks**; every PR6 test constructs mode explicitly.
  - QA scenarios: happy - run each safe/final boundary with pending transients, live aliases, copied payloads, adopted Card, and persistent tag and assert exact destruction order/counters; failure - pause invocation, attempt early CardFinished/event-stack drain, inject wrong affinity or early return, and require deletion blocked or invariant failure without UAF. Evidence: `<attemptDir>/task-6-lua-card-lifetime-safety-{red,focused,cumulative,shutdown}.{txt,json}`.
  - Commit: Y | `fix: drain transient cards at safe lifecycle boundaries`.

- [ ] 7. PR7 - Enable managed reclamation through one reversible default switch (T4 pre-gate)
  - What to do: first add a real `RoomState::reset` / `RoomState::resetCard` integration test exercising the T4 canonical-owner path (setup-normalized outers/inners on `Room::thread()`, lease-before-move, no-fail commit, and quiescent retirement of replaced inners) — this test is a PR7 entry gate and must pass before the switch. Then verify every N6 prerequisite gate, then change only `defaultCardLifetimeMode()` from `ObserveOnly` to `ManagedReclaim` plus the inseparable default-mode expectation/release note. Keep all other tests on explicit mode selection. Produce default-off/default-on A/B counter and timing evidence at the same source SHA apart from the switch.
  - Default-on gate: N6 zero gates plus T4 gate. `unknown_unclaimed == 0`, `unknown_qvariant_card_payload == 0`, `change_list_self_cycle == 0`, `change_list_cycles == 0`, `change_list_reuse_reconnect == 0`, `unapproved_card_raw_delete == 0`, `card_delete_bypass == 0`, `adoption_failed == 0`, `affinity_transfer_failed == 0`, zero unlisted persistent tag/root/lease rows, zero persistent `QVariant<const Card*>`, zero worker/runtime final gauges, source checker clean, `RoomState::reset`/`resetCard` integration test green (T4 canonical owner, lease-before-move, post-move no-fail, affinity on `Room::thread()`), focused/Lua/cumulative gates green.
  - Regression-first proof: before flipping the default, add the RoomState integration test and the unique selector-default regression that expects managed orphan/delete/safe-point behavior with no explicit mode override and observe the selector test fail while explicit `ManagedReclaim` (and the new RoomState test in explicit `ManagedReclaim`) passes; flip the single default and make both green. Also prove explicit `ObserveOnly` still matches the legacy timing baseline. No other test may omit an explicit mode argument.
  - Must NOT do: no new mechanism, sink, mode, public flag, extension edit, unrelated refactor, hard-coded PR8 docs assertion, or removal of ObserveOnly tests.
  - Parallelization: Wave 1 | Blocked by: 6 | Blocks: 8.
  - References: this plan N6, N11, N12 decision 10; `src/core/card-lifetime-manager.{h,cpp}` created by PR1; all PR1-6 evidence and `tools/check-card-lifetime.py`.
  - Acceptance criteria: PR7 semantic diff is limited to the default selector and inseparable expectation/note; all zero gates above are machine-asserted; default managed tests, explicit ObserveOnly tests, `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` (focused, 含具名 Lua CTest) and `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure` (cumulative) pass; a dry-run patch that restores ObserveOnly makes only the default-mode expectation change while PR1-6 safety/diagnostic tests remain green, proving emergency behavior rollback. Never use `debug\QSanguosha.exe --lua-test`.
  - QA scenarios: happy - run identical clone/delete/adopt/orphan fixtures in explicit ObserveOnly, explicit ManagedReclaim, and the unique default-mode test and compare exact counters/timing; failure - force any prerequisite gauge nonzero and require default-on startup/test gate to refuse approval. Evidence: `<attemptDir>/task-7-lua-card-lifetime-safety-{red,focused,cumulative,ab,rollback}.{txt,json}`.
  - Commit: Y | `feat: enable managed card reclamation by default`.

- [ ] 8. PR8 - Prove bounded scale, real 20p behavior, generated bindings, and documentation
  - What to do: finish the parameterized 10,000-iteration ring (batch 64 / ring 64) and 30/50-actor synthetic rows with exact N6 parameters **plus T6 strict synthetic spec**: 單一獨立 `CardLifetimeManager` domain（`ManagedReclaim`）、單一 canonical owner thread（`Room::thread()`／測試 owner thread）、每 epoch 生產者 barrier 對齊、每 actor 每 epoch 四項操作固定順序 `create -> expose -> request delete -> tryDrain`、seed `2026082201`、epochs `200`、唯一 CTest（`qsanguosha_card_lifetime:synthetic-30`／`qsanguosha_card_lifetime:synthetic-50` 參數化），每 epoch 斷言 `managed_live<=actor_count && pending_delete<=actor_count && wrapper_leases<=2*actor_count` 且 drain 後 `==0`。為 `tools/autotest/headless_runner.py` 新增明確 `--exe` 參數（固定使用 Debug executable `debug/QSanguosha.exe` 或 `builds/cmake-vs2026/debug/QSanguosha.exe`，禁止僅 `--exe-root` 隱式）並保留 `--seed` 直通；執行真實五局固定種子 `20p` headless；更新 `docs/lua-ext-spec.md`（維持拼法不變、兩 fixed Lua error、same-stack PendingDelete grace、alias／adoption／definition 行為、ObserveOnly／ManagedReclaim 條件語意，不寫死當前 default）。RSS 僅作佐證，不作 CI 門檻。所有 PR8 測試除沿用 PR7 唯一 selector-default 外皆顯式構造 mode。
  - Regression-first proof: add seed-runner parsing/forwarding and peak/baseline assertions before implementation; capture failure for missing seed forwarding or iteration-dependent token growth, then make them green. Explicitly reject attempts to invoke nonexistent 30p/50p product modes; those numbers exist only as synthetic actor rows.
  - Must NOT do: no production 30p/50p modes, brittle exact RSS threshold, generated-wrapper commit, extension migration, undocumented default assertion, new runtime behavior beyond PR7, or weakening stress counts to obtain green.
  - Parallelization: Wave 1 | Blocked by: 7 | Blocks: F1-F4.
  - References: this plan N6, N13, Verification strategy commands; `src/core/engine.cpp:337-367`; `src/server-main.cpp:17-45`; `src/server/server.cpp:2044-2069`; `tools/autotest/headless_runner.py:66-140,146-220`; `tools/autotest/runner_common.py:231-246`; `tests/card-lifetime-manager-test.cpp`; `tests/CMakeLists.txt`; `docs/lua-ext-spec.md:1409-1421`; `swig/sanguosha.i`; generated wrapper inspection only.
  - Acceptance criteria: N6 ring 與 30/50 synthetic rows 公式通過且每 epoch 內 `managed_live<=actor_count && pending_delete<=actor_count && wrapper_leases<=2*actor_count` 且 epoch 末 drain 後全部回零；`actually_destroyed==eligible_created_count`；runner 透過 `--exe` 固定 Debug executable 並直通 unsigned `--seed`（非法 seed 拒絕）；`ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure`（含 synthetic 唯一 CTest）與 `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure` 皆 exit 0，**不含 `debug\\QSanguosha.exe --lua-test`**；headless 以 `python tools/autotest/headless_runner.py --exe <Debug exe> --modes 20p --games 5 --parallel 1 --seed 2026082201` 執行，校驗 5/5 完成、零失敗、done marker、每局唯一 JSON final-gauge marker（缺失／重複／任一值非零即失敗並寫入 CSV）、最終 Room gauges 回 baseline；generated wrapper 已重建且 `git diff -- swig/sanguosha_wrap.cxx` 為空、legacy extensions 未改；docs 條件式描述兩模式。
  - QA scenarios: happy - run exact ring, 30/50 actor（單一 domain／owner thread／barrier／四操作順序）於唯一 CTest、完整 `ctest --test-dir builds --output-on-failure`、具名 Lua CTest、以及 `python tools/autotest/headless_runner.py --exe <Debug exe> --modes 20p --games 5 --parallel 1 --seed 2026082201`，封存 logs／CSV（含 marker 校驗）／counters；failure - 非法 seed 以既有 stable error 退出、opaque／unknown 計數器非零阻擋成功、刻意 iteration-retained token 違反 `peak_live_tokens` 公式、marker 缺失／重複／非零使 PR8 失敗並寫入 CSV、跨越 `actor_count` 的 per-epoch peak 失敗。Evidence: `<attemptDir>/task-8-lua-card-lifetime-safety-{red,focused,cumulative,lua,stress,20p}.{txt,json,log,csv}`.
  - Commit: Y | `test: add card lifetime stress and rollout coverage`.

## Final verification wave
> Runs in parallel after ALL todos. ALL must APPROVE. Surface results and wait for the user's explicit okay before declaring complete.
- [ ] F1. Plan compliance audit (T4)
  - Verify the final SHA against every Must have/Must NOT have item, N1–N14 including N8.1–N8.6 and N9 T4 canonical owner (`Room::thread()`), all 12 owner-decision traceability rows, eight PR evidence triplets (including PR1 dispatcher-liveness characterization and pre-PR7 `RoomState::reset`/`resetCard` integration test), source/sink ledger, frozen QVariant matrix, exact Lua errors, exact opaque QVariant error, N6 counter baselines/default-on gates (including `change_list_*` zeros), and dependency/revert contract. Re-run `python tools/check-card-lifetime.py ...` and structural searches for bypasses, persistent `const Card*` tags, `Player::equips` inner-pointer stores, and non-canonical adoption targets. Output APPROVE only with zero unexplained row or missing artifact; otherwise cite file/line/task and request changes. Evidence: `<attemptDir>/final-F1-plan-compliance.{md,json}`.
- [ ] F2. Code quality and concurrency review (T4)
  - Review the full diff and relevant unchanged callers for token linearization, Lua-invisible side-table authority, destruction-won wrapper refusal, outermost-pcall conversion pins (no `lua_error`-skippable RAII), manager/Lua lock order, Qt affinity/move/BlockingQueuedConnection safety to canonical owner `Room::thread()`, Card-as-QObject `DeferredDelete` dispatch, adoption failure atomicity (all recoverable before move, no-fail after move, target lease before move, setup normalization), lease copy/move/release, shutdown order, ABI/source compatibility, and bounded storage. Run `cmake --preset vs2026-x64` (if needed) + `cmake --build --preset debug --parallel 8` + `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` + `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure`. Output APPROVE only if diagnostics are clean, checker 說明 AST／pattern 能力與盲區且不以 scan clean 單獨宣稱閉合，並確認 no manager lock encloses Lua/Qt/Card work. Evidence: `<attemptDir>/final-F2-code-quality.{md,txt}`.
- [ ] F3. Real manual QA (T4)
  - Use the built Debug artifact, not mocks alone: run `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure`（含具名 Lua Card lifetime CTest，取代已移除的 `debug\QSanguosha.exe --lua-test`）、exact N6 deterministic stress rows（單一 domain／owner thread／barrier／四操作，per-epoch `managed_live<=actor_count && pending<=actor_count && wrapper_leases<=2*actor_count` 且 drain 後零）、the pre-PR7 `RoomState::reset`/`resetCard` integration test, and the exact `python tools/autotest/headless_runner.py --exe <Debug exe> --modes 20p --games 5 --parallel 1 --seed 2026082201` headless command. Inspect the generated wrapper, headless log/CSV（含每局唯一 JSON final-gauge marker 校驗：缺失／重複／非零即失敗）、stale/unknown exact error text (`Lua error: attempt to use deleted Card`／`Lua error: attempt to delete Card with unknown ownership`／`Card lifetime error: rejected opaque QVariant Card payload`)、destruction-affinity traces (canonical owner `Room::thread()`), PR1 dispatcher-liveness evidence (4 variants), and final counter snapshots. Output APPROVE only for 5/5 games, no crash/UAF/double delete, zero worker-affinity adopted survivors, marker 校驗通過，且 every Room/runtime gauge at baseline. Evidence: `<attemptDir>/final-F3-manual-qa.{md,log,csv,json}`.
- [ ] F4. Scope and design-system fidelity (T4)
  - Audit `git diff --stat` and full diff for Card-only scope: no general QObject/lifetime redesign, no all-Card eager tokens, no permanent raw-address tombstone, no SWIG-local RAII conversion pins, legacy extension or `lua/ai` edits, tracked generated wrapper, public `change_cards` removal, public EquipCard* getter-shape change, persistent `QVariant<const Card*>`, address-rescan reconnect of change-list edges, claimed cross-thread mutation intercept, Engine physical-card ownership takeover, production 30p/50p modes, public feature flag, unrequested dependency, GameOver quarantine, unrelated cleanup, or non-canonical adoption owner substitution (must be `Room::thread()` per T4 with no silent fallback). Verify PR7-only emergency rollback and full 8->1 order remain valid and that ordinary/1v1/3v3/XMode share one canonical owner. Output APPROVE only when all guardrails hold. Evidence: `<attemptDir>/final-F4-scope-fidelity.{md,txt}`.

## Commit strategy

- Use one atomic commit/PR per implementation todo; keep each implementation with its direct tests and evidence references. Never land a failing red test, generated wrapper, build output, `.omo/evidence`, or unrelated user change.
- Detect the live branch/upstream and recent subject style before execution; current observed style is lower-case Conventional Commit type plus concise English summary. Do not commit/push from L/H directly unless the later `$start-work` invocation explicitly authorizes it; use a task-owned worktree for `--make-pr`/`--ship`.

| PR | Subject | Revert contract |
|---:|---|---|
| 1 | `test: characterize card lifetime hazards` | removes only diagnostics/ledger/test foundation after downstream PRs are gone |
| 2 | `fix: add generation-safe Lua card aliases` | revert after PR3-8; ObserveOnly timing was preserved |
| 3 | `refactor: route card deletion through managed provenance` | revert after PR4-8; restores original ingress |
| 4 | `refactor: stabilize native card ownership sinks` | revert after PR5-8; preserve non-Card deletes |
| 5 | `fix: manage card leases and wrapped adoption` | revert after PR6-8; production still ObserveOnly before PR7 |
| 6 | `fix: drain transient cards at safe lifecycle boundaries` | revert after PR7-8; production still ObserveOnly before PR7 |
| 7 | `feat: enable managed card reclamation by default` | independently revertible emergency behavior switch |
| 8 | `test: add card lifetime stress and rollout coverage` | full rollback starts here; explicit-mode tests/docs remain valid if only PR7 is reverted |

- Full rollback order is `8 -> 7 -> 6 -> 5 -> 4 -> 3 -> 2 -> 1`. Arbitrary removal of a middle PR while retaining dependents is explicitly unsupported.

### Rollback rehearsal (隔離 worktree, final SHA)

> 演練必須在 final SHA 的隔離 worktree 完成，禁止在 L/H 主工作樹直接回退；所有驗證指令與正式 gate 同形。

**演練 1 — 正常全回退 8→1：** 於 final SHA 以 `git worktree add --detach <tmp>` 建立隔離 worktree；依序 `git revert`/`git reset` PR8→PR7→PR6→PR5→PR4→PR3→PR2→PR1（嚴格依相依序），每步執行 `cmake --build --preset debug --parallel 8` + `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` + `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure`，確認每個 prefix 皆可建構且測試綠態；全程 `git diff -- swig/sanguosha_wrap.cxx` 為空。

**演練 2 — 單獨回退 PR7（保留 PR8）＋全量驗證後再全回退：** 於另一隔離 worktree 僅回退 PR7（`defaultCardLifetimeMode()` 回 `ObserveOnly`，保留 PR8 stress/文件），重跑：`cmake --build --preset debug --parallel 8`、`ctest -R "^qsanguosha_card_lifetime$" --output-on-failure`（focused）、Lua 子集 `qsanguosha_card_lifetime:lua`、`ctest` 全量、`ctest -R "synthetic-(30|50)"` 或 `qsanguosha_card_lifetime:synthetic-30/50` stress（含 per-epoch `managed_live<=actor_count && pending<=actor_count && wrapper_leases<=2*actor_count` 且 drain 後零）、`python tools/autotest/headless_runner.py --exe <Debug exe> --modes 20p --games 5 --parallel 1 --seed 2026082201` 的 ObserveOnly 20p（5/5 完成、零失敗、JSON marker 缺失/重複/非零即失敗）。驗證 PR1–6/8 綠態且預設行為回退；其後於**同一 worktree** 接續依序回退 PR8→PR6→PR5→PR4→PR3→PR2→PR1 完成全回退至基線。每步同樣檢查 `git diff -- swig/sanguosha_wrap.cxx` 為空且無產品程式碼殘留。

## Success criteria

- Every Lua wrapper/alias of a destroyed or retired Card rejects conversion before native dereference with exact `Lua error: attempt to use deleted Card`; address reuse never revives or equates an old generation.
- A known transient Lua `card:deleteLater()` remains source-compatible and usable until the current safe scope ends; physical destruction then occurs exactly once on affinity. Owning wrapper GC cannot double-delete.
- Unknown `ObservedExternal` Lua deletion returns exact `Lua error: attempt to delete Card with unknown ownership` without state/queue change. Known definition/adopted deletion is a compatibility no-op with the correct ignored counter and an otherwise uninterrupted callback.
- Token allocation is proportional to observed/managed Cards only; unobserved Engine/Package Cards receive no token/ownership sidecar. Dead tokens survive only while stale wrappers/references exist. Authoritative generation and original-own bits are Lua-invisible. Every SWIG exposure observes while a live owner/borrow/`LuaInvocationScope` remains; destructor-won control refuses wrapper creation. Conversion pins are held by the outermost `LuaInvocationScope` (not SWIG-local RAII nor `LuaRuntime::Binding`).
- All gameplay Card raw/default-deleter paths are manager-aware; only audited quiescent owners physically delete, and all bypass/unknown gauges are zero before the default switch.
- Player equipment stores outer `WrappedCard *`; public EquipCard* getters resolve `getRealCard()` at query and return borrows valid only until the next Room mutation/safe point. `change_cards` sidecar edges are `(source_generation, target_generation, duplicate_count)`; self-cycles fail the gate; other cycles block reclaim and are zero before PR7; source destruction clears outgoing edges; address reuse does not reconnect. Event structs, CardMoveReason, Card/Player/Room tags, and ComboMoves follow the frozen QVariant matrix. Persistent `QVariant<const Card*>` is forbidden. Opaque Card-bearing metatypes return the exact rejected-opaque error and fail the gate rather than guessing.
- WrappedCard adoption (T4) cancels the exact pending generation, acquires target lifecycle lease before move, transfers affinity to canonical owner `Room::thread()` before commit, commits no-fail after a successful move, preserves the old Card only on pre-move failure, and never leaves a worker-affinity adopted Card for final drain; setup-normalized RoomState outers/inners and all ordinary/1v1/3v3/XMode paths share the same canonical owner, verified by the PR1 dispatcher-liveness characterization and the pre-PR7 `RoomState::reset`/`resetCard` integration test.
- Normal/skip/TurnBroken/StageChange/extra-turn/GUI and normal/1v1/3v3/XMode final paths reclaim only after zero scopes/leases/owners; preclose/close/postclose leaves every Room/runtime gauge at baseline and posts no work to an exited worker.
- The N6 10,000-iteration and synthetic 30/50-actor formulas pass without iteration-proportional token/pending growth. Real fixed-seed `20p` completes 5/5 with exit 0, zero failed games, no crash, and baseline counters.
- Except the unique PR7 selector-default regression, every lifetime test explicitly selects mode. `cmake --preset vs2026-x64` (if needed) + `cmake --build --preset debug --parallel 8` + `ctest --test-dir builds/cmake-vs2026 -C Debug -R "^qsanguosha_card_lifetime$" --output-on-failure` (具名 Lua Card lifetime CTest，取代已移除的 `debug\QSanguosha.exe --lua-test`) + `ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure` + source checker + generated-wrapper inspection and F1-F4 all approve on the same final SHA. `git diff --check` passes; tracked `swig/sanguosha_wrap.cxx`, legacy extensions, `lua/ai`, third-party `include/`/`lib/`, and production mode registry remain untouched. Per-epoch `managed_live<=actor_count && pending<=actor_count && wrapper_leases<=2*actor_count` and drain-after-zero, plus per-game unique JSON final-gauge marker (missing/duplicate/non-zero fails PR8 CSV), are part of the success gate.
