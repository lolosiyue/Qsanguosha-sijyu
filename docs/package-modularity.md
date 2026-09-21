# Lua package modularity: source contracts and lifecycle

This document records the source interfaces for the first two package
milestones. It does not claim that native installation, Android import, Web
runtime execution, or a complete game has passed acceptance.

## Phase boundaries

| Phase | Scope | Evidence boundary |
| --- | --- | --- |
| 1 | Native desktop package catalog and Lua/media loading; directory/ZIP install, remove, update and rollback UI; safe migration tools | Source implementation and written migration contracts do not prove GUI acceptance or successful rollback. |
| 2 | Import modular ZIPs and bundled packages into Android's existing snapshots; same-origin Web package loading; Browser Solo package distribution | Packaging structure and local URLs do not prove Android import, browser gameplay, or a clean full game. |
| 3 | Remote package index, PAD/CDN delivery, updater | Not implemented here. |
| 4 | Workshop | Not implemented here. |

Phase 1 uses `manifest.json` as the package metadata filename. Phase 2's
Browser Solo package descriptor is carried as `content` in the Solo runtime
manifest. Existing `declared-v2` runtime content remains readable when the
supplied asset root has no modular packages.

## Package manifest schema 1

The package root has a `manifest.json` plus package-relative files. Its
required metadata is `schema_version: 1`, a lowercase `id` matching
`[a-z0-9][a-z0-9_-]*`, a non-empty `version`, `engine_api: 1`, package
`dependencies`, and an ordered `extensions` array. The array may be empty when
the package contains engine-compiled extensions and explicitly owned media.
Each extension entry has a
name, package-relative `script`, extension dependencies, and `libs`, `lang`,
and `ai` arrays. Scripts and libraries live under `lua/`, AI under `lua/ai/`,
and translation scripts under `translation/`. Extension array order
is meaningful and must be retained.

`assets` maps a legacy `image/...` or `audio/...` path to its new package-
relative path. The migration mapping JSON's optional `files` object maps
legacy source paths to the package-relative destinations declared in the
manifest. `files` in `manifest.json` is a sealed inventory of every declared script,
library, translation, AI script, and asset. Each record contains `path`,
`role` (`rules`, `ai`, `presentation`, or `data`), byte `size`, and lowercase
SHA-256 `sha256`. Media ownership is an explicit migration decision; the
scanner reports media as unresolved until a human mapping assigns it.

## Runtime paths and activation

| Interface | Behavior |
| --- | --- |
| `package://sijyu/general/hero.png` | Resolves to the active package's `image/general/hero.png`. |
| `package://sijyu/audio/skill.ogg` | Resolves inside the active package's `audio/` directory. |
| Legacy `image/...` / `audio/...` | Uses an explicitly declared package alias when present, otherwise the existing runtime path. A missing mapped file reports an error instead of silently loading another version. |
| `sgs.PackagePath(uri)` | Returns the validated local file path, or `nil, error` when unavailable. |
| `sgs.PackageDataPath(id, relative)` | Returns a path under user data storage, separate from the immutable package inventory. Removing a package retains this user data. |
| `sgs.RequirePackage(id, module)` | Loads a declared package Lua module; same-name helpers in different packages remain separate. |

Desktop **Packages → Manage packages** stages a directory or ZIP install,
update, removal, or restore of the previous version. Activation occurs at the
next start. The store validates the complete dependency set before activation;
an invalid pending set leaves the active set available and records a visible
error. Interrupted activation uses the durable transaction journal and previous
tree. A failed Lua boot after activation triggers previous-version recovery on
the next start. This is recovery storage, not a historical-version selector.

Package declarations replace a same-name legacy extension in its original
registration position. New entries are appended in dependency order. Package
Lua, translations and AI declarations travel with the package; core engine
code remains compiled into the engine. The existing external extension Git
repository remains authoritative: pilot migration copies are local artifacts,
not a second tracked source of extension code.

