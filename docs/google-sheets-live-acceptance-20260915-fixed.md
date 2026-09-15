# Google Sheets 修復後真人驗收（2026-09-15）

## 範圍

使用者本輪核准既有 SGS 文件的一局 05P、真人 1＋AI 4、無託管、最多 60 分鐘，核對 GAME_OVER／勝方／正常退出／程序及埠清理；遇新阻塞即保留證據並停止。未授權擴大原生除錯或反覆重開對局。

修復與 focused 證據見 [共用規則與隱私修復](google-sheets-completion-20260915.md)。

## 對局資料

| 項目 | 值 |
|---|---|
| 工作樹 | `L:\finaldebug\QSanguosha-v2`，`debug`，另有未提交差異 |
| 模式 | `05p`，1 真人＋4 AI，OperationNoLimit=true、CountDownSeconds=0 |
| Seed | `7285743983534185411`，由真實 Sheets 戰報讀取 |
| 真人 | `sgs1`，群陰・神呂布，反賊 |
| 原始素材 | 前一局完整擴展 runtime；未修改外部 Lua |
| 產物 | 重新建置 `QSanguoshaSheetsBridge.exe`、`QSanguoshaExcelServer.exe`，SHA-256 見 focused manifest |
| 證據目錄 | `builds/google-sheets-qa/acceptance-20260915-fixed/` |
| 服務期限 | 以 `launch.json` 的開始與 60 分鐘 deadline 為準 |

## 現場記錄

- 配對既有 SGS 文件，確認工作表房間設定，建立五人私有對局。
- 以 D7 勾選武將，預檢成功後提交。
- 無懈可擊提示已顯示「五谷豐登」；略過與實際無懈提交均走過，戰報確認打出牌及無謀／狂暴結算後進入本人出牌階段。
- 以儲存格選牌提交赤血青鋒、八卦陣[青春]，牌桌確認已裝備。
- 逐近棄遠指定主公，`choose_card` 顯示公開赤兔與 `-1` 隨機暗置手牌候選；未以這個單人觀察取代多人文件隔離驗收。
- 自動輪詢期間側欄按鈕會鎖定；需停止更新、等待既有請求結束後送出操作，再恢復更新。UI 工具等待超時不表示遊戲命令已送出，均以後續側欄與工作表核對。
- 第二輪完成射擊的出閃、八卦選「是」、無閃略過、無中生有、無謀移除標記、射擊指定主公並造成傷害、失算代價、棄置多餘八卦陣。
- 查詢本人手牌「兄弟齊心」得到完整規則、花色點數、owner/place 與卡牌識別；保留第一輪快照 `SGS-turn1.xlsx`。
- 文字呈現限制：部分擴展名稱／參數化選項仍顯示 `sfofl_young_eight_diagram`、`shisuan`、`shisuan2=sgs3`、`damaged`／`dismark`，未在本局中新增原生或 Lua 修復。
- 第三輪走過瀕死前龍版桃、死亡逃殺、`ask_peach` 自救及陣亡後選人；沒有可用救援牌時逐項回應，未託管。`ask_peach` 仍只有通用提示，需補足救援對象與需求。
- 玄武暗魂附體顯示可選、範圍 0～1，但「取消互動」回報不能取消；選定夏侯楙後可正常提交並繼續。這是呈現與取消規則不一致，尚未定位原因；沒有在本局中重開或修改規則。
- 陣亡後仍收到 `Promote_xuanwu` 技能確認；選「否」成功，隨後牌堆與 AI 回合繼續推進。

## 結果

**PASS：本輪唯一一局真人儲存格 05P 已完成 GAME_OVER、勝方及正常關閉。**

| Gate | 證據 |
|---|---|
| 完整對局 | 第五輪，`QSAN Board!G2=GAME_OVER`；真人第三輪陣亡，完成陣亡互動後繼續觀戰，未託管、未重開 |
| 勝方 | `QSAN Board!C4` 的 `winner_tokens=lord,loyalist`，主公＋忠臣勝；最終匯出 `SGS-final.xlsx` |
| 原生正常退出 | 側欄「已正常關閉會話」；slot `exit.json`：exit 0、clean_native_exit=true、forced_termination=false、cleanup_error=null |
| 程序清理 | Bridge 49316、ExcelServer 50280 均正常退出；Gateway 39804 在原生關閉後 Ctrl-C 停止（exit 1），已核對路徑的 cloudflared 49112 隨後停止 |
| 埠釋放 | 實際使用的 2082、4205、4206、8766、20241 均無 listener；`cleanup.json` 無殘留程序 |
| 時間上限 | 20:18:45 啟動，21:01:58 完成清理，少於核准的 60 分鐘 |
| 機器可讀紀錄 | `game-run.json` 綜合 UI 匯出與 cleanup；slot 的 `full_game_acceptance=NOT_RUN` 是 gateway 不自行認證 GUI 對局的保守欄位，未被改寫 |

本結果解除本局 Sheets 路徑的 E3 正常關閉阻塞，並證明可完成一局；不等同所有客戶端、所有模式或每一種互動均通過。絕途修復在 focused 案例通過，本局使用神呂布，沒有再次抽到絕途武將。

仍待完善／驗收：`ask_peach` 的具體對象與需求、部分技能與參數化選項翻譯、附體選人的可取消呈現，以及勝方將結構資料轉為易讀文字。28 類逐項互動、多人文件隔離、遠端 CI 與交付包尚未完成。這些限制沒有被本局 PASS 覆蓋。未 commit、未 push。
