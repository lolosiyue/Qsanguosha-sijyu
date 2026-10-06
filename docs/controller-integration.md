# 手掣對局整合與驗收

固定基底：`bf3671548800167bec4056e1da5842de41bec864`，分支 `feature/controller-only-gameplay`。
此變更只處理對局互動，沒有修改 `qml/home/**` 或 BP 大屏版面。

## 架構與操作

`ControllerService` 在 Qt 主執行緒每 16ms 讀取 SDL gamepad；Qt 擁有所有視窗與事件迴圈。
`ControllerAction` 是語意動作；`ControllerRouter` 管理焦點、巢狀視窗、文字輸入和派發；
`DesktopGamePresentation` 重用既有穩定 ID、選擇草稿和原生 RoomScene intent。
回應仍經 ClientCore、ClientLiveSession 與伺服器原生規則驗證，不以滑鼠座標選取。

| 實體位置 | 動作 |
|---|---|
| D-pad／左搖桿 | 群組內左右移動、上下切換群組；標準視窗採幾何焦點／控制項原生移動 |
| South | 選取／反選；按鈕啟動；文字欄位開啟手掣文字鍵盤 |
| West | 確認；觀星面板／文字鍵盤／合約對話框使用明確確認按鈕 |
| East | 取消／返回；強制要求拒絕取消，或只清除本地草稿 |
| North | 已公開的卡牌、技能或控制項詳情；沒有個別詳情時開啟文字快照 |
| LB／RB | 上／下一群組或 Qt 控制項；可離開表格，不被表格 Tab 攔住 |
| LT／RT／右搖桿 Y | 翻頁／捲動；對局候選每次跨十項，支援 50P 目標瀏覽 |
| Start | 對局選單；巢狀視窗中移到下一控制項 |
| Back | 恢復可見且有效的焦點 |
| L3／R3 | 原生排序控制項前移／後移；也提供可由 D-pad＋South 操作的明確按鈕 |

多張卡牌、目標與多票數使用原生選擇順序；複數選取的文字標籤顯示序號。
Anytime／preshow 技能和技能選項透過既有技能按鈕；選單提供排序、觀星控制面板、
加機器人、填席開局、合法暫停、投降、聊天及明確的託管入口。託管不屬於 controller-only 驗收。
結算、再開局、存檔與返回主選單保留原生 Qt 按鈕行為。

確認與取消不重複觸發；方向重複先等 360ms，再每 150ms 一次。
熱插拔／程式重新取得焦點後須先回到中立；舊裝置 epoch 和跨請求／跨視窗的排隊動作會被丟棄。
左搖桿預設死區 0.25，進入門檻加 0.15，避免邊界抖動。只有一個 SDL owner，Qt 不讀取第二個手掣來源。
鍵盤／滑鼠正常模式仍可共存；診斷 controller-only 模式拒絕外部鍵盤、滑鼠按鍵、滾輪和觸控事件。

`QSettings` 支援 `Controller/Deadzone`、`Controller/MappingFile` 和
`Controller/Bindings/<SDL button name>`。SDL 名稱為 `a,b,x,y,dpup,dpdown,dpleft,dpright,...`，
不是控制器印刷字母的假設。設定介面與不同實體裝置配置還需要產品驗收。

文字 fallback 支援可編輯 QLineEdit／QTextEdit／QPlainTextEdit，ASCII 按鈕與 Unicode `U+` 十六進位碼點。
尊重密碼遮罩、最大長度、input mask 和 validator；只有 Done 提交，取消保留原文字。
沒有承諾提供完整中文輸入法。檔案選擇器使用 Qt widget 版本以取得可巡覽控制項。

## 有限 Lua／QML 合約

標準 Qt 控制項可巡覽；自訂 QML 使用 `parameters.controller_ui` 的替代控制項。
不透明 QML／自訂繪製在 controller-only 模式顯示未支援提示並保持原請求，不自動空回覆。
偵測控制器時也啟用這個邊界，不依賴裝置必須在 RoomScene 建立之後才插入。

```json
{
  "controller_ui": {
    "contract_version": 1,
    "title": "選擇與排序",
    "can_cancel": false,
    "fields": [
      {"key":"choice","type":"choice","options":[{"value":"red","label":"紅"},{"value":"blue","label":"藍"}]},
      {"key":"order","type":"selection","options":[{"value":"first"},{"value":"second"}],"min_selection":1,"max_selection":2},
      {"key":"count","type":"integer","minimum":0,"maximum":10,"default":0},
      {"key":"note","type":"text","max_length":128,"default":""},
      {"key":"enabled","type":"boolean","default":true}
    ]
  }
}
```

一般回應是含全部欄位且沒有額外欄位的 QVariantMap；selection 是唯一值的有序字串清單。
選项最多 1000 個、欄位最多 32 個；整數上下限在 ±1000000 內，文字最多 4096 UTF-16 單位。
可取消合約必須明確提供 `cancel_value`，按原值回覆；不得把未宣告取消當作空值。
作者須維持這個合約與原 QML 回應語意一致，使用相同 request ID／session generation 及原生驗證。
此有限契約不表示任意 Lua 腳本都能自動轉換或已驗收。

## 依賴與建置

