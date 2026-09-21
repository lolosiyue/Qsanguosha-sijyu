# Browser Solo verification — 2026-09-12

Build and Browser Use acceptance recorded on 2026-09-12:

| Gate | Evidence / status |
| --- | --- |
| Launcher Release compile/link | Passed again after the browser handoff fix; PE dependencies are only WS2_32, SHELL32, USER32, ole32, KERNEL32. |
| Native export tool Debug build | Passed; `builds/cmake-vs2026/Debug/qsanguosha_rules_fixture_runner.exe`. |
| Native declared-v1 export | Passed from clean staging; 108 rules files and 117 presentation files. This operation starts no game. |
| Single-thread Web client | Passed in `builds/cmake-wasm-qt6111`; paired runtime packaged into `web/public/rules`. |
| Multithread Solo WASM | Passed in `builds/cmake-wasm-solo-qt6111`; paired module/WASM seal generated. |
| Shared build identity | Native/client/solo C++ hash `e1c60ce76bce97913f4ea71a181fc55ee9bec8e829db3c164c6488ab73759fcc`, matching bindings/protocol hashes. |
| Web static/build checks | TypeScript, protocol/translation checks and Vite production build passed with Node 22.20.0. |
| Offline directory / content hashes | Passed in `builds/browser-solo-artifacts/qsanguosha-solo`; 34,357 files, 2,753,202,851 bytes, with 108 rules / 117 presentation / 165 AI-dependency entries. Original config bytes are preserved. |
| ZIP artifact | Passed inventory comparison for all 34,357 files and hash comparison for 396 critical files after the launcher update. Created at `builds/browser-solo-artifacts/qsanguosha-solo-win-x64.zip`; 2,536,584,814 bytes. SHA-256: `63cc4c391eb46d9716ec3a387903642db6ef117e26b6734829256c13a5d90d3c`. Details are in adjacent `package-report.json` and `.zip.sha256`. |
| Controlled Chrome startup / content preparation | Passed through Browser Use on `http://127.0.0.1:9529/`: 05p selected, 4,279 general entries, enabled start button, no captured warning/error logs. HTTP 200 and COOP/COEP/CORP headers checked. The initial startup check started no match; the later full-game check is recorded below. |
| Default-browser automatic launch | A `QSanguosha Compact - Google Chrome` window was observed after launcher startup. Content read through Computer Use hit an app-approval timeout; automatic URL/content acceptance remains incomplete. |
| Complete 05P solo game / game end | Passed one natural full game through Browser Use, seed `14350027982269135049`; the game-over panel reported defeat for Chen Gong and winners Xiao Qiao (lord) and Wu Guotai (loyalist). No surrender or injected outcome. |
| Exercised gameplay UI / log rendering | Selection, response, equipment, targeting, card picking, discard and game-over controls passed. Log rendering failed: virtual cards displayed `牌 NaN`, and HTML tags appeared as literal text. Overall UI acceptance remains failed. |
| Return home / prepare again / test cleanup | Returned home and prepared a fresh setup successfully without starting a second game. Browser Use closed the test tab; the static helper PID 45480 was stopped and confirmed port 9529 was released. Natural launcher exit was not accepted. |
| Local preferences | The setup retained the test values of 60-second operation timeout and 200-ms AI delay; these remain saved. Persistence across a browser restart is unrun. |
| Card / Worker lifetime / fully disconnected reopening | Unrun. No independent native GAME_OVER log, CARD_LIFETIME_ZERO or Worker memory/orphan measurements were captured. Returning to setup does not establish these gates. |
| Other modes / Win10+11 Chrome+Edge matrix / CI | Unrun; mini/custom scenario content is not yet included. |

The builds include unrelated concurrent workspace changes and are not a clean-commit
release. No local CTest was run. The full-game check used the existing distribution
without product source changes or another build. The recorded report path is
`builds/browser-solo-artifacts/acceptance-05p-20260912-01/summary.md`.
That local report is unavailable in this checkout; the results above retain the
original dated record. Log-rendering and lifetime gates remain separate.

The startup validation uncovered an unchecked Shell call on a worker without a
Windows message loop. The launcher now initializes COM, uses `ShellExecuteExW`
with `SEE_MASK_NOASYNC`, and displays launch errors while keeping static serving
available. This follows Microsoft's [ShellExecute COM guidance](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecutew)
and [SHELLEXECUTEINFO message-loop requirements](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shellexecuteinfow).
The previous missing automatic-tab observation was not conclusively attributed to
this code defect. Detailed scope and limitations are recorded in
`builds/browser-solo-artifacts/browser-startup-report.json`.
