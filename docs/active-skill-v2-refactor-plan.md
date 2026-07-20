# ActiveSkillV2 與 SkillCard／ViewAsSkill 現代化重構計劃

## 1. 文件定位

本文件是 `ActiveSkillV2`、舊 `SkillCard/ViewAsSkill` 過渡橋接及相關多實例整合的權威設計契約與分批實作票據。

程式基線以 `H:\Program file\Game\sgs\Qsgs\github\QSanguosha-v2` 的 `main` 分支為準。`L:\finaldebug\QSanguosha-v2` 是較早的 `view_as_skillV2` 先行實驗分支，只可用來理解舊構想，不得反向覆蓋 main 的較新多實例實作。

依賴文件：

- [玩家技能多實例重構計劃](skill-instance-refactor-plan.md)
- [TriggerV2Skill 系統說明](TriggerV2Skill系統說明.md)
- [SkillCard V2 Bridge 舊構想](SkillCard-V2Bridge計劃.md)
- [ActiveSkillV2 舊技能遷移規範](active-skill-v2-migration-guide.md)
- [ActiveSkillV2 驗證矩陣](active-skill-v2-test-matrix.md)

舊 Bridge 文件只作需求來源，不是實作規格。本文件的鎖定規則優先。

## 2. 目標與非目標

### 2.1 目標

- 建立雙層漸進改造：底層橋接舊技能，上層提供新的 `ActiveSkillV2` 作者 API。
- 舊 `SkillCard/ViewAsSkill` 不必立即改寫，也不設定移除期限。
- 所有由技能產生卡牌的路徑均可進入既有七個 `EventSkill*` 攔截時機：出牌、response-use、純 response、nullification。
- 精確區分技能根來源、玩家實際點擊的 activation、最初發動者及攔截後的最終使用者。
- 支援同一技能多實例、跨玩家 attached skill、巢狀使用及重入。
- 讓新 V2 技能不再必須建立每技能一個 `SkillCard` 類別。
- 保持伺服器權威；客戶端只作 UI 預測。
- 提供 C++ 與 Lua 作者介面、遷移規範、測試技能及可逐票編譯的實作路線。

### 2.2 非目標

- 不批量遷移任何正式技能。
- 不建立新舊二進位客戶端互連；專案原本已限制跨版本連線。
- 不移除或加入 `Q_DECL_DEPRECATED` 至舊 API。
- 不為舊技能的任意 `validate()`／自訂 `onUse()` 自動推斷代價與效果。
- 不建立完整自動化 `Room` 整合測試框架。
- 不改動 `include/`、`lib/` 內第三方庫。
- 不以動態裁切武將全圖取代 `attachedlord/*.png` 素材。

## 3. 核心術語與身份模型

### 3.1 定義物件與持有實例

- Engine 中每個技能名稱仍只有一個共享 `Skill/ViewAsSkill/ActiveSkillV2` 定義物件。
- `instanceID` 屬於玩家持有關係 `SkillInstance`，不屬於共享 QObject。
- 新 API 不 clone 技能定義物件。

### 3.2 SkillInstanceRef

跨玩家關係不得只用 `SkillInstanceKey`。目標型別：

```cpp
struct SkillInstanceRef {
    QString ownerObjectName;
    SkillInstanceKey key; // skillName + instanceID
};
```

`SkillInstance::parent` 升級為 `SkillInstanceRef parentRef`。舊快照沒有 parent owner 時，僅為相容而預設為目前 holder。

### 3.3 四個不可混用的角色

| 名稱 | 語意 | 可否在執行中改變 |
|---|---|---|
| `sourceRef` | 技能根來源實例 | 否 |
| `activationRef` | 玩家實際點擊的技能入口實例 | 否 |
| `initiator` | 提交發動、完成選牌的玩家 A | 否 |
| `invoker` | 最終被視為使用卡牌／技能的玩家 | 只可在 `EventSkillWillInvoke` 提交一次 |

直接技能通常 `sourceRef == activationRef`。attached skill 則不同。

黃天範例：

```text
張角持有 huangtian#N
其他玩家持有 huangtian_attach#M
huangtian_attach#M.parentRef = 張角/huangtian#N

sourceRef     = 張角/huangtian#N
activationRef = 出牌者/huangtian_attach#M
initiator     = 出牌者
invoker       = 出牌者，除非 WillInvoke 攔截器委託給 B
```

### 3.4 Card 欄位分工

| 欄位 | 權威語意 |
|---|---|
| `skillName` | 卡牌效果與日誌歸因 |
| `sourceSkillName + skillInstanceID` | 根來源技能名稱與 root instance ID |
| `activationSkillName + activationInstanceID` | 玩家點擊的入口技能與 instance ID |

舊欄位保留；不可再用「虛擬牌且 `skillName` 非空」作唯一來源推斷。

## 4. Attached skill 多實例規則

### 4.1 來源類型與登記表

- 新增 `SourceAttached`，不可假裝成 `SourceAcquired` 或 `SourceHelper`。
- Package 以獨立 registry 明確登記 `root skill -> activation skill`。
- 不以 `_attach`、`_attach2` 命名慣例或 `related_skills` 推斷。
- registry 可選填 `displayGeneralName` resolver；沒有覆寫時由 root instance 的 provider 決定。

### 4.2 自動建立與移除

- 每個 root instance 對每個符合條件的 receiver 建立一個獨立 activation instance。
- 唯一鍵是 `(receiver, activationSkillName, parentRef)`。
- `Room::attachSkillToPlayer()` 對已登記關係自動精確展開，且同一 parentRef 重複呼叫必須冪等。
- 未登記關係沿用 legacy fallback。
- 舊 detach-by-name 只移除該登記 activation 名稱的全部 `SourceAttached`，不得移除其他來源技能。
- 移除 root 時先快照所有 children，再移除／通知 root，最後按 seat/key 決定性遞迴移除 children。
- 建立 parentRef 時必須確認 parent 存在且不形成循環。

### 4.3 執行中移除

一旦 invocation 正式開始，root／activation 後續失效或被移除，不取消已建立的 execution。execution 使用開始時的身份快照完成結算。

## 5. ActiveSkillRequest 與 ActiveSkillExecution

### 5.1 ActiveSkillRequest

