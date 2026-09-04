# 純文字 Client 架構研究

- 狀態：設計提案
- 日期：2026-08-30
- 前置依賴：Protocol V2、Client core／UI 完整解耦（由其他 PR 處理）

## 1. 定義與結論

此處的 TUI 是「在 terminal 內運作的純文字 client」，不是用字元模擬 GUI。

介面固定為：

- stdout 逐行追加遊戲事件、狀態及可接受指令；
- stdin 每行輸入一條完整命令；
- 不畫框線、面板、表格、按鈕、卡牌圖或 ASCII art；
- 不清屏、不移動游標、不重畫既有內容；
- 不使用 mouse、方向鍵、terminal raw mode、alternate screen 或 terminal 尺寸排版；
- 每行文字只表達資料或操作，不以所在位置代表 UI 狀態。

因此不需要 FTXUI、ncurses、notcurses 或自製 ANSI renderer。`qsanguosha_tui` 應是
Qt Core／Network 上的 line-oriented text client。

目標資料流分為四層：

1. Protocol V1／V2 codec 與 transport，由其他 PR 負責。
2. `ClientSession`、完整狀態及規則查詢，由 Client core 解耦 PR 負責。
3. `TextClientFrontend` 把純值 event／snapshot／request 格式化成文字行。
4. `TextCommandParser` 把輸入命令轉成 semantic intent，再交回 `ClientSession`。

純文字 frontend 不解析 wire command、不產生 V1／V2 封包，也不持有 `Player *`、
`Card *`、`ClientPlayer *`、`QWidget` 或全域 `Self`／`ClientInstance`。

「完整遊戲」定義為：能在宣告 text-client 相容能力的 server／package 組合中，從連線、
入房、開局、所有遊戲互動一直走到 game over。任意 legacy QML 或未結構化自訂 dialog
必須先轉成 structured schema，或在進房前以 capability negotiation 拒絕不相容組合。

## 2. 現況與缺口

### 2.1 已有能力

- `qsanguosha_client_core` 只連結 `Qt6::Core`。
- 28 類 canonical interaction 已有 typed request／response；另有 1 類明確標示的 legacy
  `QML_INTERACT` adapter。29 類均已有 request id、deadline、validation 與 exactly-once
  completion。
- typed interaction 的 V1 wire reply 已集中經 `LegacyV1InteractionReplyAdapter`。
- `SkillDialogInfo` 已能描述 `guhuo`、`juguan`、`tiansuan` 三種 Lua skill dialog。
- `src/tui/tui-skill-dialog.*` 以無 widget 方式重現這三種 dialog 的選項枚舉、啟用判斷
  與 `Self` tag 寫入；TUI 在技能編號後寫 `=<牌名>` 即可宣告（選牌 + 選視為牌）。
- TUI 的界面文字全部離開了原始碼：`src/tui` 只寫 key，經 `tuiText()` 查
  `lang/<語言>/TUICommon.lua`（254 條，與 `Common.lua` 同一個 `lua/sanguosha.lua`
  載入路徑）。查不到即原樣印出 key，所以缺一條看得見；例外只有 `tui-main.cpp` 的
  命令行說明（在引擎起來之前就要印）與玩家輸入時接受的簡繁別名。

### 2.2 尚未具備完整純文字 client 的部分

- `ClientGameState` 目前只有玩家名稱、生存狀態及 card id space，不是完整局面。
- `Client` 仍建立 `ClientPlayer`、`QTextDocument`、`QFont`、`QMessageBox` 與
  `DesktopInteractionView`。
- `RoomScene` 直接讀取 `ClientInstance`／`Self`，並接收大量 `ClientPlayer *`、HTML
  字串及 GUI 型別 signal。
- 出牌、回應、view-as skill、目標選擇與 `targetsFeasible()` 等規則仍由
  `RoomScene`／`Dashboard` 驅動；目前沒有 production `ICardEligibilityProvider` 能向
  非 GUI client 提供完整 action candidates。
