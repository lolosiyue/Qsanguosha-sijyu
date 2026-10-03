#include "audio-backend.h"
#include "null-audio-backend.h"

#include <QFileInfo>

#ifdef QSAN_AUDIO_BACKEND_FMOD
#include "fmod-audio-backend.h"
#endif
#ifdef QSAN_AUDIO_BACKEND_QT
#include "qt-audio-backend.h"
#endif
#ifdef QSAN_AUDIO_BACKEND_ANDROID
#include "android-audio-backend.h"
#endif

// Backend selection is centralized here; call sites do not branch on Q_OS_LINUX.

// Which backend gets compiled in is decided by CMake's QSAN_AUDIO_BACKEND:
//
//   QSAN_AUDIO_BACKEND=FMOD     → QSAN_AUDIO_BACKEND_FMOD（Windows GUI Release）
//   QSAN_AUDIO_BACKEND=QT       → QSAN_AUDIO_BACKEND_QT（Linux GUI）
//   QSAN_AUDIO_BACKEND=ANDROID  → QSAN_AUDIO_BACKEND_ANDROID（Android SoundPool / MediaPlayer）
//   QSAN_AUDIO_BACKEND=NULL     -> neither is defined


IAudioBackend *createConfiguredAudioBackend()
{
#if defined(QSAN_AUDIO_BACKEND_FMOD)
    return new FmodAudioBackend;
#elif defined(QSAN_AUDIO_BACKEND_QT)
    return new QtMediaAudioBackend;
#elif defined(QSAN_AUDIO_BACKEND_ANDROID)
    return new AndroidAudioBackend;
#else
    return new NullAudioBackend;
#endif
}

IAudioBackend *createNullAudioBackend()
{
    return new NullAudioBackend;
}

QStringList shortUiEffectNames()
{
    // Only the frequently pressed, low-latency ones are preloaded. General voices
    // are neither transcoded nor preloaded into memory - they run on the player pool.
    return QStringList{
        QStringLiteral("button-down"),
        QStringLiteral("button-hover"),
        QStringLiteral("choose-item"),
        QStringLiteral("pop-up")
    };
}

AudioChannel classifyAudioFile(const QString &filename)
{
    if (filename.isEmpty())
        return AudioChannel::Voice;
    // The short UI sounds under audio/system/ use Effect; everything else (general
    // voices, win/lose, scene switches and other one-shot long sounds) runs on the Voice pool.
    const QString base = QFileInfo(filename).completeBaseName();
    return shortUiEffectNames().contains(base) ? AudioChannel::Effect : AudioChannel::Voice;
}
