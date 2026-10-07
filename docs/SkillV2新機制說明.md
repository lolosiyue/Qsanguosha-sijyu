# Skill V2 新機制說明

本文件彙整新版技能（TriggerSkillV2／ViewAsSkillV2 系列）撰寫時必須使用或了解的引擎機制：
Resolution History 查詢、LimitScope 使用次數、新版 CardLimitation、Skill Instance State。
主體 API 與遷移流程仍以 [TriggerSkillV2 系統說明](TriggerSkillV2系統說明.md) 與
[ViewAsSkillV2 遷移規範](active-skill-v2-migration-guide.md) 為準；本文件只補齊「新版技能要接哪幾套機制」。

## 機制總覽

| 機制 | 用途 | C++ 層 | Lua 層 | 詳細文件 |
|------|------|--------|--------|----------|
| Resolution History | 查「已發生的事」：用牌、移牌、傷害 | `Room::historyScopes` 等 + `ResolutionHistoryService` | `room:queryHistory*`（純 table） | [resolution-history.md](resolution-history.md) |
| LimitScope | 每回合／階段／輪次／全局使用次數 | `Skill::getLimitScope`、`isUsable/addUsage/resetUsage` | `limit_scope`、`max_usage_limit`、`check_custom_usage`、`on_add_usage` | 本文件 §3 |
| CardLimitation | 限制玩家對牌的操作（用／應／棄／移動／獲得） | `CardLimitSkill`、`Player::setCardLimitation` | `sgs.CreateCardLimitSkill`、`room:setPlayerCardLimitation` | [CardLimitation 系統說明](CardLimitation系統說明.md) |
| Skill Instance State | 每個技能實例的私有持久狀態 | `Player::*SkillInstanceState*` | `ServerPlayer:setSkillInstanceStateValue` 等 | 本文件 §5 |
| CorrectSkillV2 | 距離／手牌上限／目標修正／攻擊範圍 | `DistanceSkillV2` 等 + `CorrectSkillContext` | `sgs.CreateDistanceSkillV2` 等 | [CorrectSkillV2 指南](CorrectSkillV2功能與開發指南.md)、§6 |
| AmountSkillV2 | 實例數值（摸牌數、修正量） | `getEffectiveAmount`、amount override | `base_amount`、`ctx.amount` | §7 |
| 宣告系統 | 虛張聲勢式宣言＋同一規則供 UI／server | `declaresCardName`、`declarationCandidates`、`SkillDeclarationSession` | `guhuo_type`/`juguan_type`/`tiansuan_type` | [view-as-skill-v2-guhuo](view-as-skill-v2-guhuo.md)、§8 |
| ctx 暫存欄位 | 結算中途替換牌／目標、攔截傳值 | `updated_card`、`updated_targets`、`interceptor_data` | 同名 SWIG 欄位 | §12 |
| 來源引用 | 精確實例身分＋借用技能 | `sourceRef`／`activationRef`、`BorrowedSkillScope` | `@@` askForUseCard | §9 |
| 進階掛鉤 | 時機鉤子、自訂分派、回合中斷清理 | `willInvoke` 等、`collectTriggerContexts` | `on_turn_broken` 等 | §10 |
| 實例描述投影 | 介面即時顯示用量／效果 | `setSkillDescriptionState` | — | §11 |

## 落地時間（git 記錄）

依提交日期排序；「多數技能還沒用上」主要是最近兩週落地的機制：

| 日期 | 提交 | 機制 |
|------|------|------|
| 2026-05-18 | `6e3a8843` | TriggerV2 cost/pay 分離、amount 系統、`skillEffect` |
| 2026-05-19 | `44080fa1`、`87600db7` | CardLimitation 加 `MethodMove`＋reason；Lua V2 綁定＋usage limit |
| 2026-05-30 | `78ae13cd` | LimitScope 加入階段指定（`phase_name`） |
| 2026-07-21 | `709a2f92` | ActiveSkill V2：`SkillInstanceRef`、GetUsage、oracle text、SWIG |
| 2026-07-24 | `3240e467` | CorrectSkillV2 全家（engine/player/room/client/SWIG） |
| 2026-08-03 | `305a8bd0` | CardLimitation `MethodGet`＋`canGet`＋逐區域移動限制 |
| 2026-08-09 | `f90be5b2` | SkillInstanceState 僅持有者同步；create 前攔 usage limit |
| 2026-09-11 | `19f7418e` | EquipsNullified `|target:` 來源條件＋短 pattern 補位 |
| 2026-09-15 | PR #39 | Shiming 多實例（SI） |
| 2026-09-20 | `f5934f33`、`ee11c718` | Resolution History journal＋Lua 查詢；實例化介面描述 |
| 2026-09-21 | `8471f538` | history 補 `extra_turn` 事件生命週期 |
| 2026-09-25 | `bd92394d`、`53767ade`、`8f6b160c` | guhuo 宣告內建；借用 V2 view-as＋依 owner 排序；view-as 依牌類記帳 |
| 工作樹未提交 | — | `CreateEquipSkillV2`／`CreateRuleSkillV2`（Lua） |

## 1. 技能家族與工廠

新版技能一律走 V2 生命週期（`can_trigger` → `on_cost` → `on_pay` → `on_effect`／`on_effect_target`），
不再實作 legacy `on_trigger`：

| 類別 | Lua 工廠 | 用途 |
|------|----------|------|
| `TriggerSkillV2` | `sgs.CreateTriggerSkillV2(spec)` | 一般觸發技（含多實例） |
| `ViewAsSkillV2` | `sgs.CreateViewAsSkillV2(spec)` | 視為技／主動技，走 `ActiveSkillRequest` |
| `EquipSkillV2` | `sgs.CreateEquipSkillV2(spec)` | 裝備觸發技；准入由 `equipment`／`equipment_type` 判定，含虛擬裝備來源 |
| `LuaTriggerSkillV2`（rule） | `sgs.CreateRuleSkillV2(spec)` | 場景／全局規則，無玩家實例，`can_trigger` 選決策者而非持有者 |

`CreateEquipSkillV2` 的 spec：`equipment`、`equipment_type`（weapon/armor/treasure/offensive_horse/defensive_horse）、
`events`、`base_amount`、`view_as_skill`（必須與裝備技能同名）、`movement_source`，回調同 TriggerSkillV2 生命周期。
`CreateRuleSkillV2` 預設 global；給 `scenario` 時改經 `scenario:setRule` 註冊。

C++ 端對應覆寫 `cost/pay/effect/effectTarget` 等虛方法；`EquipSkillV2::usesEventSource` 保證
「裝備離開持有者裝備區」事件才可作為移牌觸發來源，Lua 版本僅接受實體裝備離槽，無法偽造虛擬授予。

## 2. Resolution History：事實查詢

