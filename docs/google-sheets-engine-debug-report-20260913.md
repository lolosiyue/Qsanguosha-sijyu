# Google Sheets 前端驗收與原生除錯報告

日期：2026-09-13（香港時間）  
工作區：`L:\finaldebug\QSanguosha-v2`  
狀態：依使用者指示停止延伸原生除錯與測試，先交付報告。未 commit、未 push。

## 結論與範圍

Google Sheets 五個前端檔案已安裝並授權，實際完成配對、開房、準備、儲存格選將、一般出牌、求桃回應與交換牌。**尚未完成一局 GAME_OVER，不能宣告完整對局驗收通過。**

本次從 Sheets 前端延伸到 ClientCore 狀態投影、Room worker 停止與 helper 生命週期。使用者雖曾授權建置、完整測試及解除 60 秒限制，凜仍應在工作擴大為原生引擎除錯時先說明並取得意見，而不是持續增加修復及測試。此處是本次工作方式的問題。

後續規則：遇到超出原需求、連續出現新根因或明顯增加時間的問題，先停止擴張，說明已知證據、影響、可選方案、預估時間與現有工作如何保留；取得使用者意見後才新增修復、建置或測試。原生缺陷留待使用者決定處理原生缺陷時再解決。

## 問題清單

| 編號 | 問題／影響 | 已知程度 | 本輪狀態 |
|---|---|---|---|
| E1 | UI 效果提示被誤當成玩家擁有技能，出牌被 `unsupported_skill` 擋住 | 已確認根因 | 已修改 reducer；精準測試通過，後續真實 Sheets 南蠻入侵已成功提交 |
| E2 | 移牌事件漏同步玩家命名牌堆，清儉無牌可選／`subcard_rejected` | 已確認根因 | 已修改 reducer 並通過精準測試；未重跑真實清儉驗收 |
| E3 | 進行中的私有房關閉未正常完成，bridge 退出碼 86 | 已確認失敗，最終根因未確認 | 已有非同步關閉嘗試與診斷；重跑仍失敗，不能視為修好 |
| E4 | 正值牌 ID 與 `open:false` 的可見性判斷 | 待確認疑慮 | 尚未追完最終 wire 編碼；不能直接斷言洩漏，也不能宣告多人隱私驗收通過 |
| E5 | 部分原生互動提示直接顯示 `@olqingjian` 等代號 | 已確認呈現缺口 | 沿用既有提示格式化函式，新增 `prompt_text`；精準測試通過，Table.gs 已儲存核對 |
| D1 | 啟動時找不到 `Qt6Cored.dll` | 已確認部署問題 | 已補同版 Qt／FMOD runtime；這不是遊戲規則回歸 |
| D2 | 開發目錄含未宣告 Lua 檔，規則身分驗證拒絕該資產根目錄 | 已確認部署前提 | 用既有 packager 整理宣告內容，保留全部配置擴展，未放寬規則身分驗證 |

## E1：效果提示污染技能清單

`UPDATE_PLAYER_UI_STATE` 的 `maxCardsSkills`、攻防距離技能及虛擬裝備欄位是顯示用效果提示。它們可能包含其他玩家的技能或參數，例如 `#mobilexinxianghai^-1^sgs1`、`yongsi^F7^sgs2`。

原本 reducer 把這些字串追加到玩家的 `skills`，原生規則驗證因找不到這種技能名稱而拒絕出牌。單純去掉 `^` 後面的字串也不正確，因為該效果未必屬於本人。

本輪改動：保留效果提示與數值，但技能擁有權只沿用既有權威技能訊息。檔案：`src/client/core/client-game-state-reducer.cpp`、`tests/client_core/client-core-test.cpp`。

證據：`native-focused-final.log` 為 2/2 通過；第三次真實 Sheets 嘗試已用儲存格選取南蠻入侵，原生預檢通過並提交成功。這只能證明該路徑恢復，不能推定全部技能均可用。

## E2：清儉牌堆同步遺漏

