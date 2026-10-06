# Big-picture mode (10-foot UI)

Big-picture mode is a TV/controller-oriented session flag: fullscreen window,
no hover tooltips, larger skin fonts, a keyboard-focusable dialog layer, and a
gamepad story that starts with Steam Input keyboard emulation and grows into a
native gamepad service. Design rationale and phased plan:
`qsanguosha-task-briefs/evidence/BP-big-picture-research-20261006/REPORT.md`
(scheme C; section 3.1 covers the 4K scale rule, 4.6 the Steam Input mapping,
7 the BP-0/BP-1 breakdown).

## Enabling

| Mechanism | Effect |
| --- | --- |
| Command line `QSanguosha --big-picture` | Enables the mode for this launch. |
| Settings key `BigPicture/Enabled = true` (in `config.ini` / `QSanguosha.conf`) | Persists the mode across launches; written by the settings UI. |
| Environment `QSAN_BIG_PICTURE=1` | Force-enables; `QSAN_BIG_PICTURE=0` force-disables, overriding both of the above. |

`--big-picture` is parsed before `QApplication` exists and normalizes to
`QSAN_BIG_PICTURE=1`; the persisted key is read with the same early lightweight
`QSettings` path `Config` uses later.

When the mode is on:

- The main window opens fullscreen (`Qt::WindowFullScreen`).
- On a primary screen whose logical height is greater than 1440 pixels while
  the device pixel ratio is still 1 (4K at 100 % OS scaling — typical on
  Linux/X11 and Windows at 100 %), `QT_SCALE_FACTOR=2` is set before
  `QApplication`, so the session renders as 1080p logical with DPR 2.
  Explicit `QT_SCALE_FACTOR` / `QT_SCREEN_SCALE_FACTORS` values are respected
  and never overridden.
- Widget tooltips are suppressed (they are a hover-only desktop concept).
- The room skin loads `skins/<name>.tv.layout.json` on top of the regular
  layout file when present (`fulldefaultSkin.tv.layout.json` ships font bumps
  to >= 18 skin px, i.e. >= 27 px at 1080p). Scene sizes are unchanged.
- `qss/bigpicture_tv.qss` is appended to the application stylesheet
  (>= 64 px buttons, visible focus ring).
- `SpatialFocusFilter` is installed application-wide: arrow keys navigate
  modal dialogs spatially, Enter activates, Escape/Backspace backs out,
  PageUp/PageDown cycle focus groups (LB/RB stand-ins), and a translucent
  scrim dims the window behind each modal dialog.

With the mode off none of the above is installed or read; behavior and
appearance are unchanged.

## Keyboard contract

The pad-to-key contract mirrors the existing native keyboard paths
(`docs/client-core-interaction-model.md`, "Native askFor keyboard support"
and the `key_press` names in
`src/ui/testing/local-response-ui-controller.cpp`):

| Input | Key | Meaning |
| --- | --- | --- |
| D-pad / left stick | Arrow keys | Move focus / browse current group |
| A | Space | Toggle the focused item (card, target, option) |
| Start | Enter | Confirm the current draft / invoke "yes" |
| B | Escape or Backspace | Cancel / back (when the request allows) |
| LB / RB | PageUp / PageDown (Steam: Shift+Tab / Tab) | Previous / next focus group |
| Y | Ctrl+Shift+I | Game-state snapshot |
| View | F6 | Return focus to the current request dialog or the table |

Notes on the two LB/RB spellings: the Steam Input template emits
`Shift+Tab`/`Tab` because Steam cannot emit `PageUp`/`PageDown` names that are
guaranteed across keyboard layouts, and the native table path already treats
`Tab`/`Shift+Tab` as group cycling. Inside modal dialogs the spatial filter
treats `PageUp`/`PageDown` as LB/RB stubs (focus-cycle placeholder) and also
honours `Tab`/`Shift+Tab` natively.

## Steam Input

`docs/steam-input/qsanguosha-bigpicture.vdf` is a bundled Steam Input
template (zero-code fallback). It maps:

| Pad | Emitted key |
| --- | --- |
| D-pad | Up/Down/Left/Right arrow |
| A | Space |
| B | Escape |
| Start | Enter (RETURN) |
| LB / RB | Shift+Tab / Tab |
| Y | Ctrl+Shift+I |
| View | F6 |

Install (Steam Deck or desktop Big Picture):

1. Add the game to Steam (Library -> "Add a Non-Steam Game" pointing at the
   `QSanguosha` binary), or install it as a Steam title.
2. In Steamworks settings, set "Steam Input Default Configuration" to
   "Custom Configuration (Bundled with game)" and point at
   `docs/steam-input/qsanguosha-bigpicture.vdf`. Without Steamworks access,
   copy the file into your Steam `controller_config`/template directory and
   select it under Controller -> Layouts -> Templates, or recreate the
   bindings manually in the controller configurator — every binding above is
   a plain `key_press`.
3. Launch the game with `--big-picture` (or enable `BigPicture/Enabled`).

X is intentionally unbound (reserved for "end turn/discard"); the left
joystick, triggers and trackpads keep their default passthrough.

## cases/native-keyboard cross-check

`cases/native-keyboard/` is not present in this checkout, so the fixture key
sequences were verified against the two normative sources instead:

- `docs/client-core-interaction-model.md` table: `Tab`/`Shift+Tab` cycle
  groups, arrows browse, `Space` toggles, `Enter` confirms, `Escape` cancels
  when allowed, `F2` focuses skills, `F6` returns focus to the request
  dialog, `+`/`-` adjust votes, `Ctrl+Shift+I` is the snapshot shortcut.
- `LocalResponseUiController::pressKey` accepts the key names `Tab`,
  `Backtab`, `Left`, `Right`, `Up`, `Down`, `Space`, `Return`, `Enter`,
  `Escape`, `F2`, `+`, `-`, `Home`, `End` with modifiers `Shift`, `Alt`,
  `Ctrl` and an `auto_repeat` count. `Backtab` already implies Shift.

Every Steam template emission maps onto this contract: arrows -> browse,
`SPACE` -> toggle, `ESCAPE` -> cancel, `RETURN` -> confirm (`Return`/`Enter`
both accepted), `TAB`/`SHIFT+TAB` -> group cycle (`Tab`/`Backtab`),
`F6` -> refocus, `CTRL+SHIFT+I` -> snapshot action. `PageUp`/`PageDown` are
not fixture keys today; they exist only as the LB/RB contract aliases handled
by the spatial filter.

## Known limits (BP-1 scope)

- `askForQml` and Lua-authored custom dialogs do not follow the spatial
  focus filter; they keep their own keyboard handling (virtual cursor is
  Phase-2 work).
- The table's `TenFoot` layout profile, seat-card simplification, focus
  ring upgrade, on-table button legend and info card are BP-2 work; the TV
  layout file only bumps fonts.
- `PageUp`/`PageDown` group semantics are stubs (focus cycling) until
  per-request zones exist.
- The pre-`QApplication` screen probe for the 4K rule reads `xdpyinfo`
  (X11/XWayland) or `GetSystemMetrics`+`GetDpiForSystem` (Windows). On a
  session where neither reports the true physical panel (nested compositors,
  remote desktops) the automatic `QT_SCALE_FACTOR=2` may not trigger; set it
  manually in that case.
- Changing `BigPicture/Enabled` takes effect on the next launch, like Steam's
  own Big Picture toggle.