- C++ package 仍有大量 `QDialog *getDialog()` override；任意 `QML_INTERACT` 仍屬
  legacy escape hatch。

只把 prompt 逐行印出仍不能完成一局。關鍵前置工作是把完整狀態、房間操作及出牌／技能
合法性移到 Client core 邊界。

## 3. 上游解耦 PR 的必要契約

純文字 client 開始接 live server 前，上游需同時滿足下列條件。類別名稱可改，語義不可
缺少。

### 3.1 Session port

- Client session 可在只含 Qt Core／Network 與 engine rules 的 target 中建立。
- frontend 由 constructor 注入；core 不自行建立 `DesktopInteractionView`。
- 所有操作只經 `IClientIntentSink` 等語義 API，frontend 不呼叫
  `replyToServer()` 或 protocol command。
- codec／transport 可替換；V1、V2 差異停留在 session 下方。
- ownership、callback thread 及 shutdown 次序有明確契約。

### 3.2 完整 snapshot 與 event

`ClientSnapshot` 至少包含：

- connection、server、room、mode、owner、ready、reconnect 狀態；
- revision、turn、round、phase、focus、countdown；
- 每位玩家的 seat、角色／陣營、武將、HP、存活、連線／托管、手牌數；
- 裝備區、判定區、公開 pile、marks、flags、已知技能與可見 card；
- self 的私有手牌、私有 pile、可操作角色及目前 selection context；
- AG、選將、排陣等暫態公開狀態；
- game result。

snapshot 是狀態查詢的 truth source。`ClientEvent` 只負責 append-only 的日誌、聊天、
牌移動、傷害、技能提示與連線通知，不能要求 frontend 重播全部事件才能還原局面。

snapshot 必須帶單調遞增 revision；訂閱、重新連線或 frontend 重建時可立即取得當前完整
snapshot。舊 revision 及舊 request id 必須可安全丟棄。

### 3.3 Action planner

core 必須提供純值 `ActionCatalog`，令 frontend 不需要呼叫 engine object：

- 當前可用的普通出牌、主動技、view-as skill 與系統操作；
- 可選／不可選的 card、player、target，及不可選原因；
- 增量 selection 後的新候選與 target constraints；
- 最終 card／skill 文字與可否 submit；
- stable action／card／player／skill id，不把記憶體位址暴露給 frontend；
- cancel、timeout、default reply 與不可取消狀態。

planner 由 core／engine rules 計算 `isAvailable()`、card limitation、prohibition、filter、
target filter 及 `targetsFeasible()`。純文字 client 只提交 id 與 semantic intent。

### 3.4 純文字模型

- prompt、log、card／skill 說明及 validation error 不可要求 `QTextDocument` 或 HTML。
- core 提供 localization key、typed arguments 及已格式化 plain-text fallback。
- player、card、skill、option 均另帶 stable id；不得要求 parser 從翻譯後文字反推語意。
- 換行、控制字元及不可見字元需由 formatter 統一轉義，確保一個 record 不會偽造另一個
  prompt 或 command result。

### 3.5 自訂互動與能力協商

- 所有必需的 C++ `getDialog()` 與 QML interaction 應轉成通用 schema 或具名 structured
  interaction contract。
- V2 hello 宣告 `frontend=text`、interaction schema version 及支援的 custom type。
- server 在入房／開局前拒絕不相容 package；不得在對局中才送出未知 prompt 而永久等待。
- capability negotiation 與 wire reject 由 Protocol V2 PR 實作；純文字 client 只提供
  capability 清單及輸出 normalized error。

## 4. 產品範圍

### 4.1 首個完整版本必須支援

