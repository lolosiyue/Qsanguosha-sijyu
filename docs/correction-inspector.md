# 技能修正檢查器 / Read-only correction inspector

Open **Options → 技能修正检查器（只读）** or **Ctrl+Shift+J** after starting or
joining a game. No host, player-role or cheat-mode permission is required.
When several sources are available, pick a server room or client calculations.
Remote GUI clients can inspect their own calculations; TUI and dedicated servers
have no inspector window.

1. Check **记录实际计算** (off by default) and reproduce the action in game.
2. Uncheck it to freeze the records, then filter by source, target, calculation
   or a card/skill/context substring.
3. Select a record to see its steps, per-skill returns and child calculations.
   Children without a target/card argument appear only with filters set to All.

Closing the window pauses capture; Clear deletes the records. History stays with
the session until it ends. Changing cheat mode does not affect capture or history.
The tool has no editing or execution controls.

## Coverage

| View | Recorded steps |
| --- | --- |
| Distance | `Player::distanceTo`: fixed skill override, fixed-distance map, seat distance, additive correction, distance fix, minimum-one clamp; null/self and negative-seat exits. |
| Attack range | Base 1, fixed maximum, physical/virtual weapon ranges, include-weapon switch, additive correction, minimum-zero clamp; InfinityAttackRange exit. |
| Hand limit | Fixed maximum or HP base, odd dual-general HP compensation, additive correction, clamp; MaxCardsCount mode. |
| Distance / range / hand-limit correction | Engine subtotal. Fixed passes take the maximum from the engine sentinel (distance 0, range/hand -1); additive passes start at 0. |
| Target modifier | Residue, distance-limit and extra-target subtotals from 0. V1 residue -1 counts as 1000; aggregate >500 short-circuits. |
| Unlimited residue | `hasResidueUnlimited`, stopping at the first unlimited hit. |
| Prohibition | V1 ProhibitSkill and per-instance ProhibitSkillV2 boolean returns, the first prohibiting skill, source-less skip, null-card and `cardIgnoreLegality` exits. |
| Can Slash | `canSlash` gates: missing/dead target, MethodUse limit, prohibition, attack-range pair, distance modifier, self-target gate. |

V2 rows show holder, instance ID, currentAmount, fixed/additive pass, returned
value and the applies/unlimited/explicitSigned flags; `noEffect` means
applies=false and system-selector rows show `system`. V1 callbacks receive no
holder, so V1 rows show the owner as unknown.

Skipped rows record a system gate rejection, no living holder candidate, a holder
without instances, a missing instance, a failed effect-availability/preview gate,
equipment consumed as subcard, and card pattern mismatch. Skills after a short
circuit are not evaluated and have no row.

## Scope

- Only calculations made on the selected side while capture is on are shown;
  filtering and refreshing never rerun Lua callbacks. Client records reflect the
  state visible to that client and are not authoritative server calculations;
  their revision is 0 because no server revision is attached. Local server rooms
  retain their complete server-side calculation source.
- Use-count and extra-target values are modifier subtotals. Card base limits, use
  history, `targetFilter`/`targetsFeasible`/`isAvailable`, ViewAs/ActiveSkill
  rules, CardLimitSkill internals (Can Slash records only the MethodUse boolean),
  Pindian prohibitions, invalidity internals and client-side target rejection are
  not traced.
- Records include AI probes and previews; revision and context show where a call
  ran. Memo hits show only the cached result.
- Records keep values, not the selected-target list, subcard IDs, hand contents
  or the Lua stack. They stay in the server's RoomRuntime or the local Client and
  never enter
  packets, replays or saves.
- Caps: 128 records, 256 rows per record, 4096 rows in total, 256 characters per
  text field. A full record evicts no-op/skip rows before contributing ones;
  omitted rows are counted.
- With capture off, each scope costs one atomic load. Capture does not change
  callback order, RNG or arithmetic.

## Translation

Simplified Chinese lives in `builds/sanguosha.ts` under the `CorrectionInspector`,
`CorrectionTrace` and `MainWindow` contexts. Recorded category and operation keys
stay English and are translated only for display; object names, instance IDs,
V1/V2 labels and enum values are not translated.
