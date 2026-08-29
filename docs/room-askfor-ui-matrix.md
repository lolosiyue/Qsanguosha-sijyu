# Room askFor 回應 UI Runner

## 用途與邊界

Local response UI runner 會在本機程序內建立實際的 `Client`、`RoomScene`、`Dashboard` 與 production `FitView`，注入 protocol request packet，再觀察 UI 狀態與 `TestClientSocket` 捕捉到的真實 reply packet。

此工具不會啟動 `Server`／`Room`、建立 TCP 連線或驗證伺服器端 `Room::askForXXX`。它驗證的是 client request handler 到 production UI、signal/slot、`Client::replyToServer()` 的路徑。

## 一般建置流程

Runner 是 Windows GUI `QSanguosha` target 的一般來源，不再由 cache option、CTest 或 `BUILD_TESTING` 控制。第一次建立 build tree 時只需一般 configure：

```powershell
cmake --preset vs2026-x64
```

平常只做增量 Debug build：

```powershell
cmake --build --preset debug --target QSanguosha --parallel 8
```

`--build` 也只執行同一條增量命令；它不會重新 configure、執行 CTest 或建置其他測試 executable。若 build tree 不存在，會顯示一次性 configure 指令後停止。

Runner 僅加入 Windows GUI target；Linux server-only target 不會因此引入 Qt Widgets、QML 或 runner sources。Parser regression test 仍由一般 `BUILD_TESTING` 控制，但與 GUI runner 是否可用無關。

## 三種執行模式

| 模式 | CLI | 顯示 | Actions | Reply 後 | 用途 |
|---|---|---|---|---|---|
| Auto | 預設 | hidden/offscreen | 自動全部執行 | 驗證後結束 | 回歸測試 |
| ShowAuto | `--show-ui` | 顯示 production UI | 自動全部執行 | 驗證後結束 | 觀察自動案例 |
| Inspect | `--inspect-ui` | 顯示 production UI 與 Inspector | 不自動執行 | 保持開啟，等使用者 Close | 人工檢查與互動 |

Inspect mode 的順序是：

1. 建立並顯示 production `FitView` 與 Inspector。
2. 透過 queued call 注入 request，避免 modal dialog 阻擋 Inspector 建立。
3. 等待 presented semantic snapshot，執行 `expect_presented`。
4. 顯示 assertion 結果並停在 `AwaitingManualInput`，不執行 JSON actions，也沒有 reply timeout。
5. 使用者直接操作 production UI，或用 Inspector 逐步執行案例 actions。
6. 捕捉真實 outbound reply，顯示 command、body 與驗證結果，但不自動關閉。
7. 使用者按 Close 後寫入最後 report 並結束。

若使用者未送出 reply 就關閉，report 為 `INSPECTED`、`reply_received=false`，程序正常回傳 0。Bootstrap、request injection、schema 或 presentation assertion 失敗會保留視窗供檢查，最後以非零狀態結束。

## Inspector

Inspector 顯示：

- Case、Mode
- Request command／serial
- Client status／current pattern
- Presentation assertion
- Reply received／command／body
- Final result

可用動作：

- `Run Next Case Action`：執行下一個 semantic action。
- `Run Remaining Case Actions`：依序執行剩餘 actions。
- `Save Snapshot`：輸出 `manual-snapshot-<timestamp>.json`。
- `Save Screenshot`：合成 main window、OpenGL viewport、所有可見 top-level dialog 與 Inspector。
- `Close`：寫入最後 report 並關閉。

Inspector actions 只驅動 production UI 的既有 probe／signal／slot；reply 仍必須經過 `Client::replyToServer()` 與 `TestClientSocket::send()`。

## Python CLI

列出案例：

```powershell
python tools\autotest\skill_ui_runner.py --list-cases
```

目前案例：

```text
ask_for_card_response
ask_for_card_view_as_skill
ask_for_card_chosen
ask_for_ag
ask_for_choice
ask_for_discard
ask_for_exchange
ask_for_gongxin
ask_for_guanxing
ask_for_player_chosen
ask_for_skill_invoke_no
ask_for_skill_invoke_yes
ask_for_yiji
extension_real_askfor
```

執行全部 Auto cases：

```powershell
python tools\autotest\skill_ui_runner.py
```

執行單一 Auto case 或顯示自動流程：

```powershell
python tools\autotest\skill_ui_runner.py --case tests\skill_ui_runner\cases\ask_for_choice.json
python tools\autotest\skill_ui_runner.py --show-ui --case tests\skill_ui_runner\cases\ask_for_choice.json
```

依精確 stem 開啟 Inspect：

```powershell
python tools\autotest\skill_ui_runner.py --inspect ask_for_choice
```

增量建置後開啟 Inspect：

```powershell
python tools\autotest\skill_ui_runner.py --build --inspect ask_for_card_view_as_skill
```

Windows 簡易入口等同上一條命令：

```powershell
tools\autotest\inspect_skill_ui.bat ask_for_card_view_as_skill
```

Inspect 不會設定 `QT_QPA_PLATFORM=offscreen`、`CREATE_NO_WINDOW` 或程序 timeout，並繼承 console stdout／stderr；因此 crash/assert 訊息可直接看到。Auto mode 保留 hidden/offscreen、每 case 一個 process、timeout 與 report summary。