Web resolves images through the verified runtime descriptor and local
`/packages/<id>/...` URLs. The existing Web client has no audio playback consumer;
this change supplies audio URL resolution without claiming browser audio playback.
Android keeps its existing snapshot lifecycle and adds a modular-package ZIP
import choice. Remote delivery and automatic downloads remain later phases.

Only code, manifests, mappings and tooling belong in the public repository.
Package image/audio directories are ignored; the pilot mappings include no
media and no automatic media download source. A file hash checks integrity;
it does not establish permission to redistribute that file.

## Native image cache lifetime

The skin image cache checks its source-reference key before resolving paths or
probing normal/`@2x` files. The key includes the runtime root, skin load revision
and `QSanPackages::catalogRevision()`. Installing or clearing the catalog advances
that revision; a skin load advances the skin revision, including partial loads.
Only cache misses resolve and validate package paths. The shared skin cache does
not retain failed resolutions. Replacing assets in place requires a skin reload
or catalog replacement;
the paint path does not poll the filesystem for edits.

Card items retain only their current face, suit and number pixmaps. They check
live card identity and the same root/revision boundaries on repaint, while
general cards continue to resolve per-general hero-skin selection. Footnote
images are reused while text, image size and skin revision are unchanged.
These are source contracts; runtime performance and visual equivalence still
require validation of the rebuilt client.

## Safe migration CLI

[`tools/packages/package_tool.py`](../tools/packages/package_tool.py) uses only Python's standard library. It reads
Lua declarations as literal text, reads optional `runtime-content.json` with a
JSON parser, and never evaluates or imports Lua. Commands are:

```text
python tools/packages/package_tool.py inspect <legacy-root>
python tools/packages/package_tool.py migrate <legacy-root> <mapping.json> <new-package-root>
python tools/packages/package_tool.py seal <package-root>
```

`inspect` preserves the order of literal `extension_names` entries and emits
missing declarations plus unresolved `image/` and `audio/` paths as JSON.
`migrate` requires a mapping object with a `manifest`, optional explicit
source-to-target `files`, and `asset_owners` mapping every asset source path to
its owning extension name. For an engine-compiled, extension-free package, the package
id itself is the explicit asset owner. It verifies every source and destination path, refuses
symlinks/reparse points and case-insensitive target collisions, writes only to
a separate new or empty destination, and leaves the legacy tree untouched.
`seal` recalculates file sizes and SHA-256 hashes from package bytes.

### Standard migration mapping example

This code-only native-package mapping is usable as a starting point. It makes
no asset claims: list media only after verifying its ownership, then map every
listed legacy path to a package-relative path and set its owner to `standard`.

```json
{
  "unresolved": ["No image/audio paths are included until ownership is verified."],
  "manifest": {
    "schema_version": 1,
    "id": "standard",
    "version": "0.0.0-local",
    "engine_api": 1,
    "dependencies": [],
    "extensions": [],
    "assets": {}
  },
  "files": {},
  "asset_owners": {}
}
```

### Sijyu migration mapping example

This source mapping uses the literal `sijyu` and AI declarations from the
legacy configuration. It intentionally maps no image/audio files; add those
only after ownership is confirmed. Set package dependency edges from the release
inventory before treating it as a release manifest. The code-only pilot map
explicitly maps the AI source to itself.

```json
{
  "unresolved": ["Version, package dependency edges, and all media ownership remain to be verified."],
  "manifest": {
    "schema_version": 1,
    "id": "sijyu",
    "version": "0.0.0-local",
    "engine_api": 1,
    "dependencies": [],
    "extensions": [{
      "name": "sijyu",
      "script": "lua/sijyu.lua",
      "dependencies": [],
      "libs": [],
      "lang": [],
      "ai": ["lua/ai/sijyu-ai.lua"]
    }],
    "assets": {}
  },
  "files": {"extensions/sijyu.lua": "lua/sijyu.lua",
            "lua/ai/sijyu-ai.lua": "lua/ai/sijyu-ai.lua"},
  "asset_owners": {}
}
```

