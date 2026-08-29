# Linux GUI M2B-B — effects profiles

One GUI client, three official visual-effects profiles. There is no fork, no
second `RoomScene`, and no `#ifdef Q_OS_LINUX` sprinkled through the UI: every
call site asks one central policy whether an effect may run.

**The three profiles never change game rules or network replies.** They change
only what is drawn while the same messages go over the same wire.

```
FULL      every animation, Spine pop-outs, GIF playback, QML skill overlays,
          background video — the current Windows behaviour, unchanged.
REDUCED   necessary state feedback kept, animations ~30% of their duration,
          no Spine, no video, no QML overlay, GIF first frame only.
NONE      no decorative animation at all; every game object reaches its final
          position/opacity/visibility immediately, and no Spine skeleton,
          QMovie or video object is constructed.
```

## Where the policy lives

| Piece | File | Depends on |
| ----- | ---- | ---------- |
| Profile contract (names, gates, duration scale, CLI/settings resolution) | `src/ui/effects/effects-profile.{h,cpp}` | Qt Core only |
| Exactly-once completion guarantee | `src/ui/effects/effects-completion.{h,cpp}` | Qt Core only |
| Runtime façade (`G_EFFECTS`), object counters | `src/ui/effects/effects-policy.{h,cpp}` | `Settings` |

`effects-profile` and `effects-completion` deliberately stay Qt-Core-only so the
contract is testable in CTest without a `QApplication`, an OpenGL context or a
single art asset (`tests/effects_profile/effects-profile-test.cpp`).

### Asking the policy

```cpp
#include "effects/effects-policy.h"

if (!G_EFFECTS.animationsEnabled()) {
    setPos(homePos());                 // final state, applied immediately
    return;
}
animation->setDuration(G_EFFECTS.scaledDuration(600));
```

Gates: `animationsEnabled()`, `spineEnabled()`, `gifEnabled()`,
`gifPlaybackAllowed()`, `videoEnabled()`, `qmlEffectsEnabled()`,
`decorativeDelayAllowed()`, `immediate()`, `scaledDuration(ms)`,
`scaledDelay(ms)`.

A gate may only ever **narrow**. `videoEnabled()` is
`profileAllowsVideo && Config.EnableBackgroundVideo`, so FULL never re-enables
something the player switched off; `gifEnabled()` folds in
`EnableAnimatedGenerals` the same way.

## Choosing a profile

Resolution order is **CLI override > user setting > default (`full`)**.

| Source | How |
| ------ | --- |
| User setting | Options → Display → *Visual effects* (`QSettings` key `EffectsProfile`) |
| Test override | `--effects-profile full\|reduced\|none` (also `--effects-profile=none`) |

Both go through the same `EffectsProfileContract::resolve()` and the same
`VisualEffectsPolicy` object — the config dialog does not maintain a parallel
switch.

An unknown value is **never silently ignored**: `--effects-profile turbo` exits
with code 5 and a message on stderr, because a mistyped flag that quietly falls
back to `full` turns a three-profile CI matrix into the same profile run three
times.

## Why NONE skips rather than sets `duration = 0`

A zero-duration `QAbstractAnimation` emits `finished()` **synchronously inside
`start()`**. Every call site that continues a flow from `finished()` would then
re-enter before it finished building its own state — that is where double
callbacks and use-after-free come from.

So NONE takes the existing *skip* branch (or a new one) and applies the final
state directly. Where a callback still has to be delivered, it goes through
`EffectsCompletion::completeNow()`, which posts a queued invocation bound to a
context object.

`scaledDuration()` enforces the other half: REDUCED clamps to a minimum of 1 ms
and never reaches 0.

## The completion contract

`EffectsCompletion` guarantees **exactly once** for every path:

| Path | Result |
| ---- | ------ |
| animation finished | callback once |
| animation skipped / never started | callback once, queued |
| animation destroyed while running | callback once (the flow must not stall) |
| watchdog timeout (`timeoutMs > 0`) | callback once |
| context destroyed | cancelled — never delivered to a dead object |

Never double, never never. `deliveredCount()` / `cancelledCount()` make it
observable, and both the unit test and the `completion` smoke stage assert on
them.

## Missing-asset handling this milestone repaired

### `PixmapAnimation::valid()` could never be false

The root cause behind most of the frame-animation fallbacks:

```cpp
QString pic_path = path + "0.png";
do {
    frames << G_ROOM_SKIN.getPixmapFromFileName(pic_path, true);
    pic_path = path + QString::number(i++) + ".png";
} while (QFile::exists(pic_path));
```

A `do`-`while` appends frame 0 **before** testing whether it exists, and
`getPixmapFromFileName()` returns a 1×1 placeholder — not a null pixmap — for a
missing file. So `frames` was never empty, `valid()` was always `true`, and
every "the art is missing, do not play" branch in the codebase was dead code:

* `GetPixmapAnimation()` never took its `else { delete pma; return nullptr; }`
  path, so callers that check for `nullptr` (`doPindianAnimation()`'s
  `else pindian_box->disappear()`) never saw it;
* `_createEquipBorderAnimations()` never took its `!valid()` path, so
  `_m_equipBorders[i]` was never set to `nullptr`.

