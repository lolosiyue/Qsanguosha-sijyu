# AnytimeSkill 說明

## 1. 定位

| 項目 | 說明 |
|------|------|
| 類型 | `AnytimeSkill` 是 `Skill` 子類，不屬於 `TriggerSkill`，也不是 `ViewAsSkill` |
| 觸發方式 | 玩家主動按技能按鈕送出請求，而不是等待某個 `TriggerEvent` 自動觸發 |
| 執行時機 | 技能不會立刻打斷當前流程，而是先進入待處理佇列，在目前 `event` 結算完成後統一執行 |
| UI 形態 | 前端把它當成 `S_SKILL_ANYTIME` 類型的 push button；送出請求後會暫時 disabled，直到 server 回傳完成通知 |
| 典型用途 | 回合外主動宣告、需要等待當前事件收尾後再執行的效果 |
| 不適用場景 | 需要即時插入當前 event、需要完整選牌/選目標出牌流程、或必須由 client 即時判斷可用性的技能 |

## 2. 物件關係

| 層級 | 檔案 | 核心對象 | 職責 |
|------|------|----------|------|
| Core | `src/core/skill.h/.cpp` | `AnytimeSkill` | 定義基底類別與預設行為 |
| Lua Bridge | `src/core/lua-wrapper.h/.cpp` | `LuaAnytimeSkill` | 讓 Lua 覆寫 `canTrigger` / `onTrigger` |
| Lua Factory | `lua/sgs_ex.lua` | `sgs.CreateAnytimeSkill` | 提供 Lua 端建構語法 |
| Protocol | `src/core/protocol.h` | `S_COMMAND_ANYTIME_SKILL` / `S_COMMAND_ANYTIME_SKILL_DONE` | 傳遞按下技能與完成通知 |
| Client | `src/client/client.h/.cpp` | `m_anytimeSkillPending` | 記錄本地 pending 技能，避免重複送出 |
| Scene | `src/ui/roomscene.cpp` | `onAnytimeSkillActivated()` / `onAnytimeSkillDone()` | 接技能按鈕、更新 enabled 狀態 |
| UI Button | `src/ui/qsanbutton.h/.cpp` | `S_SKILL_ANYTIME` | 決定按鈕樣式、互動型態與啟用/停用策略 |
| Server | `src/server/room.cpp` | `handleAnytimeSkillRequest()` / `processPendingAnytimeSkills()` | 驗證請求並在適當時機執行技能 |
| Player State | `src/server/serverplayer.h/.cpp` | `m_pendingAnytimeSkills` | 記錄每位玩家待處理的 AnytimeSkill |
| Scheduler | `src/server/roomthread.cpp` | `RoomThread::trigger()` | 在 event stack 清空後呼叫 `processPendingAnytimeSkills()` |

## 3. C++ 介面

```cpp
class AnytimeSkill : public Skill
{
     Q_OBJECT

public:
     AnytimeSkill(const QString &name);

     virtual bool canTrigger(ServerPlayer *player) const;
     virtual bool onTrigger(Room *room, ServerPlayer *player) const;

     inline bool isAnytime() const { return true; }
};
```

| 成員 | 預設行為 | 備註 |
|------|----------|------|
| `AnytimeSkill(const QString &name)` | 內部呼叫 `Skill(name, NotFrequent)` | 基底預設頻率是 `NotFrequent` |
| `canTrigger(ServerPlayer *player)` | 回傳 `true` | server 端收到請求時會再檢查一次 |
| `onTrigger(Room *room, ServerPlayer *player)` | 回傳 `false` | 目前 `Room::processPendingAnytimeSkills()` 不使用這個回傳值 |
| `isAnytime()` | 回傳 `true` | 供 UI / 邏輯分流辨識 |

## 4. 執行流程

```text
玩家按下技能按鈕
  -> RoomScene::onAnytimeSkillActivated()
  -> Client::triggerAnytimeSkill(skill_name)
  -> Client::m_anytimeSkillPending.insert(skill_name)
  -> notifyServer(S_COMMAND_ANYTIME_SKILL, skill_name)

server 收到封包
  -> Room::handleAnytimeSkillRequest(player, arg)
  -> 檢查 skill 是否存在、玩家是否擁有技能、skill->canTrigger(player) 是否為 true
  -> ServerPlayer::addPendingAnytimeSkill(skill_name)

當前 event 結算完成，且 RoomThread 的 event stack 清空
  -> RoomThread::trigger()
  -> 若有 pending summons 先處理 summons
  -> Room::processPendingAnytimeSkills()
  -> 依玩家順序取出 ServerPlayer::m_pendingAnytimeSkills
  -> 逐個執行 skill->onTrigger(room, player)
  -> Room::notifyAnytimeSkillDone(player, skill_name)

client 收到完成通知
  -> Client::handleAnytimeSkillDone(arg)
  -> Client::m_anytimeSkillPending.remove(skill_name)
  -> emit anytime_skill_done(skill_name)
  -> RoomScene::onAnytimeSkillDone(skill_name)
  -> 對應按鈕恢復 enabled
```

