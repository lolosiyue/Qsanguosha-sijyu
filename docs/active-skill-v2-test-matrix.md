# ActiveSkillV2 驗證矩陣

本矩陣記錄核心重構的實際驗證證據。它不授權遷移任何正式技能；正式技能仍須依
[遷移規範](active-skill-v2-migration-guide.md) 逐項人工審議。

## 已自動驗證

| 範圍 | 證據 | 結果 |
|---|---|---|
| instance 名稱、attached parent 與 execution registry | `tests/skill-instance-utils/skill-instance-utils-test` | 通過 |
| replay provenance V2 cross-owner | `tests/replay-game-state/replay-game-state-test` | 通過 |
| replay provenance V1 compatibility | 同上 | 通過；owner 採 initiator best-effort fallback |
| malformed provenance | 同上 | 通過；拒絕 payload |
| Play bridge 的 V2 early-exit 收束 | `Room::useCard()` | 已整合；cost/pay 為 `PayFailed`，取消或無效更新為 `InvalidTargetUpdate`，均發 `Finished` 與 server-only audit |
| Lua fixture 載入與 headless Room 初始化 | `etc/testScenes/active-skill-v2-lua-smoke.txt` | 通過；`QSanguosha.exe --test-scenario=active-skill-v2-lua-smoke --headless` 返回 0 |
| 全專案 C++／SWIG 整合 | `tools/build-release.ps1` Release x64 | 通過（0 errors） |

## `~test` 手動整合場景

既有 `TestPackage` 位於 `src/package/standard-generals.cpp`，package 名稱為 `~test`。
`active_skill_v2_tester` 持有 `active_skill_v2_test`：它是零副牌、選一名其他存活角色、
由 server 建立普通 Slash 的 C++ 合成技能。它只供下列手動場景使用，不得改動正常武將包。
`extensions/active-skill-v2-test.lua` 提供等價 Lua fixture
`active_skill_v2_lua_tester`，並刻意令 `effect` 回傳 nil 以驗證 ContinueEffects 語意。
`etc/testScenes/active-skill-v2-lua-smoke.txt` 是 Lua 載入與 Room 初始化的 headless 入口：
`release\QSanguosha.exe --test-scenario=active-skill-v2-lua-smoke --headless`。主程式會優先辨識
`--test-scenario`，不會錯誤進入裸 `--headless` 的 stress mode。

| 場景 | 入口 | 期望結果 |
|---|---|---|
| Play | V2 proxy use | sourceRef、activationRef 與 execution audit 一致；Finished 一次 |
| response-use | V2 轉換牌 | legacy packet fallback 與 server re-create 均不信任 client source |
| pure response | V2 response | cost/pay failure 結束為 PayFailed，並發 Finished |
| nullification | V2 virtual card | effect skip 不取消原錦囊流程 |
| multi-instance | 同名兩個 activation | 精確 ID 可選；legacy AI 僅選最小有效 ID |
| attached | 不同 root owner | V2 provenance 回放保留兩個 owner |
| delegation/interceptor | WillInvoke 改 invoker/card/targets | audit 只在 server 記 executionID，client/replay 不含此 ID |

## 目前限制與後續票據

- 目前沒有通用 `ActiveSkillCard`；`active_skill_v2_test` 只覆蓋 V2 建立普通卡的路徑。
- 現有 V2 AI callback 回傳 legacy card 字串，尚無 proxy request 的 selection／target 編碼，不能驅動 `ActiveSkillV2` custom action。
- Lua fixture 仍需在實際 Room 對局中執行；目前未自動化其 lifecycle 場景。
- 上述 smoke 僅驗證 extension 載入與 Room 初始化；不代表 Lua V2 技能已被 AI 啟動或完成 lifecycle。
- `DoLuaScript()` 在 `--headless` 下會以 `qCritical` 報告 Lua 載入錯誤而非開啟 modal dialog，讓本機自動化可取得失敗原因；GUI 模式維持既有對話框。
- `LuaActiveSkillV2`、選用 AI callback、provenance V2 與 execution audit 已經編譯整合；仍需以合成技能自動化端到端驗證。
- 不得以本矩陣代替 validate/onUse 的人工分類；請使用 migration guide 第 6、7、14 節的模板。