These examples contain mapping metadata only; they do not include copied
extension code or copyrighted media. The standard example leaves media
unresolved, and the sijyu example leaves its media and release metadata open.
The code-only pilot maps are [`pilot-standard.json`](../tools/packages/migrations/pilot-standard.json)
and [`pilot-sijyu.json`](../tools/packages/migrations/pilot-sijyu.json). Their
`0.0.0-local` version is for isolated migration experiments and is not a
release version. Use the actual legacy root and a separate new output path:

```text
python tools/packages/package_tool.py migrate <legacy-root> tools/packages/migrations/pilot-standard.json <new-standard-package>
python tools/packages/package_tool.py migrate <legacy-root> tools/packages/migrations/pilot-sijyu.json <new-sijyu-package>
```

## Web and Browser Solo publication

When `--asset-root` contains `packages/<id>/manifest.json`,
[`tools/package-web-solo.py`](../tools/package-web-solo.py) validates each listed file's size and hash, then
copies the manifest and listed files into the offline output while preserving
the package-relative tree. The emitted Solo manifest uses:

```json
{
  "content": {
    "schema_version": 3,
    "profile": "packages-v1",
    "runtime_content": {
      "schema_version": 3,
      "profile": "packages-v1",
      "extensions": [{"name": "example", "script": "packages/example-pack/lua/example.lua",
        "dependencies": [], "libs": [], "lang": [], "ai": []}],
      "packages": [
        {"id": "example-pack", "version": "0.1.0", "dependencies": [],
         "assets": {"image/example/icon.png": "image/example/icon.png"}}
      ]
    },
    "files": [
      {"path": "packages/example-pack/lua/example.lua", "role": "rules",
       "size": 12, "sha256": "..."}
    ]
  }
}
```

The assets object maps legacy aliases to package-relative media paths; the
files array contains package Lua records. Web requests use local same-origin
`/packages/...` URLs. Vite serves `/packages/...` only from
`<runtimeRoot>/packages`; `QSAN_RUNTIME_ROOT` selects the runtime root, with
the repository root as the development default. The middleware checks decoded
paths and resolved file containment, and returns media types for supported
image/audio extensions. It does not contact a CDN. If no modular package root
is present, the packager retains the existing schema 2 `declared-v2` content
manifest for compatibility.

When local packages exist, full Solo packaging requires a freshly exported
`packages-v1` rules bundle with matching package metadata and extension
declarations. The packager preserves the exported rules closure; it does not
rewrite a v2 rules identity into v3. Optional legacy image/audio directories
may be absent for a package deployment. Binary assets are served over HTTP;
the Web rules virtual filesystem receives declared Lua content only.

## Source tests

Focused standard-library test cases are in `tools/packages/tests/`. They cover
declaration ordering, unresolved media, explicit ownership, safe copy behavior,
and sealed hashes. Their existence is not a report that they were executed;
run/build gates are recorded by the coordinating phase checkpoint.

## Phase 2 checkpoint validation — 2026-09-16

Implementation and validation were performed in the isolated
`codex/package-modularity-phase2` worktree, based on
`2759c82413701c4eeb998bfcfe684f2cf1624f49`. Builds and executable tests started
only after the phase 2 source checkpoint was declared complete.

| Gate | Result and scope |
| --- | --- |
| Windows native build | PASS: VS 2026 / Qt 6.11.1 Debug engine, `QSanguosha`, and six focused test targets. Existing optional Qt TaskTree and FreeType PDB warnings did not block the build. |
| Native focused executables | PASS: package catalog, package store, package runtime, rules content manifest (43 checks), Android content store, and production Lua package loader. Each invocation finished within 60 seconds; final Android run took about 22 seconds. |
| Web | PASS: TypeScript checking, three focused test files / 23 tests, and Vite production build. |
| Python | PASS: 15 migration/packaging tests, including a complete synthetic Solo packaging fixture. Synthetic WASM bytes test packaging only; no WASM execution is claimed. |
| Local HTTP | PASS: served the real migrated sijyu manifest/Lua bytes through `/packages`; encoded traversal returned HTTP 403. The temporary server was stopped. |
| Code-only migration | PASS: standard empty native-package manifest and sijyu Lua/AI pilot generated under ignored artifacts; native catalog accepted both. Copied sijyu source/AI SHA-256 values matched the original files. |
| Static checks | PASS: `git diff --check`; package Lua/manifests remain trackable, image/audio paths are ignored. |
| Native GUI interaction | NOT RUN; executable built only. |
| Android APK/device and WASM runtime | NOT RUN; Android store was tested as a desktop focused executable. |
| Full game, local CTest, remote CI | NOT RUN. |

