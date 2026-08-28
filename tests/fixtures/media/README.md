# Multimedia smoke fixtures

These files are **generated**, not copied from the game's asset tree. They are
synthetic sine tones written by `tools/ci/make-media-fixtures.py`:

| File              | Content                     | Used by                                   |
| ----------------- | --------------------------- | ----------------------------------------- |
| `button-down.wav` | 0.08 s, 880 Hz, 8 kHz mono  | `--multimedia-smoke` stage `ui_effect`    |
| `voice-line.wav`  | 0.15 s, 440 Hz, 8 kHz mono  | `--multimedia-smoke` stage `voice`        |
| `bgm-loop.wav`    | 0.30 s, 220 Hz, 8 kHz mono  | `--multimedia-smoke` stage `bgm`          |

Regenerate them with:

```bash
python3 tools/ci/make-media-fixtures.py
```

Two constraints that are easy to break:

* **No real game assets here.** The full art/audio set is large and
  copyrighted; it must never be committed. Keep every fixture in the kilobyte
  range and synthetic.
* **`button-down.wav` must keep that exact name.** `classifyAudioFile()`
  decides "short UI effect" vs "voice" from the file's base name, so renaming
  this fixture silently moves the `ui_effect` stage off the `QSoundEffect` path
  it is supposed to exercise.

There is deliberately **no video fixture**. Producing a valid MP4/WebM requires
an encoder that is not guaranteed on a runner, and a real background video is
far too large to commit. The `video` stage therefore verifies the QML media
component's *initialisation and fallback* contract instead: with no video
backdrop present, the home page must report a classified reason and fall back
to a static background rather than failing to load.
