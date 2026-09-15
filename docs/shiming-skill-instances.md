# Shiming skill instances (SI)

## Contract

Mission status belongs to a live `SkillInstanceRef(ownerObjectName, key)`.
The key's ID must be positive; there is no name, wildcard, minimum-ID, or
surviving-sibling fallback. `Skill` objects remain shared definitions.

| API | Behavior |
| --- | --- |
| `getShimingStatus(ref)` | `0` pending, `1` success, `2` failure. Invalid/stale references return `0`; validate ownership before treating a reference as pending. |
| `setShimingStatus(ref, status, index = -1)` | Set/reset an exact instance. Returns false for invalid input or unchanged status; otherwise true. Explicit transitions between terminal states remain supported. |
| `sendShimingLog(ref, success = true, index = -1)` | Complete a **pending** exact instance through the same status/event/callback pipeline. Returns false if already complete or stale. Callers must check the result before awarding effects. |
| `onShimingSuccess/Fail(room, player, ref)` | V2 definition callback for that exact instance, after the corresponding event. |

`state.shiming_status` is authoritative. `state.shiming_revision` distinguishes
nested transitions, including success → reset → success. Both use existing
ServerPlayer state synchronization and snapshot serialization. The state is
owner-only; observers receive ordinary public logs, not private metadata.
Removal destroys state; reacquisition starts pending with a new ID.

`EventShimingSuccess` / `EventShimingFail` data now contains `SkillInstanceRef`.
C++ listeners use `data.value<SkillInstanceRef>()`; Lua listeners use
`data:toSkillInstanceRef()`. Observers can listen to another mission's event,
but must compare the **owner and key** when matching a particular mission.
After event listeners run, the completion callback is skipped if its reference
was removed or its transition was superseded. Public log/voice/translation
labels deliberately remain base skill names, as required by the instance plan.

The name/Skill-pointer Room overloads and `name__success` / `name__fail` status
marks were removed. Old saved name marks are not copied into new instances.
Pre-SI scripts and snapshots carrying only those marks require migration; their
ambiguous histories cannot be assigned to individual instances automatically.

## Lua migration

```lua
-- In a V2 callback:
local ref = ctx:getActivationRef()
if room:getShimingStatus(ref) == 0 then
    room:sendShimingLog(ref) -- true only for a newly committed success
end

on_shiming_success = function(self, room, player, ref) ... end
on_shiming_fail = function(self, room, player, ref) ... end
```

The first three completion callback arguments are unchanged; the previous
`lua-ext-spec.md` event/data/ask_who signature was inaccurate.

Mission skills use `sgs.CreateTriggerSkillV2 { shiming_skill = true, ... }`.
`can_trigger(self, event, room, player, data)` returns eligible exact keys;
`on_cost` and `on_effect` use `ctx:getActivationRef()` and `ctx.original_data`.
RoomThread expands and validates instances through the existing V2 dispatcher.
Automatic mission events use compulsory frequency so completing a mission does
not introduce an optional trigger-order cancellation. Selectors filter event
conditions and pending instances; effects revalidate after intervening V2 hooks.
Completion callbacks remain on the same V2 definition. No mission factory or
legacy callback adapter is required. Cross-player selectors use the existing
V2 owner-return format. The ordinary legacy scheduler is unchanged.
`Player:getValidSkillInstanceIds(name)` is exposed through SWIG.

## Inventory and deliberate shared effects

All nine C++ mission implementations explicitly dispatch valid instance IDs:

| File | Skills / changes |
| --- | --- |
| `src/package/maotu.cpp` | `mtnianchou`: exact completion and exact self-detach |
| `src/package/dream.cpp` | `iflitian2`: per-instance completion; card recovery modifier runs once |
| `src/package/yinhu.cpp` | `yhjifeng`, `yhtanyou`: exact outcomes; Tanyou's limited-use flag and chosen players are instance state |
| `src/package/mobileshiji.cpp` | `secondmobilexinqingyu`, `secondmobilexinmibei`, `xinpowei`: exact outcomes; Powei's target lists are instance state |
| `src/package/mobile.cpp` | `weiming`, `zhongao`: exact outcomes; Weiming targets are instance state; death logs identify the mission owner |

