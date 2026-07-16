# 玩家技能多實例重構計劃

## 1. 文件目的

本文件是玩家技能多實例 (Multi-instance Skill) 重構的實作票據與驗收規格。

目標是讓同一名玩家可同時持有多個同名技能實例，每個實例具有獨立 instanceID、來源、父子關係與私有狀態，同時維持共享的全域 `Skill` 定義。

本計劃以 `L:\finaldebug\QSanguosha-v2` 的現況為準；`H:\Program file\Game\sgs\Qsgs\github\QSanguosha-v2` 只作舊版唯讀參考。

## 2. 已鎖定的核心模型

### 2.1 Skill 是共享定義

- Engine 中每個技能名稱只有一個 `Skill`／`TriggerSkill`／`ViewAsSkill` 定義物件。
- instanceID 不屬於 `Skill` QObject。
- 禁止恢復 `Skill::m_instanceId`、全域 Skill 計數器或按 instance clone Skill QObject。

### 2.2 SkillInstance 是玩家持有關係

建議基礎結構：

```cpp
struct SkillInstanceKey {
    QString skillName;
    int instanceID;
};

struct SkillInstance {
    QString skillName;
    int instanceID;
    SkillInstanceSource source;
    SkillInstanceKey parent;
    bool visible;
    QVariantMap state;
};
```

`parent` 只供 related helper 使用；一般實例為空。

### 2.3 Player 權威容器

```cpp
QMap<QString, QMap<int, SkillInstance>> m_skillInstances;
QMap<QString, int> m_nextSkillInstanceIds;
```

- `m_skillInstances` 是唯一真實來源 (Single Source of Truth)。
- `skills`／`acquired_skills` 的 `skill#N` 字串不可繼續作為權威資料。
- 舊 getter 所需的字串清單由 `m_skillInstances` 派生。
- 禁止長期保存 QMap 元素指標；需要時按 key 重新查詢。

### 2.4 instanceID 規則

| 規則 | 決策 |
|---|---|
| 唯一範圍 | 同一玩家、同一技能名稱內唯一 |
| 真實 ID | 一律為正整數 |
| ID 0 | wildcard／未指定，不代表原生技能 |
| 分配 | 單調遞增 |
| 移除後 | 永不重用 |
| 初始順序 | 主將、再副將、再按後天獲得時間 |

例：

```text
主將 skill → skill#1
副將 skill → skill#2
後天獲得   → skill#3
移除 #2 後再次獲得 → skill#4
```

### 2.5 狀態生命週期

- 每個實例持有獨立 `QVariantMap state`。
- state 只允許可序列化值：整數、布林、字串、ID、`QVariantList`、`QVariantMap` 等。
- 禁止保存 `ServerPlayer *`、`Card *`、`QObject *` 等裸指標；改存 objectName 或 card ID。
- 實例移除後立即銷毀 state，不保留 tombstone。
- 只保留同名技能的 next-ID 計數器。
- 延遲事件引用不存在的實例時驗證失敗並跳過，不得復活舊 state 或改用其他同名實例。

## 3. 已鎖定的行為規格

### 3.1 獲得技能

- 每次呼叫 `Room::acquireSkill()` 都建立新實例。
- 不新增 `ensureSkill()`。
- 需要冪等的舊呼叫點由人工加入 `hasSkill()`／`ownsSkill()` 防重。
- `Player::acquireSkill()` 與 `Room::acquireSkill()` 回傳新 instanceID。
- 一般後天獲得技能立即公開。
- 可保留顯式綁定主將／副將來源的能力，但國戰完整可見性不是本階段阻塞項。

### 3.2 原生技能

- 原生同名技能也必須是不同實例。
- 隱藏、暗置、重新明置不重建實例，也不更換 instanceID。
- 真正更換或移除武將時才銷毀該來源實例。

### 3.3 移除技能

- 移除 API 回傳實際移除的 instanceID；不存在候選時回傳 0。
- 選擇不可取消。
- 持有者失去多個同名技能之一時，由持有者選擇。
- 新增 A 主動棄置 B 技能實例的 Room API；由 A 選擇 B 的可見實例。
- A 看不到的實例不可出現在候選資料中。
- 若 A 沒有可見候選但 B 存在隱藏實例，由引擎移除 instanceID 最小者。
- 暫時失效或被封禁的實例仍可被移除；持有與有效性分離。