Room 持有 server authoritative 的 `ResolutionHistoryService`（詳細契約見
[resolution-history.md](resolution-history.md)）。新版技能**不自己監聽設 mark 來記「有沒有發生過」**，
改查這份 journal：

| 問題 | API | 備註 |
|------|-----|------|
| 目前 scope | `room:historyScopes()` → `{round_id, turn_id, phase_id}` | `"0"` 表示沒有該 scope |
| 本回合用過幾張牌 | `Room::queryCardHistory`／`countHistoryCards`（C++） | `scope` 限 `turn`/`phase`/`round`/`game`；不完整回 `-1` |
| 本回合造成過傷害 | `room:queryActualDamage{turn_id=..., from=...}` | `amount = absorbed + hp_loss` |
| 移牌軌跡 | `room:queryHistoryMoves{...}` | 查後須以 `room:getCardPlace` 複核現況 |
| 通用事件／事實 | `room:queryHistoryEvents/Facts{...}` | filter 白名單 + `limit`/`after`/`watermark` 分頁 |

**契約重點**（Lua／C++ 都適用）：

- 所有歷史查詢都須檢查 `error` 與 `complete`；查詢不完整是「未知」，不是「沒有發生」。純角色、數值或移牌等公開事實不要求技能來源歸因完整；只有按 `source`／`activation`／`executing skill` 判定的查詢才另外要求 `attribution_complete`。不得因普通牌沒有技能歸因，拒絕完整的公開歷史。
- `turn_id` 指**正在結算的回合**（含插入的額外回合），不是該角色最近一次自己的回合。
- 分頁固定 `watermark`，用 `next_after` 續頁；`limit` 只限制頁大小，不影響儲存量。
- `historyParent(id, kind)` 只查祖先鏈；scope 不要從 parent chain 推測（round 可建立於 turn 內部）。

### ViewAsSkillV2 的 `history_key`

普通視為技在歷史中依**產生牌的 card class**記帳，而不是技能名：

- C++：覆寫 `historyKey(const ActiveSkillRequest &)`（預設：有宣告對話時用宣告牌名的 class name）。
- Lua：`sgs.CreateViewAsSkillV2{ history_key = "Slash" }`；宣告式（`guhuo_type`）技能預設已按宣告名歸類，通常不用設。
- 接受用牌／回應時，把已解析的 key 保存為不可變 fact 的 `history_key`；普通轉換牌維持實際 card class，SkillCard 才套用自訂 key。
- 查詢端由 `queryCardHistory(player, scope, className)` 的 `className` 同時匹配歷史 card classes 或 `history_key`（如 `MTYinglveCard`、`ZhibaCard`），同一筆只計一次。明確名稱可查 SkillCard；空名稱／`"."` 仍只查普通牌。
- 舊 fact 缺 key 且無法由 class 確認匹配時，名稱查詢不完整；`countHistoryCards` 回 `-1`，不得當成零次。
- `TargetModSkillQueryScope` 以本次 `use_card` event ID 精確排除自身的投影項目；尚未記錄、純回應、其他玩家與巢狀用牌都不誤扣，原始 facts 不變，技能不用自行減一。

## 3. LimitScope：使用次數

定義於 `Skill::LimitScope`（`src/core/skill.h`），計數走 **player mark**（不進 history journal）：

| LimitScope | mark 後綴 | 重置時機 | Lua 常數 |
|------------|-----------|----------|----------|
| `Limit_None` | 無 | 不限次 | `sgs.Skill_Limit_None` |
| `Limit_Turn` | `-Clear` | 每回合結束 | `sgs.Skill_Limit_Turn` |
| `Limit_Phase` | `-PhaseClear` 或 `-<phase_name>Clear` | 該階段結束（`phase_name` 指定時只清該階段） | `sgs.Skill_Limit_Phase` |
| `Limit_Round` | `_lun` | 每輪結束 | `sgs.Skill_Limit_Round` |
| `Limit_Game` | `_game` | 不重置 | `sgs.Skill_Limit_Game` |
| `Limit_Custom` | 自定 | 自定 | `sgs.Skill_Limit_Custom` |

計數 holder 由 `getUsageRef(ctx)` 依 `activationRef`（不可變 provenance）解析，
legacy ctx 只允許 owner/invoker＋`instanceID` 回退。Lua spec 對應：

```lua
sgs.CreateTriggerSkillV2{
  name = "my_skill",
  events = sgs.Damage,
  limit_scope = sgs.Skill_Limit_Turn, -- 或 Phase/Round/Game/Custom
  phase_name = "Play",                -- 僅 Limit_Phase 且要指定階段時
  max_usage_limit = 2,                -- 預設 1
  -- Limit_Custom 時改用：
  -- check_custom_usage = function(skill, ctx) ... return bool end,
  -- on_add_usage = function(skill, ctx) ... end, -- 結算成功後更新自訂狀態
  can_trigger = function(skill, event, room, player, data) ... end,
  on_effect = function(skill, event, room, player, ctx) ... return false end,
}
```

- `check_custom_usage` 拋錯＝拒絕發動；`on_add_usage` 錯誤只中止該 callback，不回滾已結算效果。
- `resetUsage(ctx)` 依 `getUsageRef(ctx)`（預設為 `activationRef`）精確清該實例該 scope 的計數；需要「重置某技能本回合次數」時用它，不手清 mark。
- `get_usage_ref(skill, ctx)` spec 回調可覆寫 usage ref（`usage_identity` 已移除）。
- **判斷用哪套**：配額限制用 LimitScope；「本回合造成過傷害才可發動」這類事實條件用 Resolution History，不要互替。

## 4. CardLimitation（新版）

兩層結構，詳見 [CardLimitation 系統說明](CardLimitation系統說明.md)；新版相比舊版補齊了
`MethodMove`／`MethodGet`／`MethodEffect`、`reason` 來源追蹤與介面描述：

### 4.1 靜態規則：`CardLimitSkill`

全局註冊的限制規則技能（Compulsory），由 `Engine::isCardLimited` 統一守門：

| 虛方法 | 回傳 |
|--------|------|
| `limitList(target[, card])` | 逗號分隔的 HandlingMethod 字串（`use,response,discard,move,get,...`） |
| `limitPattern(target[, card])` | ExpPattern（`Slash`、`BasicCard`、`.|heart`、`1`、`.`） |
| `limitReason(target[, card])` | reason 字串，供介面顯示與按原因移除 |

Lua：`sgs.CreateCardLimitSkill{ name=..., limit_list=fn, limit_pattern=fn, limit_reason=fn }`。

### 4.2 執行期限制：`Player::setCardLimitation`

```cpp
// C++
player->setCardLimitation("use,response", "Slash", "skill_x", true);  // 單回合
player->removeCardLimitation("use", "Slash", "skill_x");
player->removeCardLimitationByReason("skill_x");
player->clearCardLimitation(true);   // 只清 $1
```

