# W2: rules-bundle identity contract — partial implementation

Baseline: `lolosiyue/Qsanguosha-sijyu`, `debug@57d64b1899d1c805edc91fb11066ee7cda0a7e91`
(merged PR #32).

## Status

This patch implements the **contract slice**, not the complete W2 negotiation.
Originally prepared as an offline handoff, this contract slice is now being
published as a Draft PR. Publishing the contract does not complete negotiation.

The optional `ServerHelloPayload.rulesIdentity` field round-trips a
`rules_identity` object without changing the existing command ID or the legacy
HELLO shape when absent. The payload parser validates that the optional field is
an object; the separate identity validators own its nested schema.

The Qt Core header and TypeScript module compare the same descriptor. They do
not calculate authoritative identity, alter gameplay rules, or enable/disable the
production Web client yet. They share 46 checked-in synthetic vectors. Object
key order is irrelevant, while package order, versions, schema inventories, all
four digests and explicit value types matter. Missing/invalid descriptors never
match. Empty identities cannot match each other.

## Descriptor v1

| Field | Required meaning |
| --- | --- |
| `schema_version` | Descriptor format, currently 1. |
| `profile` | Explicit content profile; not a label supplied by an arbitrary client. |
| `protocol_major` | Protocol major; this consumer supports 2. |
| `bridge_api` | Rules bridge contract; this consumer supports 1. |
| `rules_code_sha256` | Common rules implementation identity, generated from defined build inputs. |
| `lua_content_sha256` | Actual loaded Lua rules content identity. |
| `lua_bindings_sha256` | Common C++/Lua binding interface identity. |
| `card_registry_sha256` | Full ordered numeric registry and identities, not merely card count. |
| `cpp_packages` | Ordered C++ package registration inventory; duplicates forbidden. |
| `interaction_schemas` | Exact map of required supported interaction schemas and versions. |

A descriptor is a compatibility claim, **not a signature or a script sandbox**.
The synthetic all-a/all-b digests in the vector corpus must never be emitted by
a product. Version 1 rejects extra fields instead of silently excluding them
from comparison. Changing descriptor meaning requires a new schema version.

## Required remaining W2 implementation

1. Add one native exporter used by both server and `ClientRulesSession::registry()`.
   Fingerprint a precisely defined set of common build inputs (not the platform
   binary, build directory, timestamp, or unrelated repository commit). Include
   bindings, relevant build options and actual package registration order.
2. Bind the content digest to the files/definitions actually loaded at startup.
   Hashing a later mutable directory or reading a sidecar cannot establish that.
   Unsupported external scripts/registration randomness must emit an explicit
   unsupported state; they cannot fall back to the builtin-v1 identity. Reusing
   the existing bootstrap closure should not create another hand-maintained list.
3. Populate the HELLO descriptor before calling `ServerConnectionContext::sendHello`.
   Keep the protocol DTO and connection-context implementation independent of the
   engine. Old desktop/TUI clients must continue accepting the optional extension.
4. Carry the server descriptor through the actual Web HELLO/session path. Invoke
   the comparator before accepting native rules results. Revalidate or invalidate
   on generation change, state sync, descriptor replacement, and reconnect. The
   descriptor from the previous connection must not survive a missing new one.
5. Bind native query/reply preparation to the agreed identity; block confirmation
   when identity is absent, unsupported or different. Preserve server final
   validation. Do not quietly fall back to TS rules or load a module URL from the
   network descriptor.
6. Extend the production native/browser gate, actual HELLO codec round-trip tests,
   controller tests and CI. Test same count/different order, same Lua skill name/
   different bytes, mixed loader/module/manifest, missing schemas, old clients,
   repeated connection and identity changes during an in-flight evaluation.

W2 is **not complete** until these paths and the real supported-toolchain tests
pass. Do not proceed to W3 on the basis of the pure comparison tests alone.

## Local verification

TypeScript strict compilation and the actual comparator over all 46 vectors pass.
The C++ test source is provided but has not been compiled here: no Qt development
kit is installed. No native/WASM parity, server handshake or browser integration
execution is claimed for this patch.

After applying in a full repository checkout:

```sh
# Pure TypeScript contract, no new npm dependency.
./web/node_modules/.bin/tsc web/src/rules-identity.ts --target es2022 \
  --module es2022 --moduleResolution bundler --strict --skipLibCheck \
  --outDir build/rules-identity-js
printf '{"type":"module"}\n' > build/rules-identity-js/package.json
node tests/client_runtime/rules_identity/check.mjs \
  build/rules-identity-js/rules-identity.js \
  tests/client_runtime/rules_identity/vectors.json

# Standalone Qt Core contract and HELLO serializer/parser tests.
cmake -S tests/client_runtime/rules_identity -B build/rules-identity
cmake --build build/rules-identity
ctest --test-dir build/rules-identity --output-on-failure
```

The read-only `Rules identity contract` workflow builds this standalone Qt Core
target and compiles the TypeScript comparator with the repository dependency.
Both consume the same 46 vectors; the native target also checks the HELLO
serializer/parser. This gate is separate from real server/WASM/browser
negotiation, which remains required before W2 is complete.
