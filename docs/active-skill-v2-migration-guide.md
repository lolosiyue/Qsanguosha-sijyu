# ViewAsSkillV2 舊技能遷移規範

## 1. 用途

本文件定義如何人工審議舊 `ViewAsSkill/SkillCard` 並改寫為 `ViewAsSkillV2`。核心重構不立即遷移任何正式技能；本文件只建立一致的分類、映射及驗收規範。

權威生命週期、身份與相容邊界見 [ViewAsSkillV2 重構計劃](active-skill-v2-refactor-plan.md)。

## 2. 遷移原則

- 一次只遷移一個技能家族，不做全域機械替換。
- 先記錄舊行為，再改 API；不能以「看起來等價」代替測試。
- root source、activation entry、initiator、final invoker 必須分別標明。
- 每個 choice、代價、效果及目標修改必須只歸屬一個階段。
- 新 V2 查詢與 `createCard()` 必須無副作用。
- 不用 Room Tag 保存 execution-local 資料。
- 舊 AI、日誌、翻譯、history key、response pattern 必須逐項審核。
- 舊 AI `@SkillCard` 可保留原具名類別及 metaobject，constructor 須提供精確技能名；伺服器會為命中 V2 的零實例 SkillCard 選取有效來源並走既有 `resolveActiveSkillRequest()` 重建，多來源沿用實例選擇。`historyKey()` 只控制計次，不參與技能解析。效果生成的普通牌不因 `skillName` 被當成另一次主動發動。
- 遇到混合 `validate/onUse` 時人工拆分，不要求橋接層猜測。

## 3. 遷移前盤點表

每個技能先建立下列表格：

| 項目 | 必填內容 |
|---|---|
| root skill | 名稱、owner、instance scope |
| activation skill | direct 或 attached；parentRef 規則 |
| 使用路徑 | play／response-use／response／nullification |
| 產生卡 | 普通卡或 custom action |
| 選牌 | 數量、區域、順序、重複、是否作代價 |
| 額外選擇 | Dialog／Player Tag／userString 格式 |
| 目標 | 普通 Card 規則或 custom target hooks |
| choice | 所有詢問及取消點 |
| pay | 棄牌、失去體力、標記、限定技 token 等 |
| effect | 全域、逐目標或整組目標 |
| validate | 是否有詢問、支付、效果、replacement |
| onUse | 是否呼叫基底；是否混合代價／效果／事件 |
| history | 舊 class name／Lua `#objectName` |
| priority | 舊 `getPriority()` override 與其值；是否依賴與 legacy 技能的相對順序（見 §7.3） |
| AI | 是否依 Player Tag、card class、source owner |
| logs/UI | skillName、語音、attachedlord provider |

## 4. 先判斷使用哪一種 V2 形式

### 4.1 產生普通卡

適用於技能只是把牌轉換成 Slash、Jink、Peach、Trick 等既有卡牌。

- `createCard()` 返回普通 Card。
- targetFilter、feasible、卡牌移動、offset、onEffect 由普通 Card 負責。
- `bypass_cost` 不免除普通卡 subcards。
- 不實作 V2 custom target hooks。

guhuo 選牌、宣告牌名重建與回應時的二次選擇，見
[以 s4_ganglu 為例的 V2 寫法](view-as-skill-v2-guhuo.md)。

### 4.2 通用 proxy custom action

適用於舊技能需要專屬 SkillCard effect，但不需要專屬 C++ Card 類型。

- `createCard()` 返回通用 `ActiveSkillCard`。
- NoTarget 或 SelectTargets 明確宣告。
- selected-card 預設由 V2 pay 原子支付。
- effect 使用 `effect`、`effectOnTarget` 或 `effectOnTargetGroup`。

### 4.3 暫不遷移

以下情況先標記並建立獨立設計票：

- 依賴自訂 Card RTTI/class name 的大量外部觸發器。
- validate/onUse 同時改寫多條卡牌流程且缺少回歸案例。
- UI 使用非標準多階段 Dialog，現有 `userString` 無法穩定重建。
- AI 嚴重依賴 Room/Player Tag 時序。