GUI 保持倉庫的 Qt **6.11** 最低要求與 CI 使用的 **6.11.1**；server／TUI 不連結 SDL 或 Qt Widgets。
SDL **3.4.18**，官方固定 release，zlib 授權，授權文字隨安裝保留。
官方來源：https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18

```sh
cmake -S . -B build-controller -G Ninja \
  -DQSAN_BUILD_GUI=ON -DQSAN_BUILD_SERVER=ON \
  -DQSAN_ENABLE_CONTROLLER=ON -DQSAN_FETCH_SDL3=OFF \
  -DSDL3_DIR=/path/to/sdl/lib/cmake/SDL3 \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/gcc_64
cmake --build build-controller
```

若啟用 `QSAN_FETCH_SDL3`，CMake 從官方 release 取源碼，核對 SHA256
`9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3`，使用靜態庫且不建立 SDL 視窗／音訊。
系統 shared SDL 套件需要額外部署其 runtime，不能只依賴 Qt deployment 自動收集。
目前控制器 backend 僅桌面版；Android、XP 明確停用，Windows／macOS／實體裝置尚須各自驗收。

## 腳本與通過標準

在固定資料及 companion extensions 版本下啟動獨立 server，等其 `Listening` 訊息，再啟動 GUI。
正常 `02p` 房間使用手掣選單填機器人開局；不得傳 `--auto-robots`、`--test-general` 或託管選項。
種子本例為 `367154`、operation timeout 為 0、AI delay 為 0。

```sh
QSAN_CONTROLLER_SERVER_TRACE=/absolute/run/server.jsonl ./qsanguosha_server \
  --asset-root /absolute/runtime --config /absolute/run/server.ini \
  --port 19549 --websocket-port 19550 --seed 367154 --operation-timeout 0 --ai-delay 0
./QSanguosha -connect:127.0.0.1:19549 --seed 367154 \
  --controller-virtual-input /absolute/run/controller.sock \
  --controller-trace /absolute/run/gui.jsonl
python3 tools/autotest/controller_gamepad_driver.py \
  --socket /absolute/run/controller.sock --trace /absolute/run/gui.jsonl \
  --server-trace /absolute/run/server.jsonl --play-basic --return-menu \
  --seconds 90 --result /absolute/run/result.json
```

診斷通道只接受物理按鍵／軸值／attach／detach JSONL，UserAccessOnly；不接受語意動作、直接回覆或遊戲規則呼叫。
`--script` 可重播物理 JSONL，每筆可含 `wait_ms`。SDL 虛擬裝置進入與真實裝置相同的硬體事件層。
trace 包含自身可見卡牌／協定資料，只應用於獲授權的本地驗收，檔案設為 owner-only。

完整對局通過須有 game start、每個人類請求的物理輸入、Core accepted、唯一 wire reply，
與 server command／message ID／reply-to 關聯一致；另有該玩家原生用牌成功、game over、原生結算控制項、返回主選單。
timeout／supersession、託管、未處理動作、不透明自訂互動、外部滑鼠／鍵盤均使驗收失敗或要求人工判定。
GUI、遊戲邏輯、SDL 虛擬輸入、實體手掣四種證據分別記錄，不互相代替。

已完成：QtCore 18 項 probe；SDL 虛擬裝置按鍵／死區／重連 smoke；Qt 6.8 相容副本上的 02p offscreen GUI 對局。
其後由 parent 在現有官方 Qt 6.11.1 SDK 驗收實際顯示的桌面、結算、返回主選單、OSK 預覽和焦點。
固定 02p 對局共有 26 個人類請求、26 次 Core accepted、26 個 wire reply 與 26 次 server validated；
另有 17 次原生用牌解析，結算／主選單／焦點檢查全數通過，Core probe 為 18/18。
以合成 incoming request 執行的有時限 fixture 通過 50P 候選 p41 翻頁、強制要求取消阻擋、按住確認、
OSK 取消保留文字、有限自訂合約的整數／布林／`a&中`／反向選擇順序，以及觀星重排與上下堆分配。
這些 fixture 是輸入與 GUI 控制項驗證，不能當作完整 50P 對局或所有 Lua 的規則驗收。

parent 驗收包的 Library ID：`libfile_0b0921745e8481918ddcc9976e3f201e`；
包 SHA256：`816b546a7d9d336ab279f163952b4478697145bc69a77c66f0eb4ac13a6e6476`。
本工作樹兩次 Library 傳輸失敗，改由已授權 task 訊息取得精確三檔 patch；本地核對解壓 7753 bytes，
SHA256 `4b3b288da15987c41144096332727ed9344b305b13fc8b93bbebce5bca7e195f`。
未在此環境重跑 parent 的 GUI；完整驗收包仍由 parent 保留。其凍結 GUI binary SHA256 為
`8bce100ff20552cbb0814cfa0dfe0513f75b40a03ac846795a93e409cad7ba23`，server binary SHA256 為
`97965980d9ee4a98298a6a4d6ccd2954c7dbce4f66587ba80d5008f400d086c9`。
parent 的缺失 home SVG 本地 workaround 沒有納入 patch。

實體手掣、其他 OS、password／mask／validator 的 live OSK、進階分配／KOF／動態技能、
結算再來一局與存檔仍未驗收；不能宣稱所有 29 類互動已全部通過。
BP 組只需消費既有 action model／焦點標記與詳情資料；不得把大屏版面重寫納入這個 patch。
