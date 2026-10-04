# External agents and no-clock rooms

This opt-in native hosting interface uses the existing `AIRequest`, `AIResult`,
viewer projection and authoritative rules. Ordinary human and SmartAI seats keep
their existing routes. No provider, credentials, HTTP client or model call is
included. The adapter owns its transport, authentication and call/cost budgets.

## Host integration

Create a robot seat and attach it before signing up the last seat (signup may
start a full room automatically):

```cpp
room->setNoClock(true); // applies to human prompts too
auto *player = room->addAIPlayer();
const auto seat = room->attachExternalAgent(player, ExternalAgentEndpoint::Pause);
// Keep this capability only in the adapter authorized for this seat.
room->signup(player, "External", "", true);
```

`attachExternalAgent` rejects a foreign player, a human seat, a duplicate
attachment, or a room whose workers have started. Dedicated hegemony, 1v1,
3v3 and XMode drafting workers are rejected in this increment; their draft
protocols and cancellation paths need separate integration tests. Other seats may be humans or
SmartAI robots. The adapter can run in a separate native thread or a Qt timer on
the host event loop. It obtains a copied request with `seat->pending(request)`
and queues a copied result with `seat->submit(result, &error)`. A transport adapter
may use the bundled local JSON-lines transport below or serialize those value
types to another wire format. Never expose the Room object or a registry of all
seats to an untrusted adapter.

No adapter callback executes on the room worker. The worker sleeps on a condition
variable, leaving the server event loop and other rooms available. `pending`,
`submit`, connection changes, cancellation and status calls use short mutex
sections. `submit` means **queued for validation**, not that the action was played.
An invalid action reopens the same request with `lastError == "illegal-action"`.
Only the room worker may accept a legal response and continue gameplay.

The adapter must echo `decisionId` and `stateRevision` unchanged. Stale, duplicate,
unknown, oversized and malformed replies are rejected. Once a request is
consumed it cannot be submitted again. Values, candidate membership, count and
uniqueness constraints are checked before entering the room. Play/response
ownership, pattern, card limits, ordered targets, prohibitions and skill quotas
are checked by the authority. Legacy card strings cannot cross this boundary.
Unprojected/custom conversions must use an authorized structured action; an
unsupported answer is not silently delegated to SmartAI.

## Visibility

Requests are built for the attached viewer on the authoritative worker. They
contain that viewer's hand and permitted state, public board facts, explicitly
revealed prompt candidates, and the existing bounded viewer-filtered event
journal. Opponent hidden hand identities, hidden roles, closed piles, private
flags/skill state and deck order are excluded. Each endpoint stores at most one
request and one reply and has no operation for selecting another seat.
`AIWorldView` remains the source of truth for visibility. Do not substitute a
server replay, raw Room tags, another agent's memory or Lua userdata.

## Waiting and lifecycle

External decisions have no clock. `status()` reports `waiting-no-clock`,
`validating`, `idle`, `paused-disconnected`, `smart-ai-fallback`, or `cancelled`.
`Room::setNoClock(true)` also makes normal interactive/race waits indefinite and
broadcasts the existing no-limit countdown representation. Global
`OperationNoLimit` now actually waits indefinitely instead of using 600000 ms.
Finite-clock rooms retain the existing timeout calculation.

The host must call `disconnect()` when its adapter transport is lost. Policy is
chosen explicitly at attachment:

- `Pause`: retain the request. `reconnect()` exposes the same decision/revision;
  no action and no takeover occur while disconnected.
- `SmartAIFallback`: release the current wait and log explicit fallback to the
  seat's configured native AI (SmartAI after normal game initialization).
  Reconnect affects future decisions; it never replays a consumed prompt.

`cancel()` cancels the session on the next decision wake, rather than inventing a
pass or a mandatory answer. Room shutdown also cancels every endpoint and wakes
native no-clock waits. The host should then perform the ordinary room teardown.
If authoritative state changed while a decision was pending, the old reply is
rejected as `stale-world` and the session is cancelled; automatic reconstruction
of arbitrary nested prompts is not implemented. Reconnection is in-process only.
**Cross-server-restart persistence and durable game saves are deferred.**

