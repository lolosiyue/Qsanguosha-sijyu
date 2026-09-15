# Google Sheets 真人驗收與前端修正（2026-09-15）

## 結論

**PARTIAL／BLOCKED。** 前端修正已部署至 SGS，focused tests 通過；本輪唯一一局真人 05P 在第二輪「絕途」保留牌互動受阻，沒有 GAME_OVER 或勝方。從側欄正常要求關閉後，bridge 退出碼為 **86**，再次符合 E3 的失敗判據；沒有強制終止。程序與監聽埠已清理。

本輪使用者核准一次 Sheets 目標增量建置、前端／gateway focused tests、同一局真人儲存格操作及清理核對，限本輪解除 60 秒限制；遇 E3／E4 停止原生延伸除錯。未執行 CTest、未重開第二局、未啟用託管、未修改原生引擎或外部 Lua 來源。

## 環境與證據

| 項目 | 值 |
|---|---|
| 工作樹 | `L:\finaldebug\QSanguosha-v2`，`debug` |
| HEAD | `f3966fb8fbcf4a62cb21cf060e0938d2294f2876`；另有原本未提交差異，並非乾淨 HEAD 驗收 |
| 遊戲 | `05p`，真人 1 人＋AI 4 人，`OperationNoLimit=true`，完整原有擴展內容 |
| 種子 | `12276844641593113362` |
| 真人 | `sgs1`／曹植[OL]／忠臣 |
| 執行期 | 165 個 AI Lua 檔、18,233 個圖片 hardlink |
| 證據根目錄 | `builds/google-sheets-qa/acceptance-20260915/` |
| 試算表 | 使用者授權的既有 SGS 測試文件；公開報告不列私人文件網址 |

`summary.json` 保存執行檔 SHA-256 與結果；`SGS-blocked.xlsx` 是關閉前從真實 Sheets 匯出的工作表快照。私人執行期與診斷保留於原目錄，不公開配對碼、token 或完整回放內容。

## 本輪完成的前端修改

| 檔案 | 修改／現場證據 |
|---|---|
| `apps-script/Table.gs` | 共用 `interactionPrompt_` 優先保留原生提示；空提示時用 interaction 的技能 ID 與現有翻譯補充。真實工作表與側欄均顯示「是否發動技能『落英』？」 |
| `apps-script/Client.gs` | poll 使用同一提示函式；區分安全重試的讀取失敗、待確認遊戲指令、配對及關閉失敗。現場已看見新版待確認指令提示，重試原指令成功 |
| `apps-script/Sidebar.html` | 記錄錯誤來源；成功 poll 只清除讀取錯誤，操作拒絕／pending 警告跨越 poll 失敗與恢復保留，直到明確操作成功 |
| `tests/client.test.cjs` | 技能提示、讀取／寫入重試、pending payload 與錯誤恢復序列的回歸案例 |
| `frontend-coverage.json` | 移除過時的「尚未建置／尚未部署」描述，保留未完成 gate |

以上三個 Apps Script 檔已經由瀏覽器更新、儲存及讀回核對。修改 Apps Script 後須**重新載入 Sheets，再從 QSanGuosha 選單重開側欄**，既有側欄可能仍使用舊版本。此次未改 sharing 或要求新增權限。

## 驗證結果

| Gate | 結果 | 證據／邊界 |
|---|---|---|
| Sheets bridge 及其 ExcelServer 依賴增量建置 | PASS | `build.log`；`cmake --build --preset debug --target qsanguosha_sheets_bridge --parallel 8` |
| Qt／FMOD 部署、隔離執行期準備 | PASS | `deploy-runtime.log`、`prepare-runtime.log` |
| 最終前端 focused tests | **24/24 PASS** | `frontend-recovery-fix.log`，202.20 ms；之前的 18／20 案例日誌是較早檢查點 |
| Gateway focused contracts | **12/12 PASS** | `gateway-contracts.log` 及其 stderr 日誌 |
| 最終 JavaScript／差異靜態檢查 | PASS | 同時完成獨立靜態審閱；保留操作錯誤的 P2 已修正 |
| 真實 Apps Script 部署 | PASS | Client／Table／Sidebar 儲存及讀回；重新載入後繼續同一局 |
| 真人完整 05P | **BLOCKED** | 第二輪 discard／絕途；無 GAME_OVER、無勝方 |
| 正常原生退出 | **FAIL（E3）** | `native_exit_code=86`、`clean_native_exit=false`、`forced_termination=false` |
| 程序／埠清理 | PASS | `cleanup.json`：17:04:48 +08:00，四個自有程序均不存在，8766／1795／1796 無 listener |
| 28 類逐一真人互動、多人隔離／E4、CI、交付包、full-suite | NOT RUN／未完整驗收 | 來源映射及 focused tests 不能替代這些 gate |

