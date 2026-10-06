# Gamepad input and table focus (BP-1a / BP-1c)

Big-picture mode's controller layer. Device input becomes a semantic `UiAction`;
the game table handles actions directly and every other surface receives the
equivalent key press, so keyboard focus work serves gamepads unchanged.

## Pieces (`src/ui/input/`)

| File | Role |
| --- | --- |
| `ui-action.*` | `UiAction`, glyph styles, key maps, legend glyphs |
| `gamepad-backend.h` | Device interface (positional buttons, axes, hotplug) |
| `sdl3-gamepad-backend.*` | SDL3 gamepad subsystem only (no SDL video/window), polled from a `QTimer` |
| `gamepad-service.*` | D-pad + left stick merge, dead zone with hysteresis, trigger thresholds, held-direction repeat (400 ms, then every 110 ms), hotplug, glyph style of the last used pad |
| `input-mode-tracker.*` | Last input: mouse / keyboard / gamepad. In big-picture mode gamepad input hides the pointer; moving the mouse more than 8 px restores it |
| `ui-action-dispatcher.*` | Table handler first, otherwise a key press/release to the focused window |
| `gamepad-bootstrap.*` | Starts the service from `main.cpp` |

## Buttons

| Pad (Xbox / PlayStation / Nintendo position) | `UiAction` | Table | Other surfaces (key) |
| --- | --- | --- | --- |
| D-pad, left stick | `up/down/left/right` | Move in the group; targets follow the seat ring | Arrows |
| A / ✕ / B (bottom) | `accept` | Toggle the focused item | Enter |
| B / ○ / A (right) | `back` | Clear the selection, then cancel | Escape |
| Start / OPTIONS / + | `confirm` | Submit | Enter |
| X / □ / Y (left) | `secondary` | Finish the play phase | Space |
| Y / △ / X (top) | `details` | Reserved for the information card (BP-1f) | — |
| LB / RB | `previous_group` / `next_group` | Previous / next group | PageUp / PageDown |
| LT / RT | `previous_page` / `next_page` | First / last hand card | Home / End |
| View, Guide | `view`, `menu` | Reserved (log drawer, room menu) | — |

Buttons are positional: on Nintendo pads the bottom button (labelled B) is
still `accept`. Actions without a key are available through
`UiActionDispatcher::actionDispatched`.

Choice boxes that keep their own keyboard handlers on the table (AG, Guanxing,
Gongxin, trigger order, general choice, KOF arrangement) receive the table keys
(`accept` = Space, `confirm` = Enter, groups = Tab / Shift+Tab).

## Table focus layer

`DesktopGamePresentation::handleUiAction` reuses the groups and guarded intents
of `handleTableKey` (keyboard behaviour is unchanged):

- Player group: Left/Right walk the seats clockwise as drawn (left column, top
  row, right column), Up/Down pick the nearest seat above/below, Up from the top
  row crosses the table. Geometry comes from the visible items, so responsive
  and large-room layouts follow what is on screen.
- Initial focus, when a request starts in gamepad mode or on the first press
  without focus (that press only reveals focus): yes/no prompts → Confirm; skill
  options → the selected/first option; otherwise first usable hand card, else the
  nearest legal target, else Cancel for responses / Confirm for other requests.
- Selecting a card that now needs targets moves focus to the nearest legal target.
- Big-picture mode draws a solid gold focus ring with glow (scaled with the
  scene) instead of the dashed outline, hides it while the mouse is used, and
  shows a button legend above the dashboard.

## Switches

- `QSAN_ENABLE_GAMEPAD` (CMake, default ON on desktop, OFF on Android/XP/Web):
  links SDL3 when `find_package(SDL3 CONFIG)` succeeds, otherwise configures
  without device polling. `QSAN_TEST_GAMEPAD=ON` builds `qsanguosha_gamepad_tests`
  (fake backend; `ctest -R gamepad_input`).
- Runtime: the service starts when `--big-picture` / `BigPicture/Enabled` is on,
  or when `Gamepad/Enabled=true`. `Gamepad/Enabled=false` disables it even in
  big-picture mode. Without either, nothing is polled and no event filter is
  installed.
- Local response UI cases accept `{"type": "ui_action", "action": "right"}`,
  delivered through the production dispatcher.