### 3.4 related helper

- 每個父技能實例建立自己的 related helper 實例。
- helper 使用自己的同名技能 next-ID 計數器，ID 單調遞增且不重用。
- 父子關係必須顯式保存，不得靠父子 ID 相同推算。
- helper 不進入玩家移除／棄置候選。
- helper 只能隨父實例級聯移除。
- 移除父實例時只刪除父鍵匹配的 helper。

### 3.5 持有與有效查詢

```text
hasSkill(name)       → 至少存在一個有效實例
hasSkill(name, true) → 至少持有一個實例，不考慮失效
ownsSkill(name)      → 明確的持有查詢
```

- 技能效果判定使用 `hasSkill()`。
- 移除、來源與實例管理使用 `ownsSkill()`。
- `SkillInvalidityRecords` 中 ID 0 封禁全部同名實例；正 ID 只封禁精確實例。

### 3.6 TriggerSkill 與數值被動

- 舊 `TriggerSkill` 即使持有多個實例仍只執行一次。
- 只有 `TriggerV2Skill` 逐實例建立 `SkillContext` 並獨立執行。
- 需要實例狀態的舊 TriggerSkill 逐步遷移至 V2。
- Distance／MaxCards／TargetMod／AttackRange 等數值被動預設不疊加。
- 只有 `InstanceStackable=true` 才逐有效實例求和。
- 現有反向的 `NoInstanceMultiply` 應由新正向屬性取代。

### 3.7 TriggerV2Skill

```text
can_trigger return "skill"   → 展開持有者全部有效實例
can_trigger return "skill#N" → 只建立指定實例的 SkillContext
```

- 展開後再次驗證 owner、持有、有效性與 instance state。
- 不存在或無效的精確 ID 直接忽略。
- `on_record` 按每個現存實例逐一呼叫，context 帶 owner、skill_name、instanceID、original_data、current_event。
- `triggerCounts`、`maxMultipliers`、`triggeredSkills`、`selected_ctx` 與選項驗證一律使用 `(owner, skillName, instanceID)`。
- 禁止使用只有 `skillName#instanceID` 的 Room 全域 key，避免不同玩家碰撞。

### 3.8 事件資料

不新增 `EventAcquireSkillInstance`／`EventLoseSkillInstance`。

- 每次建立實例都觸發既有 `EventAcquireSkill`。
- 每次移除實例都觸發既有 `EventLoseSkill`。
- data 改為 `SkillChangeStruct`。

建議欄位：

```cpp
struct SkillChangeStruct {
    QString skillName;
    int instanceID;
    SkillInstanceSource source;
    QString parentSkillName;
    int parentInstanceID;
    bool visible;
};
```

相容規則：

- C++ `data.toString()` 與 Lua `data:toString()` 永遠回傳基礎技能名。
- C++ 新碼使用 `data.value<SkillChangeStruct>()`。
- Lua 新碼使用 `data:toSkillChange()`。
- 舊監聽者會在每次實例獲得／失去時執行，語意不正確者列入人工審核。

### 3.9 客戶端 UI

- 每個實例顯示獨立技能按鈕。
- 按鈕內部 canonical key 使用 `skill#N`。
- 定義查詢前必須解析 baseName；不得向 Engine 查 `skill#N`。
- 隱藏技能 `#helper#2` 必須解析為 baseName=`#helper`、ID=2。
- 同名只有一個實例時，按鈕顯示翻譯技能名，不顯示 ID。
- 同名多於一個時，顯示「翻譯技能名 #N」。
- 1→2 或 2→1 時刷新全部同名按鈕標籤。
- 按 instanceID 升序排列。
- 右方公開 log 永遠只顯示基礎技能名，不顯示 ID。

### 3.10 Card／CardUseStruct

- ViewAs／主動技能產生的卡牌新增獨立 `skillInstanceID`。
- `skillName` 保持基礎名稱，不得改成 `skill#N`。
- instanceID 必須穿過 Card、CardUseStruct、客戶端回覆、伺服器驗證、事件及重播。
- 新 UI 按鈕傳送精確 ID。
- 舊 Lua／AI 傳 0 時，伺服器解析為使用者最小的有效同名實例；沒有有效實例才拒絕。
- fallback 解析後立即寫回精確 ID，後續流程不得重新 fallback。
- 卡牌合法開始使用後，即使技能實例中途消失，CardUse 仍繼續完成。
- 後續依賴已移除 instance state 的技能回調驗證失敗後跳過，不保留 state 副本或改用另一實例。

