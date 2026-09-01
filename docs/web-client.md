# Web compact client

TypeScript compact SPA in [`web/`](../web/). It talks Protocol V2 over the
existing WebSocket gateway (default `9528`). It does not embed the C++ engine,
Replay, PWA, or WSS.

## Run

The dedicated server (or GUI embedded server) must already be listening.
Vite only serves HTML.

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

Open `http://127.0.0.1:5173/` to join `current`, or
`http://<host>:5173/room/<roomId>` to sit in that waiting room. The page
connects to `ws://<same-host>:9528` unless `?ws=` or the connection form
overrides it. `?reconnect=1` sets `reconnect_requested`.

`npm run preview` serves the production build with the same `/room/:id`
fallback.

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

Connect uses `config.ini` `BackgroundImage`; the waiting room uses
`TableBgImage`. After `GAME_START`, `EnableAutoBackgroundChange` loads the
lord kingdom table (`skins/fulldefaultSkin.image.json` `tableBg*`); battle
then follows `CHANGE_TABLE_BG` and lightbox `background=`.
The right-hand log is a fixed pane with internal scroll so it cannot
stretch the table. Portrait stacks table / log / dashboard so the room
stays on one screen.

Battle log lines are composed in [`web/src/log-text.ts`](../web/src/log-text.ts),
matching the desktop `ClientLogBox` templates rather than `split` + `tr()`.
Interaction `prompt` strings from `askForCard` / `askForDiscard` /
`askForPlayerChosen` use the same colon list as GUI `Client::formatPromptList`
(`key:%src:%dest:%arg:%arg2`); do not `tr()` the raw wire string. Dump
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

All 29 production interactions have a GUI-style widget. Play and response
prompts grey out illegal cards and Photos using the same three checks as
desktop `Dashboard::enableCards` / `Card::targetFilter`: response `pattern`,
`CARD_LIMITATION`, and seat distance / attack range. Unknown extension cards
stay selectable; the Room is still authoritative. Unknown commands are
shown as a visible failure plus cancel. Vite serves the local `image/` tree at
`/assets/` (dev and preview). Hand and prompt cards use
`image/card/<object_name>.jpg` with `unknown.jpg` fallback; hidden cards use
`image/system/card-back.png`; seats, waiting-room avatars, and choose-general
use `image/generals/card/<name>.jpg` (then png/webp). Game Photos and the
Dashboard avatar use `image/fullskin/generals/full/<name>.jpg` first. Spine, hero-skin
composite, and audio are not loaded. Layout is one component tree; CSS reflows
portrait and landscape. The `image/` directory is gitignored; a checkout
without local art falls back to unknown / text.

## Out of scope

PWA, HTTPS/WSS, Qt WASM, desktop-complete RoomScene, browser-local server.
