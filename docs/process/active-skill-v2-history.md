# ActiveSkill V2 實作歷程

記錄範圍：2026-07-17 至 2026-09-06。[現行契約](../active-skill-v2-refactor-plan.md)與[測試矩陣](../active-skill-v2-test-matrix.md)分別維護介面及驗收情境。

- 2026-07-29 `@@skill` 指名回應 bridge 補齊：client 依同名技能按鈕解析精確 activation instance，並以包含 reason／pattern 的 `ActiveSkillRequest` 呼叫 `canActivate()`；legacy `response_pattern`／`isAvailable()` 分支保持不變。Lua V2 factory 對舊 `response_pattern` 以遷移提示 fail-fast，尚待 GUI／Room lifecycle 實跑。
- 2026-07-21 client UI bridge 補齊：通用 `ActiveSkillCard` 的 target preview 會建立只讀 `ActiveSkillRequest`，並委派 `canSelectTarget()`／`targetsFeasible()`；技能按鈕以精確 activation instance 呼叫 `canActivate()`，Dashboard 裝備區亦改用 `canSelectCard()`。所有 legacy `ViewAsSkill`／普通 Card 分支及網路協議保持不變；新增 `active_skill_v2_proxy_ui_test` 手動 fixture，尚待完整工具鏈實跑。
- ViewAsSkillV2 amount 擴充：C++／Lua 已加入 `base_amount`、`getBaseAmount()` 與 `getEffectiveAmount(ctx)`；Play、pure response 與候選配額 context 均在生命週期開始時以 base amount 初始化。配額例外維持由 `Limit_Custom` 統一負責，不另增語意重複的 `isUsageExempt`。
- Ticket 13：核心與 fixture 完成（2026-07-20）。配額策略已收斂為單一 `getUsageRef(ctx)`：預設 activation、覆寫可選 immutable source，移除 `UsageIdentity` enum／setter／Lua 常數；保留 legacy activation fallback、source fail-closed、root-source 配額解析與 Lua `get_usage_ref` callback。generic scope 以 committed mark + counted reservation 支援巢狀重入，pay failure／Pay cancel／`StageChange`／`TurnBroken` 會釋放未提交 reservation，bypass 仍 commit。Play 與 pure response 的控制事件會補發 `EffectFinished(NoResult)` 最多一次並重新拋出原事件；effect／target hooks 後會還原 immutable provenance。legacy instance-0 reset 已恢復，`Limit_Custom` 不建立 generic reservation且不自動 add/reset。`tests/skill-instance-utils` 與 `~test` 已補 shared-root、owner isolation、nested、failure/cancel、bypass、reset、Custom 與 finished-once fixture；Lua callback smoke 位於 `lua/test/examples/test_active_skill_v2_usage_ref.lua`。2026-07-30 已以 CMake 完成 Release x64（現行工具鏈為 Qt 6.11.1／VS 2026 v145，見 [`tools/build-release.ps1`](../../tools/build-release.ps1)），build tree 的 SWIG wrapper 亦已自動生成並編譯通過；console／Lua smoke／Room lifecycle 仍待實跑。
- Ticket 10：完成（2026-07-17）。已加入 `LuaViewAsSkillV2`、`sgs.CreateViewAsSkillV2`、read-only `ActiveSkillRequest` getters 與所有 query/cost/pay/target/effect callbacks；Lua callback error 均 fail-closed，effect 的 nil 結果為 `ContinueEffects`。`swig/sanguosha_wrap.cxx` 已由 `tools/swig/swig.exe` 重新產生。驗證：Release x64 0 errors。
- Ticket 11：進行中。已加入 provenance V2（cross-owner source/activation refs）、V1 replay fallback、選用 request-aware AI callback、server-only execution audit 與 replay parser fixture；Play bridge 的 cost/pay/cancel/invalid early exits 現均會 Finished/audit 收束。V2 AI callback 已回傳綁定當前 activation 的 `{ cards, targets, user_string }` 結果，Room 以 server-created proxy 將 choices 送入既有 resolver；尚缺 AI/lifecycle 合成技能端到端場景。
- Ticket 12：進行中。已加入 replay console fixture、驗證矩陣與 `~test` V2 C++/Lua 合成技能；尚缺自動化完整 Room lifecycle matrix。

- 設計契約：已定稿。
- Ticket 1：完成（2026-07-17）。已加入 `SkillInstanceRef`、`SourceAttached`、Room-owned attached registry、跨玩家 parent snapshot（保留舊 8 欄 payload fallback）、root detach cascade 與開局 snapshot。驗證：Release x64 0 errors；`tests/skill-instance-utils` console test passed。
- Ticket 2：完成（2026-07-17）。已加入 Card source/activation identity、UseCard／RespondCard 2/3/4 欄相容 codec、server holder/source resolution、replacement propagation、V1 replay provenance notification/parser 與舊 replay 一次性 diagnostic。驗證：Release x64 0 errors；`skill-instance-utils-test` 覆蓋 packet codec。
- Ticket 3：完成（2026-07-17）。已加入 Room-owned monotonic execution registry、move-only RAII guard、owned QVariant backing、finished-once result、CardEffect execution ID 與擴充 SkillContext identity/mutation/interceptor slots。驗證：Release x64 0 errors；`skill-instance-utils-test` 覆蓋 nested registry、cleanup 與 finished-once。
- Ticket 4：進行中。已完成 Play bridge 的 execution context、standard/custom whole-effect skip 分流、target registry lookup、WillInvoke mutation commit 與 validate replacement single lifecycle；尚缺 deterministic integration fixture。
- Ticket 5、6、8、9：已完成基礎實作（2026-07-17），仍待 Ticket 12 整合矩陣驗證。
  Ticket 7：進行中。通用 `ActiveSkillCard` proxy、selected-card 原子支付與 quota reservation/commit/release 已存在；Release x64 已於 2026-07-30 通過，尚缺完整 Room lifecycle 自動化驗收。
- 正式技能遷移：不在本計劃實作範圍。

## 2026-09-06 工作樹背景

`L:\finaldebug\QSanguosha-v2` 現為本倉庫的主開發分支 `debug` 工作樹，已包含 Protocol V2 cutover（16a49c9）、GameSessionController（eadf59b）、TUI 與 Qt 6.11.1 工具鏈；早先「`view_as_skillV2` 先行實驗分支、不得反向覆蓋 main」的定位已不適用。`H:\Program file\Game\sgs\Qsgs\github\QSanguosha-v2`（分支 `doom`）為主工作樹，負責提交與合併。
