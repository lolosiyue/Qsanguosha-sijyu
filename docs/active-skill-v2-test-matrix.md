# ActiveSkillV2 驗證矩陣

本矩陣記錄核心重構的實際驗證證據。它不授權遷移任何正式技能；正式技能仍須依
[遷移規範](active-skill-v2-migration-guide.md) 逐項人工審議。

## 已自動驗證

| 範圍 | 證據 | 結果 |
|---|---|---|
| instance 名稱、attached parent、execution registry、usage reference 與 reservation ledger 純邏輯 | `tests/skill-instance-utils/skill-instance-utils-test` | 既有案例曾通過；Ticket 13 新增 shared-root、owner isolation、nested、pay/cancel release、bypass commit、reset、Custom 與 finished-once 案例，待本機 Qt toolchain 執行 |
| replay provenance V2 cross-owner | `tests/replay-game-state/replay-game-state-test` | 通過 |
| replay provenance V1 compatibility | 同上 | 通過；owner 採 initiator best-effort fallback |
| malformed provenance | 同上 | 通過；拒絕 payload |
| Play／pure response 的 V2 early-exit 與控制事件收束 | `Room::useCard()`、`Room::askForCard()` | 已整合；pay/cancel 釋放未提交 reservation；`StageChange`／`TurnBroken` 發 `Finished(NoResult)` 最多一次並重新拋出原控制事件 |
| Lua `get_usage_ref` smoke 與 Room 初始化 | `lua/test/examples/test_active_skill_v2_usage_ref.lua` | 待執行；`QSanguosha.exe --lua-test lua/test/examples/test_active_skill_v2_usage_ref.lua`，assertion 失敗必須回傳非零 |
| 全專案 C++／SWIG 整合 | `tools/build-release.ps1` Release x64 | Ticket 13 修改後待執行（本機沒有 qmake／C++ 編譯器） |

## `~test` 手動整合場景

既有 `TestPackage` 位於 `src/package/standard-generals.cpp`，package 名稱為 `~test`。
`active_skill_v2_tester` 持有三個 fixture：`active_skill_v2_test` 是每回合上限 2、以
覆寫 `getUsageRef()` 回傳 source ref、由 server 建立普通 Slash 的 C++ 合成技能；
`active_skill_v2_quota_root` 在開局把前者 attached 給其他玩家，讓多個入口共用同一 root quota；
`active_skill_v2_custom_usage_test` 驗證 `Limit_Custom` 只由作者邏輯讀寫，不會自動呼叫 generic
`addUsage()`。它們只供下列手動場景使用，不得改動正常武將包。
`lua/test/examples/test_active_skill_v2_usage_ref.lua` 是等價 Lua fixture：它以
`sgs.CreateActiveSkillV2` 與 `sgs.CreateTriggerV2Skill` 驗證 `get_usage_ref` callback、
舊欄位遷移提示及錯誤回傳 fail-closed，且不會將測試武將包載入一般對局。
執行入口為：
`QSanguosha.exe --lua-test lua/test/examples/test_active_skill_v2_usage_ref.lua`。

| 場景 | 入口 | 期望結果 |
|---|---|---|
| Play | V2 proxy use | sourceRef、activationRef 與 execution audit 一致；Finished 一次 |
| response-use | V2 轉換牌 | legacy packet fallback 與 server re-create 均不信任 client source |
| pure response | V2 response | cost/pay failure 結束為 PayFailed，並發 Finished |
| nullification | V2 virtual card | effect skip 不取消原錦囊流程 |
| multi-instance | 同名兩個 activation | 精確 ID 可選；legacy AI 僅選最小有效 ID |
| attached | 不同 root owner | V2 provenance 回放保留兩個 owner |
| attached shared quota | 兩名 receiver 使用同一 quota root 的 attached 入口 | 兩次合計達上限；第三次拒絕；另一 root owner 不受影響 |
| nested reservation | 第一個 execution 尚未 commit 時重入相同 root | committed mark + reservation count 達限，不能超發 |
| pay failure／Pay event cancel | reserve 後支付失敗或攔截器取消 | 釋放完全相同的 resolved key；下一次仍可發動 |
| bypass | `EventSkillPay` 設定 `bypass_cost` | 跳過 `pay()`，但 quota 仍 commit |
| control interruption | reserve 後拋出 `StageChange`／`TurnBroken` | 未 commit reservation 釋放；Finished(NoResult) 最多一次；原事件繼續向外拋出 |
| reset／Custom | 精確 context reset；`active_skill_v2_custom_usage_test` | generic reset 只清 identity instance；Custom 無 generic reservation／自動 add/reset |
| delegation/interceptor | WillInvoke 改 invoker/card/targets | audit 只在 server 記 executionID，client/replay 不含此 ID |

## 目前限制與後續票據

- Ticket 13 已完成核心與測試 fixture，但尚未執行工具鏈：`getUsageRef(ctx)` 已將配額所屬實例集中為單一策略入口；source sharing、nested reservation、pay/cancel release、bypass commit、reset、Custom 邊界及控制事件收束均已有代碼／案例。在 Release x64 與 Room lifecycle 實測前，不得宣稱整合測試通過。
- 通用 `ActiveSkillCard` 與 request-aware V2 AI selection／target 結果已存在；目前缺口是把上述 lifecycle 場景納入可重複執行的 Room 端到端測試。
- Lua smoke 仍需在實際 Room 對局中執行；目前未自動化其 lifecycle 場景。
- 上述 smoke 僅驗證 Lua factory／enum 與 Room 初始化；不代表 Lua V2 技能已被 AI 啟動或完成 lifecycle。
- `DoLuaScript()` 在 `--headless` 下會以 `qCritical` 報告 Lua 載入錯誤而非開啟 modal dialog，讓本機自動化可取得失敗原因；GUI 模式維持既有對話框。
- `LuaActiveSkillV2`、選用 AI callback、provenance V2 與 execution audit 曾完成編譯整合；Ticket 13 的 wrapper、quota ledger、immutable provenance 與中斷收束修改仍待重新編譯，再以合成技能自動化端到端驗證。
- 不得以本矩陣代替 validate/onUse 的人工分類；請使用 migration guide 第 6、7、14 節的模板。