### 3.11 同步與資訊邊界

- 初始化與重連由伺服器發送完整、依接收者權限裁切的 SkillInstance metadata snapshot。
- 遊戲中使用 acquire／detach 增量通知。
- 客戶端不得自行分配 instanceID。
- 同步公開 metadata，不同步完整 `QVariantMap state`。
- 需要顯示的狀態使用 mark、property 或專用通知。

## 4. Ticket 拆分與依賴順序

每個 ticket 完成後必須單獨編譯；弱模型禁止跨票順手重構。

### Ticket 1：基礎型別與名稱解析

範圍：

- 新增 `SkillInstanceKey`、`SkillInstance`、`SkillChangeStruct`、`SkillInstanceSource`。
- 加入 QVariant metatype、QString converter 與 SWIG/Lua 宣告。
- 集中實作 instance 名稱格式化與解析。
- 補齊 `skill#1`、`#hiddenSkill`、`#hiddenSkill#2` 測試。

驗收：不改遊戲行為；Release x64 編譯通過。

### Ticket 2：Player 權威容器

依賴：Ticket 1。

範圍：

- 新增巢狀 QMap 與 next-ID map。
- 實作 create/find/remove/list instance API。
- 實作 state get/set/remove API。
- 改寫 `hasSkill`、`ownsSkill`、`getSkillInstanceIds`、`getValidSkillInstanceIds`。
- 讓舊 skill list getters 從新容器派生。
- 修正 `getSkillList()` 等函式先解析 baseName 再查 Engine。

驗收：主副將同名實例可共存；ID 不重用；舊 getter 不回傳 nullptr Skill。

### Ticket 3：Room 獲得技能與 helper

依賴：Ticket 2。

範圍：

- `acquireSkill()` 每次建立新實例並回傳 ID。
- 建立來源 metadata。
- 每個父實例建立獨立 helper 並保存父鍵。
- 使用 `SkillChangeStruct` 觸發 EventAcquireSkill。
- log 只顯示基礎技能名。

驗收：連續獲得產生 #1/#2/#3；helper ID 獨立；父子查詢正確。

### Ticket 4：Room 移除與外部棄置

依賴：Ticket 3。

範圍：

- 實作持有者選擇移除實例。
- 新增 A 選擇棄置 B 公開實例的 API。
- 選擇不可取消。
- 無可見候選時按最小 ID fallback。
- 精確移除、state 銷毀、helper 級聯。
- 使用 `SkillChangeStruct` 觸發 EventLoseSkill。

驗收：只刪除所選實例；其他同名實例與 helper 關係正確保留。

### Ticket 5：有效性與被動修正

依賴：Ticket 2。

範圍：

- 實作全體／精確 instance invalidity。
- 持有與有效查詢分離。
- 新增 `InstanceStackable`。
- 改寫數值修正聚合。

驗收：封禁 #1 不影響 #2；未標記舊技能不疊加；標記技能正確求和。

### Ticket 6：TriggerV2Skill 實例化流程

依賴：Ticket 2、Ticket 5。

範圍：

- base return 展開、精確 return 驗證。
- `on_record` 逐實例。
- RoomThread runtime key 改為完整實例身分。
- 修正 AI／客戶端 trigger order 的精確匹配。
- 舊 TriggerSkill 維持單次執行。

驗收：不同玩家的同名同 ID 不碰撞；各實例 usage/state 獨立。

### Ticket 7：伺服器快照、增量同步與 UI

依賴：Ticket 2、Ticket 3、Ticket 4。

範圍：

- 新增 metadata snapshot 與 acquire/detach 增量協定。
- 客戶端建立對應 SkillInstance metadata。
- 每實例建立獨立按鈕。
- 實作標籤、排序與動態刷新。
- 嚴格裁切來源 metadata；不傳 state。

驗收：初始化、遊戲中變動與重連後，伺服器／客戶端實例清單一致。

### Ticket 8：Card 與主動技能 instanceID

依賴：Ticket 2、Ticket 7。

範圍：

- Card／CardUseStruct／協定／重播加入 `skillInstanceID`。
- 按鈕把精確 ID 傳入 ViewAs／SkillCard。
- 伺服器驗證、legacy ID 0 fallback 及寫回。
- 確保卡牌複製、包裝及序列化保留 ID。