Local evidence is under `artifacts/package-validation/`: `native-results.json`,
per-target logs, native build logs, `web-validation.log`, `python-tests.log`,
`pilot-hashes.json`, and `probe_http.log`. These generated artifacts are ignored.
The changes are uncommitted; no merge or push was performed. No files in the
original QSanguosha worktree or the authoritative external extension repository
were modified by this work.

## Device acceptance follow-up — 2026-09-16

The earlier checkpoint table is a historical result. Subsequent Android and
Browser Solo acceptance uses the same isolated branch and records each attempt
under `artifacts/package-device-acceptance-20260916/`. Final gate outcomes and
cleanup evidence are in that directory's `summary.md`; builds, startup, gameplay,
and clean shutdown remain separate gates.

The follow-up fixes same-ID Android package migration, lazy package Lua preload,
and the Solo package AI closure. The ZIP reader now bounds retries for a source
that temporarily returns no bytes and recognizes a fully consumed fixed-length
source even when its EOF flag lags. Focused tests cover both cases. A device ZIP
whose digest differed from the host was replaced with a verified copy; this is
separate from the reader robustness change.

The external `scarlet.lua` description skill `s4_txbw_general_duel_rule` is now
registered before it is attached. Unknown-skill validation remains enabled.
Only that Lua file was synchronized to the authoritative extension repository;
its other pre-existing changes were preserved. No commit or push was made.

After replacing client WASM artifacts, publish their bundle into
`web/public/rules` **before** running Vite: the frontend pins that bundle's digest
at build time. Replacing only `web/dist/rules` leaves the old loader bound to an
older deployment and correctly produces `rules_reload_required`.

Local media and migrated pilot Lua are test inputs in ignored runtime/artifact
directories. They are not source-control deliverables. Device coverage uses an
API 33 x86_64 emulator with ARM translation and 4 KB pages; it does not establish
physical arm64 or 16 KB runtime compatibility.

Final follow-up outcome: native/APK/WASM builds and focused regressions passed.
Browser Solo completed one natural `03_1v2` game with `lord+loyalist` winning,
then returned home and prepared again. Complete UI timeout/fallback coverage and
normal launcher exit remain unverified. Android imported and activated the full
verified media ZIP, but the standard-audio APK crashed in AudioTrack before game
start (`SIGSEGV`, `__cfi_slowpath` / `maybeCallDataCallback`). Android online is
failed and same-process Solo is blocked; no audio-engine fix or repeated game
was attempted under the two-issue authorization. The paired server shut down
normally with exit 0. See the per-attempt evidence for cleanup and limitations.


## Debug integration — 2026-09-16

The isolated package branch is fast-forwarded to debug `49c4213` and retains
phase 1–2 changes. Android inherits the NULL audio backend and software rendering
workaround. Audio remains temporarily muted; this does not repair the audio engine.

Android catalog loading skips filesystem inventory and resource digests, while
retaining manifest path/role checks and required Lua files. Missing presentation
assets and media-only APK inventory changes do not block startup. Desktop package
verification and network rule identity remain strict. ZIP path, size, and CRC
validation are retained. Integration evidence is under
`artifacts/package-debug-integration-20260916/`; earlier device results are historical.