Executable 探測順序為：

1. `--exe <path>`
2. `builds/cmake-vs2026/Debug/QSanguosha.exe`
3. `debug/QSanguosha.exe`

Runner 會先呼叫 capability command，不會把舊 executable 開到 lobby：

```powershell
QSanguosha.exe --local-response-ui-capabilities
```

預期輸出：

```json
{"schema_version":1,"auto":true,"show":true,"inspect":true}
```

若 executable 太舊，會要求執行增量 Debug build。

## Artifacts 與 report

預設輸出位於 `artifacts/skill-ui/<case>/`，包含 snapshots、actions、assertions、captured packets、Qt messages、screenshots 與 `report.json`。

有 reply 的 Inspect report 核心欄位：

```json
{
  "mode": "inspect",
  "presentation_result": "PASS",
  "reply_received": true,
  "reply_result": "PASS",
  "closed_by_user": true
}
```

無 reply、人工關閉：

```json
{
  "mode": "inspect",
  "result": "INSPECTED",
  "reply_received": false,
  "closed_by_user": true
}
```

## Room askFor UI 覆蓋矩陣

| 類型 | Case | 驗證重點 |
|---|---|---|
| invoke | `ask_for_skill_invoke_yes/no` | Dashboard OK／Cancel 與 boolean reply |
| choice | `ask_for_choice` | 翻譯選項、modal dialog 與 choice reply |
| card response | `ask_for_card_response` | pattern、prompt、卡牌可選狀態與 response-card reply |
| view-as | `ask_for_card_view_as_skill` | skill button、subcard、確認與 virtual card reply |
| discard | `ask_for_discard` | discard 數量、卡牌選取與 card id reply |
| exchange | `ask_for_exchange` | exchange 限制與 discard-card reply |
| player chosen | `ask_for_player_chosen` | player 可選狀態、確認與 player name reply |
| card chosen | `ask_for_card_chosen` | `PlayerCardBox` 顯示、disabled card 與 card id reply |
| AG | `ask_for_ag` | `FILL_AMAZING_GRACE` 前置通知、容器可選狀態與 card id reply |
| Yiji | `ask_for_yiji` | 指定手牌、目標選擇與 `[card_ids, player]` reply |
| Guanxing | `ask_for_guanxing` | 上下區卡牌呈現、點擊換區與雙牌堆 reply |
| Gongxin | `ask_for_gongxin` | enabled card 篩選、容器互動與 card id reply |
| extension | `extension_real_askfor` | 真實 extension skill 的 invoke request 與 reply |

QML `askFor` 不納入本 runner；其 production surface 尚未完成，依目前測試範圍明確排除。

## ClientCore（Client Architecture F1）

`choice`、`player chosen`、`invoke`、`card response`／`view-as` 與 choose general
的 reply 已經改為先經 `ClientCore` 驗證（可選集合、數量、cancelable、死線、
exactly-once），再由 `Client` 送出 packet；desktop 呈現仍由同一批 RoomScene／
Dashboard slot 負責，所以本 runner 的案例與斷言不變。其餘案例（AG、discard、
exchange、gongxin、guanxing、yiji、card chosen、extension）仍行舊路。分層、
驗證規則與未遷移清單見 `docs/client-core-interaction-model.md`。

## CTest

CTest 是選用的 parser／launcher regression coverage，不是開啟或檢查 askFor UI 的必要步驟：

```powershell
ctest --test-dir builds/cmake-vs2026 -C Debug --output-on-failure
```

## F1.1 更新（2026-08-29）

上方 ClientCore 段落保留的是 F1 首個五-command slice 的歷史快照。F1.1 已把
`Client::m_interactions` 的 29 個 built-in commands 全部遷移；現有 14 個可見 GUI case
仍使用 production RoomScene／Dashboard／dialog surface，protocol fixture test 另逐一覆蓋
29 個 command 名稱與 serial。QML 現在有 versioned structured model 與 registry policy；
舊 QML surface 透過明確 `legacy.qml` adapter 保留，不再是無界定的 passthrough。

`qsanguosha_ui_runner_contract` 在一般 `BUILD_TESTING` 下合併 local-response parser、
startup/network CLI 與 skill UI runner；它仍驗證 capability probe、stem resolution
與純增量 `--build` 契約，且不依賴舊 runner cache option。
# F1.1 canonical interaction status（2026-08-29）

本文件其餘 ClientCore 段落若提到「首五條已遷移」或「其餘 24 條未遷移」，只屬 F1 歷史快照。PR #13 的現行權威狀態如下：

| 分類 | 數量 | 備註 |
|---|---:|---|
| Canonical typed gameplay interaction | 28 | typed request/response payload；統一 ClientCore reply boundary |
| Explicit legacy adapter | 1 | `S_COMMAND_QML_INTERACT` / `legacy.qml` |
| Implicit passthrough | 0 | built-in interaction 全部由 production descriptor registry 登記 |

目前 local-response UI suite 的 14 個 production GUI cases 仍是可見操作與 wire capture 的主要回歸集合；這不代表只有 14 個 canonical interaction。Production matrix 由 `debug/QSanguosha.exe --interaction-inventory` 生成，CTest 另以 fake recorder 實際 dispatch 29/29 presenters。完整架構與特殊語意見 `docs/client-core-interaction-model.md`。
