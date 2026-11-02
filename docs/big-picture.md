# Big-picture mode (10-foot UI)

Big-picture mode is a TV/controller-oriented session flag: fullscreen window,
no hover tooltips, larger skin fonts, a keyboard-focusable dialog layer, and a
native SDL3 input path shared with controller-only gameplay. Design rationale and phased plan:
`qsanguosha-task-briefs/evidence/BP-big-picture-research-20261006/REPORT.md`
(scheme C; section 3.1 covers the 4K scale rule, 4.6 the Steam Input mapping,
7 the BP-0/BP-1 breakdown).

## Enabling

| Mechanism | Effect |
| --- | --- |
| Command line `QSanguosha --big-picture` | Enables the mode for this launch and locks the settings toggle. |
| Home Settings or in-game Configure → 大屏模式 (`BigPicture/Enabled`) | Applies immediately as a preview; Save persists it and Cancel restores the previous mode. |
| Environment `QSAN_BIG_PICTURE=1` | Force-enables and locks the toggle; `QSAN_BIG_PICTURE=0` force-disables, overriding both of the above. |

`--big-picture` is parsed before `QApplication` exists and normalizes to
`QSAN_BIG_PICTURE=1`; the persisted key is read with the same early lightweight
`QSettings` path `Config` uses later.
The live mode is held per process, so a child instance resolves its own mode
instead of inheriting an unsaved preview. Switching applies the stylesheet, input
filters, pointer policy, fullscreen, home TV styling and room layout at once.
Leaving fullscreen restores the desktop placement saved on entry. Skin fonts follow
at the next room, and `QT_SCALE_FACTOR=2` only changes at the next launch.

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
  layout file when present (`fulldefaultSkin.tv.layout.json` and
  `fulldefaultSkinAlt.tv.layout.json` — the Linux skin list — ship font
  bumps to >= 18 skin px, i.e. >= 27 px at 1080p). Base and overlay are merged
  before parsing, preserving base colors, derived variants and layout geometry.
- `qss/bigpicture_tv.qss` is appended to the application stylesheet
  (>= 64 px buttons, visible focus ring).
- Physical keyboard/Steam keyboard events use `SpatialFocusFilter` for modal
  spatial navigation. Controls retain their own editing/list keys; acceptance
  and cancellation retain the native mandatory-choice contract. SDL actions
  and their synthetic keys are handled exclusively by `ControllerRouter`.
  A translucent, pointer-transparent scrim dims each modal dialog's host.
- The existing table adapter adds a solid focus ring and contextual button
  legend. New requests can acquire visible enabled focus after gamepad use;
  an input that first reveals missing focus does not immediately select it.
  BP target Left/Right traversal follows visible seat geometry; Up/Down and
  shoulders retain the controller-only group/command navigation.
- On landscape viewports of at least 1280x720 the room switches to the
  `TenFoot` layout profile. It lays out a scene based on 1280x720 (wider or
  taller viewports extend it) and the view zooms the whole scene, so at 1080p
  the dashboard, hand, pile, seats and skin fonts all render at 1.5x. Opponents
  sit in one row across the top of the table, using the largest of the Normal
  layout or a scaled Small layout that fits every seat; only beyond that does
  the row page. `Config.UIScale` does not apply in this profile. Seat cards drop
  the decorative frame art (portrait, names, HP, hand count, role, equipment,
  judgement and marks remain), the controller legend renders at the top of the
  table band clear of the hand row, the pile and seat targets, and
  confirm/cancel keep >= 64 scene-unit touch targets. Smaller or portrait
  windows keep the existing responsive profiles or the legacy desktop layout.
- Gamepad use hides the pointer in BP; deliberate mouse motion, a mouse button
  or physical keyboard input restores it. Synthetic keys do not switch modes.
- Closing a BP session preserves the saved desktop window size, position and
  state.

With BP off, TV styling, skin overrides, fullscreen, tooltip suppression,
modal scrims and the TV legend are disabled. The existing controller input
remains available; its shared mode tracker does not hide the pointer.

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
| B | Escape | Cancel / back (when the request allows) |
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

The default desktop input is the existing native SDL3 controller path; use a
Steam **gamepad** layout for it. `docs/controller-integration.md` defines its
South/West/East/North and Start mappings.