## Coverage and mock

Coordinator decisions include play/use-card, choices, general selection,
invocation/order, discard/exchange, card/player selection, Guanxing, Yiji and
response cards. Jink, Peach and Nullification are response decisions, including
nested response prompts. The established human-first Nullification ordering is
preserved. External seats do not participate as fake network clients.

The host interface does not add a strategy for arbitrary extension actions.
For example, non-enumerable hidden-card selection and incomplete skill-conversion
projections may require additional value contracts before an adapter can answer
those prompts. Such unsupported requests remain pending; cancellation and the
explicit disconnect policy remain available. Audit custom extension call sites
that bypass `AiDecisionCoordinator` before claiming full coverage of that package.

`mockExternalAgentAnswer` is deterministic and value-only. It passes play and
optional prompts and picks the first offered mandatory choices. It deliberately
has no competitive strategy or fallback. `tests/external-agent-test.cpp` polls it
from a timer while a two-player room runs with a SmartAI opponent.

```sh
cmake -S . -B build -DQSAN_BUILD_GUI=OFF -DQSAN_TEST_EXTERNAL_AGENTS=ON
cmake --build build --parallel 4
ctest --test-dir build -R external_agent --output-on-failure
```

CTest stages an isolated runtime from this engine and the sibling `extensions`
checkout; override `QSAN_EXTENSIONS_SOURCE_DIR` if it lives elsewhere. The fixture
keeps the native package bundle, includes `addFunction`, and excludes optional Lua
expansion packages. It uses an explicit sufficient native general pool. Tests
cover seat visibility, bounded/validated replies,
asynchronous waiting, native no-clock waits, cancellation, disconnect/reconnect,
explicit fallback and a complete game with cleanup. They do not establish
provider integration, every extension's strategy coverage, or restart recovery.

## Separate-process local transport (version 1)

`ExternalAgentLocalTransport` wraps exactly one endpoint on the host event-loop
thread. `listen()` binds only IPv4 `127.0.0.1`, with an OS-assigned port; there is
no bind-address override. It accepts one client at a time. Its `bootstrap()`
contains `version`, `host`, `port`, and a random 256-bit seat capability. Pass
this JSON through a private parent/child pipe. The capability lives only in
memory for that transport instance: do not save it, put it in command arguments,
log it, or publish/forward the port. No persistent credential setup is required.
A second seat needs a separate transport/capability; there is no seat selector.

The runnable `qsanguosha_external_agent_host` starts one external seat (Pause
policy) and one real companion SmartAI in a two-player native-role room. Its only
bootstrap line on stdout has the prefix `QSAN_AGENT_BOOTSTRAP `. The supplied
Python client reads that line through a subprocess pipe. The two processes share
no engine pointers, Lua state, or decision callbacks.

```sh
cmake -S . -B build -DQSAN_BUILD_GUI=OFF -DQSAN_BUILD_EXTERNAL_AGENT_HOST=ON
cmake --build build --parallel 4
python3 tools/autotest/stage_external_agent_runtime.py \
  --engine . --extensions ../extensions --output build/agent-runtime
QSAN_ASSET_ROOT="$PWD/build/agent-runtime" \
QSAN_USER_DATA_ROOT="$PWD/build/agent-runtime" \
python3 tools/autotest/external_agent_mock.py \
  --host "$PWD/build/qsanguosha_external_agent_host" --exercise
```

A custom local adapter can replace the Python client. Each TCP frame is one UTF-8
JSON object followed by LF. First send:

```json
{"op":"hello","version":1,"token":"<capability from the private bootstrap>"}
```

Only a successful `{"ok":true,"version":1}` admits further operations. Invalid
capabilities receive `unauthorized` and the socket closes without observations.
The ten-second unauthenticated handshake limit is a transport limit; authenticated
game decisions have no deadline.

