# DeepSeek Flash and TypeSafe Jev external-agent integration

Cloud validation completed on 2026-10-04 against engine debug
`49b130059ccbd9111e97c91896cedec2a6af43e8` and companion extensions main
`7a7fa18a2b43670f718dfc3ff0fe2eb0a0841a2a`. The environment's default main
checkout is not the engine target. Work occurred in isolated detached worktrees;
no push, public listener, new API credential, or security-setting change occurred.
The requested environment configuration version was
`cecfgver_6ac19c85035481a1aa414602a18522d0`; the runtime did not expose an
independent version attestation.

## Results

- All five original core/repair patches were reconstructed from authorized exact
  records, verified against their SHA-256 hashes, and applied in the required order.
- Native debug build completed. Focused native regressions: **9/9 pass**.
- Separate-process mock game: **49 decisions**, winner `rebel`, reconnect,
  stale revision/ID, duplicate, malformed reply, seat isolation, and cancellation
  checks pass. Native callback errors and explicit fallback: zero.
- Provider/client offline financial and boundary tests: **24/24 pass**.
- Each real API passed authenticated readiness and one structured Jink probe.
- A real combined-routing native game against SmartAI completed with **31 external
  decisions**: 13 Flash, 12 Jev, and 6 sole legal actions submitted without inference.
  Winner `rebel`; host return code 0, no callback errors or explicit fallback,
  workers stopped, room destroyed, and both host/client processes exited.
- Including probes: **27 inference requests, zero retries or validation errors**.

| API/model | Requests | Input tokens | Output tokens | Peak-rate estimated USD | Median latency |
| --- | ---: | ---: | ---: | ---: | ---: |
| DeepSeek `deepseek-flash` | 14 | 12,603 | 97 | 0.003897300 | 873.5 ms |
| TypeSafe `jev-1.13.0` | 13 | 17,989 | 604 | 0.000755538 | 167 ms |

Combined conservative peak-rate estimate: **$0.004652838**. Neither documented
response reports billed dollars, so provider-reported billed USD remains null;
this estimate is not an account billing statement. Jev confidence ranged from
0.00 to 0.94 (median 0.37). This single game verifies integration and routing;
it is not a comparative strategy benchmark.

## Contracts and prices

Only the approved models and destinations are used. DeepSeek uses bearer auth and
`POST https://api.deepseek.com/chat/completions`, bounded `messages`, disabled
thinking, `max_tokens=256`, and JSON-object output containing a legal option key.
Jev uses bearer auth and `POST https://api.typesafe.ai/v1/systemone` with
`state`, pinned `model`, and a map of typed `questions`. Jev is not treated as a
chat model. Its choice, probability distribution, confidence, versioned model,
and usage are validated.

Official sources checked before paid calls:

