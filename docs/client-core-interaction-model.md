# ClientCore interaction model

## Production contract

Production interactions use the direct typed path defined by
[InteractionDescriptorRegistry::descriptors](../src/client/interaction-descriptor-registry.cpp).
This includes `S_COMMAND_QML_INTERACT`. Generate the [interaction inventory](#inventory-and-gates)
to inspect the registered commands and support classifications.

```text
Protocol V2 typed request
  -> ProtocolGameplayPayloadRegistry validation
  -> ProtocolInteractionRequestBuilder / InteractionDescriptorRegistry
  -> canonical InteractionRequest
  -> DesktopInteractionView or TuiInteractionView
  -> canonical InteractionResponse
  -> ClientCore validation / deadline / exactly-once
  -> InteractionReplyEncoder
  -> Protocol V2 typed reply with full quint64 reply_to
```

GUI 與 TUI 共用同一份 registry、canonical model、validator、deadline、correlation
與 reply encoder。Public submission 只有 typed `submitInteractionResponse()`；不為
每個 interaction 建立 `respondTo*` API。Invalid、stale、duplicate、expired、disabled
或 correlation mismatch 都不會發出 wire reply。

## Shared live state boundary

```text
NativeClientSocket -> ClientLiveSession -> typed ProtocolMessage
  -> ClientGameStateReducer -> ClientGameState
  -> GUI Client signal adapter / TUI renderer
```

Production GUI 不再自行擁有第二套 socket decoder 或 gameplay reducer。GUI-only
presentation 仍留在既有 `Client` facade；TUI-only rendering 留在 `src/tui`。Reconnect
通知在 `STATE_SYNC begin/end` 之間 reduce 到 staging state，完成後才原子替換，且
舊 generation 的 pending response 不會重送。When the GUI preference enables
reconnection and the server replies `reconnect_target_missing`, `ClientLiveSession`
automatically opens one fresh (non-reconnect) connection as a fallback; this is
GUI-only — TUI does not enable it.

## Shared game presentation and desktop keyboard panel

The shared presentation contract is implemented in `src/client/core/` and links
only Qt Core. `ClientGameState` remains the state source; these models neither
reduce protocol messages nor restore gameplay from logs.

| Contract | Content |
|---|---|
| `GameViewState` | Recipient-visible players, current/operating player, phase, prompt, hand, equipment, judging area, private piles and recent events; JSON and translated plain text |
| `GameActionModel` | The typed `InteractionRequest`, versioned action/card/player/skill entries, selected draft, limits, confirmation/cancellation/finish availability and unavailable reasons |
| `GameEventStream` | Incremental cursor over existing presentation events; sequence numbers and a maximum of 200 entries |

Generation, presentation revision and request ID are serialized as decimal
strings to retain the full `quint64` range. State synchronization publishes after
commit. The desktop refreshes the model before applying an intent and rejects
old generation/revision/request, expired requests and unassessed actions.
Revision changes include committed state changes, even when candidate actions
are identical. No raw event payload is copied into the text snapshot.

`GameViewFormatOptions` supplies labels, translation, rule-derived distance and
explicit authorization for another player's visible hand. Hand visibility does
not follow from `operatingPlayer` alone. Unknown cards have no published identity;
private-pile identities must match the current owner, place and pile. Unknown
distance is represented separately from zero.

### Qt desktop entry points

Panel labels, accessibility names, action explanations and snapshot labels use
Qt translations from `builds/sanguosha.ts` (`zh_CN`, Simplified Chinese).
`QSanguosha_lupdate` includes the shared snapshot formatter;
`QSanguosha_lrelease` compiles `sanguosha.qm` for the existing runtime loader.
Desktop strings use English source keys; shared formatter keys remain compatible
with other clients. Card, player and skill labels retain the existing engine
translation source. Stable action IDs and keyboard mnemonics do not change.
Phase enum names resolve to the existing lowercase locale keys, with explicit
NotActive/RoundStart mappings. Localization validation on 2026-09-16 passed the
GUI/QM build, 107-key catalog checks and the existing panel executable (6 results
including init/cleanup). Windows accessibility inspection confirmed Simplified
Chinese controls and snapshot text. This is separate from NVDA/manual gameplay
acceptance; evidence: `builds/game-panel-translation-validation.md`.

- **View → 游戏状态**, or **Ctrl+Shift+I**: open/update a non-modal read-only text
  snapshot with Update, Copy and Close. Text remains frozen until explicitly
  refreshed. The shortcut works with table hotkeys disabled and consumes the
  corresponding `I` release without selecting a card.
- **View → 游戏操作面板**: standard Widgets lists for options, skills, cards and
  targets. Tab changes sections, arrow keys move focus, Space toggles a selection;
  named buttons confirm, cancel or finish the play phase. Closing the panel does
  not answer/cancel the game request.
- `DesktopGamePresentation` reads the existing Dashboard/RoomScene draft and
  eligibility, then routes intents through the same card/target/skill/button
  handlers. It does not create another rule evaluation or reply encoder. Mouse
  changes are reflected in the panel while stable list items retain focus.
- Simple options backed by existing dialog buttons, boolean prompts, ordinary
  play/response, player choice, show/pindian and discard/exchange are supported.
  View-as selection uses existing skill buttons and selection rules.
- Yiji distribution selects cards and one recipient per reply, using the same
  pending card, target eligibility and confirm/cancel path as the table. The
  panel shows the selected count, minimum/maximum and remaining card count.
- Guanxing shows the current top and bottom lists in draw order. Standard buttons
  move a card earlier/later or append it to the other list, respecting UpOnly /
  DownOnly. `GuanxingBox` owns the lists; mouse and keyboard edits share its move
  helper, step notification and `draftChanged` signal. Confirmation still uses
  RoomScene and the existing ClientCore validation. Mirrored views and drafts
  whose card identities do not match the active request are not editable.
- Guhuo/Juguan/Tiansuan skill options appear in the Dashboard and panel's option
  list. Guhuo subclasses keep their virtual eligibility checks (including Huomo);
  Tiansuan uses text tiles and rechecks removed-lot marks before confirmation.
  Selecting and confirming change that same
  local draft before the existing view-as flow continues. Cancelling this local
  choice does not cancel the containing play/response request. Standard QWidget
  skill dialogs can also be opened from the skill list and use their native Tab,
  arrow-key and Space handling and accessible names.
- Gongxin displays only the request's disclosed cards and permits only its
  selectable subset. Mouse single-click and panel Space edit the same one-card
  draft; double-click or Confirm submits through the existing Client reply.
  Confirm with no selection ends inspection without choosing a card; Cancel does
  the same when permitted. Timeout never submits an unconfirmed card draft.
  The prompt includes all disclosed cards, including read-only ones, and is
  keyboard-focusable/selectable. The explicit text snapshot includes this prompt.
- Trigger order projects the actual graphical options, including aggregated
  counts, owner/target labels and stable skill-instance IDs. Space selects a
  draft and Confirm submits it; optional Cancel uses the existing typed cancel
  response. Mandatory requests cannot be cancelled from the panel. Request
  cancellation or replacement clears the old draft and timer callbacks. The
  existing timeout default remains separate from the user's selection draft.
- Custom QML, other bespoke graphical dialogs and specialized assignment
  interfaces retain their original entry points. This
  is not an arbitrary multi-recipient assignment editor or a claim that every
  skill's custom interface is fully playable from the keyboard.

### Native askFor keyboard checkpoint (2026-09-21)

Scope: the modern GUI client, action panel **closed**, excluding `askForQml`.
The expanded checkpoint contains 61 native-keyboard fixtures covering representative
and boundary paths for all 26 non-QML `Client::askFor*` handlers, plus general
arrangement. The Debug GUI incremental build passed in
`builds/native-keyboard-arrangement-build-retry.log`. Automatic run evidence is
stored under `builds/native-keyboard-final-run/` and its targeted recheck directory.
Latest results: **60/61 PASS**; the 50-seat target fixture exceeded both the
45-second batch and 60-second isolated initialization limits before keyboard input.
It remains unverified. Consolidated results: `builds/native-keyboard-final-run/final-summary.json`.
Bootstrap explicitly controls hotkeys, intellectual selection and automatic targets;
Longdan and Yiji have both automatic and manual-selection cases.

This batch also repairs optional trigger-order cancellation's empty-string reply,
defers the optional free-general chooser until opened, and keeps arrangement replies
at the server-required three slots even when more generals are offered. Runner
fixtures use the current typed protocol, valid mode seat counts, and exact typed
reply payload comparisons. Original failed reports remain available.

| Client request family | Native keyboard path, without the action panel |
| --- | --- |
| `askForCardOrUseCard`, `askForNullification`, `askForSinglePeach`, play-card requests | Tab / Shift+Tab cycles available card, target, skill and command groups; arrows browse; Space toggles the focused item; Enter confirms the existing draft. F2 focuses skills; Space activates one, then Tab starts its costs/options. |
| `askForDiscard`, `askForExchange`, `askForCardShow`, `askForPindian` | Same table navigation, including equipment costs. Card count and eligibility stay with the existing pending skill. |
| `askForPlayerChosen`, `askForYiji` | Same table navigation; Tab reaches targets independently of seat count. Space toggles a target, + / - adjusts votes when supported. Large rooms reveal and outline the existing native target projection. |
| View-as skills / native skill declaration choices | Tab reaches the existing option tiles, hand/expanded-pile cards and equipment. Space toggles costs without clearing earlier choices. Enter confirms. No second selection draft is created. |
| `askForAG` | Arrows / Tab browse enabled cards; Space selects; Enter submits the current card (first enabled if none). Escape cancels only when allowed. |
| `askForCardChosen` | Arrows / Tab browse enabled cards across the disclosed zones; Space focuses a card; Enter uses the original reply handler. A concealed hand card keeps its unknown-ID sentinel. Escape only when optional. |
| `askForGongxin` | Arrows / Tab browse enabled cards; Space toggles the selection; Enter submits the selection or acknowledges inspection without a card. Read-only/empty selections can also be acknowledged. Escape only when optional. |
| `askForGuanxing` | Arrows / Tab browse; Shift+Left/Right reorders within a pile, Shift+Up/Down transfers to top/bottom, Space transfers between piles. Enter checks the request's pile counts/mode before replying. Mirrors cannot edit or reply. |
| `askForTriggerOrder` | Arrows / Tab selects an existing native option; Enter submits it. Escape only when optional. |
| `askForSkillInvoke`, `askForLuckCard`, `askForSurrender` | Enter invokes the enabled yes/OK action; Tab reaches yes/no commands and Space activates the focused command. Escape uses the existing allowed no/cancel path. |
| `askForGeneral`, `askForChoice`, `askForSuit`, `askForKingdom`, `askForDirection`, `askForOrder`, `askForRole3v3` | Original QWidget dialog: Tab / Shift+Tab focuses buttons; Space / Enter activates. OptionButton preserves its separate general double-click and direction/order click handlers. |
| `askForAssign` | Original role/seat dialog: arrows select a player, Tab reaches the role combo and move/confirm buttons. Browsing players does not overwrite their assigned roles. |
| `askForGeneral3v3`, general arrangement requests (1v1 / 3v3 / XMode) | Arrows / Tab browse available generals; draft Enter/Space chooses. Arrangement Space adds/removes a general, Alt+Left/Right reorders chosen generals, Enter submits exactly three. |

F6 returns keyboard focus to the current native request dialog (preserving its
focused option), or to the table when no request dialog is open. It works across
windows within the application and does not require `EnableHotKey`. It is not an
OS-global shortcut: after switching to another application, return to the game first.

Native Tab/arrow/Space/Enter paths do not require `EnableHotKey`. With table
hotkeys enabled, existing letter/arrow hand selection remains available before
entering Tab navigation. Ctrl+Tab leaves table traversal for normal widget focus;
chat, embedded editors and modal dialogs keep their own keyboard handling.

`FitView::event` catches Tab before QWidget focus traversal. `RoomScene` dispatches
against the current request, rejecting replay/state-sync/expired inputs. A handled
press consumes its release and repeat events, preventing a second legacy reply.
The table adapter reuses `GameActionModel` and the existing guarded intents;
specialized boxes reuse their original selection/reply routines. Native focus
outlines are presentation only and are cleared when the request changes.

This is not P7 controller/Android TV acceptance. P7 requires directions, confirm
and back alone; the current keyboard path still distinguishes group navigation,
toggle and submission and uses extra keys for rearrangement. Device input mapping,
remote-only focus recovery and a complete game remain separate work.

Static review and `git diff --check` cover this checkpoint. The runner's
`cases/native-keyboard/` fixtures use actual key press/release events through
production FitView or the focused request widget and fail if the action panel is
open. The 61 cases include concealed opponent hand counts, equipment costs,
50-seat target selection, optional/mandatory cancellation, held Enter and modifier
handling. Coverage represents request families, not every skill or gameplay state.
Manual GUI parity, NVDA, full games and CI remain separate gates; a 50-seat UI
fixture does not establish full-game acceptance.

- Standard-widget accessibility preserves Qt's effective names and explicit
  application names; unnamed actionable controls can use authored tooltip or
  placeholder text. Helper-owned fallback names track changes. No automatic
  narration of every event is enabled.

### Other client adapters

The next source checkpoint connects TUI, Web and Android to the same contract.
Existing network messages, Lua/SWIG APIs and response encoders remain unchanged.

| Client | Presentation source and entry point | Action boundary |
|---|---|---|
| TUI | Committed ClientGameState projected to GameViewState; existing board and `/status` consume the projection and ordered events | Existing parser and ClientCore remain authoritative; unassessed candidates are disabled |
| Web | Native rules stream `presentation` exposes the serialized GameViewState, plain text and incremental events; browser UI retains an explicit text snapshot | Current selection and known WASM rule evaluations supply GameActionModel; replies still use prepareReply and the existing encoder |
| Android | Overflow menu → 遊戲狀態 / 遊戲操作面板, using the same RoomScene adapter as desktop | Same graphical selection draft, request guards and submission route |

The Web snapshot adapter currently uses `selfName` as its operating character
and leaves distance unknown. It does not infer another character's hand
visibility from focus, turn order or known-card counts. A future operating-seat
adapter needs explicit visibility authority and existing active rule metrics.
Web confirmation checks the captured presentation generation/revision/request
before using the current native reply, including after selection changes.

Android uses maximized standard Widget dialogs, scrollable control sections and
48 logical-pixel touch targets. Focused controls scroll into view; closing a
panel does not cancel the game request. Background application state disables
actions, and dispatch rechecks application, connection and synchronization state.
An external keyboard uses the same snapshot shortcut and release guard.
TalkBack behavior and small-screen layout require separate device acceptance.
Excel and legacy XP retain their existing interfaces in this checkpoint.

### Manual desktop and screen-reader handoff

The user will perform final Windows Qt 6.11.1 + NVDA verification. Until actual
results are recorded, that gate stays **NOT RUN**. A compiled widget contract or
an accessibility-tree inspection does not establish NVDA reading behavior.

Record the executable build/time, NVDA version, case, observed focus/name/state
and actual reply outcome. Keep a failed case's visible prompt and reply report.

| Check | Expected result |
|---|---|
| Ctrl+Shift+I with table hotkeys on and off | Opens/updates snapshot without selecting the I card; extra modifiers do not match |
| Snapshot reading and Copy | Hand/HP/phase/zones/prompt match permitted information; text stays frozen during incoming events; copied text matches |
| Tab, arrows, Space and named buttons | Focus and selected/disabled states are readable; mouse and panel show the same draft; closing restores focus without a reply |
| Play, response and skill options | Candidates update after each selection; one Confirm produces one accepted response |
| Yiji and Guanxing | Recipient/card draft is shared; top/bottom ordering and movement restrictions match the table |
| Gongxin and trigger order | Only disclosed cards are readable; read-only completion works; duplicate labels preserve distinct instance IDs; mandatory cancel stays disabled |
| Request timeout/replacement/reconnect | Old focused actions cannot answer a newer request; editing resumes only after committed synchronization |

The existing [local response inspector](room-askfor-ui-matrix.md) includes
buttons for the production control panel and text snapshot. Its captured reply
can verify a single fixture interaction without a full match. It does not cover
the MainWindow shortcut/menu, live TCP reconnection or full-game acceptance.

### Presentation gates

The [2026-09-16 presentation report](reports/client-core-presentation-20260916.md) records the focused checks, full CTest run, failed fixture and retry, and subsequent keyboard checkpoint. Live-room keyboard parity and screen-reader acceptance remain separate checks.

## QML interaction

QML requests use a structured custom-interaction object containing a type,
schema version, title, payload, and response schema. A reply is:

```json
{
  "schema_version": 1,
  "has_value": true,
  "value": {}
}
```

Cancellation uses `has_value: false`. There is no positional
`[qml_path, parameters]` wire payload and no `LegacyV1InteractionReplyAdapter`.

## Cancellation

Cancellation is explicit and schema-specific. Examples include
`cancelled: true` and `has_value: false`. Card identifiers never use `-1` as a
wire cancellation sentinel.

## Inventory and gates

The production GUI writes the registry-derived artifact:

```powershell
debug\QSanguosha.exe --interaction-inventory artifacts\client-core-interaction-matrix.json
```

The generated artifact reports its schema version, totals and support classifications
from the registry. Focused executables cover registry completeness,
presenter dispatch, response validation, typed reply encoding, and artifact
drift. Remote cross-platform, production GUI and complete live TCP games
are validated separately; dated results belong in verification reports.