### 4.4 無 V2 對應的基底類別

下列舊基底類別目前沒有 V2 對應機制，衍生技能暫留 legacy，不納入遷移批次；確需遷移時先開獨立設計票（計數為 2026-09-25 稽核值，動工前以 `grep ': public <Base>'` 重查）：

| 舊基底類別 | 存量 | 備註 |
|---|---|---|
| `ProhibitSkill` | 約 59 | 無 V2 對應 |
| `CardLimitSkill` | 約 57 | 無 V2 對應 |
| `FilterSkill` | 約 18 | V2 無對應過濾層 |
| `ViewAsEquipSkill` | 約 7 | 與 `FilterSkill` 組合（如 doudizhu `RuyiBf`/`Ruyi`）無 V2 對應 |
| `DetachEffectSkill` | 約 18 | 無 V2 對應 |
| `InvaliditySkill` | 約 15 | V2 以精確 instance invalidity 取代，需逐一比對 |

判定流程：技能若只繼承上表基底而不含其他可遷移行為，直接標記暫留；若同時有可遷移的主動部分，只遷移主動部分並保留 legacy 被動層。

### 4.5 SkillCard::use／onEffect 的落點

舊 SkillCard 存量大（`ol.h` 122 類、`tenyear.h` 199 類等），多數帶 `targetFilter`、部分自訂 `onUse`。選型判準：

- `targetFilter`＋呼叫基底的 `use`＋`onEffect` 的標準形狀：優先走 §4.1（普通卡）或 §4.2（proxy custom action）；`targetFilter` 對應 proxy `canSelectTarget`。
- 自訂 `onUse`（混合代價／效果／手動觸發）：按 §6、§7 拆分；屬 monolithic 時先標記暫緩（見 §7.2 的標記實況）。
- `SkillCard::onEffect(CardEffectStruct &)`：逐目標效果對應 `effectOnTarget`，一次吃整組目標對應 `effectOnTargetGroup`；不屬於單一目標的部分進整體 `effect`。

## 5. 舊 API 到 V2 的映射

| 舊位置 | V2 位置 |
|---|---|
| `isEnabledAtPlay/isEnabledAtResponse/isEnabledAtNullification` | `canActivate(request)`，按 reason/pattern 分支 |
| `viewFilter` | `canSelectCard` |
| `viewAs` | `cardSelectionFeasible + createCard` |
| SkillCard `targetFilter` | proxy `canSelectTarget`；普通卡則不搬 |
| SkillCard `targetsFeasible` | proxy `targetsFeasible` |
| Dialog／Self Tag | 可沿用，結果放入 `userString` |
| 發動前可取消詢問 | `cost` |
| 實際棄牌／失去資源／移 token | `pay` |
| 全域 setup/effect | `effect` |
| `onEffect` | `effectOnTarget` |
| 一次處理完整 targets | `effectOnTargetGroup` |
| SkillCard class history | `historyKey()` override |

`CreateViewAsSkillV2` 不接受舊 `response_pattern`。一般回應與 `@@skill` 指名回應都由
`can_activate(skill, request)` 檢查 `request:getReason()`／`request:getPattern()`；client 在指名回應時會先解析
實際 activation instance，再呼叫同一 callback，不經 legacy `isAvailable()`。

### 5.1 裝備視為技入口

- 裝備視為技使用 `ViewAsSkillV2` 並保留 `isEquipSkill()`；`canActivate()` 仍須驗證
  `hasWeapon()`／`hasArmorEffect()`／`hasTreasure()` 等實際效果資格，不能只看分類。
- 無玩家技能實例的裝備入口使用名稱及 ID 0，該 `activationRef` 不是有效的技能實例。
  UI 沿用裝備牌上的點擊入口，不新增武將技能按鈕或虛構技能實例。
