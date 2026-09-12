# Web compact client

TypeScript compact SPA in [`web/`](../web/). It talks Protocol V2 over the
existing WebSocket gateway (default `9528`). Card-selection rules run in a
dedicated Web Worker using the opt-in C++/Lua WASM build target, while the
browser keeps TypeScript/DOM presentation. This PR #31 follow-up is
source implementation only: no tests, configure, compile, rebuild, artifact
packaging or browser acceptance were run for this change.

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
a `translations.json` freshness check against `lang/zh_CN/*.lua`, `tsc --noEmit`,
and `vitest run`. The drift check compares `protocol.ts` Command IDs and
`replies.ts` `REPLY_COMMAND` to
[`artifacts/protocol-v2-flow-matrix.json`](../artifacts/protocol-v2-flow-matrix.json)
(command_id and request→reply pairing only; listed client-emitted field names
are existence-checked, payload types are out of scope). `npm test` is vitest only.
If translations.json is missing or older than the Lua tables, re-run the dump
command above.

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

Battle log lines are composed in [`web/src/log-text.ts`](../web/src/log-text.ts),
matching the desktop `ClientLogBox` templates rather than `split` + `tr()`.
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
