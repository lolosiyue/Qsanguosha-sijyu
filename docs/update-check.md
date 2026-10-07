# Homepage update check

The existing **Check for updates** button opens a desktop dialog. Opening that
dialog checks the newest 100 GitHub Releases and, when configured, a public
material catalog. The game does not start an update check or asset download at
startup. Checking catalogs never downloads a package; the player selects one
update and explicitly chooses **Download selected**.

## Private configuration and the required R2 catalog

Enter the full public HTTPS **manifest object URL** in the dialog, or set
`Updates/MaterialManifestUrl` in the player's runtime settings. A bucket base
URL alone does not identify an object. No production hostname, credentials,
bucket list operation or write operation is built into this feature. Keep
production URLs in private runtime configuration outside the repository,
examples, QA artifacts and release defaults.

The URL must return UTF-8 JSON using this schema (the URL, size and hash below
are illustrative and must be replaced with the actual public object metadata):

```json
{
  "schema_version": 1,
  "packages": [
    {
      "id": "sijyu-art-001",
      "version": "1.2.0",
      "game_version": "20251231",
      "name": "Sijyu portraits, shard 1",
      "notes": "Updated portraits for this shard.",
      "url": "https://assets.example.invalid/sijyu-art-001-1.2.0.zip",
      "size": 1048576,
      "sha256": "0000000000000000000000000000000000000000000000000000000000000000"
    }
  ]
}
```

`schema_version` and `packages` are required. Every entry requires `id`,
`version`, `game_version`, `url`, `size` and `sha256`; `name` and `notes` are
optional plain text. IDs must match `[a-z0-9][a-z0-9_-]{0,127}`, be unique and
must not be `core`. Versions are strict `YYYYMMDD` dates or semantic versions
such as `1.2.0` / `1.2.0-rc.1` (an optional leading `v` is accepted). Different
version families are not ordered. `game_version` must equal the running game's
`QSanVersion::Number` exactly. `size` is a positive integer describing the ZIP's
bytes, and `sha256` is its 64 lowercase hexadecimal SHA-256 digest.

The catalog is limited to 4 MiB / 1,000 entries; names to 180 characters and
notes to 65,536 characters. Endpoints and every redirect require HTTPS, no URL
username/password or fragment, and the default port or port 443.

Each ZIP must be an existing modular **PackageStore package**, containing a
sealed `manifest.json` and the exact declared file inventory (see
[package modularity](package-modularity.md)). The catalog's ID/version must
match the ZIP manifest. Raw loose-object key lists and the local
`assets-manifest.json` presence report are not online catalogs and cannot be
installed by this dialog. Use the existing package tools to prepare asset
packages with explicit asset ownership/mapping; the client never writes to R2.

## Generate a catalog locally before publishing

The R2 library currently has no version manifest. The local generator reads an
explicit directory of already prepared PackageStore ZIPs, streams archive/file
hash checks and atomically writes a catalog. It performs no network calls and
has no upload function:

```sh
python3 tools/generate-update-manifest.py --help
python3 tools/generate-update-manifest.py --input-dir /path/to/package-zips --game-version 20251231 --base-url https://assets.example.invalid/packages/ --output /path/outside/repository/material-manifest.json
```

Use the actual `QSanVersion::Number` of the target game. Each package must have
an independently increasing version, a valid package manifest, explicit asset
mappings and an exact hashed file inventory. The generator rejects loose files,
unsafe archives, mismatched hashes and duplicate IDs. It does not infer an asset
mapping from filenames. If the library is loose image/audio objects, first use
the existing package tools and [package modularity](package-modularity.md) to
prepare bounded, independently versioned ZIP shards from an authorized local
copy. Do not fetch the full remote library just to generate this catalog.
A local preparation sequence, after reviewing the explicit mapping/ownership
file, is:

```sh
python3 tools/packages/package_tool.py migrate /authorized/local/assets /private/shard-mapping.json /private/material-shard
python3 tools/packages/package_tool.py seal /private/material-shard
python3 -m zipfile -c /private/package-zips/material-shard-1.0.0.zip /private/material-shard
```