- 連線、版本／能力協商、登入、選房／模式、ready、房主開局與加 AI；
- 斷線通知、重新連線、完整 resync、退出；
- 所有玩家與牌區的可見狀態、牌局日誌、聊天；
- 選將、選角色、選項、玩家、花色、勢力、技能確認、trigger order；
- 出牌、回應、棄牌、展示、求桃、無懈、拼點；
- AG、觀星、攻心、遺計、分配、交換、排序、排陣；
- structured skill／custom interaction；
- 托管、投降、取消、逾時安全回覆及 game over。

### 4.2 不屬於完整遊戲的必要條件

- 圖像、皮膚、音訊、影片及視覺特效；
- 全屏介面、框線、面板、按鈕、選單、mouse 或 terminal 尺寸排版；
- GUI 的像素級對應；
- replay editor、作弊／管理工具；
- 任意未宣告 schema 的 legacy QML／QWidget dialog。

Replay playback 可列入後續版本，不能阻塞首個可完整對局的純文字 client。

## 5. 文字輸出契約

### 5.1 Human-readable 模式

輸出是 append-only transcript。程式只能在尾端新增完整行，不能修改已輸出的內容。

```text
已連線至 127.0.0.1:9527，模式 02p。
回合 4：p2 劉備進入出牌階段。
p2 使用「殺」指定 p1。
你的手牌：c17 殺[黑桃7]；c23 閃[紅桃2]。
[請求 r42] 請出牌，剩餘 12 秒。
可用動作：a1 使用 c17，可指定 p2。
可輸入：use r42 a1 p2；或 pass r42。
```

這些行只描述事件、資料及命令語法，不依靠欄位位置、框線或字元圖形表達 UI。

固定規則：

- gameplay transcript 寫 stdout；內部診斷及 crash 資訊寫 stderr；
- UTF-8 pipe 輸出不得包含 ANSI escape、游標控制或清屏序列；
- 每個 player、card、skill、action、option、request 都顯示 stable id；
- 翻譯文字可變，stable id 與輸入命令關鍵字不可隨 locale 改變；
- 每個 interaction 先輸出 request id、deadline、候選及合法命令格式；
- command 被接受、拒絕、逾時或作廢時必須另輸出結果行；
- 事件過多時可用 filter 設定減少非必要敘述，但不能隱藏 active request 或 deadline。

### 5.2 狀態查詢命令

純文字 client 不維持常駐畫面。需要重看狀態時，由玩家輸入查詢命令：

| 命令 | 語意 |
|---|---|
| `status` | 連線、房間、回合、階段、focus、deadline |
| `players` | 所有可見玩家狀態 |
| `hand` | self 手牌及可見私有 pile |
| `skills [player-id]` | 技能、可用 action 及禁用原因 |
| `cards <player-id>` | 裝備、判定區及公開 pile |
| `inspect <stable-id>` | card／skill／player／option 詳細資料 |
| `requests` | 目前 active request 與合法回覆格式 |
| `log [count]` | 最近的 client-side transcript |
| `chat <text>` | 傳送聊天 |
| `trust on|off` | 切換托管 |
| `surrender` | 進入既有投降確認流程 |
| `help [command]` | 顯示命令說明 |
| `quit` | 正常斷線並退出 |

查詢命令只讀取當前 `ClientSnapshot`，不建立另一份會與 core 漂移的局面模型。

### 5.3 Interaction 命令

28 個 canonical interaction 共用少量穩定 command grammar：

| Response shape | 命令形式 |
|---|---|
| option／confirm | `choose <request-id> <option-id>` |
| player selection | `select-players <request-id> <player-id>...` |
| play／response | `use <request-id> <action-id> [target-id...]`、`respond ...` |
| card selection | `select-cards <request-id> <card-id>...` |
| assignment／distribution | `assign <request-id> <card-id>... to <player-id>` |
| rearrangement | `order <request-id> top <id>... bottom <id>...` |
| general arrangement | `arrange <request-id> <general-id>...` |
| cancel／pass | `cancel <request-id>`、`pass <request-id>` |
| structured custom | schema 登記的具名命令，不接受任意 QML code |