The first upgrade attempt exposed a stale APK-owned [`lua/sanguosha.lua`](../lua/sanguosha.lua): the
old loader rejected newly installed modular paths. Baseline upgrades now refresh
that bootstrap atomically and mark `bootstrap_version=1` only after publication.
The constant metadata marker also repairs an already migrated baseline carrying
the same APK resource revision. Existing snapshots and user extension overrides
remain preserved. Focused upgrade fixtures cover both revision cases.

A separate retained-declaration gap is still open: the upgraded private runtime
contains `lang/zh_CN/Audio/MaotuPackageLines.lua` without a corresponding retained
Lua declaration. The strict rules identity reports `rules_content_unsupported`;
this is not a missing-image or media-hash launch gate. The extra migration repair
and connected retest require the separately requested bounded follow-up.

The final integration HEAD is `31a9e81` (the additional commit changes documents
only; compiled code remains based on `49c4213` plus the isolated modifications).
Native and Android incremental builds, package catalog checks, Android content
store focused checks, and Web type checking passed. The repaired APK reached the
home screen with retained media and both pilot packages active.

One Android same-process `03_1v2` game reached the natural result screen at
10:27, with the rebel/farmer side winning. Trustee was active, so manual UI
acceptance is not established. The process then aborted during RoomRuntime
shutdown with non-zero Card lifetime gauges; this is a failed clean-exit gate,
not a full-game acceptance pass. A preceding Scudo size-class exhaustion warning
is retained as evidence, without attributing its cause. No second Solo game or
native-lifetime repair was attempted. Test settings were restored, host servers
exited normally, and the existing emulator was left running. Connected gameplay
remains blocked by the declaration gap above. WASM was not rebuilt or rerun after
this debug integration, and physical-device coverage remains untested.

## Retained translation repair and WASM rerun — 2026-09-16

The retained-declaration gap above is repaired. APK-owned `lang/*.lua` declarations
are refreshed through an atomic presentation migration (`presentation_version=1`),
including existing baselines with the same APK revision. Rule scripts, dependencies,
package versions, existing snapshots, and user translation overrides remain pinned.
New APK translations in an overridden package resolve through the APK presentation
baseline. No media inventory scan or resource hashing is added. Regression fixtures
cover translation overrides, immutable snapshots, and legacy-to-modular migration.

Native, APK, client WASM, and Solo WASM builds succeeded with matching C++/binding/
protocol fingerprints. The Android content-store focused executable passed in
57.14 seconds. Web protocol/translation checks, TypeScript checking, production
build, and Solo packaging passed. No local CTest was run.

The repaired device snapshot declared the formerly missing translation, retained
media readiness, and activated both pilot packages. Its paired server produced a
valid `declared-v2` ServerHello. Android gameplay remains **BLOCKED**: another task
replaced the shared emulator APK at 06:11:18 UTC, after this test launched at
06:11:06 UTC. The user chose to defer Android and finish WASM. The host server
exited normally (0); its watcher and reverse mapping were cleaned up. No further
Android installation or game was attempted.

One Browser Solo `03_1v2` game ran from 06:21:01 UTC to the observed natural result
at 06:28:31 UTC. The UI reported `lord+loyalist` winning (Zhao Yun as lord); the
human Lu Xun seat was a rebel. Manual UI covered general selection, card reveal,
Jink response, Peach, Iron Chain recast, equipment, discard, and dying responses.
Neither trustee nor surrender was clicked. Timeout/fallback coverage was not
instrumented, and the raw GAME_OVER packet was not separately captured. The UI
returned home and prepared again without starting a second game; captured console
warnings/errors were empty. This establishes the observed natural-result and
home/reprepare gates, not comprehensive manual-UI or engine-shutdown acceptance.

The created browser tab was closed. The local launcher remained running and was
explicitly stopped after verifying its PID/path; normal launcher exit is therefore
not proven. Test ports 9529 and 19542–19544 were released. Card lifetime was not
modified during this follow-up, as requested; its existing debug-branch issue and
Worker clean-shutdown gate remain outside this repair. Evidence, source hashes,
and separate gate results are in `artifacts/package-followup-20260916/summary.md`.
No commit or push was made; local media remains ignored.
