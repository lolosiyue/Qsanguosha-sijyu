# Lua skill state conveniences

These five operations remove repeated marshalling, selected-key cleanup and
direct-child lookup from Lua. They reuse `SkillInstance.state` and
`SkillInstance.correctState`; they introduce no store, protocol, snapshot or
history format. Statistics and rollback code are unchanged.

The example migration uses companion `extensions/main` at
`d3fd79fc47027824ecb4f0365e011c52ccf2a667`. Engine base is debug
`55e6532ca704e3d978481c225bbf0ad43c53d52b`, with the existing TV/scenario fixes
preserved through integration base `a774b9fb14d1401bd4638c51acb26ecba2f6dae6`.
Apply the companion diff to the current file during integration; do not copy an
old whole `scarlet.lua` over concurrent edits.

## API contract

| Lua operation | Result | Semantics |
| --- | --- | --- |
| `player:getSkillInstanceStateStringList(name, iid, key)` | string array | Empty array for missing instance/key or a value whose QVariant type is not QStringList. Does not parse scalar strings. Returns a copy. |
| `player:setSkillInstanceStateStringList(name, iid, key, values)` | boolean | False for missing instance or empty key. Dense Lua string array; preserves order, duplicates, empty strings and delimiters. Empty array removes the key. Otherwise stores a QStringList QVariant. |
| `player:removeSkillInstanceStateKeys(name, iid, keys)` | integer | Removes only existing named keys. Empty/missing keys and repeats are skipped. Returns number removed, or zero for missing instance. |
| `room:setChildSkillInstanceCorrectState(owner, parentName, parentId, childName, key, value)` | integer | Exact named direct SourceHelper children on this owner's parent instance. Uses existing Room correctState setter for each match. Returns number accepted, including an unchanged accepted value. |
| `room:removeChildSkillInstanceCorrectState(owner, parentName, parentId, childName, key)` | integer | Same selection; uses existing Room remover. Returns number removed; absent keys give zero. |

The two child operations require the exact live owner in this Room, a live
parent, and nonempty child name/key. No match, another player's instance,
SourceAttached grant, descendant, sibling root, or non-V2-correction definition
is modified. They use the parent owner as the existing setter's `source` for
description attribution. The QVariant value keeps its type; zero, false and an
invalid QVariant are passed through with existing correctState semantics.
Use the explicit remove operation to erase a correction key.

Lua list inputs reject numbers, booleans, sparse arrays and named table fields
before mutation. Other existing QStringList bindings retain their conversion
rules. The getter has no default parameter because the typed empty list is its
only default; use the original QVariant getter for other types/defaults.

## Visibility, notifications and lifetime

`state` stays private. Mutations dispatch through the existing virtual
`setSkillInstanceStateValue`/`removeSkillInstanceStateValue`, so ServerPlayer
retains owner/controller-only notification and normal UI updates. A selected-key
cleanup emits the existing per-key remove notifications in input order; it is
not an atomic batch. No state map is replaced, so unrelated keys survive.

`correctState` remains restricted to Distance/MaxCards/TargetMod/AttackRange V2
definitions. Child updates go through the existing coordinator and its
`canReceiveSkillInstance` filter. This is **not an unconditional broadcast**;
SourceHelper and Hegemony concealment rules still apply. No private state is
automatically copied to it. AI/external-agent and UI projections are unchanged.

Both stores keep the existing instance lifetime. The Room detach path removes
the root and its direct helper instances, including their state. The new methods
neither create instances nor revive removed IDs. Turn/phase/death expiry stays
in the skill's existing callbacks; the methods do not infer an expiry scope or
clear unrelated effect descriptions/marks.

Full Yongyi suit records, Yongqian targets and Yingzi's hand/HP/equipment
conditions stay private. The migration preserves the previously declared marks,
effect descriptions and correction counts. Delimiter filtering, badge reference
counts and stale badge/death cleanup remain skill-specific Lua.

## Actual before/after examples

Yongqian's two local marshalling functions formerly looped over
`getSkillInstanceStateValue(...):toStringList()`, and wrapped writes using
`QVariant():setStringList(table.concat(targets, "|"))`, with a special empty-list
remove branch. Call sites now use:

```lua
local targets = player:getSkillInstanceStateStringList(
    "s4_cloud_yongqian", ctx.instanceID, "target")
-- existing target membership / insertion remains here
player:setSkillInstanceStateStringList(
    "s4_cloud_yongqian", ctx.instanceID, "target", targets)
```

Yongyi formerly constructed a SkillInstanceKey, traversed
`getChildSkillInstanceKeys`, compared the child name, constructed a
SkillInstanceRef and called `setSkillInstanceCorrectState`. The same update is:

```lua
room:setChildSkillInstanceCorrectState(player, "s4_cloud_yongyi", instance_id,
    "#s4_cloud_yongyiAttackRange", "count", sgs.QVariant(#records))
```

Banjiang's duplicated child search now has just the existing positive/zero
branch around the set/remove child operations. Yingzi's four remove calls become:

```lua
player:removeSkillInstanceStateKeys("s4_cloud_yingzi", instance_id,
    {"x", "hand", "hp", "equip"})
```

The four representative skills change 17 added/61 removed lines; no skill
effects, marks, expiry callbacks or full-package state refactor are included.

## Focused validation

Configure with `QSAN_TEST_EXTERNAL_AGENTS=ON` and point
`QSAN_EXTENSIONS_SOURCE_DIR` at the companion checkout. Build
`qsanguosha_skill_state_tests` and run `ctest -R '^skill_state_convenience$'`.
The shared native-role fixture supplies the real Lua/SWIG runtime; this test
does not play a game, use an external API or run a 50-player benchmark.

Coverage: typed empty/wrong-type defaults, list delimiters/Unicode, strict Lua
array rejection without mutation, false/zero selective cleanup, exact parent
and owner isolation, correction definition rejection, existing recipient gates,
external-agent private-state exclusion, concealed Hegemony helper exclusion,
Room detach cleanup and fresh instance isolation. Parse the migrated Scarlet
with the engine's Lua parser as an additional syntax check.
