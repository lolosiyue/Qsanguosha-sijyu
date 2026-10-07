# LLM-assisted playable general authoring

This MVP adds **Tool → Author playable general ...** (`Alt+G`) to the existing
card editor. The feature branch is `feature/llm-general-authoring`, based on
current remote debug `548b5c45`, which contains the requested last-verified
`77926a85` baseline. The newer debug theme/home/table changes remain intact. It does not integrate
`feature/lua-state-convenience`, the separate unpublished updater, statistics,
or rewind changes. No card rendering, avatar cropping, image export or homepage
layout is redesigned.

## User workflow

1. Open the card editor's authoring action. The current name, title, kingdom,
   HP, lord flag and skill titles seed a separate document. The card editor has
   one shared skill-description surface, so its text initially goes in the first
   skill row. Assign each skill its own description before requesting code.
2. Choose an unused lowercase package ID, a general ID prefixed by that package
   ID, and skill IDs prefixed by the general ID. Set initial HP, armor, gender,
   display text and designer. The supported ranges are HP 1–20, armor 0–20,
   1–8 skills, with initial HP no greater than maximum HP.
3. Enter the provider's final **HTTPS chat-completions endpoint**, API key and
   model name. This is an OpenAI-compatible adapter, not a vendor account flow.
   No API call is made until **Preview request → Send request**.
4. Read the complete request preview. It includes the original specification,
   current specification, current reviewed code, diagnostics, correction
   instructions and a bounded engine contract. Artwork, source image paths,
   project paths and credentials are excluded from model content. The key is
   sent only in the HTTP Authorization header to the selected endpoint.
5. Read and edit the candidate and its comparison with the reviewed code.
   **Apply reviewed candidate** validates and updates only the authoring
   document. It never executes Lua, registers a general or installs a package.
6. Edit reviewed skills between the markers. Correction rounds include these
   exact manual edits and the original specification. A model can propose
   changing them; its proposal remains separate and requires explicit apply.
   Metadata, registration and translations belong to the specification and are
   assembled deterministically. Editing the metadata scaffold directly produces
   a diagnostic instead of silently changing the general.
7. Save/open JSON projects to retain reviewed code, metadata and the last 16
   checkpoints. **Undo version** and **Restore selected version** recover manual
   work. Incomplete metadata and syntactically invalid code can be kept as inert
   project text; requests and exports require the stricter validation.
8. **Export disabled package** writes a new, versioned staging directory outside
   any path component named `packages` or `extensions`. It refuses symlink
   ancestors and never replaces an existing package or general. Review and
   manually test in a disposable, isolated runtime before separately choosing
   the child package directory in the existing package manager. Installation
   must be an explicit user action. Gameplay bans do not prevent startup Lua
   loading and are not used as a safety mechanism.

The provider configuration and key are memory-only and disappear with the
authoring dialog. No OS credential store integration is shipped: the repository
has no credential-store dependency for this editor. The key field is masked and
uses sensitive-input hints. Nothing writes a key to QSettings. Known credentials
are excluded from prompts, history, projects, responses and exports, including
JSON-escaped content. Provider error bodies and URLs are not logged or shown as
diagnostics. A user should remove any credential pasted into authoring content;
such content is blocked from saving/sending rather than automatically rewritten.

## Implementation

| Component | Responsibility |
| --- | --- |
| `cardeditor.cpp/.h` | One action, read-only metadata/skill getters, collision snapshot and a capture of the existing card-image export surface. |
| `general-authoring-dialog.cpp/.h` | Structured form, skill-description editor, request preview, code/candidate/diff/history panes, save/open/export controls. Existing generic controller widget routing handles focus and cancel. |
| `general-authoring.cpp/.h` | Inert document, deterministic Lua scaffold, schema checks, bounded compile-only syntax check, declared API lint, revision IDs, history, JSON projects and atomic staging export. |
| `general-authoring-provider.cpp/.h` | Injectable asynchronous Qt transport; HTTPS, no redirects/cookies/cache, request/body/time bounds and generic redacted failures. |
| `src/dialog/authoring/context.json` | Embedded, bounded engine contract, exact selected SWIG signatures, helper source, actual extension callback excerpts and source hashes. |
| `tools/authoring/build_context.py` | Deterministic refresh from this checkout without evaluating Lua. |
| `tests/general-authoring` | Standalone native document/transport/UI mock regression harness. |

The MVP supports local `sgs.CreateTriggerSkillV2` definitions with name as their
first field. It deliberately has a bounded list of events, player/room/variant
methods, Lua functions and library functions. A complex active/view-as/general
selection skill may need APIs outside this contract; it should remain a manual
authoring task or receive a separately verified contract expansion. The context
is not a generic upstream Sanguosha prompt. It records the General constructor
from `swig/sanguosha.i`, V2 setup from `lua/sgs_ex.lua`, actual V2 callbacks from
`extensions/AIgeneral.lua`, and signatures from SWIG. The five convenience APIs
on the separate helper branch are not included.

### Schema contracts

The specification has exactly these fields:

```json
{
  "package_id": "diy_demo",
  "general_id": "diy_demo_hero",
  "display_name": "My general",
  "kingdom": "wei",
  "max_hp": 4,
  "start_hp": 4,
  "armor": 0,
  "male": true,
  "lord": false,
  "title": "",
  "designer": "",
  "skills": [
    {"id": "diy_demo_hero_skill", "title": "My skill",
     "description": "At the start of your turn, you may draw one card."}
  ]
}
```

