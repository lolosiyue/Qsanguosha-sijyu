# BP input integration

The `qs-claude/bp1-gamepad-focus` presentation work is integrated into the
existing controller-only path. There is one SDL owner (`ControllerService`),
one action router (`ControllerRouter`) and the original `ClientCore` draft and
reply validation. A second GamepadService, backend, bootstrap and
UiActionDispatcher are deliberately excluded.

The reusable input-mode tracker, TV focus ring and table button legend are
ported into the existing adapter. The legend uses the actual default native
bindings (South select, West confirm, East cancel, Start menu), rather than
the superseded branch's A/B/X semantics. Input-mode tracking ignores synthetic
keys and restores the pointer on deliberate mouse/keyboard use. TV-only
presentation follows `qsanBigPictureModeActive()`; existing controller gameplay
remains available with BP off.

New gamepad requests obtain enabled initial focus when the queued presentation
refresh runs. Querying actions for a current input never creates an unseen
default that that same input can activate. Request/session changes clear the
cursor; removed or disabled entries cannot be activated. Visible seat geometry
orders BP target Left/Right traversal, including large-room projections.
Up/Down, shoulders, ten-entry paging, multi-vote commands, native arrangements,
information panels and custom-dialog contracts keep the established behavior.
Back retains the existing per-request cancellation semantics rather than the
older branch's generic clear-all-then-cancel loop.

Panels, status hints, focus markers and legends follow `DesktopGamePresentation`
and `RoomScene` lifetimes. Marker and legend handles are QPointer; queued work
uses QObject contexts. Widget scopes are guarded across actions that close a
window, and modal scrims follow the active host without taking mouse focus.

`docs/controller-integration.md` documents native input and diagnostic virtual
SDL injection. `docs/big-picture.md` documents TV startup and the exclusive
`--controller-keyboard-fallback` launch for Steam keyboard emulation. The
fallback skips native SDL service/router creation rather than competing with it.

Validation must distinguish focused compatibility compilation from the official
Qt 6.11.1 GUI build and actual desktop/pad acceptance. No full 50-player suite,
paid JEV or large asset download is needed for this integration.