`ActiveSkillRequest` 是客戶端 UI 與伺服器最終驗證共用的只讀發動申請，不是執行狀態。

目標欄位：

```cpp
struct ActiveSkillRequest {
    CardUseStruct::CardUseReason reason;
    QString pattern;
    const Player *initiator;
    SkillInstanceRef activationRef;
    QList<int> selectedCardIds;
    QStringList selectedTargetNames;
    QString userString;
};
```

規則：

- 共用型別只暴露 `const Player*`／只讀 getter，不暴露 `Room` 修改能力。
- request 只保存牌 ID，不同時保存另一份權威卡牌指標列表。
- 可提供 `selectedCards()`／`selectedTargets()` 便利解析函式，但解析結果不是第二份狀態。
- 不新增 `requestParameters/QVariantMap`；額外模式繼續使用現有 Player Tag、Dialog Tag 與 `userString`。
- `userString` 是不可信的客戶端輸入，由 `createCard()` 解析；無效時返回 `nullptr`。
- request 不接受客戶端提供 sourceRef、invoker 或 executionID。

### 5.2 ActiveSkillExecution

`ActiveSkillExecution` 由 Room 擁有，代表一次已接受並正在結算的發動。

必要資料：

- Room 單調遞增、永不在本局重用的 `executionID`。
- sourceRef、activationRef、initiator、目前 invoker。
- 已鎖定 request 快照及權威 CardUse/CardResponse backing `QVariant`。
- 擴充後的 `SkillContext`。
- request targets、confirmed targets、effective targets。
- 配額 reservation／commit 狀態與最終結果。
- 是否已發出 `EffectFinished`。

Room 以 registry 依 executionID 查找 execution。使用 RAII 保證巢狀與例外收束；不得用 Room Tag 或卡牌指標作 execution 唯一鍵。

`EffectFinished` callbacks 返回後立即從 registry 移除 execution。SkillContext、original_data 或 execution pointer 均不得被技能保存到非同步工作或下一次事件。

### 5.3 executionID 傳播

- `CardUseStruct`、`CardResponseStruct`、`CardEffectStruct` 增加僅伺服器使用的 `skillExecutionID`。
- ID 隨內部呼叫鏈複製，以便 `CardEffect` 找回正確 execution。
- 不由客戶端提交，不進跨版本協議，不參與重播判定。
- 可寫入診斷／重播除錯資料，但播放不能依賴它。

## 6. 擴充 SkillContext

不建立平行的 `ActiveSkillContext`。擴充現有 `SkillContext`，由 `ActiveSkillExecution` 擁有其生命週期。

### 6.1 權威身份

- `sourceRef`：root 身份，Active 流程的權威來源。
- `activationRef`：點擊入口身份。
- 既有 `owner/skill_name/instanceID` 作 legacy mirror，不再單獨決定 Active 身份。
- `initiator` 與 `invoker` 同時保留。

### 6.2 資料分區

| 分區 | 用途 |
|---|---|
| `choice/extra_data` | 來源技能自己的 execution-local 資料 |
| `interceptor_data` | 依攔截器 owner/name/instance 命名空間隔離的資料 |
| mutation fields | `is_canceled`、`bypass_cost`、`updated_invoker/card/targets` |

不得使用 Room Tag 保存 Active execution context。`original_data` 指向 execution 擁有的 backing `QVariant`，不可指向暫時區域變數。

### 6.3 修改提交點

欄位採軟規範，不因寫錯時機 assert／abort。引擎只在固定 checkpoint 讀取：

| Event | 讀取修改 |
|---|---|
| `EventSkillWillInvoke` | cancel、updated invoker、updated card |
| `EventSkillPay` | bypass_cost |
| `EventSkillTargetConfirming` | updated targets |
| `EventSkillEffect` | whole-effect cancel |
| 每次 `EventSkillEffectTarget` | 本次 target cancel |

checkpoint 之後才寫入的欄位不追溯生效。

### 6.4 多攔截器

- 同一時機的所有攔截器仍按 TriggerV2 既定順序執行。
- 前一攔截器的修改立即對下一個可見。
- 後寫覆蓋前寫，包括解除先前的 cancel／bypass。
- 引擎在時機結束後提交最終值。
- 診斷記錄每次修改者及最終提交者。
- `EventSkillEffectTarget` 的 cancel 每個列表位置重設，不跨目標保留；`interceptor_data` 可跨目標保留。

## 7. 權威生命週期

### 7.1 共用主流程

```text
1. 解析 activation/source、檢查 initiator 持有有效 activation 與 parent/root
2. 檢查 Card/使用者基本限制
3. 舊卡先執行 validate()/validateInResponse()；返回 null 即結束，沒有 execution
4. 建立 ActiveSkillExecution 與 SkillContext
5. ActiveSkillV2::cost()；false 即取消，沒有 EffectFinished
6. EventSkillWillInvoke
7. 若 cancel：取消，沒有 pay/history/effect/EffectFinished
8. 提交 updated_invoker、updated_card
9. ActiveSkillV2 reserve activation/source quota；舊技能略過
10. EventSkillPay
11. 若非 bypass_cost，執行 ActiveSkillV2::pay()
12. pay false：release reservation，不回滾，結果 PayFailed，發出 EffectFinished
13. pay/bypass 成功後 commit quota；EventSkillTargetConfirming 並提交 targets
14. EventSkillInvoking
15. EventSkillEffect
16. 進入正常 CardUse/CardResponse 管線或 V2 proxy effect
17. 對真正將執行的存活目標觸發 EventSkillEffectTarget
18. 原有 CardFinished/PostCardResponded 完成
19. EventSkillEffectFinished，恰好一次
```

`validate()` 必須保持在 V2 WillInvoke 之前。舊 validate 返回另一張卡時，sourceRef／activationRef 鎖定，不建立第二條 lifecycle。

### 7.2 validate replacement 歸因

- replacement card 的 `skillName` 為空時繼承原卡。
- replacement card 明確設定新 `skillName` 時保留其效果／日誌歸因。
- 伺服器重寫原有 sourceSkillName/root ID，保留 activationRef。
- 不建立 provenance chain；一次提交只有一個 execution。
- 只有 replacement 明確再呼叫 `Room::useCard()` 才建立巢狀 execution。

