# Lua package modularity: source contracts and lifecycle

This document records the source interfaces for the first two package milestones.

## Phase boundaries

| Phase | Scope |
| --- | --- |
| 1 | Native desktop package catalog and Lua/media loading; directory/ZIP install, remove, update and rollback UI; safe migration tools |
| 2 | Import modular ZIPs and bundled packages into Android's existing snapshots; same-origin Web package loading; Browser Solo package distribution |
| 3 | Remote package index, PAD/CDN delivery, updater (not implemented) |
| 4 | Workshop (not implemented) |

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

## Platform differences and upgrade contract

Android catalog loading skips the filesystem inventory and the resource digests
while retaining manifest path/role checks and the required Lua files. Missing
presentation assets and media-only APK inventory changes do not block startup.
Desktop package verification and network rule identity stay strict, and ZIP path,
size and CRC validation are retained.

Baseline upgrades refresh the APK-owned [`lua/sanguosha.lua`](../lua/sanguosha.lua)
bootstrap atomically and publish `bootstrap_version=1` only after publication.
Retained `lang/*.lua` declarations are refreshed the same way through
`presentation_version=1`, including baselines that carry the same APK resource
revision. Existing snapshots, package versions, rule scripts and user translation
overrides are preserved.

Focused standard-library cases for the packaging tools live in
`tools/packages/tests/`; they cover declaration ordering, unresolved media,
explicit ownership, safe copy behavior and sealed hashes.

Client WASM artifacts must be published into `web/public/rules` **before** the
frontend's Vite build: the frontend pins that bundle's digest at build time, and
replacing only `web/dist/rules` leaves the loader bound to the older deployment
and correctly produces `rules_reload_required`.