```lua
-- Lua（Room 封裝，走 notify 同步）
room:setPlayerCardLimitation(player, "use", ".", true, "skill_x")
room:removePlayerCardLimitationByReason(player, "skill_x")
room:clearPlayerCardLimitation(player, false)
```

- `single_turn=true` 在 pattern 尾加 `$1`（回合末清）；`$0` 為持久。
- `reason` 是技能作者的來源標識：介面用翻譯鍵 `@<reason>.effect` 顯示自訂文案（缺翻譯時 fallback 顯示 pattern），
  `getCardLimitationDetails()` 把 method/pattern/reason/single_turn 投影給 UI／TUI。
- 裝備無效化走 `MethodEffect`：`addEquipsNullified(pattern, reason, single_turn)`，
  pattern 支援 `|target:<objectName>` 指定來源玩家。

### 4.3 選型

| 需求 | 用哪個 |
|------|--------|
| 「某技能在場時大家都不能用殺」類全局規則 | `CardLimitSkill` |
| 技能效果給某人下「本回合不能棄牌」等暫時限制 | `setPlayerCardLimitation`（帶 reason，便於撤除與顯示） |
| 讓某張裝備對某來源無效 | `addEquipsNullified` + `|target:` pattern |

## 5. Skill Instance State：實例私有狀態

每個 `(skillName, instanceID)` 實例可帶一份 `QVariantMap` state：

- **權威在 server**；只經 `Room::notifySkillInstanceState` 同步給**持有者自己的 client**，其他玩家不可見（比 `mark` 更適合私隱資訊）。
- 實例移除時 state 隨之銷毀——`EventLoseSkill` 時已拿不到，需清理的東西要在更早的事件先留可序列化身分（objectName／card string／event id），**不得存 Card\*、Player\*、QVariant 事件物件或 userdata**。
- 只能放可序列化 primitive（string/number/bool/list/map）；快照與 replay 沿用同一份值。

| API（C++，`Player`） | Lua（`ServerPlayer`） |
|----------------------|------------------------|
| `setSkillInstanceState(name, id, map)` | —（整表寫入以逐 key 版本代替） |
| `getSkillInstanceState(name, id)` | — |
| `setSkillInstanceStateValue(name, id, key, v)` | `setSkillInstanceStateValue` |
| `getSkillInstanceStateValue(name, id, key, def)` | `getSkillInstanceStateValue` |
| `removeSkillInstanceState(Value)` | `removeSkillInstanceStateValue` |

Lua 的字串清單讀寫、指定鍵清理及子修正技同步，另見
[共用 state 便利接口](lua-skill-state-conveniences.md)。這些接口沿用現有私有 state／
correctState 投影，不新增公開資料或自動到期規則。

介面投影：state 的 key 可用翻譯 `<key>.type = player|players` 把 objectName 顯示為玩家名、
`<key>.value.<v>` 顯示自訂文案（`player.cpp` 的 descriptionStateValue／readableState）。

**用途定位**：跨 callback／跨事件的技能暫存（例：已問過的選項、待處理的 card identity）。
計數用 LimitScope，事實查 history，UI 通用標記用 mark——state 是「這個實例自己知道、要撐到下次回調」的東西。

## 6. CorrectSkillV2：修正型被動

四個 V2 修正家族共用 `CorrectSkillContext`／`CorrectSkillResult`，詳見
[CorrectSkillV2 功能與開發指南](CorrectSkillV2功能與開發指南.md)：

| 類別 | Lua 工廠 | 修正對象 |
|------|----------|----------|
| `DistanceSkillV2` | `sgs.CreateDistanceSkillV2` | 距離（to/from） |
| `MaxCardsSkillV2` | `sgs.CreateMaxCardsSkillV2` | 手牌上限 |
| `TargetModSkillV2` | `sgs.CreateTargetModSkillV2` | 用牌目標數／距離不限／使用次數 |
| `AttackRangeSkillV2` | `sgs.CreateAttackRangeSkillV2` | 攻擊範圍 |

spec 共同鍵：`base_amount`（預設 1）、`holder_selector`
（`sgs.CorrectSkill_Primary`／`Secondary`／`Participants`／`AllHolders`／`System`）、
Lua `correct_func(skill, ctx)` 回 `nil`／`false`（無效果）、`true`（使用 currentAmount）或 number（指定數值）、`fixed_func`（固定值）。

- C++ 的 `CorrectSkillResult`（不是 Lua 回傳型別）：`noEffect()`／`useAmount(n)`／`unlimitedResidue()`（目標修正的「不限次」）。
- `ctx` 帶 `instanceRef`、`holder`／`primary`／`secondary`、`card`、`modType`、
  `maxCardsType`（`MaxCardsType::Max`/`Normal`/`Min`）、`includeWeapon`、`currentAmount`，
  以及 `getStateValue(key)`——讀**持有者實例**的 `correctState`，跨實例修正不必另建查表。
- `TargetModSkillV2` 另接 `pattern`（預設 `"Slash"`），在 `planTargetModSkillReveal` 前保留選擇順序，
  查詢時 `TargetModSkillQueryScope` 以 event ID 排除本次用牌；不論舊 Player counter 是否增加，journal 投影都不重算本次，技能不自行補償。

## 7. AmountSkillV2：實例數值（base_amount）

`TriggerSkillV2`／`ViewAsSkillV2`／CorrectSkillV2 家族都實作 `AmountSkillV2`。
「摸幾張／補幾點／加幾目標」這類數值不要刻死常數，走四層 amount：

| 層 | 設定方式 | 生命期 |
|----|----------|--------|
| 定義預設 | spec `base_amount`／`setBaseAmount`（各類預設 1） | 技能定義 |
| 實例覆寫 | `room:setSkillInstanceAmount(source, ref, amount, reason)`、`room:addSkillInstanceAmount(source, ref, delta, reason)`、`room:resetSkillInstanceAmount(source, ref, reason)` | 跟實例；owner 同步＋描述投影 |
| 單次結算 | `ctx.setModifiedAmount(n)`／`clearModifiedAmount()` | 本次結算，結束自動清 |
| 讀取 | `skill:getEffectiveAmount(ctx)` | modified 優先，否則 `ctx.amount`，一律 clamp ≥0 |

流程：框架組 `SkillContext` 時把 `ctx.amount` 填成 `room:getSkillInstanceAmount(activationRef)`
（實例有 override 用 override，否則 `getBaseAmount()`）；callback 裡只讀
`getEffectiveAmount(ctx)`，不直接讀 `ctx.amount`，就不會漏掉實例覆寫與單次修正。