Repeat with distinct package IDs and bounded shards. These paths are examples;
the input source and output directories must be chosen explicitly. The catalog
generator then verifies the archive limits and all sealed hashes before writing
the catalog. It does not generate or approve the ownership mapping.

Use a private HTTPS base URL when generating the real file, keep the generated
file outside the repository and confirm its object paths match the intended
public package keys. Publishing the ZIPs and catalog is a separate authorized
operation. No objects have been uploaded or changed by this feature. After that
operation, supply the exact public URL of the catalog object in private runtime
settings; the bucket base URL cannot substitute for the catalog object URL.

## Selective downloads and installation limits

Split large libraries into independently versioned packages. Installed versions
that are equal/newer and packages with pending changes cannot be downloaded.
Only the selected package is fetched. Completed files are cached under the
player's `update-downloads` directory by SHA-256; cache bytes are reverified
before reuse. Partial files resume with HTTP Range when supported; a server
returning HTTP 200 instead safely restarts that individual file. Size, range,
encoding and SHA-256 mismatches fail safely. Cancellation/offline interruptions
keep bounded partial files for retry; corrupt bytes are discarded. Cached
downloads are not automatically removed, so players may delete them when idle.

Existing installer limits remain unchanged: 256 MiB ZIP, 512 MiB expanded per
staged package, 128 MiB per member, 50,000 entries and 200:1 compression ratio.
Package manifests are additionally bounded to 16 MiB. ZIP traversal, symlinks,
collisions and inventory/hash errors are rejected by the existing reader and
catalog. Cancelling extraction prevents staging; once the existing atomic
staging operation begins, it finishes rather than interrupting publication.

The installed set defaults to **8 GiB / 200,000 entries**, including package
manifests. This supports a 3–4 GB library split into the bounded packages above.
A private `package-store/limits.json` under the player's user-data directory can
set a bounded aggregate quota and a free-space reserve:

```json
{"schema_version":1,"max_installed_mib":8192,"reserve_free_mib":512}
```

`max_installed_mib` accepts integers from 512 to 32768; `reserve_free_mib`
accepts 512 to 4096. Invalid, oversized or symlinked configuration fails staging
safely. The per-package/archive/member limits are unchanged. Sparse files count
by logical length, so sparse allocation cannot bypass the aggregate quota.

Before extraction, copying, validation or rollback, metadata-only preflight
checks the effective installed set and available space on both user-data and
runtime filesystems. It conservatively includes per-entry allocation padding,
bounded workspace/journal overhead, validation copies, extracted/incoming trees,
next-start transaction copies and
changed-package backups. Existing active, pending, cache and previous trees
already consume measured free space. Shared devices combine the restart peaks;
separate devices each retain the configured reserve. Downloads also require
space for their full declared size plus a 512 MiB reserve, even when resuming.

The current PackageStore transaction still copies the complete active library
for effective-set validation and again for restart activation. This is bounded
local disk I/O, not a full network download: SHA verification and copying use
256 KiB buffers, not GB-sized allocations. A large library therefore needs
roughly another full-library copy's free space, changed-package rollback space
and the reserve; extraction and staging can require additional package copies.
Concurrent disk consumers can exhaust space after preflight; write errors abort
without promoting the new tree. A startup resource failure preserves the pending
journal, reports its error in Package manager and boots the previous active set.

Successful material installation stages content in the existing PackageStore;
active assets do not change until the normal next-start transaction. The normal
boot-attempt recovery and Package manager restore flow remain available. No
running executable is replaced.

## Game releases and build compatibility

Stable releases are the default; **Include prereleases** is explicit. Drafts
are ignored. Tags must be comparable to the current game version using the
same version grammar; older tags are excluded. Same-version releases require
the same commit ancestry proof as higher-version releases. Unknown or
incompatible version formats fall back to the project release page.

