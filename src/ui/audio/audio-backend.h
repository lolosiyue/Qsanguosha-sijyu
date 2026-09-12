#ifndef QSAN_AUDIO_BACKEND_H
#define QSAN_AUDIO_BACKEND_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// The M2B-A audio backend abstraction.
//
// The product has only ever had the one `Audio` facade (src/core/audio.h); no second
// facade is opened here: `Audio` stays the single entry point and merely delegates to one of the backends below.
//
//     Audio  ──►  IAudioBackend
//                   ├── FmodAudioBackend      Windows GUI Release
//                   ├── QtMediaAudioBackend   Linux GUI (Qt Multimedia)
//                   └── NullAudioBackend      dedicated server / tests / fallback
//
// Backend selection happens only in one place - CMake (QSAN_AUDIO_BACKEND) and
// audio-backend-factory.cpp; call sites never scatter #ifdef Q_OS_LINUX.

// Short UI sounds and general voices run two different resource paths on the Qt
// backend (QSoundEffect vs player pool), so the facade must tell the backend which kind this call is.
enum class AudioChannel
{
    Effect,
    Voice
};

struct AudioVolumes
{
    float master = 1.0f;
    float effect = 1.0f;
    // Voice is a sub-trim of the effect volume, not a separate channel: at the
    // default 1.0, voiceGain() == effectGain(), and the legacy Windows
    // "voice follows EffectVolume" behavior is preserved as-is.
    float voice = 1.0f;
    // BGM volume is passed in explicitly by the call site (Audio::setBGMVolume);
    // only the last value is remembered here so it can be reapplied when master / mute changes.
    float bgm = 1.0f;
    bool muted = false;

    float effectGain() const { return muted ? 0.0f : master * effect; }
    float voiceGain() const { return muted ? 0.0f : master * effect * voice; }
    float bgmGain() const { return muted ? 0.0f : master * bgm; }

    float gainFor(AudioChannel channel) const
    {
        return channel == AudioChannel::Voice ? voiceGain() : effectGain();
    }
};

class IAudioBackend
{
public:
    virtual ~IAudioBackend() = default;

    // "fmod" / "qt" / "null". Shows up in the multimedia smoke report, so it is a contract.
    virtual QString name() const = 0;

    // Create the underlying resources. Returning false means this backend is unusable
    // on this machine; the facade switches to NullAudioBackend instead of letting the GUI hang.
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // Whether a truly usable output device exists. No audio device is not an
    // initialize() failure: the GUI keeps running, just silent; this flag lets smoke / diagnostics tell the two apart.
    virtual bool hasOutputDevice() const = 0;

    virtual void play(const QString &filename, bool superpose, AudioChannel channel) = 0;
    virtual void stopAll() = 0;

    virtual void playBGM(const QString &filename) = 0;
    virtual void setBGMVolume(float volume) = 0;
    virtual void stopBGM() = 0;

    // Backends without a resumable playback implementation intentionally keep
    // the compatibility no-op.  Qt Multimedia overrides this for Android.
    virtual void setApplicationSuspended(bool suspended)
    {
        Q_UNUSED(suspended);
    }

    // Pushed down by the facade when master / effect / voice / mute changes.
    virtual void applyVolumes(const AudioVolumes &volumes) = 0;

    virtual QString version() const = 0;

    // Structured state for --multimedia-smoke and the about dialog.
    virtual QJsonObject diagnostics() const = 0;
};

// CMake's QSAN_AUDIO_BACKEND decides which implementation is compiled in. The
// implementation is in audio-backend-factory.cpp, the only backend-selection point in the whole codebase.
IAudioBackend *createConfiguredAudioBackend();
IAudioBackend *createNullAudioBackend();

// 會預載成低延遲短音效的 audio/system/<name>.ogg 名單。
QStringList shortUiEffectNames();

// Whether a playback request is a short UI sound or a voice / one-shot long sound.
// This is the only classification point; call sites need not know it themselves.
AudioChannel classifyAudioFile(const QString &filename);

#endif