```lua
-- Lua 慣例（scarlet s4_chiyuan 同款）：摸牌數可被實例調整
sgs.CreateTriggerSkillV2{
  name = "my_draw",
  events = sgs.EventPhaseStart,
  base_amount = 1,
  on_effect = function(skill, event, room, player, ctx)
    local n = skill:getEffectiveAmount(ctx)
    if n <= 0 then return false end -- 覆寫成 0 = 本次不摸，不是畫面 bug
    player:drawCards(n, "my_draw")
    return false
  end,
}
```

```cpp
// C++ 慣例（mountain.cpp 全包）：倍率、迴圈上限、回復量都吃 effective amount
player->drawCards(getEffectiveAmount(ctx), objectName());
target->drawCards(2 * getEffectiveAmount(ctx), objectName());
for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) { ... }
room->changeMaxHpForAwakenSkill(player, -getEffectiveAmount(ctx), objectName());
```

- 「某實例永久改為 2」（限定技、強化）→ `room:setSkillInstanceAmount`；想記在介面顯示
  `base → override`，順手帶 `reason`。
- 覆寫掛哪個實例由 `getAmountRef(ctx)` 決定（`ViewAsSkillV2` 預設 `activationRef`）。
- `ctx.amount` 也可手改，但那是本次結算的事前注入值；要「結算中間才決定多少」用
  `setModifiedAmount`，生效優先序高且不用擔心還原。

## 8. 宣告系統（guhuo dialog）

`ViewAsSkillV2` 內建「宣言牌名」流程：spec 設 `guhuo_type`（或 owner 上的對話）後，
**同一套規則**同時供給 client 對話、預設 `createCard()`、歧義回應的 `cost()` 與 `historyKey()`：

- `usableNames(request)` 給介面列可宣言名；`canDeclare(request, name)` 依序檢查
  對話過濾／禁表／`allowDeclaration`／鎖定／pattern。
- `declaresByDialog(reason)` 為 false 的回應直接取用 pattern 牌名，不彈對話。
- 非對話管道送來的宣言（AI 指名）若通過 `canDeclare` 才會成牌——宣言外名稱不會變牌。
- 介面／AI 用 `declarationCandidates` 與 `declarationReason` 取得同一組候選與禁用理由，
  技能作者不再各端複寫一份名單。細節見 [view-as-skill-v2-guhuo.md](view-as-skill-v2-guhuo.md)。

### 另外兩種宣言對話

`SkillDialogInfo` 目前有三型，底層共用 `SkillDeclarationSession`（`src/core/skill-declaration.h`）：

| 對話 | spec 鍵 | 參數 | 用途 |
|------|---------|------|------|
| guhuo | `guhuo_type` | left/right/playOnly/slashCombined/delayedTricks/refresh | 宣言基本／錦囊牌名 |
| juguan | `juguan_type` | `cardNames` 指定可選牌名單 | 舉薦式：只給一份固定名單 |
| tiansuan | `tiansuan_type` | `choices` 自訂選項 | 天算式：宣言的是非牌的選項值 |

三者提交後都落在 `SkillDeclarationSession::tagKeyFor(info, skillName)` 對應的 Self tag；
`activeFor(info, reason)` 可先問「此 reason 下這個對話會不會宣言」不必建候選。
juguan／tiansuan 目前使用率遠低於 guhuo，但機制已完整，新技能直接設 spec 鍵即可。

## 9. 來源引用與借用技能

V2 的身分不靠「技能名＋玩家名」字串，靠 `SkillInstanceRef`（`ownerObjectName`＋`skillName`＋`instanceID`）：

| 欄位 | 意義 |
|------|------|
| `ctx.sourceRef` | 本次效果的精確根來源（不可變 provenance）；借用或已生效授技可能與執行實例不同 |
| `ctx.activationRef` | 本次實際啟動的精確技能實例；預設配額與私有狀態歸此實例，不能改存根來源 |

- **借用視為技**：`Room::BorrowedSkillScope` 包住 `room:askForUseCard("@@" .. skillName)`，
  讓他人的 V2 view-as 以原持有者身分合法使用；`borrowedActivationMarkName(skill)` 是協調用標記。
- 配額用 `skill:getUsageRef(ctx)`；私有狀態用實際持有實例；歷史查詢則按規則分別比對根來源、執行技能與 activation。三者不能只以相同技能名互相代替。
- legacy ctx 只有 owner/invoker＋`instanceID`，沒有 ref；V2 路徑不要手刻 `objectName + instanceID`。

## 10. 進階觸發掛鉤

大多數技能用不到，但寫 rule／特殊分派時直接用引擎掛鉤，不要繞路：

| 掛鉤 | 時機／用途 |
|------|-----------|
| `willInvoke` → `invoking` → `effect` → `effectFinished` | 觸發確認後的時機鉤子（含 `targetConfirming`） |
| `collectTriggerContexts` | 自訂觸發收集（rule 技能用它選決策者，不走 owner 索引） |
| `recordEvent` | rule 技能的記錄階段入口 |
| `usesEventPriority`／`triggerOrderPlayer` | rule 用事件優先序、指定觸發排序玩家 |
| `acceptsRemovalEvent` | 訂閱精確移除事件，不復活已退役實例 |
| `on_turn_broken`（Lua） | `TurnBroken`/`StageChange` unwind 時清理自訂狀態 |

## 11. 實例描述投影與輔助技能

- **`setSkillDescriptionState(usage, validity, effects)`**（server 端，`roomthread` 髒標記驅動）：
  技能的「用量／合法狀態／效果」即時投影到介面說明。`effects` 條目目前支援
  `invalidity`（技能失效）、`card_limit`（§4 的執行期限制）與自訂 `text`；
  state／effect 值經 `<key>.type`、`@<reason>.effect` 翻譯鍵轉顯示。
- **`InvaliditySkill`**（`sgs.CreateInvaliditySkill{ skill_valid = fn }`）：宣告「條件成立時某技能失效」，
  `Engine` 統一檢查，不要在自己技能裡攔對方技能。
- **`DetachEffectSkill`**（`sgs.CreateDetachEffectSkill`）：註冊隱藏 `EventLoseSkill` 觸發，
  技能被移除時自動清 `pilname` 牌堆或跑 `on_skill_detached`——配套 §5 state 的銷毀時機。
- **`MarkAssignSkill`**：GameStart 時直接發 mark（`MarkAssignSkill("mark", n)`），不用刻一個開局觸發。
- **Shiming 實例**：`shiming_skill = true` + `on_shiming_success`/`on_shiming_fail`（Lua），
  每實例各自記成功／失敗；見 [shiming-skill-instances.md](shiming-skill-instances.md)。
- **`hasIndependentAIConversion`**（ViewAsSkillV2）：opt-in AI 契約——恰好 `getN()` 張互不相關手牌、
  每次選擇產生同一牌面與目標規則；回 true 前須有契約測試，AI 據此直接決策不試誤。