IDs match `[a-z][a-z0-9_]{2,63}`. Package/general/skill namespaces must be distinct;
loaded engine general, skill and extension names plus active package IDs are
checked case-insensitively. Lord `$` notation is added by the scaffold to the
General constructor and does not enter filenames. Hidden/never-shown are false
in this MVP. Strings are emitted as UTF-8 Lua literals with escaped quotes,
backslashes and decimal byte escapes for controls. Translations cover the
package, general, display alias, title, designer, skill names and descriptions.

Requests use non-streaming `messages` with a system contract and a JSON user
content object. The provider must return one chat choice with
`finish_reason: "stop"`; its message content is exactly this JSON object:

```json
{
  "schema_version": 1,
  "skills_lua": "local diy_demo_hero_skill = sgs.CreateTriggerSkillV2 { name = \"diy_demo_hero_skill\", ... }",
  "notes": "Explanation or unsupported behavior"
}
```

The application constructs the actual complete package/general Lua around that
fragment. The response schema disallows additional fields. Code is at most
64 KiB; requests at most 128 KiB; provider bodies at most 512 KiB; requests have
60-second inactivity and total deadlines. Redirection is refused even when it
would stay on the same host. Endpoint user-info, query and fragment are refused.
No ambient cookies or response caching are used.

Project schema 1 is exactly `schema_version`, `original_spec`, `spec`,
`reviewed_code` and `history`; a history record has `spec` and `code`. Projects
are at most 2 MiB and 16 history records. Candidates and provider credentials
are not persisted. Only the current card-art snapshot can be included in an
export; projects preserve code and specifications, while artwork remains in the
existing card editor and its existing image files.

### Cancellation, revisions and manual work

Each request captures a serial ID and the current document revision. Another
request supersedes it; cancellation invalidates it and aborts transport. A
metadata/reviewed-code edit during the request invalidates the result. A
completed candidate is also invalidated by later document edits. Apply checks
this revision again. Replies never replace the reviewed buffer automatically.
The provider callback is tied to its QObject lifetime, and closing/importing a
project cancels pending work. The mock harness covers repeated, cancelled and
out-of-order requests and manual edits.

### Validation limits and execution boundary

The syntax checker uses a new Lua state with an 8 MiB allocator cap. It calls
`luaL_loadbufferx(..., "t")` only, opens no libraries and closes the state without
calling Lua. Text-only mode rejects bytecode. No engine state or engine userdata
is exposed. Input is bounded by the code-size limit. Opening/importing/applying/
exporting a project has no Lua execution path.

Lint checks the fixed metadata scaffold, expected local skill declarations,
constructor/name counts, selected engine globals/methods and permitted function
names. It reports unsupported host-loading or dynamic lookup syntax. This is
lexical lint, **not** a security sandbox, full type checker or proof of gameplay
correctness. For example, a top-level infinite loop compiles successfully; the
regression test proves it is never evaluated by this editor. Lint cannot prove
that callbacks use the right event actor/data or that a method is called on the
right object, and it is not designed to detect every obfuscated Lua program.

No secure runtime verification sandbox is supplied, and no generated code is
executed in the main application as a verification step. The manual QA guide
requires a disposable OS-isolated environment before any runtime testing of
untrusted outputs. Static success is always accompanied by a visible limitation
message.

### Export layout and rollback

```text
<chosen-staging>/diy_demo-disabled-<uuid>/
  DISABLED.txt
  authoring.json
  diy_demo/
    manifest.json
    lua/diy_demo.lua
    image/diy/diy_demo_hero.png   # optional existing card-image snapshot
```

The child package follows schema 1/engine_api 1 with ordered extension metadata,
empty optional AI/language/library dependencies, sizes and SHA-256 inventory.
The designer-card alias `image/diy/<general>.png` explicitly maps to the same
package-relative image path. It is not treated as an avatar crop; existing avatar
and image export behavior stays intact. Translations are inline in Lua. The
project and disabled marker are siblings of the installable child package, so
neither becomes an undeclared package inventory file.

Writes first stage under a unique temporary directory; a successful export
renames it to a fresh UUID destination. Earlier exports remain available for
rollback and are never overwritten. A failed staged write removes its private
staging tree. Runtime package installation/rollback remains the existing package
manager's explicit next-start lifecycle and is not triggered by this editor.

## Verification in this task

- Focused standalone build and CTest on Qt 6.8.2: passed.
- Native package-catalog validation accepts the exported manifest and sealed files.
- The same suite with UndefinedBehaviorSanitizer and float-cast-overflow: passed.
- Focused `cardeditor.cpp` source compatibility compilation on Qt 6.8.2: passed,
  using QML/video/spine-disabled compile flags for that isolated source check.
- Simplified Chinese TS compiled with `lrelease`; existing unrelated unfinished
  translations were retained.
- Production GUI configure correctly rejects Qt 6.8.2 because Qt 6.11 is
  required. No requirement was lowered. No full app link, desktop/gamepad
  acceptance, isolated gameplay run or paid provider request is claimed.

See [the Qt 6.11 QA guide](llm-general-authoring-qt611-qa.md) for the remaining
application acceptance work and [the test README](../tests/general-authoring/README.md)
for the focused harness.