### 本局實際走過的互動

- 配對、私有開房、準備、勾選選將及預檢／提交。
- `exchange_card`：聲東擊西選兩張牌；`response_card`：無閃時取消受傷，以及有閃時成功防禦。
- `skill_invoke`：落英選「是」，戰報顯示 `sgs1 invoked olluoying`。
- `play_card`：青釭劍、順手牽羊、刺殺、殺、無天無界；實際卡牌／目標勾選、戰報及傷害可見。
- `choose_card`：順手牽羊選 `-1` 隨機暗置手牌候選，後取得閃；未將這項觀察當作 E4 多人可見性通過。
- `nullification`：取消與實際無懈可擊提交均走過；聯軍盛宴結算後遊戲繼續。
- `discard_card`：第一輪選兩張棄牌；`ask_peach`：沒有桃時略過，後由 AI 救援。
- `choice`：無天無界完整顯示楊武、逆瀾、絕途及說明，選絕途成功。
- 詳情：查詢牌名、規則、圖片及本人手牌花色。
- 不確定結果恢復：透過「重試待確認指令」讀回既有收據或完成原指令，沒有重新出牌。

## 新的阻塞：絕途保留牌預檢

停止時畫面：第二輪、`sgs1` discard、2/3 HP、手牌數 2；牌堆 818，五人存活，勝方空白。

| 觀察 | 實際結果 |
|---|---|
| 互動 | `response_card`，提示「绝途：请选择保留的各花色牌」 |
| 本人兩張手牌詳情 | `493` 鍵／club／A；`1022` 殺／club／9；均 owner=`sgs1`、place=`0` |
| 選技能 `zujuetu:1` 後預檢 | 候選縮為該技能、兩張手牌及裝備，未補齊時 `incomplete_card_selection` |
| 兩張梅花一起選 | `subcard_rejected`，符合每花色只能留一張的限制 |
| 技能＋單獨 493 | `incomplete_card_selection` |
| 技能＋單獨 1022 | 經原指令重試確認後仍為 `incomplete_card_selection` |
| 取消 | 「目前互動不能取消。」 |

只讀核對 `extensions/olClan.lua:4944` 起的現有技能：`@@zujuetu!` 的 `view_filter` 禁止同花色重複，`view_as` 要求涵蓋本人全部手牌花色；因此依目前顯示的兩張梅花牌，任一單張應能構成保留集合。但單牌均未被原生預檢接受，且前端沒有可用的取消出口。**根因尚未隔離**：未證實是原生手牌鏡像、ViewAs 上下文或其他互動轉換問題，不據此直接判定 Lua 技能本身有錯。

現場與報告已保留；未跳過原生驗證、未輸入偽造技能卡、未繞過不可取消限制、未修改引擎繼續此局。此案例不算技能組牌通過。

## 仍需另案處理的邊界

1. **E3 再現**：側欄要求正常關閉後顯示「會話已結束，但未正常退出」，exit.json 為 86；無殘留不等於正常退出。
2. **絕途預檢阻塞**：後續需專項比對本人手牌鏡像與 `@@zujuetu!` 的 ViewAs 執行上下文，取得同一合法草稿的原生診斷。此次不延伸原生除錯。
3. **無懈可擊提示上下文**：現有 canonical request builder 保留 source／target，但未保留 `trick_name`；前端不能可靠地靠最後一條戰報推定錦囊名稱。修復完整語意需跨原生互動契約。
4. **E4／多人／CI／打包**：本局沒有關閉這些 gate。完整完成 Google Sheets 端的結論仍不可成立。

## 收尾

- `exit.json` 在 `f7a572a0-c57b-48f7-9ca7-e21e229d229f/` 的本輪私人 slot 目錄內。
- 正常關閉請求失敗已如實保留；後續只停止 gateway 與 Cloudflare 臨時隧道。沒有 force-kill 原生 bridge／helper。
- SGS 保留修正後腳本及失敗會話的工作表快照；臨時 HTTPS 服務已停止，畫面不是正在運作的對局。
- 未 commit／push，未撤回原本 `src/excel/excel-view.cpp`、`tests/CMakeLists.txt`、`tests/client_core/client-core-test.cpp` 與舊報告的 dirty work。