- 伺服器對 ID 0 的裝備提交（包含舊 AI 字串）重新驗證來源、材料持有及選牌規則，再以
  `createCard()` 重建卡牌。同名 `EquipSkillV2` 必須實際持有該視為技；來源檢查沿用其
  `prepareSource()`／`isSourceAvailable()`，不以卡名猜測其他技能。
- 實體裝備不產生武將技能來源；虛擬裝備保留授予技能的精確 `sourceRef`，沿用預亮、揭將
  及失效檢查。已存在的有效技能實例入口仍走原有實例驗證。裝備 activation 的來源準備
  由 `prepareEquipSource()`／`isEquipSourceAvailable()` 承接，不為裝備虛構 Player 技能實例。
- 朱雀羽扇只接受一張未使用中的普通殺，產生保留花色、點數與材料的火殺；支援出牌與
  回應使用，不支援純打出。`historyKey()` 保留 `FireSlash`，不另計羽扇使用次數。

### 5.2 精確借用來源

`Room::BorrowedSkillScope(room, player, skillName, parentRef)` 接受本次授予來源的精確 ref。
即使受詢者已有同名技能，回覆亦限定為該 parent 的 attached child；`activationRef()` 可查得它。
scope 保存並恢復外層 selector，只有本次新建的 child 才在退出時移除，不刪既有或外層附掛。
精確 ref overload 亦可附掛 TriggerSkillV2，交由 `triggerSkillSources()` 執行；純觸發技不設定視為技 selector。
伺服器、AI 與 UI 的正式入口呼叫 `canActivateRequest()`：先核對同步的內部 selector，再呼叫
技能覆寫的 `canActivate()`。selector 使用 `sys_` mark，避免引發遊戲的 MarkChange 事件；
舊零實例 SkillCard 亦遵守同一 selector，不能自行改選另一份實例。這不改變技能配額或材料規則。

### 5.3 三種 scope 的責任

| 資料 | 既有機制 | 邊界 |
|---|---|---|
| 每份技能的選擇、進度、授予 ref | instance state | 僅保存 primitive values；按規則明確清理，目前沒有通用自動到期 StateScope |
| 每階段、回合、輪或整場的使用額度 | `Skill::LimitScope` | 以 activation ref 計次；舊 `historyKey` 可保留相容用途，不再用共享 `hasUsed` 封鎖不同實例 |
| 已發生的用牌、移牌或實際傷害 | [Resolution History](resolution-history.md) | 明確選擇 phase/turn/round scope，檢查完整性與分頁；scope 0 或未知不等於零次 |

公開的持續效果／AI 相容標記可保留。先確認 journal 已涵蓋原規則需要的事實與時間點，
不可把尚未記錄的事件或當前待結算傷害當作完整歷史。

### 5.4 宣言的精確實例

按實例限制可宣言牌名時，覆寫 `allowDeclaration(const ActiveSkillRequest &, const QString &)`，
由 `request.activationRef` 讀取該份技能的 state／quota。`canDeclare()`、`usableNames()` 及
宣言視窗共用此掛鉤；舊 `allowDeclaration(const Player *, ...)` 預設仍由新版掛鉤轉呼叫。
`SkillDeclarationSession` 的尾參數可接收 `activationRef`，原生 guhuo/juguan/tiansuan 視窗與
共用 client session 傳入所選實例，快取亦按 ref 區分。借用請求沿用其精確 selector。
未選定實例的初始建構可能仍帶空 ref，實例型規則不得把它猜成第一份同名技能。

### 5.5 舊 TriggerSkill 子類 → TriggerSkillV2

| 舊基底類別 | 隱含事件 | 舊 callback | V2 對應 |
|---|---|---|---|
| `PhaseChangeSkill` | `EventPhaseStart` | `onPhaseChange(player, room)`（返回 true 中斷事件） | `events << EventPhaseStart`；`triggerable()` 篩 `player->getPhase()`；詢問進 `cost`、效果進 `effect` |
| `MasochismSkill` | `Damaged` | `onDamaged(target, damage)` | `events << Damaged`；`DamageStruct` 由 `ctx.original_data` 讀取 |
| `DrawCardsSkill` | `DrawNCards` | `getDrawNum(player, n)` | 見 §5.6 |
| `GameStartSkill` | `GameStart` | `onGameStart(player)` | `events << GameStart`；setup 進 `effect`，需詢問時進 `cost` |
| `RetrialSkill` | `AskForRetrial` | `onRetrial(player, judge)` | `events << AskForRetrial`；改判寫回 `JudgeStruct` |