## 5. Lua API

### 建構函式

```lua
local skill = sgs.CreateAnytimeSkill{
     name = "my_anytime_skill",
     frequency = sgs.Skill_NotFrequent,
     can_trigger = function(self, player)
          return player and player:isAlive()
     end,
     on_trigger = function(self, room, player)
          return false
     end
}
```

### 參數

| 參數 | 類型 | 必填 | 預設值 | 說明 |
|------|------|------|--------|------|
| `name` | `string` | 是 | 無 | 技能名 |
| `frequency` | `number` | 否 | `sgs.Skill_NotFrequent` | 會寫入 `LuaAnytimeSkill` 的 `frequency`，但不決定 Anytime 按鈕分流 |
| `can_trigger` | `function(self, player)` | 否 | 永遠回 `true` | server 收到請求時用來做最終檢查 |
| `on_trigger` | `function(self, room, player)` | 否 | 永遠回 `false` | 技能真正執行的地方；目前回傳值不參與後續流程 |

### 最小範例

```lua
local bloodDrawSkill = sgs.CreateAnytimeSkill{
     name = "blooddraw",
     frequency = sgs.Skill_NotFrequent,
     can_trigger = function(self, player)
          return player and player:isAlive() and player:getHp() > 1
     end,
     on_trigger = function(self, room, player)
          room:loseHp(player, 1)
          room:drawCards(player, 2, self:objectName())
          return false
     end
}

local general = sgs.General(extension, "my_general", "wei", 4, true, true)
general:addSkill(bloodDrawSkill)
```

## 6. 協議與排程細節

| 項目 | 說明 |
|------|------|
| Client -> Server | `S_COMMAND_ANYTIME_SKILL` |
| Server -> Client | `S_COMMAND_ANYTIME_SKILL_DONE` |
| 去重策略 | client 端用 `QSet<QString> m_anytimeSkillPending`；server 端用 `QStringList m_pendingAnytimeSkills` 並先 `contains()` 再加入 |
| 排程插點 | `RoomThread::trigger()` 在 `event_stack.isEmpty()` 後呼叫 `room->processPendingAnytimeSkills()` |
| 與召喚流程的先後 | `processPendingSummons()` 先於 `processPendingAnytimeSkills()` |
| 執行順序 | 先按 `getAlivePlayers()` 順序，再按該玩家 pending 清單順序 |

## 7. UI 現況

| 項目 | 現況 |
|------|------|
| 按鈕類型 | `QSanSkillButton::setSkill()` 只要發現 `skill->inherits("AnytimeSkill")`，就直接走 `S_SKILL_ANYTIME` 分支 |
| 與 frequency 的關係 | `AnytimeSkill` 的按鈕類型不走一般 `Frequent` / `NotFrequent` / `Limited` 分支判定 |
| 圖資 | `QSanRoomSkin::getSkillButtonPixmap()` 若找不到 `anytime` 專用圖資，會 fallback 到 `proactive` |
| 字色 | `skin-bank.cpp` 目前把 `S_SKILL_ANYTIME` 映射到 `proactiveFontColor` |
| 待補資源 | `image/system/button/skill/anytime/*.png` 與對應色彩配置 |

## 8. 開發注意事項

| 注意點 | 說明 |
|--------|------|
| `can_trigger` 不會在 client 預檢 | client 按下按鈕後會先把技能記進 `m_anytimeSkillPending`，再送封包給 server；目前協議只有 `done`，沒有對稱的 `reject` 回補通知，因此 `can_trigger` 應盡量保持快速、穩定，不要把高度瞬時的 UI 條件全壓在這裡 |
| `on_trigger` 的 bool 目前不控制流程 | `Room::processPendingAnytimeSkills()` 目前直接呼叫 `skill->onTrigger(this, player)`，然後無條件通知 done；不要把回傳值當成 success / fail 訊號 |
| 這不是打斷式技能 | AnytimeSkill 只會在當前 event 收尾後執行，不能拿來取代會即時介入結算的 `TriggerSkill` |
| 這不是出牌式技能 | 若技能需要選牌、選目標、驗證 pattern，通常還是應該走 `ViewAsSkill` / `askForUseCard` 類路徑 |
| UI 按鈕會暫時變暗 | 正常流程下要等 `S_COMMAND_ANYTIME_SKILL_DONE` 才會恢復，這也是避免重複送出的主要機制 |