# Google Sheets 共用規則與隱私修復（2026-09-15）

## 範圍與驗收邊界

延續使用者「繼續完善 google sheets」要求，處理絕途選牌、無懈可擊錦囊名稱及 E4 移牌封包隱私。E3 的獨立修復與證據見 [原生關閉修復](e3-native-shutdown-fix-20260915.md)。

本頁不取代 [上一局真人驗收](google-sheets-acceptance-20260915.md)：該局停在第二輪，沒有 GAME_OVER 或勝方。本輪 focused 程式均以 55 秒外部 timeout 限制；沒有執行 CTest。

## 絕途：根因與修法

以原驗收完整擴展素材重建 `@@zujuetu!`、兩張本人梅花牌（493 鍵、1022 殺）、技能實例 1 的合法單牌草稿。這是最小重建場景，不是上一局完整狀態快照。

- 原生 registry 確認兩張牌均為 club。
- 完整技能實例資料下，未修復結果是 `known=true`、`can_confirm=false`、`incomplete_card_selection`。
- 在 `builds/` 的獨立素材副本加入暫時診斷，確認模式為 `@@zujuetu!`、已選一張牌；呼叫 `sgs.Self:getHandcards()` 失敗，錯誤為 `attempt to index a nil value (field 'Self')`。
- C++ 已設定 `QSanEngine::Self`，SWIG 卻未公開 Lua 的 `sgs.Self`。技能 callback 因此失敗，並被選牌驗證呈現為組牌未完成。
- 最小修復在 `swig/sanguosha.i` 公開唯讀 getter，沿用 native `setEngineSelf()` 與清理機制。支援沒有自有卡牌的本人玩家，不經卡牌擁有者反查，不改外部 Lua 技能。

SWIG 的 Lua module 變數透過 getter 存取，`%immutable` 可限制寫入，參考 [官方 Lua bindings 文件](https://www.swig.org/Doc4.4/Lua.html)。這項修改沿用同步 query 的既有生命週期，沒有新增巢狀／重入保證。

## 無懈可擊

`ProtocolInteractionRequestBuilder` 在沒有明確 prompt 時保留 `trick_name` 作提示；明確 prompt 優先，缺少兩者時維持空值。新增三個契約案例核對名稱、來源、目標與原有互動型別。

## E4 移牌隱私

`Room::notifyMoveCards()` 逐接收者建立 move 副本，未授權 ID 在送出前改成 `S_UNKNOWN_CARD_ID`，保留數量、順序與原始 move。公開來源、獲授權牌堆觀察者，以及既有 special/draw visible 旗標例外維持相容。

`CardsMoveStruct::tryParse()` 補上未知 ID／空 card 的檢查，避免舊解析路徑對已遮蔽的 `-1` 解參照。

驗證分兩層：notifier 測試讀回 `ServerPlayer::message_ready` 的實際編碼封包；Excel/Sheets view 測試確認隱藏 ID 不產生可查詢的牌面，並保留手牌數與本人授權候選。

## 驗證紀錄

本輪日誌與重建請求存於 `builds/sheets-completion-20260915/`；`verified-results.json` 核對原生結果與 SHA-256。工作樹為 `debug`，基準 HEAD `f3966fb8fbcf4a62cb21cf060e0938d2294f2876`，另有未提交差異。

| 檢查 | 結果 | 證據與邊界 |
|---|---|---|
| SWIG 重產、Sheets bridge/helper、probe、server tests 建置 | PASS | `build-final-lua-linkage.log`、`build-probe-context.log`；generated getter 讀 native Self 並使用 immutable setter |
| Qt／FMOD 部署 | PASS | `deploy-runtime.log`；目標位於 `excel-debug` |
| 絕途單張 493 | PASS，10.859 秒 | `juetu-fixed-single493`，`can_confirm=true`，`#zujuetuCard[club:A]:493:` |
| 絕途單張 1022 | PASS，11.344 秒 | `juetu-fixed-single1022`，合法技能實例與 wire reply |
| 絕途兩張同花色 | PASS，11.313 秒 | `juetu-fixed-duplicate-suit`，仍拒絕為 `subcard_rejected`、wire=null |
| 規則生命週期與 Lua Self | PASS，20.984 秒 | `rules-lifecycle-final`；含手牌、空手、唯讀拒寫、clear 後 nil，以及原有 A-B-A／無效輸入恢復；既有 `verify_native` 核對通過 |
| E4 實際編碼封包 | PASS，15.156 秒 | `room-notifier-final`；私有牌堆／手牌遮蔽、授權牌堆 viewer、special/draw visible 旗標、公開牌來源、控制路由及 CARD_LIFETIME_ZERO |
| TUI request 契約（含無懈三案例） | PASS，10.469 秒 | `tui-contract-complete-assets`，failures=0 |
| Excel／Sheets view | PASS，0.125 秒 | `excel-view`；消費端隱藏牌面、牌数與本人候選，非多人真人驗收 |
| 靜態差異與獨立審閱 | PASS | `diff-check.log`；已修正 reviewer 發現的 probe 缺少 ClientRoomContext fixture 問題 |
| 本輪真人 05P／正常 Sheets 關閉 | PASS（另行授權的一局） | 第五輪 GAME_OVER，主公＋忠臣勝；exit 0、無強制終止、程序及埠釋放，見[真人驗收](google-sheets-live-acceptance-20260915-fixed.md) |

診斷途中保留的失敗也在同一證據目錄：最小請求缺技能實例、TUI 隔離目錄缺素材／coverage artifact、notifier fixture 未初始化房間牌及呼叫 `addToPile` 引發未準備的事件路徑；最後 fixture 改用正式 RoomState 初始化與直接設定 pileOpen。首次新增 Lua C API 測試的 linkage error 以正確 `extern "C"` 宣告修正。這些失敗未被算成通過，也未以關閉 production 驗證來避開。

真人完整 05P 已通過；28 類逐項真人操作、多人文件隔離、CI 與交付包仍須分開驗收。focused manifest 的 full_game=NOT_RUN 保留其原始範圍，真人結果位於另一份 `acceptance-20260915-fixed/game-run.json`。未提交或推送變更。
