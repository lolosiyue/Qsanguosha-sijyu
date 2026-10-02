# Web compact client

TypeScript compact SPA in [`web/`](../web/). It talks Protocol V2 over the
existing WebSocket gateway (default `9528`). Card-selection rules run in a
dedicated Web Worker using the opt-in C++/Lua WASM build target, while the
browser keeps TypeScript/DOM presentation.

## Run

The dedicated server (or GUI embedded server) must already be listening.
Vite only serves HTML.

Build and package the production WASM runtime into `web/public/rules` as
described in [web-client-wasm-runtime.md](web-client-wasm-runtime.md#production-build-and-packaging)
before serving the Web client. It requires the `.mjs`, `.wasm` and
`.bundle.json` artifacts from the same runtime output directory (the retired
`.assets.json` file is no longer produced). The Worker
loads them at `/rules/qsanguosha_client_wasm.*` and verifies the deployment
bundle manifest; an absent or mismatched runtime
disables positive card-selection confirmation and shows the failure reason.
The source checkout does not contain prebuilt runtime binaries.

```powershell
# terminal 1
debug\qsanguosha_server.exe

# optional translations and card names
debug\qsanguosha_tui.exe --dump-translations web\public\translations.json
# writes translations.json and sibling cards.json

# terminal 2
cd web
npm install
npm run dev
```

WebSocket signup requires the server to seal a `declared-v2` rules identity:
the content scan must match `lua/config.lua` `extension_names` exactly. A
development worktree fails the seal when it carries undeclared Lua (for
example `extensions/temp/*.lua`) or `etc/`, and the server then rejects every
Web client with `rules_content_unsupported`. Native TCP clients are not
affected (W2 legacy path). Run the server from a clean declared closure
instead — the scan exempts `lua/ai/` as server-only content:

```powershell
python tools\package-web-solo.py --prepare-content --asset-root . `
  --destination builds\web-declared-content
Copy-Item config.ini builds\web-declared-content\
# Fresh isolated root only: exclude VCS metadata and runtime debris from AI.
python -c "import shutil; shutil.copytree('lua/ai', 'builds/web-declared-content/lua/ai', ignore=shutil.ignore_patterns('.git', '.stignore', 'logs', 'data', 'temp', '*.bak', '*.bak-*'))"
New-Item -ItemType Directory -Force builds\web-declared-content\lua\lib | Out-Null
Copy-Item lua\lib\middleclass.lua builds\web-declared-content\lua\lib\
# Debug server/TUI require the matching Qt Debug DLL directory on PATH.
$serverExe = (Resolve-Path debug\qsanguosha_server.exe).Path
$runtimeRoot = (Resolve-Path builds\web-declared-content).Path
$qtPathBefore = $env:PATH
$runtimeRootBefore = $env:QSAN_RUNTIME_ROOT
Push-Location $runtimeRoot
try {
  $env:PATH = "H:\Qt6111\6.11.1\msvc2022_64\bin;$env:PATH"
  $env:QSAN_RUNTIME_ROOT = $runtimeRoot
  & $serverExe --bind-address 127.0.0.1 --port 9527 --websocket-port 9528 `
    --asset-root $runtimeRoot
} finally {
  Pop-Location
  $env:PATH = $qtPathBefore
  $env:QSAN_RUNTIME_ROOT = $runtimeRootBefore
}
```

Open `http://127.0.0.1:5173/` to join `current`, or
`http://<host>:5173/room/<roomId>` to sit in that waiting room. The page
connects to `ws://<same-host>:9528` unless `?ws=` or the connection form
overrides it. `?reconnect=1` sets `reconnect_requested`.

`npm run preview` serves the production build with the same `/room/:id`
fallback. `web/preview.html` is a separate development entry (`npm run dev`
only): it renders the in-room UI with simulated data through
[`web/src/ui-preview.ts`](../web/src/ui-preview.ts) — no socket, Worker or
server is started — for styling and layout iteration.

`npm run build` runs the Protocol V2 drift check
([`web/scripts/check-protocol-sync.mjs`](../web/scripts/check-protocol-sync.mjs)),
a `translations.json` freshness check against `lang/zh_CN/*.lua`, a seat-ring sync
check and `tsc --noEmit`, then `vite build`. The drift check compares `protocol.ts` Command IDs and
`replies.ts` `REPLY_COMMAND` to
[`artifacts/protocol-v2-flow-matrix.json`](../artifacts/protocol-v2-flow-matrix.json)
(command_id and request→reply pairing only; listed client-emitted field names
are existence-checked, payload types are out of scope). There is no `npm test`
(vitest was removed 2026-09-25).
If translations.json is missing or older than the Lua tables, re-run the dump
command above.

## Web 10P reuse and troubleshooting

2026-10-03 已完成實際 Web Client 加九個 AI 的身份 `10p` 托管局，
自然結局為反賊勝；Web 結算、server exit=0、分頁關閉與連接埠回收均已確認。
續作先讀 [本局報告](../builds/web-10p-20261003/report.md)、
`summary.json` 和 `game-02/artifact-manifest.json`。
`builds/` 不入 Git；這些是本機證據，清除建置目錄後仍以本節和
[WASM 初始化判讀](web-client-wasm-runtime.md#initialization-diagnostics-and-dependencies)
作操作入口，不把歷史成功當作新來源的驗收。

### Reuse order

1. **先核對，符合才重用。** 沿用 `builds/repository-review-wasm` 的增量快取、
   同一套 `.mjs`／`.wasm`／`.bundle.json` 和已準備的隔離 runtime。
   比對本局 artifact manifest、目前來源／內容與 server 的 rules identity；
   來源、內容或產物不符才處理受影響步驟。保留其他工作的 dirty 修改，
   不重新清倉、全量 configure 或複製整個工作樹。
2. **按實際變更重新配對。** 原生來源變更可能改變 code identity，即使
   該檔案沒有修改 Web UI，也不能沿用不符的 WASM。需要建置時先完成
   授權檢查點，再增量建置相應 server／WASM，package 三個產物，最後
   `npm run build`。前端會嵌入 deployment bundle ID，順序不能倒置。
3. **隔離內容完整才開服務。** `--prepare-content` 產生 declared closure，
   server AI 另行部署並排除 `.git`／`.stignore`／logs／data／temp／備份檔；
   另補 `lua/lib/middleclass.lua`。既有隔離目錄已符合時不重做複製，
   不用完整 extensions／etc 目錄補洞，也不改使用者根目錄設定。
4. **先完成候選與開局詢問，再托管。** 本局 `FreeAssign=true`，配置為
   1 主公、3 忠臣、5 反賊、1 內奸；九個 AI 加入後仍須提交身份、準備、
   選將及開局技能詢問。本次主公選太史慈[国]，回答「職業選擇」否後才按
   托管。若已有待答詢問，直接按托管會留下舊 Web 提示，見下表。
5. **沿用正在推進的一局至結算。** 未回覆的候選詢問、原生初始化與
   AI 計算都計入服務 watchdog。第一局 1800 秒期限在第八位首次回合
   用盡；第二局改為 7200 秒服務期限，實際遊戲 18 分 35 秒結束。
   7200 秒是該局的 watchdog 設定，並非其他配置的必然完成時間。
   下次執行完整局仍依當輪授權設定期限，不因本次結果自動取得重跑授權。

本次隔離設定為 `GameMode=10p`、`EnableHegemony=false`、
`Enable2ndGeneral=false`、`EnableCheat=false`、`FreeChoose=false`、
`FreeAssign=true`、`OriginAIDelay=0`、`AIDelayAD=0`。server 使用
`--game-mode 10p --ai on --ai-delay 0 --operation-timeout 120 --seed 20261003`，
`--asset-root` 與 `QSAN_RUNTIME_ROOT` 均指向隔離 runtime；WebSocket URL 由
`?ws=` 明確指定。下一局先確認要求是否仍相同，再修改隔離設定，
不要只從武將的 `[国]` 名稱或隱匿畫面推斷已變成國戰模式。

| 問題／可見證據 | 本次原因或處理 | 下次沿用方式 |
|---|---|---|
| `[object Object]`，繼而 `ExitStatus exit(1)` | Worker 的一般物件錯誤和 Qt 原生日誌原先不可見；createMode 無政策時也載入被 Client 排除的 server AI | 先看 stage、name/message/errno/code 及有界原生日誌；沿用條件載入修復，不再先猜技能或塞入全套 AI |
| server 啟動找不到 middleclass | declared Client closure 不等於 server AI 所需檔案 | 在隔離根補 `lua/lib/middleclass.lua`；與缺 Qt Debug DLL 分開判讀 |
| Emsdk 環境設定、FileTracker 或本機監聽被沙箱拒絕 | 已確認是本輪工具執行權限限制 | 在允許的工具權限下執行相同有界工作，不反覆跑同一失敗命令、不把它寫成遊戲回歸 |
| code identity／部署配對不符 | 同期原生來源改動需要重新封存、配對產物 | 對齊 server／WASM 後 package，再建置前端；不混用兩次輸出的檔案 |
| 托管後舊技能詢問／倒數仍顯示 | server 托管會釋放待答請求，Web 尚有舊提示殘留；晚按否可得到 request_expired | 目前先回答待答詢問再托管；此為操作避開方式，UI 修復仍未完成，不能據此宣告後端逾時 |
| 第一局 watchdog 到期 | 期限從服務啟動計算，未等到 GAME_OVER | 保留逾時局證據為未完成；程序仍推進時不反覆重啟；新局使用獨立輸出目錄與 stop.request |
| 回合快照 unsupported／lossy 警告 | 觀察到 QDateTime 的 MeleeModeStartTime 和 CardEffectStruct 的 TrickEffectData | 保留警告，對局可繼續；不得宣告快照／重播／seek 通過 |
| `Pindian card for shuangren not found!!` | 本局仍有後續拼點牌、傷害及自然結局，警告成因未修復 | 保留為技能待查項，不把整局成功等同雙刃正確性驗收 |
| 大量技能控制項溢出 | 結算可讀，但 Web 外觀尚有已觀察問題 | 保留畫面與債務，不把托管局成功當作響應式／完整 UI 驗收 |
| 根目錄 config.ini 雜湊漂移 | 同期工作中發生，來源未確認 | 使用隔離 config；記錄差異，不擅自還原其他工作的設定 |

### Finish and preserve evidence

完整局需同時保留 server 的 `game start` → `game over <winner>`、
Web「遊戲結束」和勝方 DOM／截圖；斷線、逾時、強制停止或僅顯示座位
都不算通過。結算證據先保存，再關閉本局 Web 分頁，向 server console
送出 `shutdown`，等待正常退出並核對 owned PID 不存在、連接埠已釋放。

本局 helper `builds/web-10p-20261003/host.py` 支援
`<新輸出子目錄> <服務期限秒數>`，每局使用自己的 stop.request，並給
server console shutdown 60 秒收尾。它是本機證據 helper，不是已追蹤的
產品工具；下次先確認檔案仍存在且參數／連接埠適用，再決定是否沿用。
第一局進行中停止時 server 收尾未在舊 15 秒等待內完成，最後以控制事件
退出，exit 非 0，故沒有判為通過。第二局自然結束後 server exit=0，
且有 CARD_LIFETIME_ZERO；preview 的 `0xC000013A` 是控制事件停止，
必須與 server 的正常退出分開記錄。

本次未驗收手動出牌、重連、重播／seek、Browser Solo、Release、CI 或
跨平台矩陣。外部 `extensions/addFunction.lua` 修復已回寫 H 端權威並核對
L／H／隔離副本相同；後續修改仍需依 `AGENTS.md` 的外部權威規則同步。

## Behaviour

| Path | Signup |
|---|---|
| `/` | no `room_id` → `current` |
| `/room/<id>` | schema 2 `room_id` |

Accepted `SignupReplyPayload` schema 2 includes `room_id`. Copy-link and a small QR sit in the top-left toolbar. They are not
drawn inside the dashboard prompt.

In game the compact client follows the native RoomScene split: other
players are Photo widgets using `image/fullskin/generals/full/<name>.jpg`,
roles are the top-right `image/system/roles/<role>.png` icon, delayed
tricks sit under each Photo, and the table centre shows PlaceTable /
仁区 / 判定 like `TablePile`. The local player is a bottom Dashboard
(avatar + equips + piles + skills + hand + prompt) that prefers wrapping
the hand in a horizontal strip instead of scrolling the whole panel.
Skill buttons toggle off when pressed again and show `:<skill>`
descriptions from the translation table.

The idle screen shows `image/logo/logo.png` instead of the
`QSanguosha idle` toolbar title.

`GAME_OVER` retains the server's result on the board: winner tokens are matched
against player names and the ordered result roles, and `standoff` shows a draw.
The result replaces gameplay prompts and remains visible after the server closes
the connection. A disconnect before a result is still reported as a failure.

Requests consume the preceding `MOVE_FOCUS` specified countdown in milliseconds,
measured from receipt of that notification. An elapsed deadline clears the prompt;
reply submission checks the same deadline even if a background tab delays timers.
Focus moving away from the local player also cancels the old prompt, while a
multi-player focus containing that player remains valid. No-limit, zero-maximum,
and unresolved default countdowns do not invent a local deadline. Timers are
discarded on replacement, reply, disconnect, state synchronization, and game end;
the browser does not synthesize a timeout reply or alter server decisions.

Connect uses `config.ini` `BackgroundImage`; the waiting room uses
`TableBgImage`. After `GAME_START`, `EnableAutoBackgroundChange` loads the
lord kingdom table (`skins/fulldefaultSkin.image.json` `tableBg*`); battle
then follows `CHANGE_TABLE_BG` and lightbox `background=`.
The right-hand log is a fixed pane with internal scroll so it cannot
stretch the table. Portrait stacks table / log / dashboard so the room
stays on one screen.

Battle log lines match the desktop `ClientLogBox` templates rather than `split` + `tr()`
(the former fixture `web/tests/fixtures/log-text-legacy.ts` was removed with the test suites).
Interaction `prompt` strings from `askForCard` / `askForDiscard` /
`askForPlayerChosen` use the same colon list as GUI `Client::formatPromptList`
(`key:%src:%dest:%arg:%arg2`); C++ TUI/GUI share `formatClientPromptList` in
`src/client/core/client-prompt.cpp`. Do not `tr()` the raw wire string. Dump
translations after editing `lang/zh_CN/Package/StandardPackage.lua` so keys such
as `shoot-jink` are in `web/public/translations.json`.
`LOG_SKILL` fills lang placeholders; `#UseCard` uses `#UseCardPhrase_*` from the
dumped translation table. `GET_CARD` / `LOSE_CARD` / `CHANGE_HP` /
`CHANGE_MAXHP` are synthesised locally into `$DrawCards` / `$addRenPile` /
`$removeRenPile` / `#GetHp` and stored
as extra `LOG_SKILL` presentation events. 仁区 membership is tracked on the
session like `RoomScene::RenPile` and cleared on `GAME_START` / `STATE_SYNC begin`. `LOG_EVENT` skill-cache refresh
(`event` 9) stays out of the pane. Dump translations after editing
`lang/zh_CN/Common.lua` or package tables such as `StandardPackage.lua`.

All 29 production interactions have a GUI-style widget. `PLAY_CARD`,
`RESPONSE_CARD`, `ASK_PEACH` and `NULLIFICATION` use the persistent native
rules runtime for physical-card availability, ViewAs subcard construction and
target selection. Cards, skills and Photos are enabled only by the current
native selection result. Ordered subcards and repeated target votes are kept;
already-selected cards/skills can be deselected and target votes withdrawn.
Successful confirmation sends the canonical C++ reply payload only while its
request ID, state and selection revisions still match the current interaction.

`SKILL_GUANXING`, `SKILL_GONGXIN` and `SKILL_YIJI` keep their existing
renderers and take their selectable set, count contract and reply from the same
runtime. Their sets and bounds come from the shared ClientCore
`InteractionRequest` the runtime forwards, not from a second reading of the wire
payload, and confirmation sends the registry encoder's payload. The skill
effect itself is never reimplemented in WASM.

The unit of coverage is the interaction shape, not the general. Each ViewAs
candidate reports its declared subcard amount, committed usage under the skill's
limit scope, instance invalidation and expand pile, so a disabled button says
why. Only the runtime's `available` enables activation; the rest is display.
Selectable cards carry the zone they sit in — hand, equip, hand pile, expand
pile or a sibling's pile — and the prompt groups them by that zone instead of
inferring it from ownership. `guhuo`, `juguan` and `tiansuan` declarations are
enumerated by the runtime and chosen before subcards; changing one drops the
subcards and targets it invalidated.

Loading, evaluating, unsupported content and runtime failures are visible in
the prompt and disable positive confirmation. Cancel remains available, and
`PLAY_CARD` retains its end-play action. A failed Worker is discarded; the
reload-rules button or a new connection creates a fresh runtime. The deployed
content profile is currently
`declared-v2` (verified by [`web/src/rules-identity.ts`](../web/src/rules-identity.ts) against
the sealed bundle identity); arbitrary extension content is not covered and does not fall
back to TypeScript skill-card guesses. The Room remains authoritative.
Unknown commands are shown as a visible failure plus cancel.

Vite serves the local `image/` tree at
`/assets/` (dev and preview). Hand and prompt cards use
`image/card/<object_name>.jpg` with `unknown.jpg` fallback; hidden cards use
`image/system/card-back.png`; seats, waiting-room avatars, and choose-general
use `image/generals/card/<name>.jpg` (then png/webp). Game Photos and the
Dashboard avatar use `image/fullskin/generals/full/<name>.jpg` first. Spine, hero-skin
composite, and audio are not loaded. Layout is one component tree; CSS reflows
portrait and landscape. The `image/` directory is gitignored; a checkout
without local art falls back to unknown / text.

## Out of scope

PWA, HTTPS/WSS gateway setup, a Qt Widgets/Quick browser UI, desktop-complete
RoomScene, browser-local server, and arbitrary extension WASM packaging/parity.