- [DeepSeek pricing](https://api-docs.deepseek.com/quick_start/pricing/): peak
  uncached input $0.30/M and output $1.20/M, 1M context and 384k maximum output.
- [DeepSeek request contract](https://api-docs.deepseek.com/),
  [thinking mode](https://api-docs.deepseek.com/guides/thinking_mode/), and
  [JSON output](https://api-docs.deepseek.com/guides/json_mode/).
- [TypeSafe models/pricing](https://docs.typesafe.ai/models): `jev-1.13.0`,
  input $0.042/M, free output, 64k request limit.
- [TypeSafe API](https://docs.typesafe.ai/api): typed System One payload and usage.

## Shared durable budget

The user's combined ceiling is $2, including both APIs and retries. The internal
shared guard stops at $1.50. Live runs always use
`/workspace/external-agent-paid-ledger.json`; never delete, reset, or substitute
this file when restarting. Integer nanodollars, a cross-process exclusive lock,
atomic writes, and file/directory fsync persist the reservation before network I/O.
The smoke process and subsequent native client reused this same ledger.

Each call reserves the entire conservative published maximum before sending:
$0.786432 for Flash (1,048,576 input + 393,216 output tokens at peak rates),
$0.002752512 for Jev (65,536 input tokens). A fully validated exact-model response
with checked usage may settle at peak token rates after its durable final write.
Unexpected reasoning, inconsistent/missing usage, incomplete generation,
model/decision mismatch, HTTP/transport failure, invalid response, or an unknown
crash retains the full reservation. A failed final write leaves the durable
pre-call reservation. Legacy rows without settlement markers also retain their
reservations. Reopening never resets spend. There are no automatic retries.

Requests are serial, at most 64 across both APIs, with at most 16,384 serialized
input bytes, 256 requested Flash output tokens, 65,536 response bytes, and a
20-second timeout. The maximum reservation does not assume the output cap is
honored. Redirects and alternate hosts/models are disabled.

## Native provider client

`game_client.py` starts `qsanguosha_external_agent_host` using private pipes,
reads its memory-only seat capability without logging it, and connects only to
127.0.0.1. It constructs an explicit whitelisted synthetic seat-visible
observation and rules. It never sends the full transport packet, capability,
credentials, raw environment, source archive, hidden hands, deck order, or other
seats' private state to either API. Request bodies and headers are not logged.

The client offers at most sixteen legal choices, selects Jev for up to four
options and Flash for larger sets, then submits the selected decision ID,
revision, native card ticket, and targets through authoritative validation.
Printed cards in the visible hand, optional pass, mandatory string/card/player
selections, and a permitted fixed Guanxing order are supported. Sole legal
choices are submitted as forced actions without inference. Skill conversions
are not offered by this bounded policy. Unsupported prompts or API/validation
errors visibly cancel; there is no silent SmartAI fallback. A decision/request
limit produces an explicitly labelled segment, not a completed game.

Build using the engine's focused options `QSAN_TEST_EXTERNAL_AGENTS=ON`,
`QSAN_BUILD_EXTERNAL_AGENT_HOST=ON`, `QSAN_BUILD_GUI=OFF`, and
`QSAN_EXTENSIONS_SOURCE_DIR=<companion checkout>`. Stage the explicit native
fixture with `tools/autotest/stage_external_agent_runtime.py` or the CTest fixture,
and set `QSAN_ASSET_ROOT`/`QSAN_USER_DATA_ROOT` to that runtime before the client.

```sh
python3 -m unittest discover -s tools/external_agents -p 'test_*.py'
ctest --test-dir <build> -R external_agent --output-on-failure
python3 tools/external_agents/smoke.py --live
python3 tools/external_agents/game_client.py --host <build>/qsanguosha_external_agent_host --provider combined
```

Live commands consume the existing ledger and require the approved configured
proxy placeholders `DEEPSEEK_API_KEY` and `TYPESAFE_API_KEY`; do not create or
copy API credentials. The recorded 27 calls already count against this task.

## Restoration and Lua correction

Apply `prior-v44-engine.patch`, then `engine-external-agents.patch` to the engine;
apply `prior-v44-extensions.patch`, `extensions-external-agents.patch`, then
`extensions-compatibility.patch` to the companion. The prior engine patch already
contains the needed SWIG repair; do not apply an optional duplicate. The bundle's
`provider-integrated.patch` is the complete current provider/client addition;
older `provider-adapters.patch` and `readme-correction.patch` are retained only as
historical recovery evidence.

The former Lua parse-error report was incorrect: setup Lua 5.2.4 rejects syntax
accepted by the engine's modified Lua 5.4.8, including bit operations and
statements after return. A checker compiled from the target engine's bundled
sources passed **334/334** current engine/companion Lua files after restoration.
The historic **631/631** result refers to a different original task inventory;
it is not this current count. Native tests execute the actual correction
callback and verify `CorrectSkillResult.noEffect` and `useAmount`.

Durable native server restart persistence and custom/draft mode coverage remain
deferred. The complete original core patches are included under `recovered-core`;
current sanitized receipts and test evidence are under `current-integration`.
The original core Library artifact remains preserved separately.
