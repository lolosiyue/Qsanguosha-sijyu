# Protocol runtime switch

## Contract

Every connection starts with Protocol V1. Capability advertisement only sets
`preferredVersion`; it never changes the active codec. A server sends the switch
command only when both peers advertised V2. Legacy peers remain on V1 and never
receive switch traffic.

```text
V1 startup -> capability negotiation -> OFFER V1 -> ACK V1 -> COMMIT V1 -> V2 active
```

The single control command is `S_COMMAND_PROTOCOL_SWITCH` (`133`). Its payload is
strictly the following object:

```json
{"schema_version":1,"phase":"offer","target_version":2,"switch_id":"1"}
```

ACK and COMMIT use the same fields with `phase` set to `ack` and `commit`.
`switch_id` is a positive canonical decimal string scoped to the connection.

## State and boundary

| Side | Transition | Active codec |
|---|---|---|
| Server | V1Active -> OfferSent | V1 |
| Client | V1Active -> AwaitingCommit after OFFER and queued ACK | V1 |
| Server | OfferSent -> AckReceived after ACK | V1 |
| Server | queue COMMIT bytes, then AckReceived -> V2Active | V2 |
| Client | AwaitingCommit -> V2Active after successful COMMIT processing | V2 |

No ordinary gameplay packet is accepted while the barrier is in progress. The
client defers setup completion and queued application traffic until COMMIT; the
server does not attach or reconnect the player to a room until its V2 state is
active. The next frame after COMMIT is V2, including when COMMIT and that frame
arrive in one TCP read. Once OFFER has begun, timeout, malformed control data, a
mismatched identifier, an invalid transition, or a frame for the wrong active
codec closes the connection; there is no silent downgrade.

## Routing and correlation

The client and each `ServerPlayer` own their session state and monotonically
increasing V2 message-ID allocator. Notifications, requests, replies, and
broadcasts become `ProtocolMessage` values and are encoded by the active
per-connection router. V2 replies correlate with the full `quint64 reply_to`;
the legacy Packet serial remains authoritative only for active V1 sessions.

## Transport

The codec owns JSON bytes, not delimiters. The socket adds one newline on write
and uses a dynamic byte accumulator on read. Frames up to 65535 encoded bytes are
accepted; larger frames and over-limit data without a newline are rejected.
CRLF input is tolerated at the framing boundary. UTF-8 bytes never pass through
a Latin-1 conversion.

## Mixed clients, replay, and reconnect

V1 and V2 clients can share a room because broadcasts are encoded separately for
each receiver. Recording occurs once after decode and normalizes the logical
message through the V1 adapter; existing replay files therefore never contain a
V2 envelope or switch-control packet. Replay playback itself remains V1 and is
not re-recorded.

A reconnect is a new transport connection. It starts on V1, advertises
capabilities again, completes a new barrier, resets its per-connection message-ID
allocator, and only then attaches to the existing player.
