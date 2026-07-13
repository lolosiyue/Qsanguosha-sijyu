# Lua Test Runner 系統說明

## 1. 定位

| 項目 | 說明 |
|------|------|
| 用途 | 無 GUI 的快速 LUA 武將/技能單元測試平台，支援精確控制 AI 決策與斷言驗證 |
| 本質 | 基於現有 `--test-scenario` headless 模式擴充，加入 `registerTestOverride()` override 機制 |
| 啟動方式 | `QSanguosha.exe --lua-test <script.lua>` |
| 適用場景 | extensions/*.lua 中的技能驗證、bug 復現、回歸測試 |
| 不適用場景 | 效能測試（用 `--headless`）、完整對局測試（用 `--test-scenario` + GUI） |

## 2. 架構

```
QSanguosha.exe --lua-test test.lua
    │
    ├─ main.cpp: QCoreApplication + Engine
    │   ├─ 載入 runner.lua（定義 sgs.TestRunner）
    │   ├─ 載入測試腳本（返回 runner table）
    │   ├─ 建立 minimal test scenario（2人, sujiang, singleTurn:lord）
    │   ├─ createNewRoom() → ROOM global → SWIG 綁定
    │   └─ 連線 game_over → RUNNER_DO_ASSERTIONS → quit
    │
    ├─ runner:execute()
    │   ├─ setup()  → 設定技能、玩家狀態
    │   ├─ run()    → registerTestOverride() 註冊覆寫
    │   └─ room:start() → RoomThread 啟動
    │
    ├─ RoomThread 遊戲迴圈
    │   └─ askForSkillInvoke / askForChoice / askForCard / ...
    │        │
    │        ├─ findTestOverride() 匹配? → 返回測試指定答案
    │        └─ 無匹配 → 走正常 AI (LuaAI / TrustAI)
    │
    └─ game_over
        └─ RUNNER_DO_ASSERTIONS()
            └─ runner:printResults() → stdout [PASS]/[FAIL]
```

### 核心類別/檔案

| 層級 | 檔案 | 核心對象 | 職責 |
|------|------|----------|------|
| Server | `src/server/room.h/.cpp` | `Room::registerTestOverride()` / `findTestOverride()` / `clearTestOverrides()` | override 表的註冊、查詢、清除 |
| SWIG | `swig/sanguosha.i` | `%extend Room` | 將 override 方法暴露給 Lua |
| CLI | `src/main.cpp` | `--lua-test` 模式 | 建立 Room、載入腳本、管理生命週期 |
| Lua Framework | `lua/test/runner.lua` | `sgs.TestRunner` | 測試 DSL（setup/run/assert 鏈 + 斷言輔助） |
| Lua Override | `lua/test/runner.lua` | `runner:registerOverride()` | Lua 端呼叫 Room override 的 thin wrapper |
| Templates | `lua/test/templates/` | `on_damage.lua` 等 | 按觸發事件分類的測試模板 |

## 3. 快速開始

```bash
# 基本測試
QSanguosha.exe --lua-test lua/test/examples/test_basic.lua

# Verbose 模式（顯示 override 匹配 + 遊戲流程）
QSanguosha.exe --lua-test-verbose --lua-test lua/test/examples/test_tuxi.lua
```

### 最簡測試腳本

```lua
local runner = sgs.test.create("基本測試")

runner:setup(function(t)
    -- t:getPlayers()[1] = lord, t:getPlayers()[2] = rebel
    ROOM:handleAcquireDetachSkills(t:getPlayers()[1], "my_skill", false)
end)

runner:run(function(t)
    local p1 = t:getPlayers()[1]
    t:registerOverride(p1, "skill_invoke", "my_skill", sgs.QVariant(true))
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertHasSkill(1, "my_skill")
end)

return runner
```

腳本必須 `return` runner table。執行順序：`setup()` → `run()` → `ROOM:start()` → 遊戲 → `game_over` → `assert()`。

## 4. CLI 參數

| 參數 | 格式 | 說明 |
|------|------|------|
| `--lua-test` | `--lua-test <script.lua>` | 指定測試腳本路徑 |
| `--lua-test-verbose` | 獨立 flag | 開啟詳細模式：顯示 override 匹配、遊戲階段、回合追蹤 |

### 輸出格式

```
==================================================
Test: 測試名稱
==================================================
[PASS] Player1 HP: expected 4, got 4
[FAIL] Player2 HP: expected 3, got 4
--------------------------------------------------
Results: 1/2 passed
        1 FAILED
==================================================
```

- 全部通過 → exit code 0
- 有任何失敗 → exit code 1
- Lua error / crash → exit code 1 + stack trace

## 5. Override 機制

### 核心原理

每個 `Room::askFor*` 方法在呼叫 AI 之前，先查詢 `m_testOverrides` 字典。若有匹配，直接返回測試預設值，跳過 AI。

```
askForSkillInvoke(player, "wusheng", ...)
    ↓
QVariant over = findTestOverride(player, "skill_invoke", "wusheng");
    ↓ 匹配?
    ├─ YES → invoked = over.toBool()       (跳過 AI)
    └─ NO  → ai->askForSkillInvoke(...)    (正常 AI)
```

### Override Key 格式

```
<playerObjName>:<queryType>:<key>
```

範例：
- `"192.168.1.1_A:skill_invoke:wusheng"` → A 玩家被問到 wusheng 時返回
- `"192.168.1.1_A:activate:phase"` → A 玩家的出牌階段行為

### 支援的 queryType

| queryType | 攔截的 Room 方法 | answer 型別 | 說明 |
|-----------|-----------------|-------------|------|
| `skill_invoke` | `askForSkillInvoke()` | `QVariant(bool)` | 是否發動技能 |
| `choice` | `askForChoice()` | `QVariant(str)` | 選項（如 `"option1"`） |
| `card` | `askForCard()` | `QVariant(int)` cardId 或 `QVariant(str)` Card::Parse 格式 | 提供卡牌 |
| `card_chosen` | `askForCardChosen()` | `QVariant(int)` cardId | 選中目標的卡牌 |
| `player_chosen` | `askForPlayerChosen()` | `QVariant(str)` playerObjName | 選中目標玩家 |
| `activate` | `activate()` | `QVariant("pass")` | 跳過出牌階段 |

### 注入點位置（room.cpp）

| 方法 | 行號 | 注入位置 |
|------|------|---------|
| `askForSkillInvoke` | 1749 | `InvokeSkill` 觸發後、AI 分支前 |
| `askForChoice` | 1828 | `EventAskForChoice` 觸發後、AI 分支前 |
| `askForCard` | 2377 | `"provided"` tag 檢查後、AI 分支前（9次重試迴圈內） |
| `askForCardChosen` | 2291 | AI 分支前 |
| `askForPlayerChosen` | 8025 | AI 分支前 |
| `activate` | 7355 | `EventPlayPhaseLoop` 後、AI 分支前 |

### 線程安全

`m_testOverrides` 訪問由 `QMutex m_testOverrideMutex` 保護：
- 註冊 override（測試腳本，主線程）→ `QMutexLocker locker(&m_testOverrideMutex)`
- 查詢 override（RoomThread，遊戲線程）→ `QMutexLocker locker(&m_testOverrideMutex)`

## 6. 斷言 API

所有斷言定義在 `sgs.TestRunner` 上，用於 `assert()` 回呼。

| 方法 | 簽名 | 說明 |
|------|------|------|
| `assertHp` | `(playerRef, expected)` | 驗證玩家 HP；playerRef 可以是 1-based index 或 screenName |
| `assertAlive` | `(playerRef)` | 驗證玩家存活 |
| `assertHasSkill` | `(playerRef, skillName)` | 驗證玩家擁有技能 |
| `assertHandcardCount` | `(playerRef, expected)` | 驗證手牌數量 |
| `assertMark` | `(playerRef, markName, expected)` | 驗證 mark 值 |
| `assertNotKongcheng` | `(playerRef)` | 驗證手牌非空 |

### 通用斷言（lua/test/assert.lua）

| 函數 | 說明 |
|------|------|
| `sgs.assert.equal(actual, expected, msg, runner)` | 相等 |
| `sgs.assert.true(value, msg, runner)` | 為 true |
| `sgs.assert.notNil(value, msg, runner)` | 非 nil |
| `sgs.assert.greater(actual, expected, msg, runner)` | 大於 |
| `sgs.assert.less(actual, expected, msg, runner)` | 小於 |
| `sgs.assert.between(actual, min, max, msg, runner)` | 在區間內 |
| `sgs.assert.contains(list, item, msg, runner)` | 列表包含 |

### 輔助方法

| 方法 | 說明 |
|------|------|
| `t:getPlayers()` | 返回所有玩家 ServerPlayer 的 Lua table（1-based） |
| `t:getPlayer(ref)` | 按 index(1/2) 或 screenName 查找玩家 |
| `t:addResult(passed, msg)` | 直接加入一條結果（擴展自定義斷言用） |

## 7. 模板系統

### 模板檔案

```
lua/test/templates/
├── TEMPLATE.lua              ← 通用空白（複製起點）
├── on_damage.lua             ← 受傷/造成傷害觸發
├── on_phase.lua              ← 摸牌/出牌/棄牌階段觸發
├── on_slash.lua              ← 使用殺/被殺指定觸發
├── on_discard.lua            ← 棄牌/失去牌觸發
├── on_card_effect.lua        ← 卡牌效果修改（免疫、無懈等）
├── on_judge.lua              ← 判定相關（改判）
├── on_turn.lua               ← 回合/階段開始
├── on_view_as.lua            ← ViewAsSkill 轉換技
└── EXAMPLE_on_phase_tuxi.lua ← 填好的實際範例
```

每個模板包含三類標記：

| 標記 | 含義 | 範例 |
|------|------|------|
| `[REQUIRED: xxx]` | **必須**替換 | `local SKILL_NAME = "[REQUIRED: skill name]"` |
| `[CHANGE]` | 依技能類型修改 | `local TRIGGER_PHASE = sgs.Player_Draw  -- [CHANGE]` |
| `[SKILL-SPECIFIC]` | 需讀取技能原始碼決定 | `-- [SKILL-SPECIFIC] t:registerOverride(...)` |

### 事件 → 模板對應表

`runner.lua` 內建 `sgs.test.templates` 索引：

```lua
sgs.test.templates = {
    on_damage      = { events = {"Damaged","DamageInflicted","DamageDone","DamageComplete"} },
    on_phase       = { events = {"EventPhaseStart","EventPhaseChanging","EventPhaseEnd"} },
    on_slash       = { events = {"CardUsed","PreCardUsed","TargetChosen","SlashEffect","SlashHit"} },
    on_discard     = { events = {"CardsMoveOneTime","CardLost","CardDiscarded"} },
    on_card_effect = { events = {"CardEffect","CardEffected","PreCardEffect"} },
    on_judge       = { events = {"AskForRetrial","FinishJudge","StartJudge"} },
    on_turn        = { events = {"EventTurnStart","EventPhaseStart","GameStart"} },
    on_view_as     = { events = {"CardUsed","PreCardUsed"} },  -- ViewAsSkill
}
```

## 8. AI Agent 工作流

模板設計讓 AI Agent 可以自動產生測試腳本。流程：

```
Step 1: 讀取技能定義
   file: extensions/xxx.lua
   skill = sgs.CreateTriggerSkill{ name="skill_name", events={...} }
   → 分析 events、can_trigger、on_trigger/on_effect

Step 2: 匹配模板
   查詢 sgs.test.templates 索引
   skill.events 包含 "EventPhaseStart" → 匹配 on_phase

Step 3: 分析技能內部呼叫
   讀取 on_trigger / on_effect 函數主體
   查找 askForChoice / askForSkillInvoke / askForCard / askForDiscard / ...
   → 決定哪些 queryType 需要 registerOverride

Step 4: 填入模板
   複製 templates/on_phase.lua
   替換 [REQUIRED] → 實際值
   替換 [CHANGE] → 依觸發階段
   uncomment [SKILL-SPECIFIC] 行 → 填入具體 override + assert

Step 5: 執行
   QSanguosha.exe --lua-test-verbose --lua-test output_test.lua
```

### 技能原始碼分析 → Override 映射

| 技能中的 API 呼叫 | 對應的 override queryType | answer 範例 |
|-------------------|--------------------------|-------------|
| `room:askForSkillInvoke(p, "skill", ...)` | `skill_invoke` | `sgs.QVariant(true)` |
| `room:askForChoice(p, "skill", "opt1+opt2")` | `choice` | `sgs.QVariant("opt1")` |
| `room:askForCard(p, "pattern", "skill", ...)` | `card` | `sgs.QVariant(cardId)` |
| `room:askForCardChosen(p, target, "h", "skill")` | `card_chosen` | `sgs.QVariant(cardId)` |
| `room:askForDiscard(p, "skill", ...)` | `card` | `sgs.QVariant(cardId)` |
| `room:askForPlayerChosen(p, targets, "skill")` | `player_chosen` | `sgs.QVariant("objName")` |

### 效果邏輯分析 → 斷言映射

| 技能中的效果呼叫 | 對應的斷言 |
|-----------------|-----------|
| `room:damage(damage)` where `damage.to != holder` | `t:assertHp(targetIndex, expected)` |
| `room:damage(damage)` where `damage.to == holder` | `t:assertHp(holderIndex, expected)` |
| `player:drawCards(n)` | `t:assertHandcardCount(index, >=initial+n)` |
| `room:throwCard(...)` | `t:assertHandcardCount(index, expected)` |
| `room:obtainCard(player, ...)` | `t:assertHandcardCount(index, expected)` |
| `room:loseHp(player, n)` | `t:assertHp(index, expected)` |
| `room:recover(player, ...)` | `t:assertHp(index, expected)` |
| `room:addPlayerMark(...)` | `t:assertMark(index, "markName", expected)` |
| `player:gainAnExtraTurn()` | （需要多回合測試，目前 singleTurn 限制） |

## 9. 實際範例：s4_cloud_tuxi

### 技能定義（scarlet.lua:37-79）

```lua
s4_cloud_tuxi = sgs.CreateTriggerSkill {
    name = "s4_cloud_tuxi",
    events = { sgs.EventPhaseStart },
    can_trigger = function(self, target)
        return target and target:getPhase() == sgs.Player_Play and target:isAlive()
    end,
    on_trigger = function(self, event, player, data, room)
        -- 其他玩家出牌階段開始時，可棄牌或扣血
        for _, p in sgs.qlist(room:findPlayersBySkillName(self:objectName())) do
            if p:objectName() ~= player:objectName() and
               room:askForSkillInvoke(p, self:objectName(), ...) then
                -- 1. 問棄牌或扣血
                -- 2. 比較手牌/裝備/HP
                -- 3. 偷牌 / 拆裝備 / 傷害
            end
        end
    end
}
```

### 分析結果

| 分析項 | 值 |
|--------|---|
| events | `EventPhaseStart` → 匹配 `on_phase` 模板 |
| 觸發階段 | `Phase_Play`（出牌階段） |
| askForSkillInvoke | 有 → override: `skill_invoke` = true |
| askForChoice | 有（選擇扣 1/2/3/4 血）→ override: `choice` = "1" |
| askForCardChosen | 有（偷手牌）→ override: `card_chosen` = cardId |
| 效果 | `room:loseHp(p, ...)` → assertHp(1, 3) |
| 效果 | `room:obtainCard(p, card_id)` → assertHandcardCount(2, 3) |

### 產生的測試腳本（templates/EXAMPLE_on_phase_tuxi.lua）

```lua
local SKILL_NAME  = "s4_cloud_tuxi"
local TEST_NAME   = "s4_cloud_tuxi: 出牌階段棄牌/扣血+比較屬性"

local runner = sgs.test.create(TEST_NAME)

runner:setup(function(t)
    local p1, p2 = t:getPlayers()[1], t:getPlayers()[2]
    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)
    ROOM:handleAcquireDetachSkills(p2, SKILL_NAME, false)
end)

runner:run(function(t)
    local p1, p2 = t:getPlayers()[1], t:getPlayers()[2]
    t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))
    t:registerOverride(p1, "choice", SKILL_NAME, sgs.QVariant("1"))
    t:registerOverride(p1, "card_chosen", SKILL_NAME, sgs.QVariant(1))
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHasSkill(1, SKILL_NAME)
    t:assertHasSkill(2, SKILL_NAME)
    t:assertHp(1, 3)
end)

return runner
```

## 10. 遊戲流程說明

### 預設場景

main.cpp 自動建立 minimal 2 人 test scenario：

```
general:sujiang|role:lord|hp:4|starter
general:sujiang|role:rebel|hp:4
extraOptions:singleTurn:lord
```

- 遊戲在 lord 完成**一回合**後自動結束（singleTurn:lord）
- 如需多回合，可在測試腳本中修改 scenario 或覆寫 `activate` 為 `pass` 加速

### 玩家物件

| 索引 | 身分 | 武將 | 可用 |
|------|------|------|------|
| `t:getPlayers()[1]` | lord | sujiang | 預設擁有測試技能 |
| `t:getPlayers()[2]` | rebel | sujiang | 目標/對手 |

`t:getPlayer(1)` ← index 是 1-based（對應 Lua table 索引）。

## 11. 進階用法

### 負向測試（技能不應觸發）

```lua
runner:run(function(t)
    -- 不註冊 skill_invoke override → AI 自行決定
    -- 若 can_trigger 返回 false，技能就不會問
end)

runner:assert(function(t)
    -- 驗證 HP 未變化（技能未觸發）
    t:assertHp(1, 4)
    t:assertHp(2, 4)
end)
```

### 自定義斷言

```lua
runner:assert(function(t)
    local p1 = t:getPlayers()[1]
    local equipCount = p1:getEquips():length()
    local passed = (equipCount >= 1)
    t:addResult(passed,
        string.format("Player 1 equipment count: %d (expected >= 1)", equipCount))
end)
```

### Verbose 除錯輸出

```
[OVERRIDE] registered: 192.168.1.1_A:choice:s4_cloud_tuxi = "1"
[OVERRIDE] activate() → matched "pass", skipping play phase
[OVERRIDE] askForSkillInvoke(s4_cloud_tuxi) → matched: true
```

## 12. 限制

| 限制 | 說明 | 可能的解決方案 |
|------|------|---------------|
| singleTurn:lord | 遊戲僅執行 1 回合 lord | 修改 scenario extraOptions 或手動覆寫結束條件 |
| sujiang 武將 | 預設使用 sujiang（無自帶技能），某些技能需特定武將 general | 在 setup() 中手動 `room:changeHero()` |
| 固定 2 玩家 | 預設為 2 人 | 修改 main.cpp 中的 temp scenario 行數或增加 `playerCount` |
| Activate override | 僅支援 `pass`（跳過出牌），不支援指定使用某張牌 | 需擴展 `activate` override 支援完整 CardUseStruct |
| 無 timeout | `qApp->exec()` 永久阻塞若 game_over 永不起火 | 加入 QTimer timeout 保護 |

[Memory Updated]