Deliberate shared effects, **not mission status storage**:

- Xietu is a separate skill with no parent-instance relationship to Weiming.
  Its upgrade explicitly aggregates live Weiming outcomes: any success wins,
  otherwise any failure, otherwise pending. The external AI uses the same rule.
- `&stscdlwei` is a public board display used by other skills. C++ XinPowei
  eligibility and target movement use its own instance lists. Its temporary
  attack-range pairs remain player-to-player effects and are cleared once.
- Mibei observes Xingqi's shared `second_mobilexin_wangling_bei` collection and
  Zifu's turn facts. They belong to separate skills, not a Mibei completion key.
- `LostPlayerPhase_*` describes phases actually lost; Tanyou's buffs and Yhkudu
  translation are effects on the player. `@yhtanyouMark` remains a display;
  `tanyou_used` gates each instance independently.
- `mtnianchou_from/to-Clear` are temporary distance effects; Zhongao's card-use
  count is a turn fact, and its Kuanggu buff/avatar are player-wide rewards.
  Companion skill acquisition/removal keeps existing game effects; only
  detaching the mission itself is made exact.
- Public translations, skill animation names, sound names and display marks
  remain base-name labels. They do not decide which mission is complete.

## External Lua content

`extensions/` and `lua/ai/` are ignored runtime inputs maintained in
`lolosiyue/extensions`. Mission migration is maintained directly in that
external repository. `tools/ci/fetch-extensions.sh` fetches its Lua content
without applying a mission migration patch from this repository.
Use an external revision compatible with the exact-reference mission API.
The inventory below describes the required integration contract; it is not
verification of a particular current external revision.

| External file | Migration |
| --- | --- |
| `extensions/scarlet.lua` | V2 `s4_fuhan`, `s4_ganglie`: exact selectors/status/callback refs; Ganglie attacker history is per instance |
| `extensions/newgenerals.lua` | `powei`, `mouhuoji`: standard V2 dispatch; Mouhuoji damage history and self-detach are exact |
| `extensions/rushB.lua` | `rushB_moubazhen`: standard V2 dispatch, instance card-name record, exact completion/self-detach |
| `extensions/DragonLoke.lua` | `dl_chaju`: standard V2 dispatch, exact card-origin progress and outcome; independent completion guards |
| `extensions/sijyu.lua` | `sijyu_xinghan`: explicit owner/ref enumeration and instance partner identity |
| `lua/ai/mobile-ai.lua`, `lua/ai/scarlet-ai.lua` | Remove old status mark reads; use explicit aggregate outcome heuristics |

External legacy ViewAs availability/history, hero replacement, public card
cancellation, charge/Zhan pools and board marks remain shared game effects.
`powei` observes Dulie's shared board marks; `rushB_moubazhen` observes the shared
death event counter. These are deliberate predicates on the board, not outcome
keys. The only old API text left in external content is a commented-out example
in `extensions/kearcane.lua`; it is not executable.

Inventory command (after fetching runtime content):

```sh
rg -n 'sendShimingLog|setShimingStatus|getShimingStatus|on_shiming|weimingShiming|__success|__fail' src swig lua extensions
```

## Automated coverage

The existing server `skill-runtime` suite tests two same-name instances,
different owners with the same ID, single-instance transitions, duplicate logs,
reset, invalid/stale refs, removal/reacquisition, reentrant completion callbacks,
owner-only deltas/snapshots, Weiming/Powei/Qingyu package behavior, and Lua exact
callback/event/dispatch bindings. `QSAN_TEST_SHIMING_EXTERNAL=1` additionally
tests external Powei/Fuhan/Ganglie behavior. No new build target is needed.
