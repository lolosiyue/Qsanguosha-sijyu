# Lua content packages

Packages place an extension's Lua code, translation scripts, AI and explicitly
owned media under one package root. `manifest.json` is a data manifest. The
engine API version is independent of the package's human release version.

## Manifest schema 1

```json
{
  "schema_version": 1,
  "id": "example-pack",
  "version": "0.1.0",
  "engine_api": 1,
  "dependencies": [],
  "extensions": [],
  "assets": {},
  "files": []
}
```

The extension array may be empty for a package whose content is compiled into
the engine; explicit media mappings may still belong to that package id.
Otherwise, extension array order is load order. Paths are package-relative, forward-slash
paths. Scripts and libraries must be under `lua/`; AI under `lua/ai/`;
translation scripts under `translation/`. Asset mappings are explicit
legacy path to package-relative path mappings under `image/` or `audio/`.
`files` is produced by `seal`; each record contains `path`, `role`, `size`, and
lowercase SHA-256 `sha256`. Supported roles are `rules`, `ai`, `presentation`,
and `data`.

Keep package source, manifests, and mapping templates tracked. Runtime media,
archives, and generated inventories are ignored by the repository patterns in
the root `.gitignore`; those patterns do not hide Lua or JSON manifests.

## Migration templates

The local code-only migration pilots are in
`tools/packages/migrations/pilot-standard.json` and
`tools/packages/migrations/pilot-sijyu.json`. Both use the non-release version
`0.0.0-local` and make no media ownership claims. See
[`docs/package-modularity.md`](../docs/package-modularity.md) for their scope
and the migration command.