已依此收斂的參考遷移：`MasochismSkill→Jianxiong/Yiji/Fankui`、`DrawCardsSkill→Tuxi/NosLuoyi/NosYingzi`、`PhaseChangeSkill→YijiObtain/Qinxue/Wangzun`、`RetrialSkill→Guicai/NosGuicai`、`GameStartSkill→MTLiaoshiChoose` 等（以現行 `src/package` 為準）。

舊基底 `triggerable` 預設檢查 `target == owner && owner->isAlive() && owner->hasSkill(objectName())`；V2 `triggerable()` 須保留同樣條件，並回傳 `TriggerList{{owner, {objectName()}}}`。

### 5.6 DrawCardsSkill 專節

舊 `getDrawNum(player, n)` 允許在回傳前做互動查詢（`askForPlayerChosen`、`showDrawPile` 等，如 bgm `Zhaolie`、olwenwu `JinHuishi`），也可回傳負數表示減少摸牌。V2 改寫契約：

- `events << DrawNCards`；`triggerable()` 篩 `DrawStruct.reason`（摸牌階段為 `"draw_phase"`；初始手牌為 `"InitialHandCards"`，對應舊 `is_initial`）。
- 詢問放 `cost()`，結果存 `ctx.extra_data`。
- `effect()`（常配合 `ctx.manual_effect = true` 的 `effectTarget`）從 `*ctx.original_data` 取出 `DrawStruct`，改寫 `draw.num` 後寫回 `*ctx.original_data`；資料由框架帶回 `AfterDrawNCards` 一側的既有流程。
- 舊技能有自訂 `getPriority()` 或依賴事件內相對順序時，另 override `usesEventPriority()` + `getPriority()`（見 §7.3；如 Tuxi 保留 `return 1`）。

參考實作：`src/package/standard-generals.cpp` 的 `Tuxi`、`src/package/maotu.cpp` 的 `MTWeiqie`。

### 5.7 覺醒技（Wake）

舊覺醒技依賴 `Skill::Wake` frequency，由引擎在 legacy 迴圈檢查 `canWake()`。該呼叫會消耗覺醒 grant 並記 log，遷移後語意改變：

- `triggerable()` **不得**呼叫 `ServerPlayer::canWake()`；只檢查 grant tag（如 `hasWakeGrant()` 讀 `<skill>_SKILLCANWAKE`）與自身條件。
- grant 的消耗移到結算時（`effect()` 執行覺醒、`changeMaxHpForAwakenSkill`、`acquireSkill` 一併處理），不再依賴 legacy 迴圈的 `canWake()` 副作用。
- 引擎對 legacy Wake 的 `canWake()` 檢查只存在於 legacy 派送迴圈，V2 不經過。

參考實作：`src/package/sp.cpp` 的 Zhiri 等已遷移覺醒技。

### 5.8 C++ 宣告式選牌 API（guhuo 類）

guhuo／juguan／tiansuan 類宣告式技能在 C++ 端有完整 API（`src/core/skill.h` 的 `ViewAsSkillV2`），不必把這類技能導向 Lua：

| hook | 責任 |
|---|---|
| `declarationDialog()` | 回傳技能（或其 owner）的宣告框資訊，決定基本牌／錦囊宣告 UI |
| `declaresCardName()`／`declaresByDialog(reason)` | 是否以宣告牌名／宣告框為準；回應按 pattern 取名時 `declaresByDialog` 為 false |
| `usableNames(request)` | 列出允許宣告的牌名（套用 dialog 過濾與禁用） |
| `allowDeclaration(request, name)` | 逐實例宣言限制（見 §5.4） |
| `buildCard(request, name)` | 由接受的牌名建卡；預設以選中材料 clone 並掛技能名，proxy 卡覆寫 |
| `declarationReason(...)` | 宣告理由分類 |

