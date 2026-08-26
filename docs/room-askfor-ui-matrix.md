# Room askFor 本地響應 UI Runner

## 目的與邊界

本 runner 在既有 GUI 執行檔內重建真人玩家的回應路徑：`Packet` 解析 → `Client` 回呼 → `RoomScene`／`Dashboard`／對話框 → 真實 UI 動作 → `Client::replyToServer` → 假 socket 擷取。它不建立 `Server`、`Room` 或 TCP 連線，也不替代伺服器端 `Room::askForXXX` 測試。

建置功能預設關閉：

```powershell
cmake --preset vs2026-x64 -DQSAN_BUILD_LOCAL_RESPONSE_UI_RUNNER=ON
cmake --build --preset debug --parallel 8
```

單一案例：

```powershell
debug\QSanguosha.exe --local-response-ui-case tests\skill_ui_runner\cases\ask_for_choice.json --local-response-ui-report artifacts\skill-ui\ask_for_choice\report.json --screenshot-dir artifacts\skill-ui\ask_for_choice
```

完整隔離套件：

```powershell
python tools\autotest\skill_ui_runner.py --qt-root H:\Qt6111\6.11.1\msvc2022_64
```

Windows runner 會固定 `QT_QPA_PLATFORM=windows`，並從執行檔旁或 `--qt-root` 尋找 `platforms/qwindows[d].dll`，避免平台外掛錯誤對話框。每個案例使用獨立程序；逾時、非 runner 結束碼及新 dump 都會保留在案例 artifact 目錄。

## 啟動生命週期

1. 載入案例指定 package／extension。
2. 建立注入 `TestClientSocket` 的真實 `Client`。
3. 以通知建立 self、其他玩家、座位、武將、技能、牌、marks 及 flags。
4. 注入 `S_COMMAND_GAME_START`，讓 `Client::startGame()` 執行正式的 `Engine::registerRoom()`；這是 `current_pattern` 與 `use_reason` 可安全查詢的必要 runtime context。
5. 將 `request` 轉為真實 `Packet` 並走 `Client::processServerPacket()`。
6. 透過語意 probe 驗證畫面狀態，並由按鈕、卡牌、玩家與技能元件的真實事件完成動作。
7. 從假 socket 擷取 reply，核對 command、serial 與語意 payload。

`bootstrap.cards[].alias` 是案例內穩定名稱；實際 card id 由 Engine 的精確 `name + suit + number` 決定。runner 拒絕重複 alias、找不到的實體牌，以及非 self 擁有的牌，避免將測試資料誤當任意合成牌。

`request.raw_body` 直接覆蓋 protocol body，供 extension 的真實 `askFor` 形狀或尚未有 adapter 的命令使用。一般案例應優先使用 `api + args`，讓 schema 和 adapter 明確表達 Room API 語意。

## Room askFor UI 矩陣