- **常用 spec 旗標**（CreateTriggerSkillV2/ViewAsSkillV2 通用）：`expand_pile`（素材取自具名牌堆）、
  `relate_to_place`（技能依位置區分）、`waked_skills`（覺醒後獲得技）、`limit_mark`、`dynamic_frequency`、
  `club_name`／`getSources`（技能出處與皮膚對應）。
- **診斷**：`SkillExecutionAudit`（room.cpp，env-gated）會把每次技能結算的 sourceRef/activationRef/
  interceptor 鍵、cancel/bypassCost 打成 debug 輸出；查歸因問題時開它比加 log 快。
- **legacy 包裝類**：`ProhibitSkill`／`ProhibitPindianSkill`（禁制規則）、`RetrialSkill`（改判）、
  `PhaseChangeSkill`（跳階段）、`MasochismSkill`（賣血）、`DrawCardsSkill`、`GameStartSkill`、`FilterSkill`（選牌過濾）、
  `ViewAsEquipSkill`（視為裝備）仍是可用捷徑，但它們是 TriggerSkill/Skill 時代封裝；
  新寫技能一律優先 V2 事件模型，這些只在沿襲舊式語意時用。
- 其他既有系統：[Aura 光環](Aura光環系統說明.md)、[Oracle text](oracle-text-system.md)、
  [Pre-selection meta](preselection-meta-skill.md)（選將階段鉤子）、`AnytimeSkill`（[anytime-skill.md](anytime-skill.md)）、
  `BattleArraySkill`／`ArraySummonSkill`（國戰陣法）。

## 12. SkillContext 暫存與攔截欄位

`SkillContext` 除了身分欄位（§9），還有一組給結算中途用的欄位，多數技能沒用到：

| 欄位 | 用途 |
|------|------|
| `updated_card` | cost／pay 階段替換產生的牌（如歧義選定後重建），activation/source provenance 沿用；純 RESPONSE 也接納付款後換牌，並在提交次數前重驗回應 pattern 與限制。私有 `@` prompt 保留技能入口判定 |
| `updated_targets` | 觸發確認階段覆寫 `targets`（roomthread 在 skillEffect 前取回） |
| `interceptor_data` | `QMap<QString, QVariantMap>`：攔截／借用技能之間傳值（例 `heg_huashen`），會列進 `SkillExecutionAudit` 的 `interceptors` 鍵 |
| `preferredTarget`／`preferredTargetSeat` | 提示 AI／介面的建議目標 |
| `multiplier` | 觸發倍率（trigger-order 選單可疊加） |
| `trigger_count` | 本次事件第幾次觸發 |
| `is_forced`／`bypass_cost`／`manual_effect` | 強制發動、跳過 cost、手動控制效果分支 |
| `extra_data`／`choice` | 跨 callback 傳值（choice 記玩家選項） |

規則：暫存到下一個回調的東西先想能不能放這裡或實例 state（§5），不要落 player tag。

## 13. askForChoice：選項、禁用項與 tip

完整簽名（`room.h:664`，Lua 同序）：

```cpp
QString askForChoice(player, skill_name, choices,
                     data = QVariant(), except_choices = "", tip = "");
// Lua: room:askForChoice(player, "name", "a+b", data, "c+d", "tip")
```

| 參數 | 內容 |
|------|------|
| `choices` | `+` 連接的可選項；**不含 `+`（單一選項）時不發問，直接回傳該值** |
| `data` | 伴隨 QVariant，AI 的 `decideAiChoice` 可讀 |
| `except_choices` | `+` 連接的禁用項：顯示成灰色不可點按鈕；typed model 記 `enabled=false`，誤答以 `DisabledOption` 拒絕 |
| `tip` | 對話提示文字（見下） |
| 回傳 | 選項字串（**含 `=` 部分原文回傳**），玩家 Esc／取消時回 `"cancel"` |

### `=` 攜帶中繼資料（含目標角色）

每個選項可寫成 `value=src=arg=arg2`：

- 按鈕 objectName＝完整字串，伺服器收到的回覆也含 `=`，技能端自行解析：
  `choice.split("=").first()`、`choice.section('=', 1, 1)`、`choice.section('=', -1)`（取首段／指定段／末段，倉內都有先例）。
- 介面標籤：查 `translate("skill:value")`，落空查 `translate(value)`；然後把譯文裡的
  `%src`／`%arg`／`%arg2` 換成 `getPlayerName(對應欄位)`——`sgs\d+` 解析成玩家名，
  其餘當翻譯鍵走（牌名、階段名、數字都過）。
- 所以「選項帶上目標角色」：`"give=sgs3"`，配翻譯 `["myskill:give"] = "交給%src"` 即顯示目標名字。
- tooltip：`value` 是技能名 → oracle＋描述；否則查 `":skill:value"`／`":value"` 翻譯鍵。

### 禁用項（except_choices）

- 與可選項走同一個 `createOptionBox(..., enabled=false)`：**禁用項也能帶 `=` 欄位**。
- 現有慣例是把資訊塞進禁用項當不可點的「資訊列」：xiaowu 用 `tips << "tip=" + src + "=" + a + "=" + b`
  傳禁用項，`"skill:tip"` 翻譯值帶 `%src/%arg/%arg2` 佔位即渲染成說明文字。
- 桌面端禁用項渲染在獨立 group box；若某值同時出現在 choices 和 except_choices，會出現
  一個可點＋一個灰色副本（sidi 就是這種「全列出、已用的灰掉」用法）。typed model 則是就地
  標 `enabled=false` 不重複。

### tip

- 桌面對話框：查 `translate("skill:tip")` → 落空查 `translate(tip)`，**純翻譯、不拆 `:`/`=`**。
  故 tip 要帶玩家名，可靠做法是把玩家名放選項 `=` 欄位（各端都解析），或讓翻譯值本身處理。
- `key:src:dest:arg:arg2` 這套 `:`-prompt 慣例（`formatClientPromptList`：`%src`/`%dest` 走玩家名
  解析、`%arg*` 走翻譯）是 `setPromptList`/`prompt_doc`、TUI 與 typed payload 前端用的；
  `@key::sgsN` 的 `::` 表示「%src 留空、sgsN 佔 %dest 位」。hegemony 包大量把它寫進 tip 參數，
  桌面端會原樣顯示——新技能不要複製這個寫法，tip 用翻譯鍵或把名字放 `=` 欄位。
- 特例：`tip` 為空且技能名含 `guhuo` 時，介面把上一步宣言紀錄（`guhuo_log`）塞進提示格。

### 伺服器端行為

- 發問前發 `EventAskForChoice` 攔截事件（`ChoiceData`）：其他技能可改 `choices`／
  `except_choices`／`tip`，或設 `forced_answer`／`canceled` 直接截答——要覆寫別人的選項對話走這裡。