同一組規則同時餵 client 宣告框、預設 `createCard()`、回應時預設 `cost()` 的二次選擇與預設 `historyKey()`；宣告框外或 pattern 外的宣言不會變成卡牌。Lua 端對應 `guhuo_type`（只決定宣告框樣式）與 factory 的 `n`／`setN()`（固定選牌張數）：宣告框樣式與選牌數分開設定，互不推導。

## 6. validate 人工分類

`validate()/validateInResponse()` 必須逐行分成以下類別：

| 類別 | 遷移位置 |
|---|---|
| 純卡牌建立／replacement | `createCard`，保持無副作用 |
| 詢問選擇，可取消 | `cost` |
| 棄牌、扣標記、失去體力、限定 token | `pay` |
| 對其他玩家或遊戲狀態的結果 | `effect` |

規則：

- 不保留「validate 先做副作用，再期待 WillInvoke 可撤銷」的舊結構。
- `createCard` 不得 ask、move、mark、log、random。
- 無法安全拆分時標記 `LegacyValidateLimited`，暫留舊技能（該標記目前無引擎實作，`src/` 零命中，僅作 ticket 分類）。
- validate replacement 不建立第二 execution；sourceRef／activationRef 保持原值。

## 7. onUse 人工分類

### 7.1 呼叫基底的 preprocessor

例如先整理 target list／設定純資料，再呼叫 `SkillCard::onUse()`：

- 純 target 轉換移到 `canSelectTarget/targetsFeasible` 或 TargetConfirming 規則。
- execution-local setup 移到 `effect`。
- 不再靠自訂 onUse 包裹 CardUsed。

### 7.2 monolithic onUse

若 onUse 內直接 loseHp、giveCard、askForChoice、acquireSkill 等：

- 詢問移到 cost。
- 真正支付移到 pay。
- 遊戲結果移到 effect／target effect。
- 原有 PreCardUsed/CardUsed/CardFinished 手動觸發全部刪除，交由引擎生命週期。
- 遷移完成前標記 `LegacyOnUseLimited`。注意：引擎側的 `LegacyOnUseLimited` 是 Card 動態
  property（`Room` 在 EventSkillEffect 攔截自訂 onUse 時讀取，`src/server/room.cpp`），
  不是技能標記，且目前沒有任何 package 設定它；遷移 ticket 中的「標記」指人工分類，
  不是可設定的技能介面。

橋接層攔截 monolithic onUse 時只保證整段跳過且不閃退，不保證代價、CardUsed 或移牌語意。

### 7.3 recordEvent 與事件優先序（遷移保命機制）

`TriggerSkillV2` 派送層有兩個舊 `TriggerSkill` 介面沒有的鉤子，決定觸發時序與記錄清理，遷移前必須盤點：

**兩段式派送**（`RoomThread::trigger()`）：

1. 所有 V2 技能先於 legacy 執行；V2 內部按 `getPriority()` 排序。
2. legacy 迴圈按 `sortTriggerSkills()`（`getPriority()`＋座位序）執行；純 V2 技能在該迴圈被跳過。
3. `usesEventPriority()` 回傳 true 的 V2 技能（裝備技 `isEquipSkill()` 自動視同 true）改在
   legacy 迴圈中按 `getPriority()` 分組、插回原 legacy 位置執行。

因此遷移一個技能就會改變它在同一事件內相對其他技能的執行順序。舊技能有自訂
`getPriority()`（或依賴與 legacy 技能的相對順序）時，必須 override `usesEventPriority()`
並保留原 `getPriority()` 值（如 Tuxi `return 1`、YijiObtain `return 4`）。