實際流程：選夏侯惇[OL] → `exchange_card` 選入毒 1073 → `response_card` 要求清儉分配。此時工作表沒有牌候選，選技能預檢回 `incomplete_card_selection`。把先前已觀察到的牌補回測試列後，原生仍回 `subcard_rejected`；此步驟不能取消，該局因此停止。

確認的資料路徑：

1. `ClientGameStateReducer::applyCardMovement()` 更新 card 的 `owner/place/pile`，卻沒有同步 player 的 `piles`。
2. `ClientPlayerModel::applyVisibleZones()` 從 player `piles` 重建原生玩家牌堆。
3. `ClientRulesSession` 的選牌池透過 `getExpandPileCardIds()` 讀取 `Player::getPile()`。
4. `OLQingjianVS` 使用 `getPile("olqingjian")` 檢查可選牌；投影後的牌堆為空，故拒絕。

本輪已做的修改：GET／LOSE 特殊區域移牌同步增減命名牌堆；去重；負 ID 不生成可選卡牌。ClientCore 回歸涵蓋移入、重複移入、`-1` 及移出。

限制：這些測試尚未覆蓋 E4 的 `open:false` 正 ID 情況；原生清儉 ViewAs 端到端案例尚未完成。**目前不能把此補丁當成完整驗收完成。**

曾有「payload.selection 巢狀欄位漏讀」的初步判斷；後續審查發現目前 `InteractionRequest::toJson()` 會展平該欄位，因此新增的 nested fallback 不是此次根因修復。該相容分支仍留在未提交修改中，後續整理應評估撤除，不應把它列為已證明必要的修復。

## E3：進行中關閉失敗

| 證據 | 結果 |
|---|---|
| 真實 Sheets 第二、三次嘗試關閉 | bridge exit 86，`clean_native_exit=false` |
| `shutdown-play-card` 原生探測 | 已到 `play_card`；active slot exit 86，idle slot exit 0 |
| 同次獨立 owner-loss 探測 | 預期 exit 86；它與 active slot 是不同程序，不能混淆 |
| 該次 active slot 清理 | 無記錄到殘留程序，已觀察埠釋放；仍不等於正常退出 |
| 加入診斷後的 `shutdown-trace` | 公開摘要仍為 `passed:false`；未完成私有診斷解讀 |

最初 helper 曾記錄 `Room worker did not stop before runtime destruction`。本輪改為保留主事件迴圈，主動停止並等待房間清理後才退出；亦加入關閉期間禁止新房間／新連線的防護。重跑後雖未再看到原先 fatal，仍未正常退出。

已查到的一般 05p 回合迴圈會檢查 session terminal，互動等待也有 abort 檢查，因此尚不能斷言普通回合迴圈忽略停止。先前 active helper 沒有 `CARD_LIFETIME_WORKER_FINAL`，可能是 worker 尚未退出或停在 finalization；目前沒有足夠證據二選一。

診斷開關 `QSAN_XP_SHUTDOWN_TRACE=1` 已加入 helper 關閉、Server 清理狀態、Room worker/destructor、Lua finalization 與 controller deadline。**沒有延長 timeout、降低清理判據或把強制終止算成功。**使用者拒絕了最後一次讀取新私有診斷的提權請求，凜未以其他路徑繞過；之後依指示停止除錯。

## E4：待確認的牌面可見性

審查發現不能只用 `card_id >= 0` 代表玩家有權看見牌面：移牌資料另有 `open`，中間序列化函式可能仍攜帶正 ID。尚未追完送至客戶端前是否另行遮罩，所以這是一項需追完資料流的疑慮，不是已證實的線上洩漏。

後續若獲准處理，應先核對最終 wire payload，再補 `open:false + positive ID`、本人可見牌堆、對手暗牌及移出失效的案例；不能直接放寬選牌或用全牌庫猜牌。

## 建置與測試結果

所有相對日誌路徑以下列目錄為基準：`builds/google-sheets-qa/validation-20260912-01/`。