`docs/steam-input/qsanguosha-bigpicture.vdf` is an optional keyboard-emulation
fallback. It must be paired with `--controller-keyboard-fallback`, which
skips native SDL polling for the entire process. Do not use the keyboard VDF
with the default native-input launch. Builds configured with
`QSAN_ENABLE_CONTROLLER=OFF` also have only the keyboard ingress. It maps:

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
3. Launch with `--big-picture --controller-keyboard-fallback`. This is an
   exclusive keyboard-emulation session: the native SDL service/router is not
   created, preventing a single pad press from also entering the SDL path.

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

- Custom QML remains subject to the existing `controller_ui` contract in
  native controller sessions; arbitrary QML is not automatically navigable.
- The home pages take the pad directly: the D-pad reaches QML's own KeyNavigation
  first and walks the tab chain when nothing accepts the key; a closed combo box
  always moves focus, and South picks the highlighted row of an open one. LB/RB and
  LT/RT switch home tabs in BP. QML text fields (search, server name) cannot be
  edited by pad: South does nothing there. On 2026-11-03 a virtual pad went from the
  desktop home to saving 大屏模式, and from the BP home through quick join and the
  game menu to a started game; physical hardware is still untested.
- The table's `TenFoot` layout profile now applies during BP sessions on
  landscape viewports of at least 1280x720 (the shared `selectsTenFoot`
  predicate; large rooms and usable folds take their existing paths). At a
  logical ~1280x706 (720p panel minus menu bar) the gate does not trigger and
  the session falls back to the pre-existing BP desktop layout — observed,
  not a TenFoot render. A 1920x1080 BP screenshot shows the TenFoot seat row,
  hand and dashboard. The black lower backdrop was traced to TenFoot using
  table-band height for later background updates and corrected to main-area
  height; full-coverage recovery is confirmed on captures (2026-10-28).
- The missing-bottom-zone defect reported on 2026-10-28 (viewport 2750x1129 at
  DPR 1.25) did not reproduce on 2026-11-02: a window capture of the virtual
  scene build on the same 3440x1440 panel shows the dashboard, hand row and
  pile. A DPI-unaware `CopyFromScreen` capture of that build still loses the
  bottom zone, because it only sees the top-left 80 % of the panel. The
  pre-virtual-scene build was not retested. Capture QA frames with a DPI-aware
  process and `PrintWindow`.
- On 2026-11-02 an 08p room was played through the SDL virtual gamepad
  (`--controller-virtual-input`) on Windows, with `--auto-robots`. The legend
  shows during active requests and Left/Right walks the seat row in screen
  order. A second run used only D-pad, South and East, as a TV remote would:
  every request it met was answered and accepted, with `command:confirm`
  reached by D-pad. Neither run reached game over. A remote has no Start, so the
  game menu stays out of its reach, and the legend describes a pad.
- The legend and status hint name the buttons printed on the connected pad
  (`SDL_GetGamepadButtonLabel`), falling back to the Xbox layout. Only the Xbox
  labels have been seen on screen; PlayStation and Switch labels were not exercised.
- A TV remote that reaches the game as a keyboard (arrows, Enter, Esc) cannot play
  yet: arrows need focus that only Tab creates, Up/Down stay inside a group, and
  selecting needs Space. No remote input path exists or has been tested.
- Still not accepted: a real 16:9 panel and physical gamepad or remote hardware.
  Earlier evidence lives in memory/work/zcode-tenfoot-20261027/ and
  memory/work/zcode-tenfoot-20261028/.
- `PageUp`/`PageDown` group semantics are stubs (focus cycling) until
  per-request zones exist.
- The pre-`QApplication` screen probe for the 4K rule reads `xdpyinfo`
  (X11/XWayland) or `GetSystemMetrics`+`GetDpiForSystem` (Windows). On a
  native Wayland session, compositor scaling is left untouched. Where neither
  probe reports the true physical panel (nested compositors, remote desktops),
  automatic `QT_SCALE_FACTOR=2` may not trigger; set it manually if needed.
- A room that switches mode live keeps its skin fonts until the next room, and a
  4K panel at 100 % OS scaling keeps its launch scale for home and dialogs until
  restart.
