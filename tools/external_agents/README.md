# External agents: game participation

This directory contains only runtime modules and their usage instructions.

- `providers.py`: provider requests, response validation, durable budget ledger
- `game_client.py`: native transport and seat-visible game decisions
- `hybrid50.py`: legal-option construction and local/JEV routing for 50 seats
- `room_hybrid_worker.py`: room-owned JEV process used by the game UI

## Select JEV for 50-player AI seats

In server setup, select **50p**, keep **Enable AI** enabled, then select
**JEV hybrid for 50-player AI seats**. The option is off by default. Human seats
continue to use normal input.

The adapter expects the repository source layout and Python 3. Start the
application from the repository root. The existing configured `TYPESAFE_API_KEY`
and provider connection are required; never place credentials in source files
or game settings. Packaged desktop and non-Linux deployment are not yet verified.

All AI seats in one room share a durable game ID. The JEV ceiling is US$0.10
per game, with an internal US$0.098 guard and conservative reservations before
requests. Failed or unknown calls retain their reservations. Do not reset the
existing budget ledger when reconnecting.

Unsupported decisions use SmartAI. Provider failure, invalid response, timeout,
exhausted budget, or an unavailable worker stops further paid work for the room
and returns play to SmartAI. Resumed/takeover rooms use SmartAI because snapshots
do not persist the paid game ID. Fallback notices do not expose private seat
information or credentials.

Only explicitly projected seat-visible information and legal options are sent
to the provider. Native validation rejects late, stale, duplicate, or illegal
answers.
