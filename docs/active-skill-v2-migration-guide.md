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
| AI | 是否依 Player Tag、card class、source owner |
| logs/UI | skillName、語音、attachedlord provider |

## 4. 先判斷使用哪一種 V2 形式

### 4.1 產生普通卡

適用於技能只是把牌轉換成 Slash、Jink、Peach、Trick 等既有卡牌。

- `createCard()` 返回普通 Card。
- targetFilter、feasible、卡牌移動、offset、onEffect 由普通 Card 負責。
- `bypass_cost` 不免除普通卡 subcards。
- 不實作 V2 custom target hooks。

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
- 無法安全拆分時標記 `LegacyValidateLimited`，暫留舊技能。
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
- 遷移完成前標記 `LegacyOnUseLimited`。

橋接層攔截 monolithic onUse 時只保證整段跳過且不閃退，不保證代價、CardUsed 或移牌語意。

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
- `skillName` 只作效果／日誌歸因；root source 使用 sourceRef。
- 舊 AI 未遷移時仍可透過 base-name fallback，但 source-sensitive attached 技能必須另審。
- 不為了通過 AI 而把 instanceID 再塞入技能名稱字串。

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

- 本文件只建立規範。
- 沒有正式技能獲准在核心 Ticket 1–12 內遷移。
- 第一批實作只可加入 console tests 與 `~test` 合成技能。