- 全程包在 `CardLifetimeScope`；AI 經 `room:decideAiChoice(skill_name, choices, data)`；
  測試可用 `findTestOverride(player, "choice", skill_name)` 覆寫答案。
- wire：`S_COMMAND_MULTIPLE_CHOICE` `[skill_name, choices, disabled_options, tip]`。

## 14. 機制選型對照

| 新版技能需要…… | 用 | 不要 |
|----------------|----|------|
| 「每回合限一次」 | `limit_scope` + `max_usage_limit` | `xxx-Clear` mark 手刻 |
| 「本回合造成過傷害／移過牌」 | Resolution History query | 自建 listener mark |
| 「宣言牌名後當該牌記帳」 | `history_key`／guhuo 宣告 | 用技能名查 `queryCardHistory` |
| 「鎖住某人不能用某牌」 | CardLimitation + reason | 自刻 flag 再攔 isAvailable |
| 「記住剛選的選項到下個事件」 | Skill Instance State | room tag 全域鍵、Pointer 暫存 |
| 「給介面看的技能效果」 | state `effects`／description 投影 | 在 state 塞格式化 HTML |
| 「距離＋1／手牌上限＋1」 | 對應的 CorrectSkillV2 家族 | TriggerSkill 改 mark 偷加距離 |
| 「摸牌數改為實例可調」 | `base_amount` + amount override／`modified_amount` | 每次結算塞 player mark |
| 「宣言後變牌」 | `guhuo_type` 宣告流程 | 自刻選項對話＋cloneCard |
| 「借別人的視為技來用」 | `BorrowedSkillScope` + `@@` 請求 | 複製一張卡直接 `useCard` |
| 「某條件下對方技能失效」 | `InvaliditySkill` | 在自己技能裡攔對方回調 |
| 「技能被移除時清牌堆」 | `DetachEffectSkill` | 另刻 EventLoseSkill 全域觸發 |
| 「宣言的是固定名單／非牌選項」 | `juguan_type`／`tiansuan_type` | 硬塞 guhuo 再過濾 |
| 「cost 後要換牌／改目標」 | `ctx.updated_card`／`updated_targets` | 動 `use_card` 或自建卡欄位 |
| 「兩個技能結算中傳值」 | `ctx.interceptor_data` | room tag 全域鍵 |
| 「跨回調暫存」 | ctx 欄位或實例 state（§5） | player tag 散落 |

| 「選項要顯示目標角色名字」 | `value=sgsN` 的 `=` 欄位＋`%src` 佔位 | tip 塞 `key::sgsN`（桌面不拆） |
| 「禁用選項／給玩家看不可選資訊」 | `except_choices`（可帶 `=`） | 塞進 choices 再靠回傳攔 |
| 「改別人的選項對話」 | `EventAskForChoice` 攔截（`ChoiceData`） | 包一層 askForChoice 代理 |

## 15. 撰寫檢查清單

- [ ] 觸發技用 `CreateTriggerSkillV2`／裝備用 `CreateEquipSkillV2`／視為技用 `CreateViewAsSkillV2`，無 `on_trigger`。
- [ ] 有配額 → `limit_scope`；自訂配額 → `Limit_Custom` + `check_custom_usage`/`on_add_usage`。
- [ ] 讀歷史 → 必查 `error`／`complete`；按 `source`／`activation`／`executing skill` 查詢才另查 `attribution_complete`。純角色／數值／移牌公開事實不受技能歸因完整度限制；真正未知時**不得**當零次放行或拒發。
- [ ] view-as 產牌要記到牌類 → `history_key`（宣告式預設已處理）。
- [ ] 執行期鎖牌 → `setPlayerCardLimitation`，帶 `reason`＋`single_turn`；裝備無效 → `addEquipsNullified`。
- [ ] 暫存值 → Skill Instance State，只放可序列化身分；`EventLoseSkill` 前完成清理。
- [ ] 修正型被動 → 用 CorrectSkillV2 家族＋Lua `correct_func` 回 nil／boolean／number（C++ 回 `CorrectSkillResult`）；讀持有者實例資料用 `ctx.getStateValue`。
- [ ] 數值 → `base_amount`＋`getEffectiveAmount(ctx)`，實例覆寫用 `setSkillInstanceAmountOverride`，單次結算用 `modified_amount`。
- [ ] 宣言變牌 → `guhuo_type`；介面／AI 靠 `declarationCandidates`，不重寫名單。
- [ ] 寫入 usage/state/history 用 `ctx.sourceRef`/`activationRef`，不刻 `objectName + instanceID` 字串。
- [ ] `askForChoice`：選項帶對象用 `value=sgsN`＋`%src` 翻譯佔位；禁用項放 `except_choices`；tip 用翻譯鍵；回傳值含 `=` 時記得 `section` 拆。
- [ ] 固定介面文案走 `tr()/qsTr()` 英文鍵＋ `builds/sanguosha.ts`，不硬編碼中文。

### 已接受效果內的短期 ViewAs 入口

`Room::AcceptedViewAsEffectScope(room, recipient, helperName, ctx)` 僅可在已接受的 `EventSkillEffect`／`EventSkillEffectTarget` 結算內建立，並檢查 `isValid()`。它建立獨立的真實 SourceAttached 入口，以原 `activationRef` 作 parent provenance，凍結 `sourceRef` 與有效 amount；不加入一般 parent removal cascade。每層 scope 有自己的 instance，`activationRef()` 及同步的 borrowed selector 限定該次提示；析構還原 selector 並移除該入口。應在 draw/recast/give 等可能觸發巢狀事件的操作前建立。

入口只向操作者同步（包括 owner-only private state）；server 依 coordinator receipt 判斷，而非同名 Effect mark、替代 grant 或將 missing source 回退為 activation。原來源消失後仍能完成已接受的提示，普通 BorrowedSkillScope／attachment 規則不變。牌、素材、目標與入口自身 quota 照常驗證。原技能的次數已由原發動支付，scope 不再代扣一次。

V2 trigger 排程對 `Compulsory`、`Wake` 及該 context 的 `is_forced` 使用既有強制候選流程；暗置來源仍沿用原公開／可發動條件，不將其他 optional context 強制化。

### 普通轉化牌的 accepted effect 與伺服器產牌入口


- `TriggerSkillV2::skillEffect` 預設只接受存活目標；復活類技能須明確覆寫 `allowsDeadTarget(ctx, target)`，且只開放其復活分支。許可不略過 `EventSkillEffectTarget`，攔截前後均重新檢查存活資格；回傳跳過或 `ctx.is_canceled` 均停止該目標效果。整體 `EventSkillEffect` 的取消亦停止效果，已付款及配額不回滾。 `ViewAsSkillV2::skillEffect` 的手動派發亦檢查回傳跳過、`is_canceled` 及攔截後目標存活；取消此目標不退還付款與配額。

