# Server / WASM rules bundle identity (W2)

W2 extends PR31 commit `311a494` using the existing Protocol V2 Hello/Signup
exchange. The retired V1/V2 capability negotiation is not reintroduced.

## Admission contract

| Connection | Missing identity | Supplied identity |
|---|---|---|
| WebSocket | Reject before Room/player binding | Validate and compare |
| Native TCP desktop/TUI | Preserve legacy signup | Validate and compare |

The transport requirement comes from the server-owned socket implementation,
not an untrusted client type string. The common gate runs before fresh signup,
room selection and reconnect ownership transfer. An explicit empty or malformed
`rules_bundle` never counts as missing metadata on legacy TCP.

Hello keeps outer `schema_version: 1`; Signup keeps version 2 (and the existing
legacy version 1 parser). Both add an optional `rules_bundle` object. Older
native readers already ignore unknown object fields. No command IDs, V2 framing,
Room/Lua/SWIG facade, or reply correlation contracts are changed.

## Identity source and comparison

`src/core/rules-bundle-exporter.cpp` is the shared native exporter. Engine records
actual successful package registration order and captures the builtin Lua file
snapshot before bootstrap. The native fixture runner's `--export-rules-bundle`
and production WASM initialization both use this exporter.

| Field | Source |
|---|---|
| `schema_version` | Identity format 1 |
| `protocol_version`, `bridge_schema` | Protocol V2; persistent runtime bridge 2 |
| `ruleset`, `content_profile` | Engine mod identity; controlled `builtin-v1` |
| `packages` | Successful registrations, preserving order |
| `card_registry_hash` | Every physical ID, object/class/package, suit and number, in ID order |
| `cpp_hash` | Portable normalized source/build-recipe digest, including native rules and Lua interpreter sources |
| `lua_hash` | Domain-separated digest of the five loaded-profile file byte hashes |
| `bindings_abi` | Shared bindings/Lua headers and bridge contract source digest |
| `interaction_schemas` | Actual Room-to-client request inventory and production schema source digest |
| `bundle_id` | Domain-separated SHA-256 of the complete identity excluding this field |

Object keys are canonicalized; arrays are never sorted. The server recomputes the
seal, rejects incomplete/malformed metadata, checks bridge/bindings and every
required interaction, then requires complete bundle equality. The Web controller
also verifies that its existing interaction handlers cover the advertised native
inventory; adding a server command does not silently advertise unsupported UI.

Platform compiler ABI, pointer sizes, build paths, timestamps and binary hashes
are not cross-compared between native and WASM. This is a shared bindings contract,
not a claim that Windows and wasm32 machine ABIs are equal. The source digest
conservatively invalidates bundles after changes in its recorded source closure,
including a C++ skill change that leaves all card/skill names intact.

## Controlled content profile

`builtin-v1` uses the existing five-file fixture closure:
`lua/config.lua`, `lua/sanguosha.lua`, `lua/utilities.lua`, `lua/sgs_ex.lua`,
`lua/lib/json.lua`. All configured C++ packages still register normally.
`.gitattributes` pins these assets to LF so checkouts do not introduce platform
line-ending differences into byte hashes.

Use an isolated deployment asset root containing that closure. Extra Lua in
`lua`, `extensions` or `lang` (except the server-only AI paths below), symlinked content, custom scenario files in asset
or user-data `etc`, and Lua search/init environment overrides are unsupported in
W2. A normal expanded desktop data tree may therefore continue serving legacy TCP
while rejecting Web with `rules_content_unsupported`. W2 does not enable extension
support or dynamically download server-provided modules.

AI is server-owned policy. Files under `lua/ai/` and the exact AI dependency
`lua/lib/middleclass.lua` are permitted in the server deployment, but excluded
from `lua_hash` and the WASM asset manifest. A server AI revision therefore does
not require a new Web download. The five shared rules files remain byte-matched;
the exception does not cover sibling paths such as `lua/ai-extra.lua`, other
libraries, extensions, translations or scenarios. Symlinks remain rejected,
including inside the AI directory.

This classification is a deployment contract, not a Lua sandbox: the server
operator must use these paths for AI policy, not register additional game rules
through them. Identity proves shared client-rule compatibility, not identical AI
decisions. The existing Room startup still loads SmartAI even with `--ai off`,
so a server deployment must provision the AI scripts and middleclass separately.
The Web runtime continues to ship only the five shared rules files.

Replacing content on disk does not relabel an already loaded Engine: its captured
snapshot must still match when identity is requested. Deploy a new complete bundle
and restart the server/runtime. Live content replacement and hot reload are outside
this controlled deployment contract. Hashes prove consistency, not publisher trust.

## Loader / WASM deployment

The WASM target produces four files:

