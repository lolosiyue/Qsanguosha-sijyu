# W2: server / WASM rules-bundle identity

This change adds a compatibility gate to the production Web rules path. It
is not an authentication protocol, a complete binary attestation, or proof
that a client submitted an honest selection. The server remains authoritative.

## Wire and bootstrap contract

`ServerConnectionContext::sendHello()` adds an optional `rules_bundle` member
to the existing schema-1 SERVER_HELLO (`CHECK_VERSION`) object. Version text,
card count, message ID, endpoints, command IDs and schema-2 SIGNUP stay
unchanged. Existing desktop/TUI typed readers may ignore the new member. An
unsupported profile is advertised as `available: false` with a diagnostic;
it does not stop the native server or prevent native clients from signing up.

The production WASM initialization JSON obtains `rules_bundle` from the same
C++ implementation. Web compares both identities before dispatching its first
rule query and checks again on preview consumption, further dispatch and
result publication. Missing/unsupported identity or any mismatch fails closed:
no fallback to matching version text, matching card count, old TypeScript rules,
or a cached successful preview. Retry cannot turn an incompatible pair into a
compatible pair. Login/transport are not an admission-control protocol here;
this is a gate on the WASM-backed interactions.

An available identity includes:

- Schema 1, protocol 2, bridge schema 1 and `qsan-client-rules-v1` ABI contract.
- `builtin-v1` content profile.
- Source/build-recipe SHA-256 from actual application C/C++ sources, headers,
  SWIG interfaces and CMake recipes, including uncommitted source edits.
- SWIG-interface/generator-version SHA-256.
- Exact bootstrap Lua bytes SHA-256.
- Ordered numeric card registry SHA-256 (ID, object/class, suit, number, package)
  and card count; the package list preserves its engine order.
- The supported `card-selection` interaction schema version.

The source hash excludes checkout paths, timestamps, git metadata and compiler
binary output. C/C++/CMake/SWIG text CRLF is normalized to LF; Lua content is not
normalized. The five builtin Lua files have explicit LF checkout attributes.
Custom compiler defines/toolchain changes are not independently attested by
this source hash. The supported build recipe and versioned ABI must be used;
a new ABI/interaction contract requires its version to change.

## No relabelling of a running Engine

`EngineBootstrap` captures the builtin closure immediately before and after
initialization and binds the resulting identity to that Engine. A direct Engine
construction which bypasses this seal reports unavailable, not guessed metadata.
Identity reads verify disk content and the actual registry against the sealed
values. Detected changes permanently invalidate that Engine's identity until
restart; restoring files or calling Web retry does not reseal it. Shutdown
clears the published identity.

This is a trusted, immutable-deployment contract. Files must not be updated
while a server is running; there is no per-frame identity broadcast to already
connected clients, arbitrary Lua mutation detector, hostile-filesystem race
sandbox or hot-reload protocol in W2. Deploy by restarting with a matching
bundle. W3/W7/W8 cover live-state ingestion, additional content profiles and
session recovery respectively.

## Scope of builtin-v1

The only admitted closure is the five bootstrap files already staged by the
native fixture harness. Extra files under Lua, extensions, language scripts or
custom/test scenario directories, symlinked content, missing files, and separate
user custom scenarios are rejected as unsupported. This deliberately errs on
rejecting an unverified deployment rather than calling a same-size card table
compatible. It does not claim that arbitrary extensions now run in the browser.
The native probe checks that this explicit profile and the existing fixture
stager still name exactly the same files; a changed closure requires an audit.

A normal development checkout with ignored extensions/language/AI files may
therefore advertise unsupported. Use the staged builtin profile for this gate.
W7 must add the actual production extension profiles; it must not remove this
check or label every unknown file builtin.

## Verification

With `QSAN_BUILD_RULES_SESSION_TESTS=ON`, build
`qsanguosha_rules_identity_probe`. It links the real engine, common identity
implementation and server connection/codec path without a GUI/TUI frontend.

```sh
python3 tests/client_runtime/check-rules-identity.py \
  --runner <build>/qsanguosha_rules_identity_probe \
  --asset-root . --artifacts <build>/rules-identity
ctest --test-dir <build> -R '^qsanguosha_rules_bundle_identity$' --output-on-failure
node --experimental-vm-modules --test tests/client_runtime/rules-controller.test.mjs
```

The native gate starts four isolated processes: two identical bootstraps with
different Qt hash initialization, a changed Lua file with the same card registry,
and extra unsupported content. It exercises real HELLO encoding/decoding, legacy
reader acceptance, live content mutation, sticky invalidation and shutdown.
Only private temporary staged assets are modified. It retains raw JSON/logs and
an `identity-summary.json`; missing or failed checks cannot produce PASS.

Controller tests use the real TypeScript comparator and controller with explicit
Worker/catalog doubles, plus the real LiveSession HELLO receiver with socket/state
doubles. They are not browser/native rule execution evidence. The existing W1
production native/Worker gate retains its full initialization JSON comparison,
now including the identity, as a separate real Qt/WASM acceptance requirement.

The extended WASM workflow builds/runs the identity probe explicitly. Ordinary
native CTest also registers it; Windows places it in the existing tests/<CONFIG>
artifact layout. Native/WASM/browser compilation and execution must be green
before this PR is marked ready. Nothing here establishes W3 live protocol state
ownership, W6 complete skill UI, W7 external-extension support or W9 release
completion.