- 會受 `MarkChange` 攔截的限時標記貢獻，可用原生 `Room::setPlayerMarkWithReceipt`：`canCommit(name, before, after)` 在前置攔截後檢查精確收據是否仍有效，`committed(name, before, after)` 在真正寫入後、`mark_changed`／`MarkChanged` 前記錄本次實際差值。兩個 callback 只可檢查／儲存狀態，不可觸發遊戲規則或改標記；回傳 `true` 表示本次曾提交，即使後置回呼隨即撤銷。到期只撤銷此收據的已提交貢獻，不能用請求量或整段回呼前後差冒充。

- 玩家／AI 提交的 V2 普通轉化牌（包括舊字串只有技能名、instance ID 為零者）必須經 `resolveActiveSkillRequest` 重建，再走 cost、pay、配額提交。普通牌不是免付款條件。
- 普通 conversion 的 PLAY／RESPONSE_USE，及 `askForCard` 的純 RESPONSE，在付款與 `EventSkillEffect` 攔截完成後呼叫 `ViewAsSkillV2::effect(ctx)`。預設 `ContinueEffects` 繼續普通用牌／回應；`FinishSkill` 或 `ctx.is_canceled` 停止本次牌，已付款與已提交技能配額不回滾，也不把未使用的 preview 寫成正式用牌。proxy 的既有 effect／target traversal 不變。技能在 effect 內自行用 `skillEffect(ctx, recipient)` 派發其額外效果的實際對象。
- effect 可以回傳 `ctx.updated_card`（必須是 owned virtual clone＋實材 subcards，不能直接回傳共享實體 Card）；引擎沿既有 owned-card／event-payload lease 接管，重蓋不可變 source／activation，重驗普通牌、回應 pattern、card limitation、targets，以及實材 ID 有效、唯一、非 using、所在區非 Unknown。不得重跑已完成的 pay 或用最初空選材要求拒絕效果產生的實材。技能作者仍須維護自己的精確材料收據：例如傲才僅能選本次揭示集合中的牌，跨巢狀 callback 後再確認該牌仍在預定區域；此責任不是任意 draw-pile ID 的通行權。
- 已接受的技能效果直接產生另一張普通牌，應呼叫 `room->useCardFromSkillEffect(use, ctx, add_history)`（預設 `false`，沿用原呼叫的計數策略）。此伺服器入口要求 `Effect`／`EffectTarget`、未取消、有效 frozen `sourceRef` 與原始 `activationRef`，或原生 admission 產生的有效 `physicalEquipSource`，不要求原 grant／實體裝備仍存在；保留普通牌規則、原 execution attribution，略過第二次 V2 activation／付款／live-general 門檻。它不替換原 execution backing context。
- 生成牌的私有標記只由 Room 寫入，CardUse／CardEffect 的 copy、move 保留，解析玩家／AI字串或封包時清除。`CardEffectStruct::setSkillUseContext(use)` 只複製同次用牌 metadata，供自訂普通 `Card::use` 派發使用；不得漏掉此步使子牌目標效果寫回父技能 execution。標記不序列化、不能由 client 指定，新的普通 `useCard` 呼叫也會清除它。實體牌只在 CardUse 保存 provenance，不修改實體 Card 的技能戳記。
- retained custom follow-up 若沒有 activationRef，應在接受當下保存原 activation 收據，結算時重建 local accepted context；不可假定 root 等於 activation、借用同名新 instance，或放寬入口。沒有可證原 activation 的舊收據仍不支援此入口。

### 已接受效果授予的真實限時技能

`int id = room->acquireSkillFromEffect(recipient, skillName, acceptedCtx, open, getmark, eventAndLog)`
沿用正常 acquire lifecycle（後三參預設皆 `true`），只接受未取消的 `Effect`／`EffectTarget` 及有效原 `sourceRef`、`activationRef`。它建立獨立 `SourceAcquired` grant，`bindHead=0`，不綁定施予者的 live parent；接受來源已退役仍可完成授予。原 source 與 activation 在首次通知／EventAcquireSkill 前凍結於 `frozenSourceRef`、`grantActivationRef`。後續 Trigger／ViewAs 以新 grant 作 activation、解析回 frozen source；Correct 以新 grant 的 instance／amount／state 正常計算。原技能失去、暗置或失效不撤銷這個已施加結果；新 grant 自身仍遵守通常失效規則。

作者保存回傳的正整數 ID，到技能規定的到期點以 `detachSkillFromPlayer(recipient, SkillInstanceUtils::formatName(skillName, id), ...)` 精確移除；引擎不自動排到期。related helpers 隸屬新 grant，隨該 grant 移除，不能另外按技能名整批刪除。`getmark`、取得事件與記錄順序沿用原 acquire 行為；永久授技仍可用既有 acquire API。

來源 provenance 僅 owner 同步，公開投影保留 acquired grant 本身；伺服器保留完整來源權威。重連及完整快照／takeover 保存與還原 frozen tuple，允許原來源實例已不存在。發動、亮將、持續修正的可用性查 live grant；來源歸因查 frozen source，兩者不可互換。這不是只能用於一次提示的 `AcceptedViewAsEffectScope`。

### 普通牌／系統授予的不綁將技能

`room->acquireSkillUnbound(recipient, skillName, open, getmark, eventAndLog)`（後三參預設 `true`）沿用正常取得技能、limit mark、通知、EventAcquireSkill 及 related helper 流程。根 SourceAcquired 與 helpers 在首次通知前即 `bindHead=0`；根仍進入 acquired_skills 的 exact ID 集合，但不加入主／副將 acquired 集合，不受藏將或移除該將的 slot cascade 影響。

此入口適用普通牌或系統本身授技；不製造 `frozenSourceRef`／`grantActivationRef`，後續來源解析即新根 grant 本身。若是已接受技能效果、需要保留施予者來源，仍用 `acquireSkillFromEffect`。到期／失去實體牌等既有規則由 caller 保存回傳 ID 後精確 detach，沒有新增引擎自動到期機制。舊 acquireSkill／acquireSkillForSlot 的綁將行為保持不變。
### 實體裝備的凍結來源

`EquipSkillV2::prepareSource` 確認實體裝備時，將 `PhysicalEquipSource` 寫入 `ctx.physicalEquipSource`；其唯讀身分為原持有者、裝備名稱、技能名稱、實體 card ID。有效收據只能由原生裝備 admission 建立，不能用空來源、instance 0 或偽造 SkillInstance 取代。虛擬裝備仍沿原有技能實例引用；未經原生核實的移牌／property 來源不會自動獲得實體收據。