**recordEvent()**：record 階段每個 V2 定義、每個事件只呼叫一次（不逐 owner 實例）；
回傳 true 即由技能自行完成本次事件的記錄／收牌，框架跳過逐實例 `record()`。
適用於「事件發生即收牌」類行為（如 YijiObtain 於摸牌階段開始時收 `yiji` 澤）。
不覆寫時，`record()` 對每個現存玩家的每個實例各得一次完整 context。

### 7.4 onUse→use 資料傳遞替代

舊 SkillCard 常以 Room Tag 在 `onUse()` 與 `use()`／`onEffect()` 之間傳遞
CardUseStruct（如 jianshu 遷移前的 `JianshuCard::onUse` 以 `setTag` 存、`use()` 讀回；
倉內 `setTag` 存量以百計）。V2 不需要：execution-local 資料放 `ctx.extra_data`
（`cost`／`pay` 寫、`effect*` 讀），逐目標續接用 `effectOnTarget`／`effectOnTargetGroup`
直接讀 `ctx.use_card`／`ctx.targets`。遷移時逐處把 Tag 讀寫改為 ctx 欄位，
不得把 Room Tag 帶進新技能（§2 既有禁令）。

## 8. cost 與 pay 判定法

使用下列問題分類：

1. 玩家拒絕後是否應視為從未正式發動？是：`cost`。
2. 操作完成後是否不可回滾，且效果被攔截仍應保留？是：`pay`。
3. 是否只是選中的普通轉換卡本身？是：交給普通 Card 管線，不放入 V2 pay。
4. 是否是 proxy selected cards？預設由 pay 原子處理。

pay 返回 false 不回滾部分副作用；技能應盡量先完整驗證再一次提交。

## 9. 目標效果選型

| 需求 | 選擇 |
|---|---|
| 每個目標獨立效果與攔截 | `EachTarget + effectOnTarget` |
| 全部有效目標一次計算 | `WholeTargetGroup + effectOnTargetGroup` |
| 不以角色為標準效果目標 | `NoTarget + effect` |
| 包含死亡角色／復活 | 不走標準 EffectTarget；在整體 effect 特別處理 |

注意：

- WholeTargetGroup 收到的列表已排除死亡及被 EffectTarget 取消的位置。
- 保留原順序與重複角色。
- 部分目標被取消後不重跑 feasible。
- TargetConfirming 的遊戲覆寫不重跑 distance/prohibit/filter。

## 10. Attached skill 遷移

每個 attached 家族必須在 Package registry 登記：

```text
rootSkillName
activationSkillName
receiver rule
optional displayGeneralName resolver
```

禁止：

- 從 `_attach` 名稱截字推 root。
- 找第一個同名 lord／provider。
- 把 activation instance 當成 root source。
- 只保存 parent instanceID 而沒有 parent owner。

測試至少包含兩個同名 root provider、同一 receiver 同時獲得兩個 activation instance、移除其中一個 root 只移除其 child。

使用次數預設綁定實際 activation entry。若同一 root 派生的多個 attached 入口必須共用配額，
C++ 技能覆寫 `getUsageRef(ctx)` 並回傳 `ctx.sourceRef`；Lua 技能提供純查詢
`get_usage_ref = function(skill, ctx) return ctx:getSourceRef() end`。回傳無效引用時核心
fail-closed，不得自行退回 activation 或依 invoker 猜測 root。

## 11. 委託 invoker 遷移

技能若允許 A 強制 B 使用：

- 在 WillInvoke 攔截階段設定 updated invoker。
- A 保持 initiator／activation quota owner／預設副牌 payer。
- B 成為 CardUsed/CardResponded/history/effect source。
- 若其他代價由 B 支付，custom pay 必須明寫。
- 不依賴 B 擁有該技能。
- 不在委託後重跑 targetFilter/feasible。

## 12. Lua 遷移