| 項目 | 已執行結果 | 邊界／日誌 |
|---|---|---|
| 初版完整 Debug build | PASS | `full-build.log`；早於後續原生修正 |
| 初版完整 CTest | 55/55 PASS，1,694.63 秒 | `full-ctest.log`；不能當作最新原生修改的 full-suite 證據 |
| 最新受影響目標增量建置 | PASS | `pile-and-shutdown-trace-build.log` |
| 最新 ClientCore＋規則生命週期 | 2/2 PASS，8.02 秒 | `pile-core-rules-test.log` |
| 最新 ExcelView focused executable | exit 0 | 已執行；`pile-view-test.log` 沒有文字輸出，不宣稱未記錄的案例數 |
| Apps Script 純前端測試 | 18/18 PASS | `frontend-prompt-final.log` |
| Python gateway contracts | 12/12 PASS | `gateway-final.log` |
| 真實 Sheets 完整 05p | BLOCKED／未完成 | 無 GAME_OVER、無勝方；見 `real-sheets-attempt-03.json` |
| 最新修改後 full build／full CTest | NOT RUN | 依使用者指示停止擴張測試 |
| 28 類互動逐類真人操作、多人、CI | NOT RUN | 來源映射與局部測試不能取代這些驗收 |

真實第三次嘗試走過 `choose_general`、`play_card`、`ask_peach`、`exchange_card`、`response_card`。其中清儉分配失敗；曾手動補入診斷牌列，後已清除，因此本次也不能宣稱全程自動候選呈現正常。未啟用託管。

## 已保留的原生修改

這些修改仍在工作區、未提交；凜沒有在使用者要求停止後擅自撤回或繼續修復。

| 檔案 | 改動用途 |
|---|---|
| `src/client/core/client-game-state-reducer.cpp` | 效果提示不再授予技能；特殊牌堆同步 |
| `tests/client_core/client-core-test.cpp` | 對應兩項回歸 |
| `src/excel/excel-view.cpp`、`tests/excel/view-test.cpp` | 提示格式化、候選呈現；含待整理的 nested fallback |
| `src/server/room.h/.cpp` | 非阻塞停止請求、worker 狀態檢查及診斷 |
| `src/server/server-core.h`、`src/server/server.cpp` | 關閉狀態、防止新工作、房間清理完成判斷及診斷 |
| `src/server/room-runtime.cpp` | worker finalization 診斷 |
| `legacy/xp/src/xp-server-main.cpp` | 保留事件迴圈等待清理及診斷 |
| `legacy/xp/src/local-server-controller.cpp` | 關閉 deadline 診斷 |

專門的清儉 native probe 曾提出但未完成；截至停止時，`rules-session-probe.cpp` 與 `check-rules-session.py` 沒有新的內容差異。其他工作區差異不應因本報告而一併提交或撤回。

## 停止與交接

- 所有本輪建置／探測已結束，子代理的延伸工作已停止。
- Gateway、Cloudflare 臨時隧道及兩個來源預覽伺服器的執行會話已停止。
- 停止後程序查詢未列出 SheetsBridge、ExcelServer 或 cloudflared。
- 第三次真實 Sheets 嘗試已有無殘留／埠釋放觀察；最後一次全埠核對因本機權限拒絕未完成，不據此補寫 PASS。
- Google Sheets 與 Apps Script 保留；工作表目前是失敗會話的歷史狀態，不是正在運作的遊戲服務。
- 下一步由使用者決定是否另行處理 E2／E3／E4。凜不再自行開局、重建或重跑完整測試。

關鍵原生探測位置：

- `builds/google-sheets-qa/native-integration-20260913/shutdown-play-card/run-fbe01ae85a794526810bf5b265ad7b28/`
- `builds/google-sheets-qa/native-integration-20260913/shutdown-trace/run-820c590c3b0e49e9bd5f36de858e7257/`

上述目錄含私有診斷，應保留既有存取限制；報告不包含配對碼或會話 token。

