# External agents: game participation

This directory contains only runtime modules and their usage instructions.

- `providers.py`: provider requests, response validation, durable budget ledger
- `game_client.py`: native transport and seat-visible game decisions
- `hybrid50.py`: legal-option construction and local/JEV routing for 50 seats
- `room_hybrid_worker.py`: room-owned JEV process used by the game UI
- `staged50.py`: bounded hierarchy over complete native tickets

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

The room worker keeps all seat endpoints connected and explicitly uses native
SmartAI for the opening preamble, including general selection and start/turn
prompts, until the room's first native Activate ticket. That ticket begins normal
hybrid routing; subsequent supported flat and staged decisions can use JEV.
This opening policy avoids paid setup choices changing the validated opening.

The bounded staged prototype supports standard **Wusheng** during Activate/UseCard:
choose the skill, then its native-legal red material, the fixed Slash output,
and a complete native target group. Printed cards can use card-then-target stages
when their combined flat list exceeds 255 options or the input limit. Every
reachable stage is checked against the 255-option/16KB limits before any call;
no legal branch is truncated. Unknown effects, other skills, declarations with
a different order, incomplete conversions or target projections remain SmartAI.
Response-accessible hand piles and physical/virtual equipment conversion sources
are outside this first contract and explicitly make coverage incomplete.

Planning submits only one final action against the original decision and revision.
Stages consume no cards or skill quota. Without Back, standard Wusheng needs at most
three provider calls (family, material, targets); its fixed output is automatic. Singleton
continuations are automatic. Back is limited to two, and all visits/calls to eight;
the existing native 30-second decision deadline still applies to the entire plan.
Cancel passes the optional original decision. Bound exhaustion requests SmartAI.
Disconnect/reconnect invalidates an outstanding bounded plan. All stages use the
same room game ID, adapter and durable ledger; provider failure or budget exhaustion
disables further paid work without resetting reservations.

A capped Linux native 50-seat run confirmed paid physical-card staging and its
native card execution. All 50 seats used the shared hybrid routing policy; six
seats made paid choices. Standard Wusheng's issued conversion and complete stage
traversal passed offline validation; a paid Wusheng conversion was not observed
in that run. GUI rendering and packaged deployment remain unverified. The run
stopped at its time cap and does not establish a full-game duration improvement.

## Stable prefixes and usage

Provider serialization explicitly places rules and reusable observation schema
before the changing seat snapshot and legal choices. A versioned SHA-256
fingerprint covers only that static contract. No decisions are cached or reused
across seats. See [prefix caching and usage](../../docs/llm-prefix-cache.md) for
provider capabilities, counter semantics and local verification commands.