技能攔截、execution context 儲存與 CardUse／CardEffect／CardResponse 複製保留此身分；普通提交重新 admission，封包解析不接受收據。已接受效果使用 `useCardFromSkillEffect` 時可延續原實體來源，即使該裝備已移走；不重新執行裝備技能付款或檢查另一張同名裝備。自訂裝備配額、轉移等需使用已凍結 `cardId()`，不能以 callback 後的新裝備替代原來源。

歷史以 `physical_equipment` primitive map 記錄 `{holder, equipment, skill, card_id}`；技能 owner 可精確歸因，但 `instance_id` 仍為 0，不假冒玩家技能實例。單次用牌、技能發動及其後續 cause 保留這份資料。此能力不放寬 `AcceptedViewAsEffectScope`／`acquireSkillFromEffect` 的技能實例契約；裝備效果直接產普通牌使用上述 ordinary 入口。

實體來源的凍結點是原生 admission：仍在槽內的裝備核對持有人、牌 ID 與裝備名；離槽事件則由移動提交在 `removeCard/onUninstall` 前保存原持有人與牌名，透過 `CardsMoveOneTimeStruct::equipmentSourceBefore(id)` 唯讀取得。這份原生快照不依賴可選的 Resolution History，也不查已換裝的新牌。`BeforeCardsMove` 僅接受仍由原角色持有且位於裝備區的精確牌。未提供可核實原生來源的 property／其他 event-owned 分支仍可執行既有事件效果，但不因此取得 accepted ordinary 的空來源通行權。

`physicalEquipSource` 在每個作者 `record`／selector／cost／pay／effect 回呼返回後復原，下一個攔截器看到同一 admission identity；整個事件退出時另有例外安全復原。攔截器仍可修改取消、目標與效果數值。此規則不授權作者在回呼中以另一份 receipt 冒充新 admission。

限時授技可使用提交回執 overload：`acquireSkillFromEffect(recipient, skillName, ctx, committed, open, getmark, eventAndLog)`，其中 `committed` 為 `std::function<void(int)>`，後三參仍預設 `true`。引擎在根 grant、frozen provenance、slot 與 acquired 集合全部提交後、首次 `skill_set_changed` 及所有取得回呼前，同步呼叫一次 `committed(id)`；參數是本次新 grant 的 exact ID。授予未通過驗證時不呼叫。原本不帶 callback 的 API 保持相容。

callback 只可儲存 ID／更新已登記的 pending 收據，不得派發規則、授予／移除技能或丟出遊戲流程例外。應在此時把 ID 寫回到期收據，不能等 API 正常回傳才補寫：後續 EventAcquireSkill／limit mark 回呼可能直接 TurnBroken。引擎不因後續例外自動回滾已提交 grant；技能仍以其既有到期規則處理。純 callback 範圍的臨時借用應先建立 RAII，再在 committed 回呼填入 cleanup ID，例外出口也只精確撤回該 ID，不掃同來源的所有新技能。
### askForCard 回應完成與材料交接

`askForCard` 的 `MethodResponse` 與 `MethodUse` 都在本入口完成 V2 付款、配額與效果；例如普通閃的 MethodUse 回應不會另經 `Room::useCard`。本入口先呼叫 cost，再派發 WillInvoke；bypass_cost 跳過 pay。原請求的 reason/pattern 固定到該次回應，不讀巢狀請求留下的全域值。

`EventSkillEffectFinished` 的 `ctx.original_data` 同步最後的 `CardResponseStruct`。原生在 `ctx.interceptor_data["native_response_completion"]` 提供 `completed`、`is_provision`、`nullified`、`result`；`completed` 只在整段回應流程成功且未無效時成立，不能以早期 CardResponded 推定成功提供。提供牌技能只在該回執成立、精確 execution/activation/材料吻合時交接材料；其他結果清理本次仍在桌面的精確付款材料。

即使進入 Finished 時完成回執成立，其後的 Finished 回呼仍可能拋出控制中斷。此時原生出口把該次 execution/history 標為 interrupted，並清理本次未交付、仍在桌面的提供牌材料，保留原中斷事件，不重跑付款或退還已提交配額。

### 已支付的 provision 回傳

`askForCard(..., isProvision=true)` 正常走完 response 與 Finished 且有有效普通牌時，native 只向目前直接外層 `askForCard` 保存有界回執：付款者、原 source/activation/physical source、確切牌值與材料。`Room::provide(card)` 僅能認領同一請求 frame 中該張確切成功回傳牌；外層一次性消耗回執，重驗牌值、牌型、角色限制及材料仍在 table/nonusing，繼續原生 CardResponded／消耗流程，不以外層角色重新 resolve 或支付／執行該轉換技能。護駕請求隊友 Guhuo 等已支付響應可直接沿用此流程，不要另設 bypass_cost。

回執不在公開 tag、client payload 或長期 pointer map；CardResponseStruct 保活牌至請求結束。未認領、取消、中斷的回執到 frame 結束即失效，只清理其仍在 table 的材料；成功繼續 provision 時將材料責任交父 frame。外層 response history 的 provenance.paid_provision.player 保存原付款者，實際外層 responding player 維持原規則。舊 `provide` 普通實體牌仍沿既有入口，任意生成牌、重播一次已消耗回執或不同請求中的同牌都不能取得免重付資格。


已支付提供牌的外層 `respond_card` 在 `provenance.paid_provision.response_event_id` 保存最初成功提供的回應事件 ID，多層提供不改成中間事件。此欄位只供精確關聯；`is_provision=true` 的 `PostCardResponded` 仍未交付材料，不是最終消耗點。沿提供鏈保留的收回牌／換將等效果，須等非 provision 的外層回應或實際用牌流程到達既有完成點，再重驗材料與原始事件。缺少有效事件 ID 時不得按牌名或玩家猜配。

### 已生效的實體改牌來源


原生技能先完成 `WrappedCard::takeOver`，確認實際替換成功，再以 `Room::setPhysicalCardEffectSource(cardId, acceptedContext)` 登記來源，最後通知改牌。此入口僅接受效果階段的精確 source／activation；收據存於 Card 私有欄位，不能由牌字串、普通 tag 或用戶端請求建立。收據綁 Room 單調 serial、實體 ID、名稱、類別、花色及點數；同來源重複施加同牌形也有不同 serial。轉化仍有效時不綁原持牌者，但一般轉交觸發的原生 refilter／reset 仍可取消轉化；之後 takeOver／reset 採新內層牌的收據，預設清除舊收據。

用牌／回應入口只接受目前 Room 的實際 wrapper（或其目前內層牌），驗證收據、持有者及合法性後走普通牌流程，不重新發動或付款，不要求原技能仍存在。一般虛擬轉化牌仍走權威重建及付款。快照保存專用 `appliedPhysicalEffectSource` 欄位，接管時校驗牌形、映射來源角色，再由僅接管期間可用的恢復入口寫入；舊快照缺欄位不推定收據。
