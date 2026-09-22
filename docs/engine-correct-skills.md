# CorrectSkillV2 數值修正系統（引擎側快參）

> **定位**：本文為引擎側快參（holder／context／amount／correctState／同步／legacy 保證／開放門檻）；**完整開發者指南、Lua 範例與遷移流程以 [`CorrectSkillV2功能與開發指南.md`](CorrectSkillV2功能與開發指南.md) 為準**。兩文件內容互補，不重複為冗餘；如有衝突以後者為準。

## 開放狀態

| 類別 | Legacy | V2 |
|---|---|---|
| Distance | 每個技能定義計算一次 | 每個有效實例分別計算 |
| MaxCards | 每個技能定義計算一次 | 每個有效實例分別計算 |
| TargetMod | 每個技能定義計算一次 | 每個有效實例分別計算 |
| AttackRange | 每個技能定義計算一次 | 每個有效實例分別計算 |

正式技能在完成 selector、amount、同步與 legacy 回歸矩陣前不得遷移。首批只使用 `~test`／Lua smoke fixture。

## 類別與 factory

| C++ | Lua factory |
|---|---|
| `DistanceSkillV2` | `sgs.CreateDistanceSkillV2` |
| `MaxCardsSkillV2` | `sgs.CreateMaxCardsSkillV2` |
| `TargetModSkillV2` | `sgs.CreateTargetModSkillV2` |
| `AttackRangeSkillV2` | `sgs.CreateAttackRangeSkillV2` |

所有 V2 類別共享 `base amount`。玩家持有的每個 `SkillInstance` 可保存獨立 current override；沒有 override 時讀取共享 base。

## 持有者選擇器 (Holder selector)

| Selector | 行為 |
|---|---|
| `CorrectSkill_Primary` | Distance／TargetMod 為 `from`；MaxCards／AttackRange 為 `target` |
| `CorrectSkill_Secondary` | Distance／TargetMod 的 `to`；沒有 secondary 時不貢獻 |
| `CorrectSkill_Participants` | primary、secondary 去重後逐一檢查 |
| `CorrectSkill_AllHolders` | 全場所有存活玩家的有效實例 |
| `CorrectSkill_System` | 無持有者與 instance ref，只計算一次共享 base |

非 System 模式只遍歷存活玩家，並由 `Player::isSkillInstanceEffectAvailable()` 排除精確失效及國戰暗置來源；不同玩家即使技能名與 instanceID 相同，仍以 owner objectName 隔離。

### 國戰被動效果閘門

CorrectSkillV2 的持續效果、fixed 修正與 UI 貢獻查詢在 callback 前套用下列閘門；`TargetModSkillV2` 選目標有下述暗置預覽例外。技能只需回傳修正值，不必逐個呼叫 `hasShownSkill()`。

| 精確來源 | 效果可用條件 |
|---|---|
| 原生主／副將 | 該實例的 `bindHead` 對應將牌已明置；同名另一實例的明置或後天獲得不代為放行 |
| helper／attached | 以共用 `SkillInstanceUtils::resolveRootRef` 追溯 local parent／跨玩家 parentRef；根來源已明置或為獨立後天來源，且鏈上的實例有效 |
| 獨立後天來源 | 不依賴將牌明置；仍檢查技能失效 |
| 缺失來源、依賴循環、未綁定原生來源 | 不提供效果 |
| System selector | 無武將來源，維持系統修正；不可拿來宣告武將技能以繞過明置 |

暗置不寫入 SkillInvalidity、不刪除 instance，也不修改 `hasSkill()`／TriggerSkillV2 的候選判斷。重新暗置後，下次查詢即停止該來源的持續修正；獨立建立、有自身期限的後天效果不因暗置而撤銷。普通身份局保留既有有效性語意。

### TargetMod V2：先選目標，用到才亮將

| 階段 | 契約 |
|---|---|
| 選目標預覽 | `correctCardTarget()` 的 Residue／DistanceLimit／ExtraTarget 可計入使用者自己可明置的暗根來源；不需要預亮 toggle。跨玩家 attached 必須追到使用者自己的根，不能預覽他人的暗將。 |
| 凍結候選 | `Room::prepareTargetModSkillReveal()` 在付款前保存 exact leaf/root；付款移除來源後不以新取得的同名實例替代。 |
| 必要性 | 付款及目標確認完成、history 記次前，`planTargetModSkillReveal()` 以原選取順序評估 public／head／deputy／both，保存合法方案及實際參與的暗置實例。callback 必須是無副作用的查詢，最多重放四組。 |
| 公開 | 共用 `Card::onUse` 的 `showRequiredTargetModSkills` 在 extraCost 後套用 V2 方案。公開來源已足夠便不亮；任一將牌足夠便只選一邊；確需兩邊才各亮。每次公開前後重查 exact leaf/root 與失效／禁止明置。 |
| 出牌次數 | `hasResidueUnlimited()` 仍只計公開來源。暗置不限次數不能免費免記 history；第一張不需要修正便不亮，之後真正需要才亮。 |
| 目標轉換 | 已接受的選擇與執行對象分開：借刀原本的兩人 draft 不以後來的單一持刀者重驗。殺後續額外選目標會重新規劃，查詢時只扣回本次 history，不改真實 history。 |
| 擴展卡契約 | 新的選目標交易需在原 draft／旗標仍有效時呼叫 `planTargetModSkillReveal()`；自行覆寫且不呼叫共用 `Card::onUse` 的卡，須在付款後、效果前呼叫 `showRequiredTargetModSkillsV2()`。不能把舊方案當新增目標的合法性證明。 |
| 範圍 | 只規劃 `m_validateTargets=true` 的 client／AI 選擇；保留 server 強制出牌的權威契約。Distance／MaxCards／AttackRange 維持原明置門檻。 |