- 使用 `sgs.CreateViewAsSkillV2`，不再同時建立 per-skill LuaSkillCard，除非技能仍暫留 legacy。
- 固定選牌張數直接設定 `n`；省略選牌 callback 時，框架預設要求恰好 `n` 張。
- 需要特殊牌堆時設定 `expand_pile`；裸名稱、`#reason`、`%pile`、`/CardClass/label` 分別表示
  自己的牌堆、`notifyMoveToPile` 偽牌堆、其他存活角色同名牌堆、其他存活角色指定類別裝備。
  伺服器會按相同規則重建合法選牌集合，通用 proxy 的預設 `pay` 亦允許並棄置這些牌。
- 自訂 `can_select_card` 需要辨識展開來源時，使用
  `skill:getExpandPileCardIds(request:getInitiator())`，避免直接以 server 端不存在的 `#reason` 實體牌堆判斷。
- 展開牌不是棄置代價時，設定 `will_throw_selected_cards = false` 並自行實作 `pay`。
- Lua 效果 callback 使用 `on_effect`、`on_effect_target`、`on_effect_target_group`；不保留舊的 `effect*` 名稱。
- 需要自行逐目標派發時，在 `on_effect` 設定 `ctx.manual_effect = true`，並呼叫
  `skill:skillEffect(ctx, target)`。
- request 只讀；不要嘗試修改 selected cards/targets。
- userString 必須驗證允許值，不直接把任意字串傳入 cloneCard。
- nil effect result 等同 ContinueEffects。
- 可用 `base_amount` 設定基礎數值；在 cost/pay/effect 以 `skill:getEffectiveAmount(ctx)` 讀取，讓 `EventSkillWillInvoke` 對 `ctx.modified_amount` 的修改生效。
- 不需設定使用次數來源時省略 `get_usage_ref`；預設按 activation instance 計數。
- 不以 Lua 全域變數或 Room Tag 保存 execution-local 狀態；使用 `ctx.extra_data`。

## 13. History、日誌與 AI

遷移前搜尋：

```text
hasUsed
usedTimes
getClassName
inherits("<OldCard>")
getSkillName
card->toString
AI use_func / ai_skill_use_func
```

- 若外部程式依賴舊 SkillCard history key，覆寫 `historyKey()`。
- `hasIndependentAIConversion()` 是 opt-in AI 契約：宣稱後 AI 可獨立提交恰好 `getN()` 張
  手牌的轉化（`canSelectCard` 對空選集也要接受、每次選擇產生相同卡身分與目標規則、
  無花色／點數繼承、裝備代價或選牌副作用）。原註釋要求 contract test；測試套件已移除，
  改以 headless 對局與 `~test` 合成技能回歸滿足（見 §16）。
- 一般轉化卡（非 `Card::TypeSkill`）的 history key 記卡牌類名，**不記**技能 key：只有真正的
  SkillCard／proxy 卡才用 activation skill 的 `#<name>Card`（引擎在 `Room::useCardInternal`
  按 `getTypeId() == Card::TypeSkill` 分流）。把普通轉化卡記在技能 key 下會繞過原生每回合
  限制（如諸葛連弩次數）。
- `skillName` 只作效果／日誌歸因；root source 使用 sourceRef。
- 舊 AI 未遷移時仍可透過 base-name fallback，但 source-sensitive attached 技能必須另審。
- 不為了通過 AI 而把 instanceID 再塞入技能名稱字串。

### 13.1 通用 AIRequest／AIResult 與 VM 遷移

- 新 AI 邊界只使用 value-only `AIRequest`／`AIResult`。`activate` 與 `askForUseCard`
  共用同一 gate；`AIRequest::DecisionKind { Activate, UseCard }` 區分決策種類，
  `AIResult::ActionKind { Pass, UseCard }` 僅兩值——出牌與回應共用 `UseCard`、取消為
  `Pass`；`AIResult` 在提交前轉成 `CardActionSpec`，由 Room 驗證 result 回送同一 request
  的 revision、牌／目標 ID 與 quota。權威 gameplay revision ledger 已接入：僅權威狀態變更
  （`CardsMoved`／`PlayerPropertyChanged`）經 `RoomRuntime::advanceStateRevision()` 推進
  （見 src/server/room-runtime.h、card-movement-service.cpp、player-state-service.cpp），
  純 request/query 不得推進 revision。
