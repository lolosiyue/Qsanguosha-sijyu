# Repository review implementation checkpoints

Scope: the 2026-09-20 repository review, R1–R8. Changes preserve existing
uncommitted work and the current transport, player visibility and Lua contracts.

| Batch | Contract | Status |
| --- | --- | --- |
| A | Localized UI phrases, unchanged stable IDs, scoped static localization gate | Source checkpoint complete |
| B | Shared declaration candidates, rejection reasons and selection; data-driven dialog factories | Source checkpoint complete |
| C | Web consumes native projection, canonical response encoding and shared event text | Source checkpoint complete |
| D | RoomScene delegates input/local commands and reuses existing interaction handlers | Source checkpoint complete |
| E | Engine remains a compatible facade with explicit narrower owners | Source checkpoint complete |

Each checkpoint records source/static, build, focused tests, GUI/runtime and CI
results separately.

## A: localization

Game text generally uses Simplified Chinese. Fixed UI phrases retain stable
English keys; display translations use Simplified Chinese across Qt, TUI, Web
and Office. Qt fixed text uses English `tr()`/`qsTr()` keys in
`builds/sanguosha.ts`.
The chongxu QML source is included in `QSanguosha_lupdate`. Desktop log phrase
translation retains the existing Qt context and indicator callback. Shared
formatter defaults are English; engine-backed adapters keep their existing
translation table. Replay filenames and archive member names retain their
stable format and identity.

TUI, Web and spreadsheet adapters retain their existing localization entry
points. Translation resources must be deployed through the existing export
pipeline; editing a source catalog does not establish that a running client
has loaded the updated catalog.

Validation: scoped localization gate, TypeScript `tsc --noEmit`, Qt XML/context
and placeholder checks, script syntax and `git diff --check` passed. Build,
translation dump/QM regeneration, focused executable, GUI/runtime, full gameplay
and CI: NOT RUN. User deferred execution until the A–E source changes are ready.

New engine translation keys: 40 TUI, 115 Web and 45 Office. Sheets shell text
is in `Locale.gs`; install it with the other Apps Script files. VBA keeps stable
worksheet IDs, reads `ui_phrases` from the bridge and uses English bootstrap
fallbacks before the first snapshot. The gate is
`node web/scripts/check-ui-localization.mjs`, also included in the existing
translation freshness command. It checks the eight migrated UI files and their
resource mappings, not every UI file in the repository. The Web log formatter
was retired from production in C; its historical comparison fixture is excluded
from the production localization scope.

## B: declaration migration contract

The shared evaluator must accept the current player, request reason/pattern,
skill identity and banned packages. It must be callable by Qt and TUI as well
as the native/WASM runtime; a runtime-private `Scene` is not a public contract.
Candidate enumeration and validation must not mutate declaration tags. The
adapter that installs the selected card must retain its lifetime through the
subsequent view-as/card-build operation and clear the old tag before replacing
its storage.

Guhuo/Juguan/Tiansuan rules include play-only gates, `!`/`$` parameters, package
bans, locked/unavailable cards and Tiansuan removal marks. Specialized dialog
rules must migrate before removing their override. The factory inventory also
includes Huomo, Taoluan, Youlong, Shefu, Pingjian, Weidi, Caozhao, Huashen and
MobileJianying. GUI-only skill/general selection must not be silently presented
as an ordinary card declaration. Lua dialog setters retain their data contract.

Implemented in `src/core/skill-declaration.*`, consumed by package dialogs,
`src/tui/tui-skill-dialog.*` and `ClientRulesSession`. Special eligibility lives
in skill hooks; Weidi/Pingjian publish skill candidates, Huashen remains a UI
preview. MobileJianying's pre-existing unsupported declaration stays explicit;
this migration does not invent missing skill behavior. Qt factories and special
dialogs are in the GUI target. Core/package/scenario no longer expose the old
`QDialog *getDialog()` fallback. Cached dialogs use QPointer and declarations
retain owned card clones through view-as evaluation.

## C: Web cutover contract

Use the existing native ingress stream and its generation/revision/request
correlation. Complete the projection before replacing the TypeScript gameplay
reducer. Keep socket/handshake and local selection state in the Web adapter.
STATE_SYNC publishes only after its atomic end; late worker results from an old
generation must not change the current screen.

