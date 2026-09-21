# ClientCore presentation verification — 2026-09-16

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

A full local regression followed on the same date.
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
Incremental configure and the
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