| Room／流程 API | Protocol command | Client handler | UI surface | Reply path | Runner |
|---|---|---|---|---|---|
| `askForGeneral` | `S_COMMAND_CHOOSE_GENERAL` | `askForGeneral` | choose-general dialog | `onPlayerChooseGeneral` | 待補 |
| `askForPlayerChosen` | `S_COMMAND_CHOOSE_PLAYER` | `askForPlayerChosen` | photos + dashboard OK | `doPlayerChoose` | 已支援 |
| `askForAssign` | `S_COMMAND_CHOOSE_ROLE` | `askForAssign` | role dialog | dialog reply | 待補 |
| `askForDirection` | `S_COMMAND_CHOOSE_DIRECTION` | `askForDirection` | choice dialog | dialog reply | 待補 |
| `askForExchange` | `S_COMMAND_EXCHANGE_CARD` | `askForExchange` | dashboard cards | `replyToServer(S_COMMAND_DISCARD_CARD)` | 已支援 |
| `askForSinglePeach` | `S_COMMAND_ASK_PEACH` | `askForSinglePeach` | dashboard response | response-card reply | 待補 |
| `askForGuanxing` | `S_COMMAND_SKILL_GUANXING` | `askForGuanxing` | guanxing box | arrangement reply | 下一批 |
| `askForGongxin` | `S_COMMAND_SKILL_GONGXIN` | `askForGongxin` | gongxin box | chosen-card reply | 下一批 |
| `askForYiji` | `S_COMMAND_SKILL_YIJI` | `askForYiji` | cards + players | yiji reply | 下一批 |
| play phase | `S_COMMAND_PLAY_CARD` | `activate` | dashboard + photos | `S_COMMAND_PLAY_CARD` | 待補 |
| `askForDiscard` | `S_COMMAND_DISCARD_CARD` | `askForDiscard` | dashboard cards | discard-card reply | 已支援 |
| `askForSuit` | `S_COMMAND_CHOOSE_SUIT` | `askForSuit` | choice dialog | dialog reply | 待補 |
| `askForKingdom` | `S_COMMAND_CHOOSE_KINGDOM` | `askForKingdom` | choice dialog | dialog reply | 待補 |
| `askForCard`／`askForUseCard` | `S_COMMAND_RESPONSE_CARD` | `askForCardOrUseCard` | dashboard + photos | response-card reply | 已支援 |
| `askForSkillInvoke` | `S_COMMAND_INVOKE_SKILL` | `askForSkillInvoke` | dashboard OK/Cancel | boolean reply | 已支援 |
| `askForChoice` | `S_COMMAND_MULTIPLE_CHOICE` | `askForChoice` | choice dialog | choice reply | 已支援 |
| trigger-order choice | `S_COMMAND_TRIGGER_ORDER` | `askForTriggerOrder` | trigger-order box | choice reply | 待補 |
| `askForNullification` | `S_COMMAND_NULLIFICATION` | `askForNullification` | dashboard response | response-card reply | 待補 |
| `askForCardShow` | `S_COMMAND_SHOW_CARD` | `askForCardShow` | dashboard cards | card id reply | 待補 |
| `askForAG` | `S_COMMAND_AMAZING_GRACE` | `askForAG` | amazing-grace box | card id reply | 下一批 |
| `askForPindian` | `S_COMMAND_PINDIAN` | `askForPindian` | dashboard cards | card id reply | 待補 |
| `askForCardChosen` | `S_COMMAND_CHOOSE_CARD` | `askForCardChosen` | card overview | card id reply | 下一批 |
| `askForOrder` | `S_COMMAND_CHOOSE_ORDER` | `askForOrder` | kingdom/order box | choice reply | 待補 |
| 3v3 role choice | `S_COMMAND_CHOOSE_ROLE_3V3` | `askForRole3v3` | role dialog | role reply | 待補 |
| `askForSurrender` | `S_COMMAND_SURRENDER` | `askForSurrender` | vote dialog | boolean reply | 待補 |
| `askForLuckCard` | `S_COMMAND_LUCK_CARD` | `askForLuckCard` | dashboard OK/Cancel | boolean reply | 待補 |
| 3v3 general choice | `S_COMMAND_ASK_GENERAL` | `askForGeneral3v3` | general dialog | general reply | 待補 |
| arrange generals | `S_COMMAND_ARRANGE_GENERAL` | `startArrange` | arrange dialog | arrangement reply | 待補 |
| QML `askFor` | `S_COMMAND_QML_INTERACT` | `askForQml` | QML dialog | variant reply | 下一批 |

## 已支援案例與語意驗證

| 類型 | 案例 | 關鍵驗證 |
|---|---|---|
| invoke | `ask_for_skill_invoke_yes/no` | 真實 OK／Cancel 與 boolean reply |
| choice | `ask_for_choice` | 選項內容、禁用選項、choice reply |
| card response | `ask_for_card_response` | pattern、prompt 翻譯、可用牌、實體牌 reply |
| view-as | `ask_for_card_view_as_skill` | 技能啟動、subcard、轉換後 card 名稱 |
| discard | `ask_for_discard` | 選牌、Discard 按鈕、card id 列表 |
| exchange | `ask_for_exchange` | exchange 狀態與正式 discard-card reply |
| player chosen | `ask_for_player_chosen` | 可選玩家、選取狀態與 player 名稱 reply |
| extension | `extension_real_askfor` | `AIgeneral.lua` 的 `deep_seek` 真實來源、翻譯及 invoke reply |

下一批 adapter 順序固定為：`askForCardChosen`、`askForAG`、`askForYiji`、`askForGuanxing`、`askForGongxin`、QML `askFor`。