Canonical responses cross the worker boundary and are validated and encoded
through ClientCore. The transport may assign the outgoing message ID, but must
not choose reply commands or build reply payloads. Native submission and the
outgoing frame echo must share an exactly-once lifecycle. Shared event text
comes from the existing C++ formatter; DOM escaping stays in the Web presenter.

Preview evaluation returns a typed `InteractionResponse` to native callers and
never a sendable wire reply. Only a correlated `submit_selection` may reserve a
reply after the persistent ClientCore accepts it. The subsequent outgoing echo
must match the reservation before the ingress completes transport observation.
Bridge schema is now 3: older WASM deployments must be rebuilt/exported together
with the Web shell. The subsequent validation checkpoint rebuilt
the WASM bundle and Web shell, exported translations and regenerated the Qt QM.

The production TypeScript reducer and log formatter are removed; historical
comparison fixtures live only in `web/tests/fixtures`. `ClientRulesIngress`
uses ClientCore's committed state directly, buffers STATE_SYNC until commit,
and formats events through the shared formatter. Native validation owns
deadlines, response correlation and exactly-once submission. The browser keeps
only its transport, UI draft and presentation state. Worker result handling
serializes native acceptance, transport send and outgoing observation; socket
close drains already received native frames before deciding the final result.

## D: scene ownership

`RoomInputRouter` translates key releases into the existing scene actions.
Mouse, keyboard and touch continue to use the same confirmation, cancellation
and card-selection handlers; the router stores no gameplay draft. Chat/local
music commands belong to `RoomChatController`, with narrow callbacks to the
existing editor, transport, log and scene music state. Existing Qt translation
contexts and controls remain in use.

## E: engine facade

`EngineTranslationCatalog` owns the mutable bootstrap translations and their
initial values. Engine retains RoomRuntime overlay resolution and composite
translation semantics. `EngineChatCatalog` owns shortcut text/voice assembly;
the old Engine methods remain compatibility forwarding methods, and ChatWidget
uses the narrower chat reader. Lua/SWIG public method names are preserved.

ChatWidget still includes Engine for its other existing needs. This is a
narrower chat API and explicit ownership change, not complete UI/Engine
decoupling or a claim that every Engine responsibility was split.

## Source delivery and validation (2026-09-20)

Executable checks were bounded to 60 seconds each; no CTest or complete game
was run. Detailed reports, logs and GUI screenshots are retained under
`builds/repository-review-validation/` (ignored runtime artifacts).

| Gate | Result |
| --- | --- |
| Scoped localization/resources/placeholder check | PASS |
| Production Web TypeScript, no emit | PASS |
| Changed package/core/UI preprocessor nesting and new CMake file registrations | PASS (static only) |
| Qt TS XML and scoped git whitespace checks | PASS |
| Test sources | Updated; focused results below |
| Changed Web session/rules/replies/native-state tests, strict TypeScript | PASS (type checking only) |
| Test-inclusive TypeScript check | Not clean: existing identity-test Node crypto/JSON inference types; not used as a passing gate |
| Native Debug configure/build/link | PASS: GUI, TUI, Excel bridge/view tests, ClientCore tests and rules ingress/session probes |
| SWIG | Fresh generation and compilation PASS in the new WASM build; native incremental build reused its existing generated wrapper |
| Translation exporter and lrelease | PASS: fresh Web translation dump and Qt QM; lrelease reports 1,586 translations and 63 remaining untranslated source entries |
| WASM build and local packaging | PASS: Qt 6.11.1 / Emscripten 4.0.7, bridge schema 3; paired hashes verified in public assets and final Web dist |
| Web production bundling and static synchronization gates | PASS: Vite, translations, protocol and seat ring; this is not the full npm build/test suite |
| Focused Web tests | PASS: 7 files / 72 tests |
| Google Sheets tests | PASS: 30 tests |
| Native rules ingress/session | PASS: both executables and both report verifiers, builtin assets only |
| ClientCore and Excel view focused executables | PASS |
| TUI log-text | PASS |
| TUI play-skills | PASS: 35.36 seconds after repairing the fixture; initial run correctly rejected a Dismantlement declaration without a valid target |
| Checker self-tests | PASS: ingress (8), session, bundle packaging (4); protocol parser mutation rejects a missing encoder |
| Windows GUI startup | PASS: application, engine, main window, event loop, home scene and normal shutdown; 30.81 seconds |
| Windows GUI interactions | PASS: view-as response (16 assertions), guanxing (17), card chosen (13); all process exits 0 and screenshots retained |
| Browser WASM execution, full games and remote CI | NOT RUN |
| Manual keyboard/chat/menu/replay visual acceptance | NOT RUN; the automated GUI cases do not establish these behaviors |
| Commit/push | NOT RUN |

