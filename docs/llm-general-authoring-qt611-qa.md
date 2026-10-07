# Qt 6.11 authoring acceptance guide

## Status and prerequisites

The cloud regression harness passed on Qt 6.8.2, including offscreen widget
checks and mocked provider calls; a sanitizer run also passed. A source-only
compatibility compile of the card editor passed with QML/video/spine disabled.
These are focused checks, not a production GUI build or desktop/gamepad QA.
Production CMake correctly rejects Qt 6.8.2 and still requires Qt 6.11.

Use the parent machine's complete Qt 6.11 SDK and normal repository build
prerequisites. Work on `feature/llm-general-authoring` based on debug `548b5c45`.
No helper/updater/stats/rewind branches need to be merged for this feature. Do not
use a real key or a paid API call for acceptance; inject an in-process mocked
network manager instead. No generated code should be installed in the
ordinary desktop runtime as a shortcut to testing.

## Build and focused automated checks

Choose the repository's normal Qt 6.11 GUI preset for the host. Examples:

```text
cmake --preset vs2026-x64
cmake --build --preset debug
```

Check `cmake --list-presets` and `cmake --build --list-presets` for the exact
available host presets. On Linux, configure with `linux-gui-gcc-debug` and the
Qt 6.11 SDK prefix, then build `linux-gui-debug`. Keep all production version
requirements and normal audio/QML configuration in place. Verify the application
links the new resource and that the authoring action opens from the card editor.

Check the embedded context is fresh without executing Lua:

```sh
python3 tools/authoring/build_context.py
```

Review any resulting diff rather than silently widening the supported API list.
Source hashes identify the exact SWIG/helper/example inputs. Do not add the five
convenience helpers unless their actual implementation is integrated and tested
on the target engine. Compile `builds/sanguosha.ts` with the Qt SDK's `lrelease`
and verify the new GeneralAuthoring and GeneralAuthoringDialog messages have
Simplified Chinese translations.

## Native desktop acceptance

| Check | Expected result |
| --- | --- |
| Existing card art/HP/kingdom/layout and skill text editing | Existing behavior, font settings, avatar rectangles and image export remain intact. |
| Tool → Author playable general / Alt+G | A separate dialog opens; current card name/title/HP/kingdom/lord/skill titles seed its form. |
| Several card skills | Shared card skill text starts in the first row; the user can assign descriptions per row using the multiline description editor. |
| Provider fields | Endpoint and model are editable; key is masked and has no save/remember option. No automatic request occurs on open or field edits. |
| Invalid IDs, duplicate/occupied names, fractional/oversized HP | Requests/exports fail visibly; no files or existing general are replaced. |
| Preview request | Complete model payload is plain text, with original/current specs, reviewed code, diagnostics and bounded context. No Authorization header, key, artwork bytes or local source/project path appears. |
| Preview Cancel / Escape | No network request is made. |
| Reviewed code edit and Validate | Syntax/API diagnostics display plainly; static success states its limitations. No generated code runs. |
| Candidate and diff | New code stays separate; Apply requires an explicit action and affects only the document. |
| Save/open project | Reviewed text, original/current specs and last 16 checkpoints round-trip. Provider configuration is excluded; opening is inert. Incomplete projects remain editable and cannot be exported until valid. |
| Undo / History restore | Previous manual edits and metadata can be recovered. |
| Export | Fresh disabled staging folder; previous exports/sibling files remain untouched. Child package manifest is sealed; optional image remains an explicit designer-card alias. |
| Close during request | Request aborts, key field clears, no late callback changes a reopened document. |
| Simplified Chinese UI | New controls and messages are translated, placeholders/IDs preserve their technical meaning. |

For native request/apply/correction/cancel acceptance use a mock transport, not a
public provider: instantiate `GeneralAuthoringDialog` with a mock
`QNetworkAccessManager` as its final constructor argument. The production default
remains the normal Qt HTTPS transport.

Use mock response variants: valid V2 skill, malformed JSON/schema, missing binding,
unknown method/function, invalid syntax, incomplete `finish_reason`, provider
error, oversized body, redirect, delayed reply, cancelled reply and superseded
reply. For corrections, manually change a draw count in reviewed skills, request
an unrelated change, and inspect that the request includes the exact manual
edit and original metadata. Edit metadata/code while a delayed reply is pending:
its candidate must be discarded. Edit the document after a candidate arrives:
Apply must refuse that stale candidate.

Inspect projects and exported files for the mock credential string; they must
contain none. A mock response that echoes a credential, including escaped JSON,
must be discarded. Error bodies must never be copied into visible diagnostics.
Changing/typing a key must not record its intermediate character prefixes as
credentials and falsely redact ordinary metadata.

## Keyboard and controller acceptance

Use both ordinary desktop mode and the existing Big Picture/controller mode.
No new SDL owner or controller router is added.

- Tab/Shift+Tab traverse provider fields, metadata, skill controls, code panes
  and action buttons. Code widgets use Tab for focus navigation. Fields have
  meaningful labels and code/status panes have accessible names.
- Alt+G previews a request, Alt+C cancels it, Alt+V validates, Alt+A applies an
  explicitly reviewed candidate, Alt+U undoes a version. Escape/back cancels a
  preview or closes the authoring dialog, aborting a pending request.
- Existing controller shoulder/group traversal can leave table/code controls;
  directional navigation and select reach the buttons. Use the multiline
  description button when table-cell editing is inconvenient.
- The existing controller text-entry UI keeps key text masked. Check small and
  large screens, active modal focus and close-during-request behavior. A hidden
  or disabled button must not be activated by the controller.

## Isolated gameplay testing of untrusted output

This editor does **not** provide a secure runtime sandbox. A successful static
check is not evidence that generated code is safe or that its gameplay is right.
Do not run untrusted output in the main application/runtime used for normal play.

If an isolated runtime test is needed, first provide a disposable VM or equivalent
OS-enforced sandbox with no host-shared folders, host credentials, network access
or personal configuration, plus bounded CPU/time and memory. Copy in only a
trusted engine build, required trusted assets and the reviewed child package.
Make installation a deliberate tester action. Stop on timeout or resource limits.
Without that environment, record **runtime verification not performed** and
complete code review/mock/static/GUI checks only.

In that isolated environment, check registration, general HP/start HP/armor,
kingdom/lord/gender, display translations, skill ownership, trigger actor/payload,
optional cost, effect count, and repeated turns. Confirm a non-owner does not gain
the skill. Observe effects without changing global engine packages. Use a prior
versioned export for rollback if a correction regresses behavior. Designer-card
art is linked under `image/diy/`; playable avatar crops still use the existing
card editor's avatar export tools and require explicit asset mappings if desired.

## Sign-off record

Record the exact base/patch hash, Qt version, host/compiler, preset/build result,
language, keyboard/controller hardware and the desktop
matrix outcomes. List skipped checks individually. Runtime testing needs its
sandbox details and resource limits; otherwise label it unverified. Do not claim
full GUI or gameplay QA based on the Qt 6.8.2 cloud run.