The build records its Git commit and dirty status before each GUI build. A
verified game download requires a clean, identified build, an explicit matching
platform/architecture asset, an HTTPS repository download URL, a declared size
up to 2 GiB and GitHub's `sha256:` asset digest. Before the download, GitHub's
compare endpoint must report the release tag **ahead of** the exact current
commit. Behind, identical, diverged, offline, rate-limited, malformed and unknown
results never authorize a binary download. Dirty development builds use manual
release review because their local changes cannot be ordered against a tag.

Supported download labels follow current packaging: Linux x86_64 `.AppImage`
or `linux-x86_64.tar.zst`, Windows x64 `windows`/`win64` ZIP/MSI/EXE. XP, server,
TUI, mismatched architectures, missing digests and unsupported platforms use
manual release review. Platform labels alone cannot guarantee OS, runtime or
driver compatibility. Android provides the explicit project release-page flow;
online material staging in this dialog is desktop-only.

Game binaries are saved as verified downloads. The dialog explains the exact
path and provides **Open download folder**. The player must exit, then manually
install/extract into a separate location and restart using their platform tools.
The updater never runs a downloaded installer, unpacks game archives or
overwrites an executable. Therefore no application archive extraction is part
of the trusted install path.

## Focused offline validation

Build the standalone focused targets with a Qt development kit and zlib:

```sh
cmake -S tests/updates -B /tmp/qsan-update-tests -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build /tmp/qsan-update-tests --parallel 3
QT_QPA_PLATFORM=offscreen ctest --test-dir /tmp/qsan-update-tests --output-on-failure
python3 tools/packages/tests/test_package_tool.py
python3 tools/packages/tests/test_package_web_solo.py
python3 -m unittest tools.packages.tests.test_generate_update_manifest
```

These tests use fake HTTPS replies and tiny generated ZIP fixtures. They cover
catalog/version/channel/platform gates, response bounds, HTTPS redirects,
range restart/resume, hash errors, offline partial preservation, keyboard
cancellation, opt-in downloads, restart staging, archive/identity rejection and
rollback, aggregate quotas, overflow and same/split-volume free-space boundaries.
Metadata and sparse fixtures cover GB-scale bounds without fetching a library.
No production assets, bucket writes, paid calls or real installers
are used. Standalone widget tests can run with Qt 6.8; the full application
continues to require Qt 6.11 and needs validation with that official GUI kit.


## Official Qt 6.11.1 GUI QA handoff

Run on the intended desktop using the official Qt 6.11.1 kit; do not lower the
application's Qt minimum. Point `CMAKE_PREFIX_PATH` at that kit and build both
GUI and focused tests in separate local build directories:

```sh
cmake -S . -B build-update-gui -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/platform -DQSAN_BUILD_GUI=ON -DQSAN_BUILD_SERVER=OFF -DQSAN_BUILD_TUI=OFF -DQSAN_AUDIO_BACKEND=QT
cmake --build build-update-gui --parallel 3
cmake -S tests/updates -B build-update-tests -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/platform
cmake --build build-update-tests --parallel 3
ctest --test-dir build-update-tests --output-on-failure
```

Use a temporary user-data profile and the repository's normal runtime-assets
setup; launch the built GUI from its supported runtime directory. Confirm that
opening the homepage alone generates no update requests. Navigate to **檢查更新**
with keyboard and controller, activate it, and verify the existing homepage
layout is unchanged. Verify Simplified Chinese dialog labels and current game,
commit and platform. Dirty builds should clearly use manual release review.
Tab/controller focus must reach the list, notes, URL field, buttons and cancel;
Escape while busy cancels and preserves partial downloads.

The focused dialog test injects fake HTTPS replies for manifest, release,
ancestry, rate-limit, staging and keyboard cases; it never downloads production
assets. A gamepad device and the full MainWindow/controller routing still need
manual verification in the official desktop GUI. On Windows repeat the focused
transfer tests to exercise reparse/hard-link rejection and verify folder-opening
behavior, without executing a downloaded installer.

A live material check remains unavailable until package archives and a generated
manifest have been deliberately published and its exact public object URL is
provided in private runtime settings. Parser/mock success and bucket connectivity
are separate evidence. Never upload QA fixtures or alter the production bucket
as part of these checks.