- ActiveSkillV2 的 activation/source identity 與 quota 僅作
  `AIRequest.skillActionContext`（型別 `AiSkillActionContext`，見 src/server/ai.h），
  不可另建技能專用 request/result，也不可把 instance ID 編碼進技能名稱或舊字串。
- 每個 Room 的 `AiLuaRuntime` 與 Gameplay Lua VM 分離，兩者均由 `RoomThread` 同步執行。
  Isolated handler 取得 value-only request（含完整 `AIWorldView`，經
  `AIRequest::worldView` 攜帶，由 src/server/ai-runtime.cpp 的 `pushAIWorldView()` 填入）、
  `AiData` 與 decision-scoped `AiRng`；未遷移 legacy AI 仍留在 Gameplay VM。
- `LegacyDirect` 僅供過渡；`LegacyAdapted` 將舊 `activate`／`askForUseCard` 結果複製成
  `AIResult`，再走通用 Room 驗證 gate。
- 第一階段 `Isolated Shadow` 以同一 request 與獨立 deterministic `AiRng` 計算，只產生
  bounded audit／差異結果，不改變正式 gameplay。遷移期間允許同一 Room 按
  decision/callback 混用 `LegacyAdapted` 與 `Isolated`，並共享 C++ `AiDataStore`。
- `RoomThread` 在同一同步 gate 完成 request 建立、AI callback、result 驗證及
  `Room::useCard`／response resolver 提交；Shadow 不得回寫 Room、持鎖等待其他執行緒或
  在 callback 返回後提交卡牌。

## 14. 單技能遷移 ticket 模板

```markdown
### <技能家族> 遷移

依賴：ViewAsSkillV2 Ticket 1–12。

舊行為：
- 使用路徑：
- root/activation：
- selected cards／targets／userString：
- validate/onUse 分類：
- history/AI/log dependencies：

V2 映射：
- canActivate：
- card selection/createCard：
- target mode：
- cost：
- pay：
- effect mode：
- historyKey：

不處理：
-

驗收：
- Play：
- Response-use：
- Pure response：
- Nullification：
- 多實例：
- Effect intercept：
- AI：
```

## 15. Code review checklist

- [ ] 沒有在 Skill QObject 保存玩家 instanceID。
- [ ] request 查詢與 createCard 無副作用、無亂數。
- [ ] server 重建卡牌，不信任 client card class/source。
- [ ] sourceRef 與 activationRef 沒有混用。
- [ ] initiator 與 delegated invoker 的代價、歷史、效果歸因明確。
- [ ] selected-card payment 不會部分丟棄。
- [ ] 普通轉換卡沒有因 bypass 變免費。
- [ ] target override 沒有被引擎擅自重跑合法性。
- [ ] EffectTarget 位於普通 nullify/offset 後。
- [ ] 死亡目標沒有進入標準 target effect。
- [ ] 重複目標與 targetIndex 行為有測試。
- [ ] validate/onUse 副作用已分類，沒有假裝橋接可回滾。
- [ ] history key、AI、翻譯、語音及 attached 圖示已審。
- [ ] C++／Lua／四種使用路徑按實際適用範圍測試。
- [ ] 沒有修改第三方庫或手改 SWIG 生成檔。

## 16. 當前狀態

- 正式技能已按批次遷移：`src/package` 現有數百個 `TriggerSkillV2`／`ViewAsSkillV2`
  （數值隨批次成長，動工前重查）；CorrectSkillV2 系亦有正式技能
  （如 `Paoxiao : public TargetModSkillV2`）。
- 本文件為遷移規範；每批遷移依 §14 ticket 模板建立，先記錄舊行為再改 API。
- 遷移驗證走 [`tools/autotest/headless_runner.py`](../tools/autotest/headless_runner.py)
  的 headless 對局（倉庫已無 CTest，2026-09-25 移除），並可加入 `~test` 合成技能。