### 7.3 委託 invoker

在 `EventSkillWillInvoke` 可把 A 選好的技能強制交由 B 使用：

- initiator A 永不改變。
- B 必須仍在 Room、存活，並通過硬性的 `isCardLimited`。
- B 可以不持有 source／activation 技能。
- 不重新檢查 B 的 ViewAs、技能所有權、使用配額、距離、prohibit 或目標條件。
- selected cards 仍是 A 的選擇；改變 invoker 不轉移或重選副牌。
- B 承擔最終 CardUsed/CardResponded、普通卡歷史、移動理由中的使用者及效果來源。
- V2 activation 配額仍屬 A 的 activation instance，除非技能宣告 `SourceInstance` scope。
- 預設副牌代價由 A 支付；要求 B 支付自己資源的技能必須覆寫 `pay()`。

### 7.4 updated_card

- 只建議在 WillInvoke 修改並於該 checkpoint 提交一次。
- sourceRef、activationRef、initiator 不因換卡而改變。
- 檢查卡牌指標、subcard ID 結構及最終 invoker 的硬性 card limitation。
- 不重新跑來源技能所有權、ViewAs、使用配額、距離、prohibit、targetFilter 或 feasible。
- pay 之後不再用 Active mutation 換卡；後續沿用普通 `CardUseStruct` 的既有變換。

## 8. 結果與中斷

### 8.1 SkillExecutionResult

第一版只需要：

```text
NoResult
Completed
EffectSkipped
PayFailed
InvalidTargetUpdate
```

不增加 `ScriptError` 或 `Interrupted`。

- Lua `pay` 出錯：記錄錯誤並作 `PayFailed`。
- Lua effect 出錯：記錄錯誤、停止剩餘效果並作 `EffectSkipped`。
- precommit 驗證失敗只寫診斷，不發 `EffectFinished`。

### 8.2 StageChange／TurnBroken

對齊現有 `Room::useCard()`：

- 先由原有 catch 清理桌面卡並補發 `CardFinished`。
- Active execution 再發 `EffectFinished(NoResult)`，最多一次。
- 不回滾支付、歷史或已發生效果。
- 最後重新拋出原有控制事件。

### 8.3 玩家死亡

`pay()` 成功後，即使 initiator 或最終 invoker 死亡，引擎也不自動取消效果。技能需要存活語意時自行在 `pay/effect` 返回或停止。

## 9. 舊版橋接邊界

### 9.1 識別

優先使用 CardUse/CardResponse 中的顯式 source／activation。只有缺欄位的舊流程才做安全 legacy inference；解析一次後鎖定。禁止僅依 `isVirtualCard() && !skillName.isEmpty()` 推斷。

### 9.2 legacy validate

舊 `validate()/validateInResponse()` 可能混合詢問、失去資源、換牌及效果。橋接層不猜測：

- validate 仍先執行。
- 返回 null 不建立 execution。
- WillInvoke 不能撤銷 validate 已發生副作用。
- bypass_cost 不能跳過 validate 內的代價。
- 這類技能標記 `LegacyValidateLimited`，由人工遷移。

### 9.3 legacy onUse

標準 `Card::onUse -> CardUsed -> use/onEffect` 的卡可保留 CardUsed/CardFinished 並跳過實際效果。

若舊卡自訂 `onUse()`，`EventSkillEffect` 攔截後：

- 整段 `onUse()` 跳過。
- 不補跑基底 `Card::onUse()`。
- 不合成 PreCardUsed/CardUsed/CardFinished。
- 不處理該 onUse 內的移牌、代價或日誌。
- 已提交的 V2 配額與使用歷史仍計入。
- 唯一相容要求是安全返回、不閃退。
- 標記 `LegacyOnUseLimited`，正確語意交由人工遷移。

### 9.4 bypass_cost

- 對 V2 proxy：跳過 `pay()`，因此預設 selected-card 代價不消耗。
- 對舊 SkillCard：可跳過可辨識的 `will_throw`，不能跳過 validate/onUse 內混合代價。
- 對 ViewAs 產生的普通卡：不可讓普通卡 subcards 免費；仍按普通卡使用流程移動。

## 10. 四種使用路徑

### 10.1 Play／response-use

沿用 CardUseStruct 與既有 UseCard 管線。完整執行 WillInvoke、Pay、TargetConfirming、Invoking、Effect、EffectTarget、EffectFinished。

### 10.2 純 response

- 使用 CardResponseStruct 作 `original_data` backing。
- 沒有 target stages。
- whole-effect skip 設定 `CardResponseStruct::nullified`，但仍視為已支付並完成 PreCardResponded/CardResponded/PostCardResponded。
- `EffectFinished` 在 PostCardResponded 後發出一次。

### 10.3 nullification

- 沿用 `useCard()`，不是純 response。
- 完整支付及使用生命週期。
- 只有實際存在 targets 時才跑 target stages。
- whole-effect skip 表示無懈已使用／支付但不取消原錦囊。

### 10.4 old replay

- 新重播記錄 activation name/ID 與 source name/ID。
- 舊重播缺 ID 時以 legacy ID 0 決定性解析第一個有效實例並警告。
- 可盡力播放，但不承諾還原精確多實例。

## 11. ActiveSkillV2 作者 API

### 11.1 類型定位

- `ActiveSkillV2` 底層繼承 `ViewAsSkill`，復用 Dashboard、回應等待與協議入口。
- V2 作者不再組合「ViewAsSkill + 每技能 SkillCard 類別」。
- `createCard()` 可返回普通卡或通用 proxy。
- 普通卡自行擁有 target rules；只有 custom action proxy 使用 V2 target hooks。
- 舊 ViewAsSkill API 不改。

### 11.2 查詢與選牌

建議介面概念：

```cpp
virtual bool canActivate(const ActiveSkillRequest &request) const;
virtual bool canSelectCard(const ActiveSkillRequest &request,
                           const Card *candidate) const;
virtual bool cardSelectionFeasible(const ActiveSkillRequest &request) const;
virtual const Card *createCard(const ActiveSkillRequest &request) const;
```

責任分工：

