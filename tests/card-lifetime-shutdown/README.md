# Card lifetime shutdown fixture

This directory is intentionally isolated from the shared test target. Build
`shutdown_protocol_test.cpp` against the normal QSanguosha engine test link
line, then run the executable with no argument, `worker`, `lease`,
`reservation`, and `lua-pin`.

The no-argument run requires one `CARD_LIFETIME_ZERO` record and four ordered
`CARD_LIFETIME_SHUTDOWN_STAGE` records, and verifies that a second shutdown is
harmless. The `worker`, `lease`, `reservation`, and `lua-pin` fixtures are
adversarial: they must exit nonzero and must not emit `CARD_LIFETIME_ZERO`.
