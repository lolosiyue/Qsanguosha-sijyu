# Web client native-rules/WASM runtime

This document defines the Web client's native rules runtime. Gameplay eligibility
uses the same C++/Lua implementation as native clients. The browser Worker uses
the `ClientRulesIngress` streaming API; the legacy per-query file evaluator is
rejected with `stream_snapshot_api_disabled` when stream mode is enabled.

The shared rules identity and WebSocket admission gate are defined in
[rules-bundle-identity.md](rules-bundle-identity.md). The streaming contract is
defined in [Stream ABI](#stream-abi) below.

## Target architecture

The browser keeps the TypeScript/DOM presentation layer. Gameplay rule queries
move behind a small client-runtime API that can be compiled natively for TUI
and tests and to WebAssembly for the browser's dedicated Worker.

```text
Protocol V2 frames
      |
      v
client-visible state reducer
      |
      v
client runtime (C++/Lua)
  - Player/Card state projection
  - card availability
  - targetFilter / maxVotes
  - targetFixed / targetsFeasible
  - ViewAs card construction
  - prohibit / distance / attack range
      |
      v
presentation-neutral selection result
      |
      v
TypeScript / DOM
```

The server remains authoritative. A browser-side positive result is only a
preview; submitted replies are still validated by the server.

## First vertical slice: target evaluation

`src/client/runtime/client-target-evaluator.h` moves the engine-facing target
selection calculation out of TUI presentation code without changing TUI
behaviour.

The evaluator deliberately preserves three native-client semantics:

1. The four-argument `Card::targetFilter(..., maxVotes)` overload is the source
   of target capacity. Its boolean return value is not sufficient for cards
   such as Collateral.
2. Repeated target names are legal when `maxVotes` is greater than the number
   of votes already spent on that player.
3. `targetFixed()` short-circuits local target validation because those targets
   are owned by the current server interaction context.

Missing client-visible player state produces an **unknown** result, not a local
rejection. Incomplete projection must never silently turn into "illegal".

## Second vertical slice: shared state projection

Frontend-neutral state projection now lives under `src/client/runtime/`:

- `client-state-projection.h` applies normalized `ClientGameState` player data
  to an engine `Player`, including scalar/dynamic properties, flags, marks,
  history, card limitations, and visible skill changes.
- `client-room-context.h` owns the client-side `RoomState`, registers the
  `EngineRuntimeContext`, applies live `UPDATE_CARD` changes to `WrappedCard`,
  exposes card owner/place lookups, and carries card-use reason/pattern.
- TUI uses `ClientRoomContext` through a compatibility alias and delegates its
  player projection to the shared helper.

## Third vertical slice: explicit runtime target

`ClientPlayer` and its player-model implementation live in
`src/client/runtime/client-player-model.*` and are compiled by the dedicated
`qsanguosha_client_runtime` static library.

The exact global Qt meta-object name remains `ClientPlayer`. This is deliberate:
existing engine client paths use `inherits("ClientPlayer")` to choose
client-visible/cached rule behaviour rather than server-only evaluation.
Moving the implementation therefore must not rename the class.

`src/tui/tui-client-player.*` is only a compatibility adapter: the old
`TuiPlayerModel` spelling aliases the shared `ClientPlayerModel`, while the
implementation and AUTOMOC ownership belong to the runtime target. TUI links
that target rather than owning a second player-model implementation.

The runtime library is intentionally engine-facing but does not propagate a
normal `qsanguosha_engine` link. Final products choose the engine link policy;
TUI currently requires `WHOLE_ARCHIVE` for package registrars, and a second
normal engine link would conflict with that CMake link feature. The runtime
itself propagates `ClientCore`, Qt Core and Qt Network dependencies. Qt Network
is currently required by the engine headers' non-desktop precompiled-header
path; the runtime also exports `QSAN_ENGINE_TEST_BUILD` so consumers use that
path without supplying frontend-specific compile settings.

## Fourth vertical slice: shared selection runtime

`src/client/runtime/client-selection-runtime.h` now owns the engine-facing
selection helpers that were previously embedded in `tui-play-skills.cpp`.
TUI remains a localized adapter over these presentation-neutral results.

The shared API provides:

- prompt-pattern to ViewAs-skill resolution;
- interaction/handling-method to native `CardUseReason` mapping;
- visible ViewAs skill candidate discovery;
- native activation availability checks for legacy ViewAs and ViewAsSkillV2;
- ordered subcard validation and native ViewAs card construction;
- target-step and finished-target evaluation through the shared target
  evaluator;
- canonical `InteractionResponse::CardSelectionData` construction. The existing
  `InteractionReplyEncoder` remains the single Protocol V2 wire encoder.

ViewAsSkillV2 construction no longer invents `CARD_USE_REASON_PLAY`. The build
request consumes the current `ClientRoomContext` reason and pattern, so a
response, response-use, named skill prompt and play-phase request reach
`canActivate`, `canSelectCard`, `cardSelectionFeasible` and `createCard` with the
same context the native client is currently answering.

Set `ClientRoomContext::setCardUseContext()` before each selection query; there
is no separate per-request override that could disagree with legacy callbacks
reading the engine context. Ordered selections reject duplicate physical IDs,
and zero-card skills reject supplied subcards. V2 activation is checked again
with the completed selection before construction. Server-named legacy prompts
retain the desktop's borrowed-skill behaviour without changing player marks.

Legacy ViewAs subcards also resolve through `Engine::getCard()` rather than the
printed engine-card table. That means an `UPDATE_CARD`/WrappedCard change seen by
the client remains visible during selection instead of silently reverting to
the card's original catalog face.

The result type contains a transient native `Card*` only for native callers that
must finish rule evaluation in the same event handler. A JS/WASM binding must
never export that pointer; it copies the canonical card text and structured
selection result before crossing the boundary.

The shared API is also called directly by `tui-play-skills` (borrowed prompts,
wrapped-card filtering, V2 context and ordered selection, target evaluation,
response encoding). No automated suite establishes the production browser
session's acceptance.

## Persistent Web runtime

`qsanguosha_client_wasm` links
the existing `qsanguosha_client_runtime`, whole engine/package registrations,
`InteractionReplyEncoder`, and the production `ClientRulesSession`/WASM entry
sources. Native product source inventories are unchanged by this opt-in product.

The WASM entry keeps one QCoreApplication/engine alive across requests.
`ClientRulesSession` creates a fresh projected scene from the client-visible
snapshot for each selection query, so removed properties and stale wrapped
cards cannot leak between snapshots. C++ owns physical-card,
ViewAs and target evaluation, then copies JSON results across the boundary;
Card/Player pointers remain inside the module. Decimal request IDs remain
strings. Browser request identity, state revision and selection revision prevent
late results from confirming a newer prompt or selection.

The dedicated Worker loads one module per session and verifies the build
deployment bundle (`.bundle.json`: schema, bridge schema and per-file SHA-256
pairing of the `.mjs` and `.wasm`) before initializing the engine. Its
module URL is fixed to the application origin, not supplied by game packets.
The TypeScript/DOM frontend continues to own Protocol V2 transport and display.
Native confirmation uses the existing C++ reply encoder's payload.

The deployed content profile is `declared-v2`: the embedded rules identity and
`rules-content-manifest` cover only the declared bootstrap/translation and
declared extension content; they do not copy ignored extensions, AI, local
configuration or the checkout into MEMFS. The Web side never supplies a
package or content manifest — the identity is exported by C++. A matching
bundle does not prove arbitrary server
extension compatibility or a complete ruleset/ABI agreement. Unknown or
unsupported content must not enable confirmation through guessed TS rules.

## Production build and packaging

The existing baseline is Qt **6.11.1**, Emscripten **4.0.7**, the single-thread
WASM Qt kit and a matching native Qt host-tool installation. This uses Qt Core
and Network in a Worker; it does not ship a Qt Widgets/Quick browser UI. The
root dependency graph also finds Qt WebSockets. Configure and build are explicit
follow-up commands, not steps executed for this source implementation:

```sh
source "$EMSDK/emsdk_env.sh"
cmake -S . -B build/web-wasm -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$QT_WASM/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQT_HOST_PATH="$QT_NATIVE" \
  -DQSAN_BUILD_GUI=OFF -DQSAN_BUILD_TUI=OFF \
  -DQSAN_BUILD_SERVER=OFF -DQSAN_BUILD_WASM_WEB_CLIENT=ON
cmake --build build/web-wasm --target qsanguosha_client_wasm
```

`QSAN_BUILD_WASM_WEB_CLIENT` defaults OFF. Native products must be OFF for that
cross build. The production session
sources are compiled only by the production WASM target.

Generated artifacts under `build/web-wasm/web-wasm/RelWithDebInfo/`:

| Artifact | Contract |
|---|---|
| `qsanguosha_client_wasm.mjs` | ES module factory `createQSanguoshaClient`, Worker environment |
| `qsanguosha_client_wasm.wasm` | Persistent C++/Lua runtime |
| `qsanguosha_client_wasm.bundle.json` | Deployment pairing manifest (schema/bridge schema/SHA-256 of the two files above); generated by the build, re-verified by the packager and the Worker |

Exports are `_qsan_client_bridge_schema` and `_qsan_client_code_identity`
(identity probes), `_qsan_client_initialize`, `_qsan_client_stream` and
`_qsan_client_shutdown`; `_qsan_client_evaluate` is still compiled but the
Worker requires the stream entry and C++ rejects the file-based snapshot query
with `stream_snapshot_api_disabled` in stream mode. Emscripten exposes `FS`
and `ENV` to the host. The module
has no `main` entry, permits memory growth, starts with 128 MiB memory and an
8 MiB stack, and preserves the WASM exception mode. These inherited sizes
are configuration, not browser memory/performance acceptance.

| Export | MEMFS/JSON contract |
|---|---|
| `_qsan_client_initialize` | Initializes once and writes `/work/init.json`: bridge schema 2, native `rules_bundle`, `card_count`, and numeric-ID registry entries with object name, integer suit, number, class and package |
| `_qsan_client_stream` | Reads `/work/stream.json` (at most 4 MiB; `schema_version`, `action`, `generation`, plus action fields) and writes `/work/stream-result.json`; the actions are listed in [Stream ABI](#stream-abi) |
| `_qsan_client_shutdown` | Ends the engine lifetime; the closed module cannot initialize again |

The host must establish isolated MEMFS configuration in `preRun`, then verify
embedded assets after the Emscripten factory resolves and before initialization.
Queries run serially. The main-thread controller discards stale generation or
revision results. Failure disposes the Worker; explicit rule reload or a new
connection creates a fresh Worker.

## Stream ABI

`_qsan_client_stream` is the browser Worker's entry point. Initialize the host
first, write one JSON object to `/work/stream.json`, invoke the export, and read
`/work/stream-result.json`. Both paths are fixed by the host and are not a
caller-selectable filesystem interface.

Every operation carries exactly `schema_version: 1`, `action` and `generation`,
plus the action's own fields. Schema and generation/revision numbers must be
integers, request IDs stay canonical decimal strings, and unknown fields are
rejected.

| Action | Additional fields | Meaning |
|---|---|---|
| `reset` | none | Bind the initialized Engine's W2 identity and start a strictly newer connection generation |
| `frame` | `direction`, `frame` | Observe an `incoming` or `outgoing` raw UTF-8 protocol frame through the existing native decoder |
| `query` | `revision`, `request_id`, `selection` | Evaluate the current request against committed native state |
| `view` | none | Read committed state only, never pending STATE_SYNC state |

The result carries `success`, `reason` and `status`; `status` reports the
generation, revision, active/failed/synchronizing flags, the current request ID
and the W2 bundle ID. A successful query also carries the production
`evaluation` result. A nonzero ABI status is a host I/O, JSON or lifetime
failure, not a game-rule answer: semantic rejections return zero with
`success: false`, and an obsolete generation can neither mutate nor poison a
newer stream. Output is replaced atomically.

Once a host opts into `reset`, the old external-snapshot `evaluate` entry rejects
with `stream_snapshot_api_disabled` for that Engine's life. Runtime shutdown is
terminal.

The stream reuses the production `ProtocolCodecRouter`,
`ClientSessionController`, `ClientGameStateReducer`,
`ProtocolInteractionRequestBuilder` and `ClientRulesSession` reply encoder; no
card-name rules are copied into JavaScript. Incoming and outgoing frame IDs
increase independently, and every accepted frame advances the revision. A query
must match the exact current generation, revision and request. `STATE_SYNC` begin
preserves the previously committed view and cancels the old selection; the
matching end commits once, while an overlap, an unmatched end or an interaction
arriving during sync fails the stream. Observing an already sent reply verifies
correlation, not server game legality.

Builds do not modify `web/public`. To package already-built artifacts:

```sh
python3 tools/package-web-runtime.py \
  --module build/web-wasm/web-wasm/RelWithDebInfo/qsanguosha_client_wasm.mjs \
  --destination web/public/rules
```

Alternatively, `cmake --build build/web-wasm --target package-web-runtime` first
builds the runtime dependency, then runs the same packaging command. The tool
requires the three artifacts (`.mjs`, `.wasm` and the build-generated
`.bundle.json`), checks the WASM header and the bundle's SHA-256 pairing, and
publishes only those fixed generated names. It does not execute the module
or provide runtime acceptance. The Worker verifies the bundle and embedded
asset bytes when the application starts.

Package before the Web frontend's normal Vite build so `public/rules` is copied
into `dist/rules`. Deploy all three artifacts together at `/rules/`, serve
`.mjs` as JavaScript and `.wasm` as `application/wasm`, and configure the server's
SPA fallback after the static `/rules/` route. Missing artifacts must return a
visible runtime failure rather than an HTML application shell masquerading as
the module. Use HTTPS or localhost for the Worker's Web Crypto asset checks.

## Initialization diagnostics and dependencies

2026-10-03 的 Web 10P 初始化問題已解決，並完成實際 Web 托管局。
操作與未解問題見 [Web 10P 重用流程](web-client.md#web-10p-reuse-and-troubleshooting)，
詳細證據見本機 `builds/web-10p-20261003/native-diagnostics/` 和 `game-02/`。

### Read the first concrete failure

`web/src/rules-worker.ts` 記錄 module 準備、部署核對、module 初始化、
native code identity、server identity、內容下載／安裝／核對及 native client
初始化等階段。Emscripten 可拋出一般物件，不能只以 `String(error)` 轉換；
錯誤回覆保留 name、message、errno、code，錯誤文字最多 8192 字元。
既有原生日誌維持有界保存（總字串長度最多 16384、單行最多 2048，
均按 JavaScript 字串 length 計算），由
`rules-client.ts` 將日誌串成文字輸出到 console。

`src/client/runtime/client-rules-wasm.cpp` 的 `_qsan_client_initialize()`
安裝 Qt message handler，把 `qFormatLogMessage` 結果寫入並 flush stderr，
使 Worker 的 Module.printErr 能保留原生 exit 前的首個原因；ABI 未更動。
只看 `ExitStatus: Program terminated with exit(1)` 仍不足以判定根因。

本次具體錯誤是 `cannot open lua/ai/mode-ai.lua: No such file or directory`。
呼叫來源為 declared extension `extensions/addFunction.lua` 的 createMode，
並非看到 `lua/sanguosha.lua` 錯誤前綴就能認定該檔直接載入 AI。
原本僅註冊模式 metadata 的呼叫也無條件 dofile server AI 模組；
Client 的 declared-v2 內容刻意排除 AI，故初始化退出。
現有修復只在 `spec.ai ~= nil` 或 `spec.teams ~= nil` 需要註冊政策，且
VM 尚未有 `sgs.registerModeAI` 時才載入 mode-ai。metadata-only 模式
可在 Client 初始化；真正 AI 政策仍保留載入與註冊流程。
不要用擴大 Client 內容清單、假 AI API 或再建一套 bootstrap 避開錯誤。
server 自身仍須有其 AI 依賴，包含隔離部署另外補入的 middleclass。

### Reuse the Windows build cache

本局實際增量目錄為 `builds/repository-review-wasm`，配置為 RelWithDebInfo、
Ninja、Qt 6.11.1 `wasm_singlethread`、Emscripten 4.0.7；host tools 使用
同版本 `msvc2022_64`。下列命令是既有目錄的續作入口，僅在受影響產物
需要更新且當輪已授權建置檢查點時執行；產物符合時可直接沿用。

```powershell
# Keep the configured Ninja cache; emsdk_env.bat must affect the build process.
cmd /d /c 'call H:\Qt6111\Tools\emsdk\emsdk_env.bat >nul && "H:\Program file\visualstudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build builds/repository-review-wasm --target qsanguosha_client_wasm --parallel 8'

# Package this output pair before Vite embeds the deployment bundle ID.
python tools/package-web-runtime.py `
  --module builds/repository-review-wasm/web-wasm/RelWithDebInfo/qsanguosha_client_wasm.mjs `
  --destination web/public/rules
Push-Location web
npm run build
Pop-Location
```

本機 Node 位於 `H:\Program file\nodejs`、Python 為 `C:\Python311\python.exe`；
需要時使用完整路徑或只對子程序配置 PATH。Emsdk 環境入口需在 kit 目錄
生成設定檔，MSBuild FileTracker 及 loopback 監聽也曾受工具沙箱限制；
應按實際權限錯誤處理執行環境，不能把它當成 native／WASM 程式缺陷。

首次 WASM 全量建置較長，增量連結後 wasm-opt 仍可能運算數分鐘；
保留正在工作的建置程序及首個失敗日誌，不因暫時沒有輸出就重新 configure
或重開相同建置。需要更新 native server 時，同步使用專案的 Debug 增量
建置入口；若改動影響 rules code identity，server／WASM 必須重新對齊後
再部署。此處不宣告任意 dirty source 或不同 kit 的 ABI 相容性。

## Structured interaction contract

`ClientRulesIngress` already builds the shared ClientCore `InteractionRequest`
through `ProtocolInteractionRequestBuilder`. `prepareQuery` now forwards that
built request as `interaction` beside the raw `command`/`payload`, and
`ClientRulesSession` consumes it. Enumerated prompts therefore take their
selectable set, counts and reply shape from one implementation rather than a
second reading of the wire payload in the session, and the browser renders from
the same structured object it is echoed in `evaluate`'s result.

`S_COMMAND_SKILL_GUANXING`, `S_COMMAND_SKILL_GONGXIN` and `S_COMMAND_SKILL_YIJI`
are answered on that enumerated path. The session validates the draft against
the typed payload — a rearrangement must partition the whole set inside its
top/bottom bounds, gongxin names exactly one selectable card, yiji stays inside
`min_cards`/`max_cards` and names one offered recipient — and then encodes the
reply with `InteractionCommandRegistry`'s own encoder for that command. No skill
effect is evaluated: those prompts resolve on the Room side.

`guhuo`, `juguan` and `tiansuan` declarations keep their existing native
enumeration — each candidate is probed through `applyDeclaration`, so the
offered list is the one the desktop dialog would allow rather than any string
that happens to clone a card. The evaluation also carries the skill's
`SkillDialogInfo` as `declaration_dialog`, so a shell implements the three
dialog shapes once instead of one branch per general.

The card-use path additionally reports, per ViewAs candidate, the declared
subcard amount (`ViewAsSkillV2::getN`, or the zero/one-card base classes),
committed usage read from the projected limit-scope mark, instance
invalidation, `isResponseOrUse` and the expand pile. These are display and
sizing hints; `canActivate`, `canSelectCard` and `cardSelectionFeasible` remain
the only legality decisions. Selectable and selected card ids are also reported
with the zone they occupy — hand, equip, hand pile, expand pile or a sibling
player's pile.

### Preview purity

A query is a preview, never a move:

- The projected `Scene` (state, room context, players) is constructed and
  destroyed per query, so declaration tags, marks and history changes cannot
  reach the next query or the committed client state. A `guhuo`/`juguan`
  probe's tag is removed before the player's actual choice is applied.
- Usage is only read. Nothing calls `Skill::addUsage`, so opening a skill,
  enumerating its declarations or previewing a card never spends a use.
- `evaluate` binds a throwaway `GameRng` for its whole body, so a rule callback
  that draws randomness cannot advance the process-wide fallback stream that a
  later query would observe.

## Scope boundary

Arbitrary extension loading and complete server/runtime ruleset negotiation
remain outside the `declared-v2` manifest profile; no claim of
arbitrary-extension parity follows from this integration. The server remains
authoritative for every reply.

The Web UI should not grow new hard-coded weapon, target, or extension tables
while this migration is in progress.

## W2 rules identity and the W3b streaming cutover

The follow-up contract is defined in [rules-bundle-identity.md](rules-bundle-identity.md).
Initialization uses bridge schema 2 with the shared native `rules_bundle`.
Package the `.bundle.json` deployment manifest before building the Web
frontend. WebSocket signup requires this identity, including reconnect; legacy
TCP clients may still omit it.

Since the W3b cutover onto the
[Stream ABI](#stream-abi) the
Worker drives the runtime through `_qsan_client_stream` instead of the
per-query `request.json`/`result.json` evaluate files; the recipe above
describes the build/packaging layout, while the PR31-era `.assets.json`
manifest and the standalone file-based evaluate no longer exist on the
production path.