| 驗證 | API |
|---|---|
| reason/pattern 下能否開始選擇 | `canActivate` |
| 每張選牌 | `canSelectCard` |
| 最終牌組 | `cardSelectionFeasible` |
| userString 與權威卡牌 | `createCard`，無效返回 null |
| 最終權威驗證 | 伺服器依相同順序全部重跑 |

`canActivate` 是唯一 V2 發動入口，但不單獨承擔所有 selection/target 驗證。

### 11.3 純查詢規範

以下函式不得詢問玩家、修改狀態、寫 Tag、使用亂數、播放日誌／動畫或依賴呼叫次數：

```text
canActivate
canSelectCard
cardSelectionFeasible
canSelectTarget
targetsFeasible
createCard
```

`createCard()` 必須在相同 request 與狀態下決定性建立相同卡。客戶端預覽與伺服器結果不同時，以伺服器為準並拒絕提交，不自動替換成其他卡。

### 11.4 target API

```text
TargetMode::NoTarget
TargetMode::SelectTargets

canSelectTarget(request, selected, candidate)
targetsFeasible(request, selected)
```

普通卡不使用上述 hooks，直接使用該 Card 的 targetFilter/targetsFeasible。

### 11.5 effect API

```text
TargetEffectMode::EachTarget
TargetEffectMode::WholeTargetGroup

EffectFlow::ContinueEffects
EffectFlow::FinishSkill
```

作者 hooks：

```text
effect(ctx)                 // 一次性的全域／前置效果
effectOnTarget(ctx, target)
effectOnTargetGroup(ctx, effectiveTargets)
```

- `effect(ctx)` 返回 `FinishSkill` 是成功完成，不是取消，結果為 `Completed`。
- Lua effect 返回 nil 時預設 `ContinueEffects`。
- `WholeTargetGroup` 先逐位置觸發 EffectTarget，再以剩餘列表呼叫 group 一次。
- effectiveTargets 為空時不呼叫 group。
- 部分目標被移除後不重跑 feasible；技能自行處理不足數量。
- V2 作者 API 不提供 `manual_effect`／自行派發 target effect 的模式；需要提早完成時使用 `FinishSkill`。

數值契約與 `TriggerV2Skill` 相同：`getBaseAmount()` 預設為 1，execution 建立前會把它寫入
`ctx.amount`；作者以 `getEffectiveAmount(ctx)` 取得有效值，優先序為正數
`modified_amount`、正數 `amount`、最後回退 `base_amount`。Lua factory 以選用的
`base_amount` 設定基礎值。攔截器可以修改 `ctx.modified_amount`，不得修改 execution identity。

### 11.6 cost／pay

- 名稱保持 `cost()`／`pay()`，兩者均返回 bool。
- `cost()` 可進行發動準備／選擇；false 在 WillInvoke 前取消，沒有 Finished。
- `pay()` 是正式支付；false 不回滾已發生副作用，結果 PayFailed。
- `bypass_cost` 只跳過 pay，不跳過普通卡牌本身的使用成本。

V2 proxy selected-card 預設支付：

- `willThrowSelectedCards()` 預設 true。
- 預設 `pay()` 先驗證 A 所選全部 card ID 仍在合法區域，再一次原子棄置。
- 任一張失效時不移動任何牌，返回 false。
- proxy 本身 `will_throw=false`，避免 Card::onUse 再丟一次。
- `willThrowSelectedCards=false` 時由自訂 pay 決定交給其他人、置頂、展示或不消耗。

### 11.7 proxy 歷史名稱

- C++ 實際類別只有一個通用 `ActiveSkillCard`。
- 對外 objectName/history key 預設按 activation skill 產生 `#<activationSkillName>Card`。
- `ActiveSkillV2::historyKey()` 可覆寫為舊 SkillCard key，供未來人工遷移對齊舊觸發器／AI。
- V2 使用配額不依賴 card history key。

## 12. 目標與效果規則

### 12.1 TargetConfirming 修改

- `updated_targets` 是遊戲效果的權威覆寫。
- 只檢查 null pointer／已離開 Room pointer。
- 不重跑 distance、prohibit、數量、targetFilter、targetsFeasible 或重複檢查。
- 結構失敗時 fail-closed：支付不回滾、不執行效果，結果 `InvalidTargetUpdate`，仍發 Finished。
- 死亡、被禁止、超距及重複角色都是遊戲層面允許的修改，不算結構失敗。

### 12.2 EffectTarget 位置

普通卡牌：

```text
原有 nullified／offset／普通卡牌防止流程
→ 已無效則不觸發 EventSkillEffectTarget
→ EventSkillEffectTarget
→ 未取消才 onEffect
→ PostCardEffected
```

V2 custom proxy：

- EffectTarget 前排除死亡角色。
- 復活／處理陣亡角色屬特例，使用整體 `effect()` 或明確自訂 onEffect，不走標準 target effect pipeline。
- 同一角色在 target list 出現多次時保留每個位置。
- 每個位置獨立觸發 EffectTarget 並重設 cancel。
- `SkillContext::targetIndex` 是原列表的穩定、只讀位置；WholeTargetGroup 保留順序與重複項。

## 13. 配額、歷史與重入

### 13.1 配額 scope

- 預設：immutable activation instance。
- 可選：`SourceInstance`，多個 attached activation 共用 root quota。
- card history 永遠屬最終 invoker。

### 13.2 reservation

- WillInvoke 通過後先 reserve，防止 cost/pay 或 nested use 重入繞過限制。
- pay/bypass 成功後 commit。
- pay false 時 release reservation。
- effect skip 仍消耗已提交配額。
- executionID 與 quota reservation 均須支援巢狀 stack。

## 14. 協議、客戶端、UI 與 AI

### 14.1 協議

- 不新增獨立 ActiveSkill command。
- Play、response-use、nullification 擴充現有 UseCard packet。
- Pure response 擴充現有 RespondCard packet。
- V2 額外提交 activationSkillName、activationInstanceID；holder 由連線玩家決定。
- 客戶端提交 selected card IDs、targets 與既有 userString。
- 伺服器忽略客戶端聲稱的最終 card class、skillName、source 與 root ID，重新跑 `createCard()`。
- 舊技能沒有 activation 欄位時走 legacy parser。

### 14.2 attached UI