request id 必須出現在修改遊戲狀態的回覆中，避免玩家把舊命令送給新 prompt。parser 只做
語法、id 型別及參數數量檢查；真正合法性、exactly-once 與 timeout 仍由 Client core
負責。

### 5.4 Machine-readable 模式

可選 `--output jsonl`，逐行輸出同一組 normalized event。每行至少包含 `seq`、`type`、
`revision`、`request_id`、`text` 及 typed `data`。JSONL 供測試、bot、pipe 及
accessibility adapter 使用，不改變 gameplay semantics。

`plain` 與 `jsonl` 共用 event／intent model，只替換 formatter。任何模式都不得直接暴露
Protocol V2 frame。

## 6. Process 與 I/O 架構

建議元件：

- `TextClientFrontend`：訂閱 session snapshot／event／request，決定要輸出的 record；
- `TextEventFormatter`：把 normalized record 格式化為 localized plain text 或 JSONL；
- `TextCommandParser`：把完整輸入行解析成 local query 或 semantic intent；
- `TextInput`：平台相關的 line reader；
- `TextOutput`：序列化 stdout，保證單行不交錯並按規則 flush。

Qt `QCoreApplication` 保持唯一 event-loop owner。POSIX 可沿用 server console 的
`QSocketNotifier` + non-blocking stdin 模式；Windows console 與 redirected pipe 由獨立
`TextInput` adapter 讀取完整 Unicode 行，再 queued dispatch 回 Qt thread。

所有 session callback 與 command result 在同一輸出序列器排序。互動 request 輸出後必須
立即 flush，避免 stdout 經 pipe buffering 後錯過 deadline。

因為不使用 raw mode 或全屏 terminal：

- 不需處理 resize、mouse、鍵位 escape sequence；
- 不需保存或恢復游標、echo、alternate screen；
- stdin 可來自 terminal、pipe 或預先錄製 command file；
- stdout 可直接 redirect 成可重播 transcript。

stdin EOF 觸發正常 client shutdown，不等同 server timeout reply；active request 的安全
完成仍由 session core 管理。

## 7. Build、資產與啟動介面

新增獨立 option／target：

```text
QSAN_BUILD_TUI=ON
qsanguosha_tui
```

依賴上限建議為 `Qt6::Core`、`Qt6::Network`、解耦後的 client session 及必要 engine
rules。禁止連入 terminal UI library、Qt Gui、Widgets、Quick、QML、Multimedia、OpenGL。

純文字 client asset manifest 只要求 gameplay Lua、extensions、locale 與 translation；
不要求 `image/`、`audio/`、`font/`、`hero-skin/`、`qss/`。所有路徑仍經 runtime path
resolver，不使用 `QDir::currentPath()` 拼接。

建議 CLI：

```text
qsanguosha_tui --host 127.0.0.1 --port 9527 --user name --avatar caocao --protocol auto|2 --locale zh_CN --output plain|jsonl
```

TUI source 對 protocol 保持中立。首個完整驗收以 V2 為準；`auto` 是否容許 V1 fallback
由 session／Protocol PR 決定，不能在 TUI 內複製 V1 parser。

## 8. 測試與驗收

### 8.1 CI 可阻擋項目

- dependency audit：binary 不得依賴 terminal UI、Gui、Widgets、QML、Quick、Multimedia；
- command parser：空白、quote、Unicode、未知命令、未知 id、參數上下限；
- snapshot／event formatter 的 plain transcript golden test；
- JSONL schema、seq、revision、request id、escaping 與 stdout／stderr 分離；
- 28 類 canonical interaction fixture、legacy QML 拒絕／相容 fixture及所有 structured
  custom interaction fixture；
