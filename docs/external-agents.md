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
has no competitive strategy or fallback.

## Separate-process local transport (version 1)

`ExternalAgentLocalTransport` wraps exactly one endpoint on the host event-loop
thread. `listen()` binds only IPv4 `127.0.0.1`, with an OS-assigned port; there is
no bind-address override. It accepts one client at a time. Its `bootstrap()`
contains `version`, `host`, `port`, and a random 256-bit seat capability. Pass
this JSON through a private parent/child pipe. The capability lives only in
memory for that transport instance: do not save it, put it in command arguments,
log it, or publish/forward the port. No persistent credential setup is required.
A second seat needs a separate transport/capability; there is no seat selector.

The runnable `qsanguosha_external_agent_host` opens a native-role room with
`--mode <id>` (default `02p`; the registry decides the player count), 
`--external-seats <n>` (default 1; the first `n` seats are external with the
Pause policy, the rest are companion SmartAI robots) and `--seed <u32>`
(default 20261003). Dedicated hegemony, 1v1, 3v3 and XMode drafting workers
reject attachment, so those modes fail fast with `AGENT attach-failed`. Every
external seat prints its own `QSAN_AGENT_BOOTSTRAP ` line on stdout, each
carrying that seat's `objectName`; single-seat clients may keep reading only
the first line. The supplied Python client reads one line through a
subprocess pipe. The two processes share no engine pointers, Lua state, or
decision callbacks.

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

### Answering against the declared candidate space

Each `cardCandidates` entry declares the complete legal action space for one
card; answering inside it is the client's responsibility.

- `targetCombinations` lists the exact legal target sequences.
  `targetFixed: true` with `targetCombinations: [[]]` means the card takes no
  player selection: submit `kind:"useCard"` with `useCardId` only and omit
  `selectedTargetNames` — global and AOE effects derive their targets at
  resolution (an empty target list resolves to all other players). Attaching
  names to a target-fixed candidate, or any sequence outside
  `targetCombinations`, is an illegal action even though the card itself was
  offered.
- `choiceOptions.cardIds`/`cards`, `cardCandidates` and `cardConversions` are
  the only authorized card sources; a physical ID not offered cannot be
  submitted. A candidate marked `limited: true` is unusable for this request
  regardless of `available`.
- Rejections surface in two shapes. Shape and decision-gate rejections reopen
  the same prompt with `lastError` (`illegal-action`). A submission that
  passes the decision gate but fails use-time validation reopens as a fresh
  `decisionId` with no `lastError` and no event: after an accepted
  submission, treat a same-shape follow-up request as a rejection signal and
  re-read the declared space instead of resubmitting the same answer.
  Consecutive legitimate asks of the same shape are distinguished by an
  advancing `stateRevision`; a true use-time rejection leaves it unchanged.
- Skill-driven view-as asks (`skillActions` non-empty) are answered with
  `kind:"useCard"` whose `skillActionContext` mirrors the offered
  `activationRef`/`sourceRef`, whose `selectedCardIds` carry the skill's own
  material — for the wind `shensu` play-phase ask (`@@shensu2`,
  `Card::MethodDiscard`): exactly one equip card from the hand — and whose
  `selectedTargetNames` satisfy the skill's target filter. A pass on such an
  ask bounces at room level even though the endpoint queues it. When the
  material does not exist (no equip in hand), a skill ask that offers no
  empty selection would have no expressible answer and loop with fresh
  decisionIds at an unchanged stateRevision — the engine now avoids this by
  not raising the ask at all (`Shensu::effect` skips the play-phase ask when
  no equip material exists, matching the skill's own `canSelectCard` rule;
  a generic external-Pass→decline mapping in the coordinator remains future
  work).

### Autonomous answering

An external seat answers from its own judgment over the projected request
DTO. Rule knowledge — skill text, card effects, targeting and resolution
contracts — is legitimate preparation and may be read from the source tree.
Peeking at the engine's own decision makers is not: an external client must
not read the SmartAI Lua policies (`sgs.ai_skill_*` and the rest of
`lua/ai/`), the isolated AI layer, or any other in-engine decision route.
Those sources belong to engine-side seats, and an agent that has seen them
is no longer an independent decision maker — audit evidence claiming
`external_agent` provenance becomes misleading. The bundled deterministic
`mockExternalAgentAnswer` is a value-shape reference only and carries no
strategy.

Incoming frames are limited to 256 KiB and outgoing/backlogged bytes to 8 MiB;
oversized/blocked connections close and apply the explicit disconnect policy.
Requests are value copies, and only the existing viewer projection is serialized.
Malformed numeric types, unknown fields/operations and ambiguous card sources
are rejected before endpoint validation. The server uses asynchronous Qt socket
signals with bounded work per event-loop turn.

`--exercise` makes the separate mock client test wrong-capability rejection,
seat-selection rejection, disconnect/reconnect with an identical pending snapshot,
stale IDs/revisions, malformed replies and duplicate submissions before finishing
the game. `--cancel` exercises cancellation from the separate client. The mock's
120-second watchdog is a test/client budget, not an engine game timer. Durable server-restart persistence remains deferred.

### Hybrid routing driver (`tools/autotest/external_agent_driver.py`)

A file-queue bridge that layers five consumption optimizations onto the v1
transport: conservative mechanical local answering, per-seat incremental
worldview briefs, stable-prefix rendering, a battle-notes file protocol, and
escalation routing. The driver never calls any LLM API; the brain stays on the
other side of the file queue.

