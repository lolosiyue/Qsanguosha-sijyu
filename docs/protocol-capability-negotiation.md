# Protocol capability negotiation — retired

Capability negotiation is not part of production. Both endpoints require
Protocol V2 from the first TCP frame; there is no advertisement, selection,
fallback, or mixed V1/V2 room behavior.

This file is retained only to mark the old design as retired. It carries no
compatibility guarantee. See [`protocol-v2.md`](protocol-v2.md).

W2 adds optional rules-bundle metadata to the existing V2 Hello/Signup, with
mandatory verification on WebSocket. This does not revive protocol version
selection or fallback. See [rules-bundle-identity.md](rules-bundle-identity.md).