驗收：兩個同名按鈕可分別使用自己的 state；偽造 ID 被拒絕；舊 AI 可 fallback。

### Ticket 9：舊呼叫點人工審核、回歸測試與文件同步

依賴：前述 tickets。

範圍：

- 列出全部 `acquireSkill()` 呼叫點，由使用者人工判斷是否加入 `hasSkill()` 防重。
- 列出全部 EventAcquireSkill／EventLoseSkill 的 `data:toString()` 監聽者，審核逐實例重複事件語意。
- 更新 `docs/TriggerV2Skill系統說明.md` 中已過時的 Skill 物件 instanceID 說明。
- 更新 `docs/engine-correct-skills.md` 的 `InstanceStackable` 規則。
- 重新產生 SWIG wrapper。
- 執行完整 Release x64 編譯與遊戲測試。

## 5. 必測案例

| 編號 | 場景 | 預期 |
|---|---|---|
| 1 | 主將與副將具有同名技能 | 分配 #1、#2 |
| 2 | 連續後天獲得兩次 | 每次建立新實例 |
| 3 | 移除 #2 後再次獲得 | 使用更大的新 ID |
| 4 | `#hiddenSkill#2` | 正確解析 baseName 與 ID |
| 5 | 父技能兩實例 | helper 亦有兩個獨立實例與父鍵 |
| 6 | 移除一個父實例 | 只移除其 helper |
| 7 | 封禁 #1 | #2 仍有效 |
| 8 | 兩名玩家各持有 skill#1 | V2 計數互不影響 |
| 9 | `can_trigger` 回傳 baseName | 展開全部有效實例 |
| 10 | `can_trigger` 回傳精確 ID | 只建立指定 context |
| 11 | 舊 TriggerSkill 多實例 | 仍只執行一次 |
| 12 | 未標記數值被動多實例 | 只套用一次 |
| 13 | `InstanceStackable=true` | 按有效實例求和 |
| 14 | UI 由 1 實例變 2 實例 | 所有按鈕刷新並顯示 ID |
| 15 | UI 由 2 實例變 1 實例 | 剩餘按鈕隱藏 ID |
| 16 | 舊 AI 傳 skillInstanceID=0 | 解析最小有效實例並寫回 |
| 17 | CardUse 中途移除實例 | 卡牌效果繼續，state 回調跳過 |
| 18 | 重連 | metadata 與按鈕完全重建一致 |

## 6. 明確延後範圍

- EquipSkill 納入 Player SkillInstance；裝備繼續使用 card ID。
- 舊 TriggerSkill 逐實例遷移。
- 國戰所有技能類型與 related helper 的完整暗置有效性。
- 任意 instance state 的客戶端同步。

已確認的國戰最小規則只記錄、不阻塞本期：隱藏／暗置不重建實例；TriggerV2 cost/pay 成功後、`EventSkillInvoking` 前可明置來源。

## 7. 弱模型實作限制

- 一次只實作一個 ticket。
- 每票完成後先編譯，禁止累積多票錯誤。
- 禁止修改 `include/`、`lib/`。
- 禁止恢復 Skill 物件 instanceID 或 clone Skill QObject。
- 禁止以散落的 `indexOf('#')` 新增解析邏輯；只使用集中 helper。
- 禁止自行批量修改全部 `acquireSkill()` 呼叫點；先輸出清單供人工審核。
- 禁止順手重構無關程式。
- 無 git 記錄，開始每票前需記錄預計修改檔案，完成後列出實際修改檔案。

## 8. 編譯驗收

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe' 'L:\finaldebug\QSanguosha-v2\QSanguosha.sln' /t:Build /p:Configuration=Release /p:Platform=x64 /m /verbosity:minimal
```

預期輸出：

- `release\QSanguosha.exe`
- 0 個編譯錯誤
# 實作狀態（2026-07-16）

- Ticket 1–8：已完成，Release x64 編譯通過。
- Ticket 9：已完成全域呼叫點盤點、C++／Lua listener typed 遷移與文件同步；人工語義審核清單見 `docs/skill-instance-callsite-audit.md`。
- 尚待人工決策：未 guard 的舊 `acquireSkill()` 呼叫應逐點判定為 stack、ensure、replace 或 temporary，禁止批量加上 `hasSkill()`。