- CLI: `--mode/--seed/--external-seats` pass through to the host.
  `--fallback mock|file` picks the resolver for escalated questions — the
  deterministic probe-equivalent answer, or `answer-<n>.json` written by the
  LLM session next to the brief (consumed and deleted on read).
  `--escalate-kinds all|0,1,...` restricts which decision kinds are routed to
  the brief queue (unlisted kinds fall back to mock). `--auto-log`,
  `--brief-dir`, `--notes` and `--max-seconds` (default 1800) relocate
  artifacts and bound the run; reopens of one decisionId are capped at 4.
- Mechanical adjudicator: answers locally only when the declared action space
  has exactly one element (single choice, exact discard count, single card /
  target, opaque random-card domain at index 1, forced pick-all sequences,
  zero-usable play phases) or an optional prompt has zero usable actions
  (`playerNames` on response kinds is metadata, e.g. the dying-peach target
  list, not part of the action space). Asks whose `skillActions` are
  non-empty are never locally passed or answered — their space is the skill
  activation and a blind pass can bounce at use-time. Every local answer
  passes a contract
  self-check before submission — including the candidateId↔useCardId pairing
  and membership of target sequences in the declared `targetCombinations`;
  for skill asks the material/target declaration checks are waived because
  the skill logic owns those spaces. A signature that is
  gate-rejected, or silently re-asked after an accepted submission (fresh
  decisionId, same declared space, no new events, unchanged stateRevision),
  is poisoned permanently
  and audited as `auto_rejected`.
- Every decision appends one line to `auto-answers.jsonl` (route
  `local|escalated`, basis/reason, resolver `mock|llm`, self-check result and
  the submitted answer), so a full run stays auditable per decision.
- Escalated decisions write `<brief-dir>/<seat>/pending-<n>.json` (full DTO,
  authority for legality checks and deep reads) and `decision-<n>.md` (the
  brief the LLM reads). Brief sections are fixed — 契約備忘 → 座位背景 → 筆記
  → 本題 → 局面增量 → 尾註 — with byte-identical template text, JSON key
  order preserved as parsed, and volatile content (decision serial,
  timestamps) confined to the tail, maximizing the stable prefix that
  provider prompt caches hit. Escalation reason (`optional`／`space>1`／
  `kind:<n>`／`auto_rejected`／`reopen`) is carried in the brief title line
  for upstream routing.
- Briefs carry per-seat cursors (`last_revision`, `last_event_sequence`):
  the seat background is reprinted only after a change, and the increment
  lists events with `sequence` above the cursor plus hp/hand/judging deltas,
  so repeated decisions stay cheap. Inside the increment, nested card
  objects are deduplicated into a per-brief card table and referenced by
  `cardRef`, and zero-information maintenance events (`kind:"other"`,
  amount 0, no card face, single-player details) are folded into one count
  line listing their trigger codes — the pending DTO stays the byte-exact
  authority for anything elided. The same card table also covers nested
  card objects inside `choiceOptions.context`, whose `original:*` mirror
  domains of `random_card_choices` are dropped (base-domain tokens are the
  answerable ones); `choiceOptions.cards` and `cardCandidates` stay full —
  they are the declared answer space. The fixed 契約備忘 template carries a
  triggerEvent→semantics legend (values from the engine's `TriggerEvent`
  enum) so numeric event codes are self-explanatory. Prompts, skill names and
  card names are annotated from the runtime's own `lang/zh_CN` Lua tables
  (verbatim simplified-Chinese text, deterministic per key): each brief lists
  a 譯名對照 block for the ask's prompt/pattern/reason/skill keys and the
  card table, and the seat background names the viewer's skills — the DTO
  carries raw keys only, so this is what makes custom-skill asks
  self-explanatory. Composite prompts render engine-style: the translation
  key is the part before `:` and its `%src`/`%dest`/`%arg` placeholders are
  substituted in template appearance order with the colon-args mapped to
  player display names (general names translated via the same tables, matching
  the client's getPlayerName behaviour); choice options translate via exact
  `skill:option` keys only. The seat background
  also lists the viewer's own hand card names.

- Replay and multi-seat options: `--replay <auto-answers.jsonl>` submits a
  prior run's recorded answers verbatim by decisionId (audited as route
  `replay`; misses fall through to the normal flow) and compares the
  recorded winner at game over — an exact-reproduction smoke for driver
  changes. `--replay-scope llm` replays only escalated answers and lets the
  current adjudicator handle the rest, regression-testing it on a live
  stream. `--shared-cursor` (multi-seat + single LLM session) broadcasts the
  event cursor across seats so later seats skip events an earlier brief
  already showed; the benefit tracks decision interleave density because
  the bounded event journal caps how far back a cursor can reach.
- `game/notes.md` is maintained by the LLM session between decisions
  (strategy, inference, seat states); the driver only cites the path and
  quotes an excerpt, never writes it.

Offline cache-hit measurement for the brief stream (the driver itself never
calls an LLM API): `tools/autotest/cache_hit_sim.py --brief-dir <run>/briefs`
models provider caching rules (minimum cacheable prefix, block granularity,
cache-read/write multipliers — all flags) under the accumulating-session and
fresh-call topologies, verifies the stream qualifies for caching (byte-stable
template, append-only), and reports hit rates plus cost ratios against the
full-DTO counterfactual from the pending DTOs.

## Lua compatibility regression

The engine embeds modified Lua 5.4.8 and deliberately accepts
unreachable statements after `return` in `src/lua/lparser.c::statlist`, as well as
other dialect extensions. A stock/setup Lua 5.2.4 parser incorrectly reports the
consecutive returns in the base `sgs10th.lua:602–603` (603–604 with the history-key
repair) as an engine syntax failure.

The separate SWIG compatibility repair exposes the already-existing
`CorrectSkillResult::noEffect()` and `useAmount(int)` factories.
