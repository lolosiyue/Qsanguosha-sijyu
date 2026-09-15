# ClientCore interaction model

## Production contract

The production registry contains 29 interactions. All 29 use the same direct
typed path; `S_COMMAND_QML_INTERACT` is no longer a legacy adapter.

| Classification | Count |
|---|---:|
| Direct typed | 29 |
| Legacy adapter | 0 |
| Implicit passthrough | 0 |

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

After explicit authorization, this client-adapter checkpoint built QSanguosha,
qsanguosha_tui and the existing panel/TUI test targets. The one panel focused run
passed 6 items including init/cleanup (0.81 s wall), and the one TUI board-view
run passed (15.09 s). Web `tsc --noEmit` passed. Initial TypeScript field/narrowing
errors and one misplaced trigger cleanup compile error were fixed before those
targeted retries; this was not a first-attempt clean run.

Guanxing Inspector exited with `0xC0000005` before a game window or interaction
report appeared. No keyboard action was sent. Gongxin keyboard validation is
**BLOCKED** by that shared startup path and was not launched. Subsequent offline
dump/PDB analysis identified an Inspector fixture failure: positional ADD_PLAYER
payloads are rejected by the typed encoder, the injection helper ignores that
error, and ARRANGE_SEATS dereferences the missing second player. SET_PROPERTY
fixtures had the same typed-payload mismatch. The controller now uses typed
payloads for both commands, propagates notification encoding failures to the
existing report, and checks the roster before arranging seats. Static review
and the subsequently authorized incremental QSanguosha build passed. An early
post-repair exit 1 was traced to translation-table Lua syntax errors and fixed.
After continued-fix authorization, both real keyboard Inspector cases passed:
Guanxing 49.69 s (top [0,45], bottom [30]), Gongxin 51.85 s (card 30), each with
exactly one terminal reply and clean exit 0. Empty-pile Tab navigation, Inspect
countdown, intermediate mirror packet handling and panel focus restoration were
also corrected. The existing final panel test passed 6 items in 1.15 s wall;
no suite or CTest registration was added. This establishes those two local
keyboard cases; NVDA, other interaction cases and full games remain separate.
Post-repair evidence: `builds/inspector-repair-20260916/validation.md`.
Earlier crash research evidence:
`builds/game-presentation-clients-20260916/inspector-research.md`.
WASM/browser, Android/device/TalkBack, user
NVDA, full game and remote CI remain **NOT RUN**. Evidence and binary identity:
`builds/game-presentation-clients-20260916/validation.md`.

The special-interaction checkpoint (2026-09-16) adds Gongxin, trigger order and
Guhuo/Tiansuan desktop integration. It extends the existing panel keyboard test
with duplicate display labels for distinct skill instances, mandatory cancel
state and explicit completion of read-only inspection. No executable or CTest
registration was added. Source review, `git diff --check`, Windows build and
the existing panel focused executable pass. The local Guanxing/Gongxin keyboard
results above cover those cases; other live mouse/keyboard cases, NVDA and
remote CI are **NOT RUN**. Earlier checkpoint results do not imply broader coverage.

Source contracts are registered as `qsanguosha_game_presentation_contract`,
`qsanguosha_game_control_panel_contract` and
`qsanguosha_widget_accessibility_contract`. The last two require a GUI testing
build and Qt Test; headless CI can supply Qt's offscreen platform externally.
They cover privacy, translation, large IDs, event cursors, keyboard intent IDs,
stable list focus, disabled candidates, close-without-cancel and explicit copy.

Implementation and static review are distinct from build/executable evidence.
Checkpoint authorization is required before local builds or focused executables;
local CTest and full gameplay remain prohibited by the workspace rules. NVDA
manual validation, real gameplay, and remote CI are separate acceptance gates.

Local checkpoint evidence (2026-09-16, Windows / Qt 6.11.1, Debug):

| Gate | Result |
| --- | --- |
| Incremental configure; QSanguosha and the three new test targets | PASS |
| Direct shared presentation executable | PASS, 17 assertions |
| Direct widget accessibility executable, offscreen | PASS, 6 test cases plus init/cleanup |
| Direct control panel executable, offscreen | PASS, 4 test cases plus init/cleanup |
| Static whitespace check | PASS |
| NVDA manual reading; live desktop interactions; full gameplay; remote CI | NOT RUN |

Each focused executable completed within 3 seconds. No local CTest was run.
Logs are retained locally in `builds/game-presentation-20260916/`. These isolated
widget tests do not establish end-to-end mouse/keyboard parity or actual screen
reader behavior in a live room.

The user subsequently authorized a full local regression on the same date.
Full Debug build passed. All 65 registered CTests ran (no skips): the first run
passed 64/65 in 1965.92 seconds; the fetch-extensions fixture failed because bare
`bash` selected Windows' unconfigured WSL launcher. Resolving Bash to an absolute
path in the fixture launcher and selecting Git Bash made the targeted retry pass
(1/1, 3.29 seconds). Full-run and retry evidence are retained separately in
`builds/game-presentation-full-20260916/`. The run also produced an MSYS grep
stackdump, preserved there with its impact uninvestigated. NVDA, live desktop
gameplay, full-game acceptance and remote CI remain NOT RUN.

The subsequent complex-keyboard checkpoint adds ordered lists to
`GameActionModel` (`top_cards`, `bottom_cards`, movement flags and
`action_context`) without changing the wire protocol. Existing panel publication
coverage also checks a cross-pile keyboard move and preservation of its card
cursor; no test executable or CTest registration was added for this extension.
After explicit checkpoint authorization, incremental configure and the
QSanguosha, panel-test and pruned server-test targets built successfully. The
existing panel focused executable passed all four functional cases plus
init/cleanup (171 ms test time, 0.60 s wall time) using Qt 6.11.1 offscreen.
Static review and whitespace checks passed. No CTest or long server suite was
run for this checkpoint; the earlier full-suite results precede these changes.
Live-room mouse/keyboard parity and NVDA checks remain **NOT RUN**. Evidence:
`builds/game-presentation-complex-20260916/validation.md`.

Test pruning in the same checkpoint removes three registrations that duplicated
cases already run by `qsanguosha_server_unit` (gongqiao-equip, songwei-once and
ai-active-skill-activation), plus obsolete V1/V2 comparison code that actually
created V2 fixtures on both sides. With the same CMake options, incremental configure
registered 62 tests instead of 65. The three removed standalone runs account
for 51.492 seconds of the old baseline; 11 additional DecisionFixture
initializations were removed, with no measured per-case time available. Unique
protocol, privacy, request/reply and lifetime coverage is retained.

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

The artifact contract is schema version 3, total 29, direct typed 29, and
implicit passthrough 0. Focused executables cover registry completeness,
presenter dispatch, response validation, typed reply encoding, and artifact
drift. TUI 對所有 Room→Client production flow 的 reducer／presentation／interaction／
session 分類另見
[`artifacts/tui-flow-coverage.json`](../artifacts/tui-flow-coverage.json)。Local CTest
不屬本次本機 gate；remote cross-platform、production GUI 及完整 live TCP game 仍是
分開的驗收證據。
