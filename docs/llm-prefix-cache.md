# LLM prompt prefixes and provider usage

The decision and general-authoring paths arrange reusable input before changing
input. They do not cache responses, keep a shared conversation, or reuse a
player's decision for another player. This prepares inputs for provider-side
prefix caching; it does not establish a cache hit or a cost reduction.

## Provider capabilities checked on 2026-10-08

- [DeepSeek context caching](https://api-docs.deepseek.com/guides/kv_cache/)
  is automatic and best effort. Common-prefix persistence and provider cache
  lifetime still control hits. No cache switch, key, retention setting or
  `prefix` completion flag is added by this change.
- [DeepSeek chat usage](https://api-docs.deepseek.com/api/create-chat-completion/)
  reports `prompt_cache_hit_tokens`, `prompt_cache_miss_tokens` and the equivalent
  `prompt_tokens_details.cached_tokens`. Hits plus misses equal prompt tokens.
  Miss tokens do not mean cache-write tokens.
- [TypeSafe's System One API](https://docs.typesafe.ai/api) accepts structured
  `state` and typed `questions`. Its documented usage has `input_tokens` and
  `output_tokens`; no cache control or cache read/write counters are documented.
  JEV therefore retains unknown cache counters. Local JSON member ordering is
  deterministic, but the service's rendering of that object into model input
  and its cache behavior remain unverified. No model-prefix hit claim is made.
- Authoring accepts an arbitrary OpenAI-compatible **chat-completions** endpoint.
  [OpenAI prompt caching](https://developers.openai.com/api/docs/guides/prompt-caching)
  is automatic on supported models, with model-specific eligibility, retention
  and controls. This generic adapter cannot infer endpoint/model capabilities,
  so it sends no provider-specific cache parameters and changes no retention.
  [OpenAI chat usage](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)
  documents `prompt_tokens_details.cached_tokens` and `cache_write_tokens`.
  These and the DeepSeek chat counters are recognized when returned. Responses
  API and Anthropic Messages API envelopes are not supported by this adapter;
  similarly named counters from those APIs are not guessed into this schema.

## Serialization and invalidation

`providers.py` validates the same synthetic seat-visible envelope, then rebuilds
wire order as `prompt_contract`, `synthetic`, `seat_visible`, `rules`,
`observation`. Within observation, only the known `player_columns` and
`public_event_window` schema fields precede dynamic fields. Columns, player rows,
legal options and all other arrays retain their original order and values.
Only static fingerprint input is recursively key-sorted. Instructions, rules,
static schema or format version changes change the SHA-256. Private hands,
player identity, HP, events and options do not enter the fingerprint. The ledger
records only the static fingerprint, never the prompt or snapshot.

Authoring emits an ordered message array: unchanged system contract, stable
user contract, dynamic user task. The stable message includes the complete
bundled engine contract, schema version, prompt format version and SHA-256 of
system instructions plus the canonical static object before adding its hash.
JSON object keys in engine context are explicitly sorted; array order and code
strings are preserved. Dynamic original/current specifications, manual edits,
diagnostics and correction requests remain in the final user message. This
avoids relying on Qt initializer order and keeps task text at user priority.
Hashes are local format/content identities, not provider cache keys. Providers
may reuse an unchanged portion before a changed token; a contract change does
not imply a purge of provider caches.

No prompt padding or paid prewarming is performed. The metadata adds a small
amount of input. Existing byte limits still apply, so decisions already near
16 KiB can cross that limit and take the existing bounded fallback path.

## Usage and budget meaning

Python receipts expose nullable `cache_read_tokens`, `cache_write_tokens`,
`cache_miss_tokens`, and `cache_usage_source`. Missing, null or unsupported fields
remain unknown, while a validated reported zero remains zero. DeepSeek duplicate
read aliases must agree, counts must be nonnegative integers bounded by input,
and reported hit/miss pairs must sum to input. Bad cache receipts fail closed and
retain the full reservation. JEV cache fields remain unknown pending a documented
contract. Existing model, legal-option, probability and response validation are
unchanged. Schema-1 ledgers without cache metadata remain readable.

Authoring `Document::lastUsage()` retains only sanitized numeric usage from an
accepted current response envelope. Missing/unsupported usage is `unknown`;
malformed or inconsistent usage is `invalid`, with counts withheld. Optional
usage does not change candidate response validation. The GUI displays input,
output, cache read and cache write counts, including explicit `unknown` values.
New requests clear previous counters, stale/cancelled responses cannot replace
them, and usage is not saved to authoring projects or exported Lua packages.

Neither path claims a provider invoice. The AGENT ledger continues reserving
its existing maxima and settling valid receipts at the existing conservative
peak/miss rates, with no cache discount. `provider_reported_billed_usd` remains
null. Authoring calculates no cost. HTTP response caching remains disabled in
`general-authoring-provider.cpp`; this is independent of model prefix caching.

## Offline verification

From the repository root:

```sh
python3 -m unittest tools.external_agents.test_cache -v
cmake -S tools/authoring/tests -B /tmp/qsan-authoring-cache -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8-or-newer
cmake --build /tmp/qsan-authoring-cache -j2
ctest --test-dir /tmp/qsan-authoring-cache --output-on-failure
```

Python tests use injected fixtures and temporary ledgers, including the real
duel and 50-seat envelope builders. No socket connection, native game, provider
credential, paid model call or production ledger is used. Authoring fixtures
link the actual document implementation and bundled Lua parser to Qt Core; they
test canonical byte identity across revisions and JSON member order, contract
invalidation, usage formats, output schema, secret rejection and request
lifecycle. They do not execute generated skills or require the GUI.

In the implementation environment, the 11 Python tests pass. The standalone
C++ fixture builds against the existing Qt 6.8.2 Core and bundled Lua 5.4 sources;
CTest passes (1/1). It compiles the actual `general-authoring.cpp` implementation
and resource, covering prefix serialization, usage and document lifecycle. Only
the standalone fixture requires Qt 6.8 or newer; the application's Qt 6.11 GUI
requirement is unchanged. Network/Test modules and a new SDK are unnecessary for
these document-layer tests. The full GUI build has **not run**.

A read-only context builder check is deterministic; the pre-existing bundled
`sources` hash for `swig/sanguosha.i` differs from current source, while contract
content otherwise matches. This change preserves that bundled contract rather
than regenerating or expanding its API scope.

Actual provider hit rate, latency, answer quality and monetary savings remain
unmeasured. Prefix equality is only a local prerequisite. Any later measurement
must be separately authorized and use reported counters rather than estimated
or fabricated cache hits.