- `qsanguosha_client_wasm.mjs`
- `qsanguosha_client_wasm.wasm`
- `qsanguosha_client_wasm.assets.json`
- `qsanguosha_client_wasm.bundle.json`

The final file is emitted by the build tool and binds the exact first three file
hashes plus the native bridge version. Packaging rejects changed/mixed artifacts.
The Vite configuration embeds the generated deployment manifest digest into the
Web loader. Package the runtime **before building the Web frontend**, and deploy
those frontend/assets together. An unprovisioned Web build remains possible but
fails closed when native rules are requested.

The production Worker fetches only the fixed `/rules/` files, checks its compiled
expected deployment digest and all artifact hashes, then imports the exact
verified module bytes using a temporary Blob URL. It requires bridge 2 exports,
verifies embedded Lua bytes and the native identity, and only then reports ready.
Deployments using CSP must permit the verified module Blob import. No module URL
comes from server metadata or player input.

The Web controller initializes before opening the WebSocket, preserving the
server's existing signup deadline. Cancellation/reconnection generations discard
obsolete readiness callbacks. Hello mismatch prevents Signup; server-side checks
also reject old clients that omit this handshake. Existing UI errors distinguish
version mismatch, reload required, and unsupported content/interactions.

## Verification

- `qsanguosha_rules_identity_tests`: pure C++ identity and Hello/Signup wire gates.
- `web/tests/rules-identity.test.ts`: readiness/cancellation, same-count reorder,
  altered Lua digest, missing schemas, bridge mismatch and mixed artifact tests.
- `check-rules-bundle.py --self-test`: deployment fingerprint and HTTP allowlist.
- Remote WASM workflow: compile the production Worker and real server; compare
  native export to real WASM identity, reach active signup, verify missing/changed
  metadata rejection including reconnect, and preserve legacy TCP signup. A
  second real browser run keeps the old compiled Web loader while serving a
  changed, valid WASM binary with a freshly paired manifest; it must request reload
  before admission even though the bridge version is unchanged.

The remote browser gate requires every named case and writes
`rules-bundle-summary.json`; missing tools, timeout or missing evidence fails.
Fixture parity remains a separate gate. No local CTest or long gameplay gate is
required by this implementation; local focused checks do not establish real WASM,
browser or full repository CI acceptance.

The admission harness accepts `--server-ai-root` (default: repository root),
copies its `lua/ai/*.lua` and `lua/ai/isolated/*.lua` into its private server
deployment, and adds the repository's `lua/lib/middleclass.lua`. It does not copy
extensions, other libraries or AI runtime data. CI fetches the external runtime
into a separate build directory for this purpose. Native exports must be equal
before/after AI deployment and AI byte changes, while similarly named extra Lua
files must still fail export. These assertions precede the real server/Web gate.

### Server-only AI local acceptance (2026-09-08)

Validated the uncommitted server-only AI change on `debug` base `52d73b9` using
Qt 6.11.1 / Emscripten 4.0.7 and real Windows Chrome:

| Layer | Result |
|---|---|
| Targeted rebuild | Native server, native exporter and production WASM linked |
| Server-only content | AI deployment and changes to SmartAI/middleclass preserve identity; similarly named extra Lua rejects |
| Harness checks | W2 3 and shared HTTP/browser 6 passed; Vite `.js` MIME handling corrected |
| Actual browser admission | Native/WASM identities equal, Web signup active, legacy TCP accepted |
| Rejection paths | Eight invalid signup cases including reconnect, plus stale paired WASM rejected by old Web loader |
| Web artifacts | TypeScript and direct Vite build passed; build/public/dist runtime hashes matched |

No full gameplay or remote CI was run. The aggregate npm build stopped at its
existing stale translation artifact check; direct Vite output does not establish
that aggregate gate. Server startup also logged an external `inovation-ai.lua`
missing-field warning, so this admission result is not full AI gameplay acceptance.

### Original local implementation evidence (2026-09-08)

Checked in the W2 working tree based on PR31 `311a494`; these results are not
remote CI or production browser acceptance.

| Layer | Result |
|---|---|
| Targeted Windows Debug compile | Engine, server, native fixture runner and identity executable linked successfully |
| Focused C++ identity/wire executable | 60 checks passed; no CTest invocation |
| Web focused tests and TypeScript | 8 tests passed; application and browser probe type checks passed |
| Python harness self-tests | W2 2 checks and shared browser harness 6 checks passed |
| Actual native exports | 495 cards, 146 package registrations, 29 interactions; reordered cards and altered Lua produce distinct identities |
| Actual native unsupported closure | An extra hidden Lua file rejects export with `rules_content_unsupported` |
| Native/Web encoding | Web verifies all three actual native identity seals and rejects both changed content bundles |
| Production WASM/browser/server gate | Added to remote workflow; not run locally |