- 客戶端同步完整 activation parentRef/sourceRef；source 身份預設是公開資料。
- 按鈕以 parentRef 找精確 provider，不以技能名拆字或第一個同名兄弟推斷。
- root instance `bindHead` 決定 head/deputy 圖；未綁定 acquired root 預設顯示 provider 主將。
- 保留 `image/system/button/skill/attachedlord/<general>.png`。
- 缺少 PNG 時退回一般 attachedlord 背景，不動態裁切 full image。

### 14.3 AI

- 舊 AI 不改，仍以 base skill name 呼叫一次。
- 舊 AI 沒有 activation ID 時，決定性選第一個有效 legacy instance。
- V2 可提供選用 callback，取得 request、activationRef、sourceRef 與 quota，能選精確 instance。
- V2 callback 每次只處理 Room 當前提供的一個 activation；回傳不重複帶 activation ref。
  callback 回傳 `nil`／`false` 表示放棄，或回傳嚴格 Lua table：

  ```lua
  { cards = { 12, 34 }, targets = { "target_a", "target_b" }, user_string = "choice" }
  ```

  三欄可省略，分別代表空牌、空目標與空字串。欄位型別不符、重複 card ID
  或不存在的 target 均 fail-closed。server 將結果補入當前 immutable request，
  並重跑 `resolveActiveSkillRequest()`；`user_string` 是由技能 callback 驗證的
  opaque 額外選擇值，不可作通用 `cloneCard` 輸入。
- source-sensitive attached AI 列入未來人工遷移規範，不在核心票批量改寫。

## 15. Lua 規則

- C++ API 先完成並固定，下一張 ticket 才加入 `LuaActiveSkillV2` 與 `sgs.CreateActiveSkillV2`。
- Lua 名稱對齊 C++：`can_activate`、`can_select_card`、`card_selection_feasible`、`create_card`、`can_select_target`、`targets_feasible`、`cost`、`pay`、`effect`、`effect_on_target`、`effect_on_target_group`。
- request 只暴露 getter；context 依既有 SWIG 規則暴露允許欄位。
- 所有 callback 以 `lua_pcall` 保護並輸出錯誤。
- 查詢／createCard 出錯安全返回 false/null。
- pay 出錯作 PayFailed；effect 出錯作 EffectSkipped。
- 修改任何 `swig/*.i` 後必須由工具重產 `swig/sanguosha_wrap.cxx`，禁止手改生成檔。

## 16. 相容矩陣

| 類型 | 零修改橋接 | 精確 EffectTarget | 完整 bypass_cost | 備註 |
|---|---:|---:|---:|---|
| 舊 SkillCard 使用 base onUse/use/onEffect | 是 | 到達 onEffect 時是 | 可辨識 will_throw | whole effect 可攔截 |
| 舊 SkillCard 自訂 monolithic onUse | 防崩潰 | 否 | 否 | `LegacyOnUseLimited` |
| 舊 validate 有副作用 | 防崩潰 | 依 replacement | 否 | `LegacyValidateLimited` |
| 舊 ViewAs 產生普通卡 | 是 | 依普通卡 CardEffect | 不使普通卡免費 | 保留普通卡規則 |
| ActiveSkillV2 產生普通卡 | 是 | 依普通卡 CardEffect | 不使普通卡免費 | server recreate |
| ActiveSkillV2 proxy | 原生 | 是 | 是 | 完整 V2 契約 |
| Pure response | 是 | 不適用 | 依卡類型 | nullified response |
| Nullification | 是 | 有目標才有 | 依卡類型 | skip 不取消錦囊 |

## 17. Ticket 路線圖

每張 ticket 必須獨立編譯。禁止跨 ticket 順手遷移正式技能或重構無關 Card/Room 流程。

### Ticket 1：跨玩家實例引用與 attached registry

依賴：既有多實例基礎。

預計模組：

- `src/core/skill-instance-types.h`
- `src/core/player.h/.cpp`
- `src/core/engine.h/.cpp`
- `src/server/room.h/.cpp`
- Package 登記入口與 SkillInstance snapshot serializer

交付：

- `SkillInstanceRef`、`parentRef`、`SourceAttached`。
- root->activation registry、精確 attach/detach/cascade。
- old snapshot parent owner fallback。
- parent 存在、無循環、冪等及 deterministic removal 測試。

不處理：ActiveSkillV2、CardUse lifecycle、UI 素材。

驗收：Release x64；純 helper console tests；黃天式兩個 root 對同一 receiver 產生兩個不混淆的 activation instance。

### Ticket 2：Card 身份欄位與現有協議擴充

依賴：Ticket 1。

預計模組：

- `src/core/card.h/.cpp`
- `src/core/structs.h/.cpp`
- `src/core/protocol.h`
- `src/client/client.h/.cpp`
- `src/server/room.h/.cpp`
- replay serializer/parser

交付：

- source/activation 欄位分工。
- UseCard／RespondCard 可選 activation name/ID。
- holder implicit、server source resolution、old ID 0 fallback。
- `skillExecutionID` 僅作內部欄位，不進 client authority。

不處理：execution registry、V2 callbacks。

驗收：舊 packet 仍解析；畸形 ID 拒絕；new replay 完整、old replay 警告且 best-effort。

### Ticket 3：ActiveSkillExecution 與 SkillContext 擴充

依賴：Ticket 2。

預計模組：

- `src/core/skill.h/.cpp`
- `src/core/structs.h/.cpp`
- `src/server/room.h/.cpp`
- `src/server/roomthread.cpp`

交付：

- Room monotonic executionID、registry、RAII guard。
- source/activation/initiator/invoker、mutation、target snapshots、interceptor namespaces。
- backing QVariant 生命週期。
- `SkillExecutionResult` 與 Finished-once guard。
- nested/reentrant lookup 測試。

不處理：真正呼叫七個事件、ActiveSkillV2 作者 API。

驗收：巢狀 execution 互不覆蓋；結束後 registry 無殘留；original_data 在整段執行有效。

### Ticket 4：舊 SkillCard／ViewAs 的 use 橋接

依賴：Ticket 3。

預計模組：

- `src/server/room.cpp`
- `src/server/gamerule.cpp`
- `src/core/card.cpp`
- `src/server/roomthread.cpp`

交付：