- `{"op":"poll"}` returns `status`, `lastError`, and `request` when waiting.
  `request` is the existing projected `AIRequest` DTO, with its camelCase field
  names, `worldView`, prompt context, choices, card candidates, conversions and
  skill actions. All 64-bit decision/revision/event sequence values are decimal
  **strings**, so JavaScript clients do not lose precision. Decision `kind` uses
  the `AIRequest::DecisionKind` integer values in `src/server/ai.h` (0 play,
  1 use-card, 13 response-card, 14 Guanxing).
- `{"op":"submit","result":{...}}` sends a reply. The result contains
  `decisionId`, `stateRevision`, `kind` (`pass`, `answer` or `useCard`) and `action`.
  Action fields are `candidateId`, `useCardId`, `selectedCardIds`, `bottomCardIds`,
  `selectedTargetNames`, `userString`, `cardSpec` and `skillActionContext`; omit
  unused fields. `cardSpec` contains `name`, `skillName`, `suit`, `number`,
  `conversionId`, `subcardIds`. `skillActionContext` contains `activationRef` and
  `sourceRef`; each ref has `ownerObjectName`, `skillName`, `instanceID`.
  Quota booleans, native pointers and legacy card strings are not accepted.
- `{"op":"cancel"}` cancels the session. The ordinary host teardown must finish.
  The example host then reports `{"status":"finished","winner":""}` to polls.
  A completed game reports the nonempty winner instead.

For example, an optional refusal is:

```json
{"op":"submit","result":{"decisionId":"9","stateRevision":"17","kind":"pass","action":{}}}
```

`ok:true, queued:true` acknowledges queuing, not completed gameplay validation.
Keep polling: illegal actions reopen the same prompt with `lastError`, while a
consumed decision is never replayed. Stale or duplicate submissions return
`ok:false` (`stale`, `duplicate`, or `not-waiting`, depending on lifecycle stage).
Closing the socket applies the endpoint's configured disconnect policy. A new
socket with the same in-memory capability resumes the same pending decision under
Pause; a finished decision cannot be resumed. No implicit fallback is introduced.

Incoming frames are limited to 256 KiB and outgoing/backlogged bytes to 8 MiB;
oversized/blocked connections close and apply the explicit disconnect policy.
Requests are value copies, and only the existing viewer projection is serialized.
Malformed numeric types, unknown fields/operations and ambiguous card sources
are rejected before endpoint validation. The server uses asynchronous Qt socket
signals with bounded work per event-loop turn.

`--exercise` makes the separate mock client test wrong-capability rejection,
seat-selection rejection, disconnect/reconnect with an identical pending snapshot,
stale IDs/revisions, malformed replies and duplicate submissions before finishing
the game. `--cancel` exercises cancellation from the separate client. CTest runs
both and checks zero callback errors, no fallback, distinct process IDs and clean
worker/room teardown. The mock's 120-second watchdog is a test/client budget, not
an engine game timer. Durable server-restart persistence remains deferred.

## Lua compatibility regression

Use the engine-linked `qsanguosha_external_agent_tests --lua-parse FILE...` for
this fork's dialect. The engine embeds modified Lua 5.4.8 and deliberately accepts
unreachable statements after `return` in `src/lua/lparser.c::statlist`, as well as
other dialect extensions. A stock/setup Lua 5.2.4 parser incorrectly reports the
consecutive returns in the base `sgs10th.lua:602–603` (603–604 with the history-key
repair) as an engine syntax failure.

The separate SWIG compatibility repair exposes the already-existing
`CorrectSkillResult::noEffect()` and `useAmount(int)` factories. The focused
`external_agent_lua_corrections` test executes the actual `ny_10th_jieling_target`
callback extracted from the companion source and checks both matching/nonmatching
Residue and DistanceLimit branches with the real bound return objects.
`external_agent_prior_repairs` additionally checks payload-sharing ON/OFF typed
semantics and snapshot isolation, 31+23 companion history keys, and the actual
`ov_enyuan` input-handling callback.
