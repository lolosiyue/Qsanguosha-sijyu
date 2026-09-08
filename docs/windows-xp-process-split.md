# XP GUI / server process boundary

Status: implementation in progress. Baseline: `debug@d948f4786e67e752b0a5264bdfe818a964cd7498`.
Existing TUI edits are outside this change. No runtime or memory acceptance is implied.

## Entry inventory

| Entry | Existing behavior | Split behavior | Required check |
|---|---|---|---|
| ServerDialog, join locally | MainWindow constructs Server | OwnedHost helper, ready then native TCP signup | owner, AI, selected settings, port conflict |
| ServerDialog, server only | Server::daemonize + StartScene(Server*) | OwnedHost, management events and discovery in helper | management, another client, no second helper |
| startLocalConsoleGame | deferred room worker in GUI | OwnedPrivate helper, loopback port 0 | cancel, retry, AI game |
| complete/failLocalRoomStart | local listen/delete | authenticated ready/error/exit | no premature connection |
| startTakeoverGame | Server(GameSessionConfig) | OwnedPrivate with snapshot/replay/manifest hashes | live takeover readiness |
| rollbackTakeover | delete Server, restore replay | stop owned helper, retain replay restore state | position, perspective, pause |
| startConnection/reconnect/restart | Client using Config endpoint | same Client, reuse ready owned endpoint | no new helper, external isolation |
| showHomePage/gotoStartScene | delete embedded Server | stop private/joined host; retain host-only management | no orphan, repeat start |
| closeEvent | quit GUI | asynchronous shutdown then exit notification | initialization, game, takeover, timeout |
| BroadcastBox | direct Server::broadcast | bounded broadcast command with reply | delivery/error |
| BanIpDialog | Room/ServerPlayer pointers, QSettings write | player snapshot, kick/ban command, owner persists ack | offline player, stale result |
| StartScene | Server log signal and startupMessages | controller log/status signals | bounded log, endpoint |
| -connect | GUI Client | unchanged, never owns remote server | remote process remains alive |
| -server | full engine then embedded Server | early QCore forwarding to standalone helper | args, exit code, missing helper |
| QSanguoshaXPServer | absent | standalone dedicated CLI or managed helper | no GUI/audio modules |

## Protocol and ownership

The private control transport is QLocalServer/QLocalSocket, unrelated to gameplay
TCP/Protocol V2. Frames have a four-byte big-endian length followed by a JSON object.
Maximum payload is 256 KiB; input and queued output are bounded. All messages carry
version, session, generation and decimal-string request ID. Seeds and generation
are decimal strings. JSON numeric fields accept only finite integral values in range.

The GUI listens with UserAccessOption and a fresh CryptoAPI random token for each
launch. The helper receives the token through its environment, removes it immediately,
and authenticates before receiving initialization. Tokens never enter ordinary logs,
game packets or command lines. This prevents accidental/cross-user attachment; it
does not defend against a debugger running with the same Windows identity.

States: Idle -> Launching -> Handshaking -> Initializing -> Ready -> Stopping -> Idle.
Failure also stops the owned process before permitting reuse. Every callback is tied
to its QProcess and generation. External/standalone processes are never adopted.

Startup, authentication, requests, takeover and shutdown have explicit deadlines.
QProcess owns the process handle. A helper also opens and verifies its parent's
creation time before engine initialization and retains that handle: PID reuse cannot
attach it to another process. A native wait thread observes parent death even during
Lua initialization; this emergency exit is forced, never graceful. Normal control loss
requests the existing server/room/engine cleanup. A shutdown timeout kills only the
owned QProcess and records the incomplete phase.

## Settings and data

The GUI captures a session-local INI before launch; all primitive persisted values,
lists and extension keys plus active game settings are preserved. The helper's Settings
object is bound to that INI before static initialization, and overrides are installed
before EngineBootstrap. The GUI owns persistent settings. Ban changes are persisted
only after a correlated successful reply. Assets are shared read-only; runtime data,
logs, replay/snapshot pairs and diagnostics use the user's writable data root.
Each managed helper redirects its native streams to
`logs/xp-server-<session>.stdout.log` and
`logs/xp-server-<session>.stderr.log`; these files are diagnostic evidence and
do not participate in readiness or control-protocol decisions.

Ready means validated settings, paired source build, matching rules manifest,
initialized runtime, prepared initial room and a successfully bound native socket.
Private sessions bind 127.0.0.1:0 in the helper; the actual socket remains bound.
Host sessions use the configured endpoint and discovery/listing behavior.
Takeover_ready is distinct from ready and commits rollback data only after the
existing Server::takeoverReady signal.

## Acceptance record

| Gate | Current status |
|---|---|
| Implementation | IN PROGRESS: the two test-tool defects below are fixed; product acceptance remains open |
| Build / PE imports | PASS: Debug v141_xp build; both EXEs are PE32 x86 5.01 and import gates passed |
| Automated protocol/lifecycle | PARTIAL: protocol executable PASS (58 ms); controller tests compiled, lifecycle run stopped at the 60-second local limit |
| Helper native diagnostics | SOURCE FIXED: stdout/stderr retained per session; fixture regression compiled, focused runtime check pending |
| Slow-load AI fill driver | SOURCE FIXED: polling begins after `roomSceneCreated`; XP GUI compiled, guest rerun pending |
| Portable manifest | SOURCE + SHORT SMOKE PASS: one atomic JSON binds identity, both EXEs and runtime files; matching pair returned 0 and a tampered helper returned 74 |
| Replay takeover / rollback GUI | NOT RUN: no product fault classified |
| Reconnect GUI | NOT RUN: no product fault classified |
| Management GUI | NOT RUN: no product fault classified |
| XP / Win7 runtime | BLOCKED: awaiting designated QA VM authorization |
| Portable / ISO | NOT RUN |
| Memory A/B | BLOCKED: baseline must be measured on the designated XP VM first |
| Modern products | NOT RUN |

Memory acceptance must be frozen after measuring the original payload and before
measuring the new payload. Record private bytes, working set, VirtualQueryEx address
map (committed/reserved/free and largest free region), handles, threads, system commit
and stage markers at one-second intervals, with identical assets, seed, mode and
player count. Never substitute working set for address-space pressure. Report both
maximum per-process pressure and combined private bytes. No threshold or PASS is
claimed before that baseline exists. 20 players remains exploratory.