Source cases cover declaration side effects/live eligibility, raw native
projection/hidden-card counts, atomic sync, stale generations/revisions,
preview-versus-submit separation, native rejection, duplicate replies,
reservation echo, deadline expiry and uint64 request correlation. These are
test implementations; only the executed gates above establish passing results.

Validation repaired three build issues: a missing Lua API include in the chat
catalog, a stray literal newline suffix in package dialogs, and the missing
client-player-model declaration in the TUI adapter. The protocol static checker
now checks production native reply descriptors instead of the removed Web reply
encoder. Python report verifiers now distinguish preview results from reserved
submit wires and preserve each request ID rather than assuming every request
uses the uint64 boundary fixture.

The TUI declaration fixture now gives the target a hand card, as Dismantlement
requires a target with cards. Production declaration availability remains strict;
the discarded recast workaround is not part of the final implementation.


## Extended validation and Web trust correction (2026-09-20)

The extended checkpoint uses Debug native binaries, mode
`03_1v2`, seed `20260920`. Full logs and identities are retained in
`builds/repository-review-full-validation-20260920/`.

| Gate | Result |
| --- | --- |
| Incremental native build, including all CTest executables | PASS |
| Full CTest, 66 entries | FAIL: 56 passed, 10 failed (4 timeouts), 0 skipped; exit 8; 2,140.47 seconds |
| Native headless TrustAI | PASS: 1/1, rebel winner, exit 0, final card-lifetime gauge zero |
| User-operated keyboard GUI | PASS: lord+loyalist winner; user confirms keyboard-only input, no trust; client/server exit 0 and ports released |
| Actual Chrome client WASM with dedicated native server | PARTIAL: visible rebel victory and GAME_OVER agree; server exit 0 and final gauge zero, but defects below remain |
| Browser Solo | NOT RUN; the browser run above uses an external native server |
| Web trust control correction | PASS: production TypeScript and Vite build; actual active prompt exposes the control |
| Remote CI / commit / push | NOT RUN |

The first isolated Web HTTP deployment omitted the documented `/assets/`
image mapping. Reusing the existing Vite preview middleware restored portraits,
cards and background images; an actual card request returned HTTP 200 image/jpeg.
Source artwork was not changed. The final rules deployment preserves all 227
declared content files and the exact source [`lua/config.lua`](../../lua/config.lua) bytes. Server-only
AI bootstrap was staged separately from the browser rules closure.

`interactionView()` now keeps the existing trust control visible during active
prompts in a started game. It lives in the header's auxiliary wrapper, outside
the confirmation/cancellation slots. Waiting-room and game-over behavior remain
unchanged. This addresses the observed disappearing-button race when zero-delay
TrustAI opponents advance quickly. Existing localization keys are reused.

The browser reconnected to the same game after this frontend correction and the
trust control was clicked. This is not an uninterrupted full game of the final
frontend. Server AI disabled is recorded, but independent server-side evidence
of the human seat's trust state was not captured before room disposal.

Remaining browser findings: the reconnected prompt displayed
`invalid_string_list`, and a stale prompt/deadline remained during automated play.
The final native log also reports `extensions/gaoda.lua:710` accessing nil `owner`
before GAME_OVER. These remain recorded defects; no external Lua or native-engine
fix was attempted during this validation checkpoint. Browser and owned game/server
processes are closed and their relevant ports released.

The CTest failures are runtime_paths, takeover_snapshot_contract, tui (board-view),
packaging_contract, fetch_extensions_contract (WSL unavailable),
websocket_gateway_contract, card_lifetime_source_check, server_unit,
roomthread_perf and extension_manifest. Four are registered timeouts. Details are
in the extended validation summary and JUnit report. No claim is made that these
failures predate the changes in the dirty working tree. All test processes are
now stopped; failed gates remain open rather than being promoted by focused passes.
