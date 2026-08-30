# Protocol V1 — retired

Protocol V1 is not part of the production protocol. Its codec, message adapter,
`Packet` facade, array envelope, and wire compatibility guarantees were removed
by the Protocol V2 breaking cutover.

Old clients and Protocol V1 recordings are rejected. No downgrade or converter
is provided. See [`protocol-v2.md`](protocol-v2.md).
