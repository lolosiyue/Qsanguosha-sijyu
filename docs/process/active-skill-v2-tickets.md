# ViewAsSkillV2 分批實作方案

方案版本：2026-09-06。現行介面見[ViewAsSkillV2 契約](../active-skill-v2-refactor-plan.md)，各票實作結果見[歷程](active-skill-v2-history.md)。

## 17. Ticket 路線圖

每張 ticket 必須獨立編譯。禁止跨 ticket 順手遷移正式技能或重構無關 Card/Room 流程。

### Ticket 1：跨玩家實例引用與 attached registry

依賴：既有多實例基礎。

預計模組：

- [`src/core/skill-instance-types.h`](../../src/core/skill-instance-types.h)
- `src/core/player.h/.cpp`
- `src/core/engine.h/.cpp`
- `src/server/room.h/.cpp`
- Package 登記入口與 SkillInstance snapshot serializer

交付：

- `SkillInstanceRef`、`parentRef`、`SourceAttached`。
- root->activation registry、精確 attach/detach/cascade。
- old snapshot parent owner fallback。
- parent 存在、無循環、冪等及 deterministic removal 測試。

不處理：ViewAsSkillV2、CardUse lifecycle、UI 素材。

驗收：Release x64；純 helper console tests；黃天式兩個 root 對同一 receiver 產生兩個不混淆的 activation instance。

### Ticket 2：Card 身份欄位與現有協議擴充

依賴：Ticket 1。

預計模組：

- `src/core/card.h/.cpp`
- `src/core/structs.h/.cpp`
- [`src/core/protocol.h`](../../src/core/protocol.h)
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
- [`src/server/roomthread.cpp`](../../src/server/roomthread.cpp)

交付：

- Room monotonic executionID、registry、RAII guard。
- source/activation/initiator/invoker、mutation、target snapshots、interceptor namespaces。
- backing QVariant 生命週期。
- `SkillExecutionResult` 與 Finished-once guard。
- nested/reentrant lookup 測試。

不處理：真正呼叫七個事件、ViewAsSkillV2 作者 API。

驗收：巢狀 execution 互不覆蓋；結束後 registry 無殘留；original_data 在整段執行有效。

### Ticket 4：舊 SkillCard／ViewAs 的 use 橋接

依賴：Ticket 3。

預計模組：

- [`src/server/room.cpp`](../../src/server/room.cpp)
- [`src/server/gamerule.cpp`](../../src/server/gamerule.cpp)
- [`src/core/card.cpp`](../../src/core/card.cpp)
- `src/server/roomthread.cpp`

交付：

- Play 路徑七事件生命週期。
- validate-first、replacement identity lock。
- WillInvoke mutation、delegated invoker及 legacy history 行為。
- base onUse whole-effect skip。
- custom onUse whole-skip 的 `LegacyOnUseLimited` 安全路徑。

不處理：response、ViewAsSkillV2 proxy、正式技能修正。

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

### Ticket 6：ActiveSkillRequest 與 ViewAsSkillV2 C++ 查詢 API

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

### Ticket 10：LuaViewAsSkillV2 與 SWIG

依賴：Ticket 8、Ticket 9。

預計模組：

- `src/core/lua-wrapper.h/.cpp`
- [`swig/luaskills.i`](../../swig/luaskills.i)
- `swig/sanguosha.i`
- Lua factory 定義
- 生成的 `swig/sanguosha_wrap.cxx`

交付：

- `LuaViewAsSkillV2` 與 `sgs.CreateViewAsSkillV2`。
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

> 補註（2026-09-06）：`tests/active-skill-v2/` 至今未成立；測試收束改走 CTest（`qsanguosha_engine_smoke`／`qsanguosha_server_unit` 內 skill-instance 相關案例）與 `tests/skill-instance-utils/`。

交付：

- 純邏輯 console tests。
- `~test` C++／Lua 合成技能，不加入正常武將包。
- 四路徑、多實例、attached、delegation、interceptor、failure、中斷完整矩陣。
- validate/onUse 人工審議模板。
- 檔案／呼叫點 audit；不修改正式技能。

驗收：Release x64；console tests；文件化 `~test` 場景全部通過；沒有正式技能 diff。

### Ticket 13：getUsageRef 與技能實例配額引用

依賴：Ticket 1–3 的 `SkillInstanceRef`／不可變 provenance，以及 Ticket 7 的
ViewAsSkillV2 reservation／commit／release 配額生命週期。本票是 ViewAsSkill 次數橋接的
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
- [`lua/sgs_ex.lua`](../../lua/sgs_ex.lua)
- `swig/luaskills.i`
- `swig/sanguosha.i`
- 工具重產的 `swig/sanguosha_wrap.cxx`
- CTest 與 `tools/autotest/` 基建（原 `lua/test/runner.lua` 已隨 `lua/test/` 移除，commit a904221；Lua smoke 必須執行 `:assert()` 並以非零狀態回報失敗）

交付：

- `SkillContext` 以 getter-only 方式暴露 `getActivationRef()`／`getSourceRef()`。
- `sgs.CreateTriggerSkillV2` 與 `sgs.CreateViewAsSkillV2` 均接受選用的純查詢 callback `get_usage_ref(skill, ctx)`；未提供時由 C++ 基底使用 activation ref。
- callback 必須回傳 `SkillInstanceRef`；Lua error、nil 或錯誤型別均 fail-closed。舊 `usage_identity` 載入時報遷移提示，不得靜默忽略。
- C++ 技能直接覆寫 `getUsageRef(ctx)`；不再暴露 `UsageIdentity` enum、setter 或 Lua 常數。

| 技能入口 | 本票責任 |
|---|---|
| C++ `Skill`／`ViewAsSkillV2` | 提供預設 activation ref 與可覆寫的 `getUsageRef()` |
| Lua `TriggerSkillV2` | factory callback、SWIG ref getter；generic scope 與 Custom 邊界均驗證 |
| Lua `ViewAsSkillV2` | factory callback、SWIG ref getter；接入既有 ViewAsSkillV2 quota lifecycle |
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
