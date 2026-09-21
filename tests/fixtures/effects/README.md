# Effects smoke fixtures

These files are **generated**, not copied from the game's asset tree. They are
written by [`make-effects-fixtures.py`](../../../tools/ci/make-effects-fixtures.py) using only the Python standard
library (a hand-rolled GIF LZW encoder and `zlib` for PNG), so a clean checkout
on any runner can regenerate them:

```bash
python3 tools/ci/make-effects-fixtures.py
```

| File                   | Content                                | Used by (`--effects-smoke` stage) |
| ---------------------- | -------------------------------------- | --------------------------------- |
| `animated.gif`         | 4x4, 2 frames, looping                 | `gif` — plays under FULL, first frame only under REDUCED |
| `single-frame.gif`     | 4x4, 1 frame                           | `gif` — a still GIF must not start a frame timer |
| `truncated.gif`        | valid header, pixel data cut off       | `gif` — `QMovie::isValid()` false → static fallback |
| `not-a.gif`            | plain text with a `.gif` name          | `gif` — malformed asset |
| `emotion/smoke/[0-2].png` | 8x8 solid-colour frames             | `animation` — `PixmapAnimation::setPath()` frame loading |
| `spine/broken/broken.*` | deliberately malformed atlas + json   | `spine` — load failure must degrade, not crash |

Constraints that are easy to break:

* **No real game assets here.** The full art set is large and copyrighted; it
  must never be committed. Every fixture stays in the hundred-byte range.
* **`truncated.gif` and `not-a.gif` must stay invalid.** They exist to prove the
  *fallback* path. If a future encoder change makes them parse, the GIF stage
  stops testing anything.
* **`emotion/smoke/` must keep the `<n>.png` naming.** `PixmapAnimation::setPath()`
  walks `path + i + ".png"` and stops at the first gap.

## Why there is no valid Spine fixture

The `spine` stage covers lifecycle and load-failure handling:

* under REDUCED and NONE, no `SpineGlItem` is constructed at all — asserted via
  the policy's object counters and the per-profile budget, not by loading
  anything; and
* under FULL, three failure shapes (missing path, malformed atlas/json, and a
  wrong-case path — Linux is case-sensitive where Windows is not) must all
  report a failed load and destruct cleanly.

If a licensed, redistributable minimal Spine fixture ever becomes available,
drop it in `spine/valid/` and add a positive case to the `spine` stage; verify its load and cleanup paths.
