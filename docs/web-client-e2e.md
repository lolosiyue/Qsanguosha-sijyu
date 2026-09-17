# Web client end-to-end full-game test

Drives the built Web client through a real 20-player game over the
production WebSocket path: browser signup, robot fill, ready, role
assignment, general selection, trust mode, `GAME_OVER`. Implemented by
[`tools/autotest/web_client_driver.mjs`](../tools/autotest/web_client_driver.mjs),
a dependency-free Chrome DevTools Protocol (CDP) driver for Node 22+.

This is an acceptance procedure, not a unit test. It needs the real
server, the real WASM rules runtime and a real Chromium browser.

## Prerequisites

- Native `qsanguosha_server` built (Release or Debug). Debug runs need the
  Qt bin directory on `PATH` first (see `AGENTS.md` §7.5).
- WASM rules runtime packaged into `web/public/rules` — see
  [web-client-wasm-runtime.md](web-client-wasm-runtime.md). The three
  artifacts (`qsanguosha_client_wasm.mjs`, `.wasm`, `.bundle.json`) must
  come from the same build as the running server, or signup is rejected
  with `rules_version_mismatch`.
- `web/dist` built: `cd web && npm run build` (runs the protocol drift
  check, translation check, seat-ring check, `tsc --noEmit`, Vitest and a
  Vite production build).
- Node 22+ (built-in `fetch` and `WebSocket`).
- Chrome or Edge.

## Procedure

### 1. Prepare the declared content closure

WebSocket signup requires a `declared-v2` rules identity. Run the server
from a clean closure, not the source tree:

```powershell
python tools\package-web-solo.py --prepare-content --asset-root . `
  --destination builds\web-declared-content
Copy-Item config.ini builds\web-declared-content\
Copy-Item -Recurse lua\ai builds\web-declared-content\lua\
```

The stock `config.ini` has `EnableCheat=true`, `FreeChoose=true`,
`FreeAssign=true` — keep them; the driver relies on all three.

### 2. Start the server (20P, WebSocket)

```powershell
$env:PATH = "H:\Qt6111\6.11.1\msvc2022_64\bin;$env:PATH"   # Debug only
cd builds\web-declared-content
..\..\release\qsanguosha_server.exe --game-mode 20p --port 9527 --websocket-port 9528 | Tee-Object server.log
```

Expect `Listening on 0.0.0.0:9527` and `WebSocket listening on
0.0.0.0:9528`.

### 3. Serve the client

```powershell
cd web
npm run preview -- --port 5173     # serves web/dist
```

### 4. Start headless Chrome with CDP

```powershell
& "C:\Program Files\Google\Chrome\Application\chrome.exe" `
  --headless=new --remote-debugging-port=9222 `
  --user-data-dir=%TEMP%\qsan-cdp-profile http://127.0.0.1:5173/
```

Headless has no visible window; take screenshots over CDP
(`Page.captureScreenshot`) when needed.

### 5. Run the driver

```powershell
node tools\autotest\web_client_driver.mjs --general s4_sunjian `
  --name web-sgs1 --timeout-min 120
```

Options: `--cdp-port`, `--url`, `--general`, `--name`, `--marker`
(server log path, surfaced when a `GAME_OVER` line appears),
`--timeout-min` (default 45 — too short for 20P, use 90–120).

## What the driver does

An in-page autopilot polls every 150 ms:

1. **Lobby** — clicks `加滿機器人`, then `準備`.
2. **分配身分 (CHOOSE_ROLE)** — `FreeAssign=true` makes the room owner
   assign roles before the game. The panel renders one `<select>` per
   seat. The driver writes the valid 20P spread
   `lord ×1, loyalist ×8, rebel ×10, renegade ×1`
   (`ZCCCCCCCCFFFFFFFFFFN` in `engine.cpp` role tables) and clicks `送出`.
   The reply array order also fixes the seat order, so index 0 (the
   owner) becomes seat 1 / lord.
3. **選擇武將 (CHOOSE_GENERAL / ASK_GENERAL)** — clicks the target
   general's button in `.general-pick` if offered; otherwise clicks the
   first offered option. Either way a `WebSocket.prototype.send` patch
   rewrites reply frames (`command` 10/65, `payload.general`) to the
   target general, which the server accepts because `FreeChoose=true`.
   `__auto.genRewritten` counts rewrites.
4. **託管** — enables trust so server AI plays the seat.
5. **Result** — waits for `section.game-result`, prints its text, saves
   `web-client-final.png` and `web-client-console.log`.

## Pitfalls observed

- **`rules_version_mismatch` / `rules_reload_required`** — the packaged
  runtime predates the server build. Rebuild the WASM target, repackage
  `web/public/rules`, rebuild `web/dist`, then hard-reload the page
  (cached JS keeps the old identity).
- **10-minute interaction timeouts** — unanswered CHOOSE_ROLE /
  CHOOSE_GENERAL requests time out after ~10 minutes each; the server
  falls back to defaults (random roles/generals). A game that "starts"
  20 minutes after ready with a random general is this failure mode.
- **Offered general list is random** — never rely on the target general
  being one of the offered `.general-pick` buttons; the wire rewrite is
  what guarantees the selection. Verify via
  `record/<timestamp>.snapshots/turn_*.json` → `state.players[0].general`.
- **`panelOf` needs `closest("section")`** — the interaction `h2` sits in
  `header.interaction-header`; the controls are siblings in
  `section.interaction`, not children of the `h2`'s parent.
- **20P games are long** — a full game can exceed 50 minutes. A server
  exit without `GAME_OVER` (log ends mid-flood of extension Lua
  warnings) is a native-side defect, not a client regression; classify
  before retrying.

## Acceptance checklist

- [ ] `game_started mode=20p` in `server.log`
- [ ] Driver log shows `picked=true gen_rw=1 confirmed=true trusted=true`
- [ ] Snapshot JSON: `sgs1` seat has `general == "s4_sunjian"`
- [ ] `GAME_OVER` / winner line in `server.log`
- [ ] `section.game-result` rendered in the page (final screenshot)