What actually happened with no art was an invisible 1×1 sprite plus a running
20 Hz timer per animation. Not a crash — but the fallbacks the original authors
wrote were never exercised, which is exactly the state you do not want when
REDUCED and NONE start relying on them.

`setPath()` is now a plain `while`, so only frames that exist are loaded. With
the full asset tree the behaviour is byte-identical (the loop condition was
already the same `QFile::exists()`); with assets missing, `valid()` finally
means what it says.

### The guards those now-live fallbacks need

Making `valid()` honest turns three previously-unreachable paths into reachable
ones, so each needed a guard:

* **`doLightboxAnimation()`'s `anim=` branch** built the 80%-opaque dimming rect
  and only removed it from `PixmapAnimation::finished()`. With
  `GetPixmapAnimation()` now able to return `nullptr`, the rect would stay on
  screen forever — the game playable but invisible. It now removes the lightbox
  and warns.
* **`_setEquipBorderAnimation()`** guarded `_m_equipBorders[index]` with
  `Q_ASSERT`, which is a no-op in Release/RelWithDebInfo, then dereferenced it.
  Now null-checked.
* **`PixmapAnimation` itself** left `_m_timerId`, `current`, `off_x` and `off_y`
  uninitialised, so `stop()` before any `start()` killed a garbage timer id;
  and `paint()` / `boundingRect()` / `advance()` indexed `frames` without
  checking it is non-empty. All four are now initialised and bounds-checked.

### Independent of the above

* **A null dereference in the animated-general path.**
  `GraphicsPixmapHoverItem` called `m_proxyWidget->show()` on a proxy that is
  only created when the item is already in a scene; when it is not, the pointer
  stays null. It now falls back to the static portrait.

## Running it

### The effects smoke (one profile per run)

```bash
bash tools/ci/linux-gui-effects-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
    --profile none --no-xvfb --platform xcb --label wslg
```

Stages, in order: `policy` → `completion` → `animation` → `gif` → `spine` →
`budget` → `shutdown`. The GUI prints one `EFFECTS_STAGE` line per stage, exactly
one `EFFECTS_PROFILE_RESULT`, exactly one `EFFECTS_RESULT`, and
`tools/ci/validate-effects-smoke.py` is the CI-side half of that contract.

Flags: `--effects-smoke`, `--effects-profile <p>`, `--effects-report <path>`,
`--effects-timeout-ms <n>`, `--effects-fixtures <dir>`.

Exit codes: `0` pass, `2` policy, `3` completion, `4` asset fallback, `5`
budget/shutdown, `6` timeout, `7` invalid arguments, `8` internal.

The smoke asserts behaviour, never pixels. Screenshots are failure artifacts,
not gates.

### A full game under a profile

The network runner drives a real TCP game through the real `RoomScene`, so it is
what proves "NONE completes a whole game and exits cleanly":

```bash
python3 tools/autotest/gui_network_smoke.py --exe-root . \
    --mode 02p --seed 20260828 --artifact-dir artifacts \
    --no-xvfb --platform xcb --effects-profile none \
    --known-base-defect server-teardown-crash \
    --require-interactions choose_general,play_phase,ask_for_card
```

The runner asserts that the client actually resolved the requested profile from
the CLI, and — for `none` — that the finished game created zero Spine, QMovie,
QML overlay and video objects. Per AGENTS.md this stays a **local** gate: a CI
runner has no art assets and crashes in the rendering path regardless of branch.

### Unit contract

```bash
ctest --test-dir build/linux-gui-gcc -R qsanguosha_effects_profile_contract -V
```

## CI

On `push main` and `workflow_dispatch`, `linux-gui-ci.yml` job `gui-effects`
runs a **blocking** matrix over all three profiles (Ubuntu 24.04, Qt 6.11.1,
GCC, Xvfb, software renderer), each on both `xcb` and `offscreen`, plus two
negative contracts: an unknown profile name must be rejected, and the app-level
timeout must produce a `timeout` result marker rather than hanging until the
runner kills it. Pull requests and `push debug` stop after compile/link plus one
Xvfb startup smoke.

The matrix runs entirely on the synthetic fixtures in `tests/fixtures/effects/`
(a 4x4 GIF, a few 8x8 PNGs, a deliberately broken Spine directory). **A
production-asset smoke is never a clean-checkout blocker** — see
`tests/fixtures/effects/README.md` for why there is no valid Spine fixture and
what would have to change to add one.

## Object budget

`EffectsSmokeReport::budgetFor()` is the executable definition of each profile,
mirrored in `validate-effects-smoke.py` so a regression cannot "fix" the budget
on one side only:

| Profile | Spine items | QMovie | QML overlays | Video |
| ------- | ----------- | ------ | ------------ | ----- |
| `none` | 0 | 0 | 0 | 0 |
| `reduced` | 0 | unbounded (first frame only) | 0 | 0 |
| `full` | unbounded | unbounded | unbounded | unbounded |

## Windows

The profile policy is platform-neutral and compiled into both targets. FULL is
the default, so existing Windows visual behaviour is unchanged; the FMOD audio
backend is chosen by CMake's `QSAN_AUDIO_BACKEND` and is untouched by this
policy (effects profiles gate *visuals*, not audio). The Qt-Core-only contract
test runs on Windows CI as well.