- Play 路徑七事件生命週期。
- validate-first、replacement identity lock。
- WillInvoke mutation、delegated invoker及 legacy history 行為。
- base onUse whole-effect skip。
- custom onUse whole-skip 的 `LegacyOnUseLimited` 安全路徑。

不處理：response、ActiveSkillV2 proxy、正式技能修正。

驗收：標準舊 SkillCard 攔截仍有 CardUsed/CardFinished；monolithic onUse 攔截整段不執行但歷史計入且不閃退。

### Ticket 5：Response、nullification 與中斷收束

依賴：Ticket 4。

預計模組：

- `src/server/room.cpp`
- `src/server/gamerule.cpp`
- CardResponse/CardUse conversion helpers

交付：

- response-use、pure response、nullification 四路徑一致身份與 execution。
- pure response nullified semantics。
- StageChange/TurnBroken 對齊 CardFinished 並 Finished(NoResult)。
- Finished 恰好一次。

不處理：新作者 API、Lua。

驗收：四路徑矩陣；validate null 無 execution；巢狀 useCard 產生新 execution。

### Ticket 6：ActiveSkillRequest 與 ActiveSkillV2 C++ 查詢 API

依賴：Ticket 3、Ticket 5。

預計模組：

- `src/core/skill.h/.cpp`
- `src/core/engine.h/.cpp`
- Dashboard ViewAs adapter
- server request resolver

交付：

- request value type、只讀 accessors。
- `canActivate`、選牌、feasible、createCard。
- client prediction 與 server full rerun。
- server recreate card、無效 userString 返回 null。
- 純查詢規範文件與測試 fake。

不處理：target effect、Lua、正式技能。

驗收：客戶端偽造 card class/source/skillName 不影響 server 建立結果；查詢函式可重複呼叫而狀態不變。

### Ticket 7：通用 proxy、cost/pay 與歷史

依賴：Ticket 6。

預計模組：

- `src/core/card.h/.cpp`
- `src/core/skill.h/.cpp`
- `src/server/room.cpp`
- CardMoveReason/history helpers

交付：

- 單一 `ActiveSkillCard`。
- per-activation object/history key 與 override。
- cost/WillInvoke/Pay/pay 順序。
- initiator A 的原子 selected-card payment。
- bypass、delegated B、pay failure、玩家死亡規則。
- activation/source quota scope、reservation、commit、release。

不處理：target modes、Lua。

驗收：部分牌失效不丟任何牌；bypass 不消耗 proxy 副牌；普通轉換卡仍消耗；B 使用但 A 支付預設副牌。

### Ticket 8：目標模式與效果分派

依賴：Ticket 7。

預計模組：

- `src/core/skill.h/.cpp`
- `src/server/gamerule.cpp`
- `src/server/room.cpp`

交付：

- NoTarget/SelectTargets。
- EachTarget/WholeTargetGroup、EffectFlow。
- V2 TargetConfirming mutation 與結構檢查。
- ordinary card EffectTarget 位於既有 nullify/offset 後。
- 排除死亡、保留重複及穩定 targetIndex。
- 多攔截器後寫覆蓋、每位置 cancel reset。

不處理：正式技能 target rules。

驗收：空 group 不呼叫；部分 target cancel 不重跑 feasible；重複目標逐位置執行；null/out-of-Room 作 InvalidTargetUpdate。

### Ticket 9：客戶端精確實例與 attached UI

依賴：Ticket 1、Ticket 2、Ticket 6。

預計模組：

- `src/ui/dashboard.h/.cpp`
- `src/ui/qsanbutton.h/.cpp`
- `src/ui/roomscene.h/.cpp`
- `src/client/client.h/.cpp`

交付：

- 按鈕提交精確 activation ID。
- ActiveSkillRequest client-side activationRef。
- parentRef 精確 provider 顯示。
- bindHead／displayGeneralName／PNG fallback。
- 同名多按鈕切換不殘留上一 instance ID。

不處理：full-image 裁切、新素材生成。

驗收：兩個同名 direct/attached instance 顯示與提交不混淆；缺圖不崩潰。

### Ticket 10：LuaActiveSkillV2 與 SWIG

依賴：Ticket 8、Ticket 9。

預計模組：

- `src/core/lua-wrapper.h/.cpp`
- `swig/luaskills.i`
- `swig/sanguosha.i`
- Lua factory 定義
- 生成的 `swig/sanguosha_wrap.cxx`

交付：

- `LuaActiveSkillV2` 與 `sgs.CreateActiveSkillV2`。
- C++ hooks 對應 callback、nil/default/error semantics。
- request getter-only；context mutation 按 checkpoint 生效。
- C++ API 已固定後才生成 wrapper。

不處理：正式 Lua 技能遷移。

驗收：C++ 與 Lua fake 行為一致；Lua 錯誤不穿透主流程；wrapper 時間戳晚於所有修改過的 `.i`。

### Ticket 11：AI、重播與診斷相容

依賴：Ticket 9、Ticket 10。

預計模組：

- client AI bridge／Lua AI bindings
- replay serializer/parser
- Room diagnostics/log helpers

交付：

- 舊 AI base-name single-call 與 deterministic first instance。
- 選用 V2 AI callback 精確 request/source/activation。
- replay 新舊策略。
- execution/interceptor mutation 診斷，不把 executionID 變成協議契約。

不處理：批量 AI 腳本遷移。

驗收：舊 AI 可使用單實例與多實例 fallback；source-sensitive fake 可選精確 root；舊 replay 不閃退。

### Ticket 12：整合測試與遷移規範收束

依賴：Ticket 1–11。

預計模組：

- `tests/active-skill-v2/`
- 既有 `~test` Package
- 測試用 Lua extension／fixture
- `docs/active-skill-v2-migration-guide.md`
- 本文件狀態表

交付：

- 純邏輯 console tests。
- `~test` C++／Lua 合成技能，不加入正常武將包。
- 四路徑、多實例、attached、delegation、interceptor、failure、中斷完整矩陣。
- validate/onUse 人工審議模板。
- 檔案／呼叫點 audit；不修改正式技能。

驗收：Release x64；console tests；文件化 `~test` 場景全部通過；沒有正式技能 diff。

### Ticket 13：getUsageRef 與技能實例配額引用