本批國戰相容範圍僅包含 V2；Legacy callback 不自動改寫。`concealedCorrectionEffects` 與 `targetModSelectionAndRevealContract` 已補原始碼案例，尚未建置或執行。

## `CorrectSkillContext`

callback 取得只讀語意欄位：

| 欄位 | 說明 |
|---|---|
| `instanceRef` | owner＋skillName＋instanceID；System 為無效 ref |
| `holder` | 本次實例持有者；System 為空 |
| `primary`／`secondary` | 本次修正的參與者 |
| `card`／`modType` | TargetMod 查詢資料 |
| `includeWeapon` | AttackRange 是否包含武器 |
| `currentAmount` | 實例 override 或共享 base |
| `getStateValue(key, default)` | 讀取該實例公開 `correctState` |

Lua callback 回傳契約：

| 回傳 | 語意 |
|---|---|
| `nil`／`false` | 本實例不適用 |
| `true` | 使用 `currentAmount` |
| 數字 | 使用明確數值，包含零與負數 |

Lua callback 出錯或回傳其他型別時記錄 warning，本實例本次貢獻為零。

fixed callback 沒有設定時代表無固定值。所有適用實例的 fixed 值取最大值。只有 `TargetModSkill::Residue` 的 `-1` 表示無限；其他修正的 `-1` 保留為有號整數。

## amount API 與事件

```cpp
room->setSkillInstanceAmount(source, ref, amount, reason);
room->addSkillInstanceAmount(source, ref, delta, reason);
room->resetSkillInstanceAmount(source, ref, reason);
```

`EventSkillAmountChanging` 可修改 `newAmount` 或設定 `canceled`；`EventSkillAmountChanged` 僅通知。相同 ref 在 Changing 至 Changed 完成前再次改值會被拒絕。reset 只有在事件後的值仍等於 base 時才移除 override；若 Changing 改成其他數值，改為保存新 override。

execution-local `modified_amount` 與持久 current amount 分離。使用 `setModifiedAmount()`／`clearModifiedAmount()`／`hasModifiedAmount()`，零與負數均為有效值。

`ViewAsSkillV2::getAmountRef(ctx)` 預設回傳 activation ref，可獨立覆寫為 source ref；不得以 `getUsageRef()` 代替。

## correctState 與同步

`SkillInstance::state` 仍為伺服器私有狀態；CorrectSkillV2 只透過獨立 `correctState` 讀取公開狀態。

```cpp
room->setSkillInstanceCorrectState(source, ref, key, value);
room->removeSkillInstanceCorrectState(source, ref, key);
room->clearSkillInstanceCorrectState(source, ref);
```

snapshot 保持舊 8／9 欄相容，在第 10 欄選擇性附加 metadata map。amount 與 correctState 使用獨立 delta，不發出 `skill_acquired`。同步沿用該實例原有可見權限，隱藏 helper 或不可見技能不向其他客戶端洩漏 metadata。

## Legacy 保證

Legacy 四類修正技能不遍歷 runtime instance，仍每個技能定義計算一次。Engine 先辨識 V2 類別，因此 V2 不會再落入 legacy callback 重複計算。

舊的全域／技能名 amount 覆寫與以 QObject property 控制多實例乘法的機制已停用；新技能不得依賴這些語意。

## 開放門檻

- 五種 selector、owner 隔離、精確失效、正負與零值。
- fixed 最大值及只有 Residue `-1` 無限。
- set/add/reset、Changing 修改／取消、Changed 通知與同 ref 防遞迴。
- correctState set/remove/clear、snapshot 重連及隱藏 metadata 權限。
- 同一玩家兩實例、另一玩家同名技能、client/server 一致。
- Legacy Mashu、MaxCards、TargetMod、AttackRange 回歸。
- Release x64、console、Lua smoke 與實際 Room lifecycle。
