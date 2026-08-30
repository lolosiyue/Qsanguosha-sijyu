# Replay system status

The replay subsystem uses Replay V2 only. The normative file/container contract
is documented in [`replay-v2.md`](replay-v2.md).

| Capability | Status | Production component |
|---|---|---|
| JSONL recording and strict loading | Supported | `ReplayWriter` / `ReplayReader` |
| PNG Replay container | Supported | `ReplayContainer` |
| Timeline and seeking | Preserved | `ReplayIndex` / recorder UI |
| Perspective switching | Preserved | `S_COMMAND_SWITCH_CONTEXT` path |
| Snapshot/state reconstruction | Preserved | `ReplayGameState` |
| Watching-time player takeover | Preserved | `ReplayTakeoverManager` |
| Replay V1 read/write | Removed | Explicit rejection |
| Replay conversion tool | Not provided | Breaking cutover policy |
| TUI Replay | Intentionally unsupported | No TUI implementation |

Takeover replies use the registered typed interaction schema and retain exact
full-range `quint64` request correlation. A takeover branch is saved to a new
Replay V2 file; the source recording is not overwritten.