依賴：Ticket 1–3 的 `SkillInstanceRef`／不可變 provenance，以及 Ticket 7 的
ActiveSkillV2 reservation／commit／release 配額生命週期。本票是 ViewAsSkill 次數橋接的
必要前置，不在同一票接管舊 `usedTimes()`。

#### 13.1 問題與契約

`LimitScope` 只回答「何時重設」，不能回答「哪個技能實例共用次數」。配額所屬實例由單一
策略入口 `getUsageRef(ctx)` 決定，不另設 identity enum：

```cpp
virtual SkillInstanceRef getUsageRef(const SkillContext &ctx) const;
```

| `getUsageRef()` 行為 | 權威引用 | 語意 | 預設 |
|---|---|---|---|
| 基底實作 | `ctx.activationRef` | 每個實際點擊入口各自計數 | 是；保持現有行為 |
| 技能覆寫回傳 source | `ctx.sourceRef` | 同一 root source 派生的 attached 入口共用配額 | 否；技能明確覆寫 |

直接技能通常兩者相同。attached skill 覆寫並回傳 source ref 後，不同玩家持有的 attach
入口只要指向同一 root instance，就必須在 root owner 上讀寫同一個 mark。
`initiator`、可變的 `invoker` 與共享 `Skill` QObject 均不得決定配額身份。

#### 13.2 C++ API 與解析規則

預計模組：

- `src/core/skill.h/.cpp`
- `src/server/room.h/.cpp`
- `src/core/skill-instance-types.h`（只在既有 `SkillInstanceRef` helper 不足時修改）
- `src/core/skill-instance-utils.h/.cpp`（純 usage reference 解析與 console test 共用）

交付 API：

```cpp
virtual SkillInstanceRef getUsageRef(const SkillContext &ctx) const;
```

作者公開策略入口只有 `getUsageRef(ctx)`；`getUsageHolder()` 與 `getUsageTagKey()` 為
`Skill`／`Room` 內部解析方法，不暴露給 Lua 或技能作者。

| 規則 | 要求 |
|---|---|
| 預設 | 基底 `getUsageRef()` 回傳有效 `activationRef`，不改既有技能語意 |
| activation fallback | 新流程使用有效 `activationRef`；只為缺 provenance 的 legacy context，才可由 `ctx.owner/ctx.invoker + objectName() + ctx.instanceID` 組出相容引用 |
| source fail-closed | 技能覆寫回傳無效 `sourceRef`、root owner 不存在或 instance 不存在時，拒絕使用並輸出診斷；不得由覆寫函式靜默退回 activation |
| holder | 由已解析 usage ref 的 `ownerObjectName` 找 `ServerPlayer`，不可直接固定為 `ctx.owner` 或 `ctx.invoker` |
| mark key | 使用已解析 ref 的 `skillName + instanceID + LimitScope suffix`；不可使用目前共享 Skill 的 `objectName()` 代替 source skill name |
| reservation key | holder object name + 完整 usage mark key；reserve／release／commit 三者必須呼叫同一解析入口 |
| 不可變性 | execution 開始後 `sourceRef`／`activationRef` 不變；WillInvoke 委託 invoker 不搬移配額 |

禁止恢復或依賴共享 `Skill::m_instanceId`。`resetUsage(ctx)` 亦必須解析同一 usage ref，確保
source identity 清除的是 root 配額。

#### 13.3 Lua 與 SWIG

預計模組：

- `src/core/lua-wrapper.h/.cpp`
- `lua/sgs_ex.lua`
- `swig/luaskills.i`
- `swig/sanguosha.i`
- 工具重產的 `swig/sanguosha_wrap.cxx`
- `lua/test/runner.lua`、`src/main.cpp`（Lua smoke 必須執行 `:assert()` 並以非零狀態回報失敗）

交付：

- `SkillContext` 以 getter-only 方式暴露 `getActivationRef()`／`getSourceRef()`。
- `sgs.CreateTriggerV2Skill` 與 `sgs.CreateActiveSkillV2` 均接受選用的純查詢 callback `get_usage_ref(skill, ctx)`；未提供時由 C++ 基底使用 activation ref。
- callback 必須回傳 `SkillInstanceRef`；Lua error、nil 或錯誤型別均 fail-closed。舊 `usage_identity` 載入時報遷移提示，不得靜默忽略。
- C++ 技能直接覆寫 `getUsageRef(ctx)`；不再暴露 `UsageIdentity` enum、setter 或 Lua 常數。

| 技能入口 | 本票責任 |
|---|---|
| C++ `Skill`／`ActiveSkillV2` | 提供預設 activation ref 與可覆寫的 `getUsageRef()` |
| Lua `TriggerV2Skill` | factory callback、SWIG ref getter；generic scope 與 Custom 邊界均驗證 |
| Lua `ActiveSkillV2` | factory callback、SWIG ref getter；接入既有 ActiveSkillV2 quota lifecycle |
| legacy Lua/C++ `ViewAsSkill` | 只可讀取基礎 API；自動扣次數留給後續 bridge 票 |

`Limit_Custom` 不受通用 `getUsageRef()` 配額流程控制：C++ 覆寫的
`isUsable/addUsage/resetUsage`，以及 Lua `check_custom_usage/on_add_usage`，仍自行管理 key、holder、
reservation 與重入語意。核心不得自動讀寫 generic usage mark。

#### 13.4 測試與驗收矩陣

| 場景 | 期望 |
|---|---|
| direct activation #1／#2，預設 `getUsageRef()` | 兩個 instance 各自計數 |
| 兩個 attached 入口，預設 `getUsageRef()` | 各入口獨立計數 |
| 兩個 attached 入口覆寫回傳同一 root source | 共用 root owner/root skill/root instance 配額 |
| 同名同 ID、不同 root owner | 不碰撞 |
| WillInvoke 將 invoker A 改為 B | 配額仍綁原 activation/source ref |
| 覆寫回傳缺少或偽造的 sourceRef | fail-closed，不扣 activation 配額 |
| `max_usage_limit > 1` 與巢狀 execution | committed mark + reservation count 精確達限 |
| pay 失敗／取消；`bypass_cost` 成功 | 前者 release，後者 commit，均使用相同 resolved key |
| `resetUsage(ctx)` | 只清除 `getUsageRef()` 選定的精確 instance |
| `Limit_Custom` | 不建立 generic reservation，不自動 add/reset |