- action selection、validation、cancel、timeout、exactly-once 及 stale request；
- pipe／PTY smoke：startup、完整行、partial line、EOF、flush、`SIGINT`／`SIGTERM`；
- UTF-8 中英文、長行、控制字元 escaping、輸出完全不含 ANSI control sequence；
- 無 art／audio/font/skin 環境的 startup 與 interaction transcript；
- protocol frame、hello、partial/coalesced read、reject 繼續留在既有 Protocol V2 contract／
  integration suites，不在純文字 client 重測 codec。

### 8.2 本機完整對局 gate

依目前 `AGENTS.md`，GUI 或 headless 真實完整對局不可成為 CI gate。純文字 client 先沿用
相同規則：在本機以固定 seed 的 `02p` 真實 server 完成登入、選將、出牌、回應、技能、
game over、reconnect；已知 `server-teardown-crash` 只在符合既有分類時降級。

若日後證明純文字 client 在無 GUI／無素材 runner 上可長期穩定，需另行修改
`AGENTS.md` 與保護測試，才可把完整對局升級為 CI blocking test。

### 8.3 完成定義

- 所有 §4.1 流程有 transcript fixture 或 local end-to-end 證據；
- unsupported capability 在開局前得到純文字拒絕，不會中途卡局；
- 網絡斷開、未知命令、重複或過期 request 不會 duplicate reply 或 crash；
- stdout 每個 record 完整、順序確定、可 pipe、無 ANSI 控制；
- 無 GUI libraries、terminal UI library及發佈美術素材仍能完成支援的對局；
- Windows console、Windows redirected pipe、Linux terminal／pipe 各完成一次中文實機
  驗收。

## 9. 建議 PR 切分

Protocol V2 與 Client core 解耦 PR 合併前，只做 contract fixture、command grammar 與
transcript prototype，避免依賴未定型 internal API。

1. **T0 — text contract**：固定 frontend port、snapshot、event、action catalog、
   capability schema、plain transcript 與 command grammar。
2. **T1 — binary shell**：`qsanguosha_tui`、build/package、stdin/stdout adapters、CLI、
   formatter、parser、dependency audit。
3. **T2 — live read model**：接 ClientSession，完成連線、房間、事件、狀態查詢、chat、
   reconnect；尚不宣稱可完整對局。
4. **T3 — typed interactions**：接通 28 類 canonical request；legacy QML 在未結構化時
   必須 preflight reject。
5. **T4 — action planner 與完整對局**：出牌、回應、view-as、target、structured custom
   interaction；完成本機 deterministic full-game transcript。
6. **T5 — release hardening**：Windows console／pipe、JSONL、packaging、錯誤恢復與文件。

每一階段均維持可編譯及可測，且不在純文字 client PR 內修改 V2 frame format。若上游
未提供 ActionPlanner 或 structured custom interaction，T4 應視為被前置契約阻擋，不能
把 engine／GUI 規則複製進文字 command parser。

## 10. 主要風險

| 風險 | 控制方式 |
|---|---|
| 出牌規則藏在 `RoomScene` | 上游 ActionPlanner 是 T4 硬性前置條件。 |
| legacy QML／C++ dialog | declarative schema；未支援者在進房前 capability reject。 |
| HTML／QTextDocument 洩漏 | localization key + arguments + plain-text fallback。 |
| 玩家名稱或翻譯有歧義 | 所有命令只接受 stable id，不解析顯示名稱。 |
| 舊命令回覆新 prompt | 修改狀態的命令強制帶 request id，core 拒絕 stale reply。 |
| event 太多淹沒 prompt | request／deadline 不可 filter；其他事件可分類過濾並以 `log` 重看。 |
| stdout pipe buffering | request、result、fatal record 立即 flush。 |
| Windows blocking stdin | 獨立 `TextInput` adapter，queued dispatch，不阻塞 Qt network loop。 |
| reconnect 後狀態漂移 | revisioned full snapshot，不靠 transcript replay 還原。 |
