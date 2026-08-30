# Protocol runtime switch — retired

Runtime switching and `S_COMMAND_PROTOCOL_SWITCH` are not part of production.
A connection starts and remains on Protocol V2. OFFER/ACK/COMMIT states and
per-connection active/preferred-version branches were removed.

This file is retained only to mark the old design as retired. It carries no
compatibility guarantee. See [`protocol-v2.md`](protocol-v2.md).