驗收：Release x64；`tests/skill-instance-utils` 增加純解析測試；`~test` 或等價 fixture
驗證 attached source shared quota；Lua smoke 證明兩個 factory callback、getter-only refs 與錯誤回傳 fail-closed；SWIG wrapper
時間戳晚於所有修改過的 `.i`。

#### 13.5 明確不處理

- 不在本票自動攔截 legacy `ViewAsSkill`／`SkillCard` 的 `usedTimes()` 或 `hasUsed()`。
- 不批量遷移正式 C++／Lua 技能。
- 不改 card history key、重播協議或客戶端提交的 trust boundary。
- 不以技能名稱後綴、第一個同名 instance 或目前 invoker 猜 root source。
- 不替 `Limit_Custom` 定義通用 reservation 行為。

## 18. 每票通用驗收

- 只修改 ticket 列出的模組；額外修改需先更新計劃。
- Release x64 必須編譯通過。
- 每張 ticket 加入與其風險相稱的最低測試，不把全部測試延後。
- 修改 broad header 後預期完整重編，不以增量成功代替乾淨驗證。
- 不修改第三方 `include/`、`lib/`。
- Skill 衍生類不加入 `Q_OBJECT`；Card/SkillCard 衍生類遵循既有 `Q_OBJECT + Q_INVOKABLE` 規範。
- 修改 `swig/*.i` 必須工具重產 wrapper，禁止手改。
- 舊 API 行為變更必須落入本文件已列出的相容邊界。

Release x64 基準命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-release.ps1
```

## 19. 計劃狀態

- ActiveSkillV2 amount 擴充：C++／Lua 已加入 `base_amount`、`getBaseAmount()` 與 `getEffectiveAmount(ctx)`；Play、pure response 與候選配額 context 均在生命週期開始時以 base amount 初始化。配額例外維持由 `Limit_Custom` 統一負責，不另增語意重複的 `isUsageExempt`。
- Ticket 13：核心與 fixture 完成、待工具鏈整合驗證（2026-07-20）。配額策略已收斂為單一 `getUsageRef(ctx)`：預設 activation、覆寫可選 immutable source，移除 `UsageIdentity` enum／setter／Lua 常數；保留 legacy activation fallback、source fail-closed、root-source 配額解析與 Lua `get_usage_ref` callback。generic scope 以 committed mark + counted reservation 支援巢狀重入，pay failure／Pay cancel／`StageChange`／`TurnBroken` 會釋放未提交 reservation，bypass 仍 commit。Play 與 pure response 的控制事件會補發 `EffectFinished(NoResult)` 最多一次並重新拋出原事件；effect／target hooks 後會還原 immutable provenance。legacy instance-0 reset 已恢復，`Limit_Custom` 不建立 generic reservation 且不自動 add/reset。`tests/skill-instance-utils` 與 `~test` 已補 shared-root、owner isolation、nested、failure/cancel、bypass、reset、Custom 與 finished-once fixture；Lua callback smoke 位於 `lua/test/examples/test_active_skill_v2_usage_ref.lua`。`swig/sanguosha_wrap.cxx` 已由 SWIG 4.4.1 重產；尚缺 Release x64／console／Lua smoke／Room lifecycle 實跑（工作機沒有 qmake、C++ 編譯器與 Lua CLI）。
- Ticket 10：完成（2026-07-17）。已加入 `LuaActiveSkillV2`、`sgs.CreateActiveSkillV2`、read-only `ActiveSkillRequest` getters 與所有 query/cost/pay/target/effect callbacks；Lua callback error 均 fail-closed，effect 的 nil 結果為 `ContinueEffects`。`swig/sanguosha_wrap.cxx` 已由 `tools/swig/swig.exe` 重新產生。驗證：Release x64 0 errors。
- Ticket 11：進行中。已加入 provenance V2（cross-owner source/activation refs）、V1 replay fallback、選用 request-aware AI callback、server-only execution audit 與 replay parser fixture；Play bridge 的 cost/pay/cancel/invalid early exits 現均會 Finished/audit 收束。V2 AI callback 已回傳綁定當前 activation 的 `{ cards, targets, user_string }` 結果，Room 以 server-created proxy 將 choices 送入既有 resolver；尚缺 AI/lifecycle 合成技能端到端場景。
- Ticket 12：進行中。已加入 replay console fixture、驗證矩陣與 `~test` V2 C++/Lua 合成技能；尚缺自動化完整 Room lifecycle matrix。

- 設計契約：已完成訪談並鎖定。
- Ticket 1：完成（2026-07-17）。已加入 `SkillInstanceRef`、`SourceAttached`、Room-owned attached registry、跨玩家 parent snapshot（保留舊 8 欄 payload fallback）、root detach cascade 與開局 snapshot。驗證：Release x64 0 errors；`tests/skill-instance-utils` console test passed。
- Ticket 2：完成（2026-07-17）。已加入 Card source/activation identity、UseCard／RespondCard 2/3/4 欄相容 codec、server holder/source resolution、replacement propagation、V1 replay provenance notification/parser 與舊 replay 一次性 diagnostic。驗證：Release x64 0 errors；`skill-instance-utils-test` 覆蓋 packet codec。
- Ticket 3：完成（2026-07-17）。已加入 Room-owned monotonic execution registry、move-only RAII guard、owned QVariant backing、finished-once result、CardEffect execution ID 與擴充 SkillContext identity/mutation/interceptor slots。驗證：Release x64 0 errors；`skill-instance-utils-test` 覆蓋 nested registry、cleanup 與 finished-once。
- Ticket 4：進行中。已完成 Play bridge 的 execution context、standard/custom whole-effect skip 分流、target registry lookup、WillInvoke mutation commit 與 validate replacement single lifecycle；尚缺 deterministic integration fixture。
- Ticket 5、6、8、9：已完成基礎實作（2026-07-17），仍待 Ticket 12 整合矩陣驗證。
  Ticket 7：進行中。通用 `ActiveSkillCard` proxy、selected-card 原子支付與 quota reservation/commit/release 已存在；尚缺 Release x64 與完整 Room lifecycle 自動化驗收。
- 正式技能遷移：不在本計劃實作範圍。
