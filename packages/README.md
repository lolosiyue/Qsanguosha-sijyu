# Lua content packages

Packages group an extension's Lua, translations, AI and explicitly owned media
under one root. Start with the [package guide](../docs/package-modularity.md)
for the manifest schema, asset ownership rules and migration commands.

The implementation is in [`package_tool.py`](../tools/packages/package_tool.py):
`validate_manifest()` validates metadata, and `seal()` writes sizes and SHA-256
hashes for declared files. Keep source, manifests and mapping templates tracked;
repository ignore rules cover generated archives and runtime media.

## Migration templates

The code-only examples are [`pilot-standard.json`](../tools/packages/migrations/pilot-standard.json)
and [`pilot-sijyu.json`](../tools/packages/migrations/pilot-sijyu.json). They use
`0.0.0-local` and contain no media ownership mappings. Follow the guide before
preparing a release package.
