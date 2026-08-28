#include "audio-backend.h"
#include "null-audio-backend.h"

#include <QFileInfo>

#ifdef QSAN_AUDIO_BACKEND_FMOD
#include "fmod-audio-backend.h"
#endif
#ifdef QSAN_AUDIO_BACKEND_QT
#include "qt-audio-backend.h"
#endif

// 全個 codebase 唯一做 audio backend 選擇的地方。
//
// 邊個 backend 編得入去由 CMake 的 QSAN_AUDIO_BACKEND 決定:
//
//   QSAN_AUDIO_BACKEND=FMOD  → QSAN_AUDIO_BACKEND_FMOD（Windows GUI Release）
//   QSAN_AUDIO_BACKEND=QT    → QSAN_AUDIO_BACKEND_QT（Linux GUI）
//   QSAN_AUDIO_BACKEND=NULL  → 兩個都唔定義
//
// 冇任何 call site 需要 #ifdef Q_OS_LINUX。
IAudioBackend *createConfiguredAudioBackend()
{
#if defined(QSAN_AUDIO_BACKEND_FMOD)
    return new FmodAudioBackend;
#elif defined(QSAN_AUDIO_BACKEND_QT)
    return new QtMediaAudioBackend;
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
    // 只有真係按得密、要求低延遲嗰幾個先預載。武將語音唔會轉檔亦唔會預載入
    // 記憶體 —— 佢哋行 player pool。
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
    // audio/system/ 下面嗰幾個短 UI 音效行 Effect;其餘（武將語音、win/lose、
    // 場景切換等一次性長音效）全部行 Voice pool。
    const QString base = QFileInfo(filename).completeBaseName();
    return shortUiEffectNames().contains(base) ? AudioChannel::Effect : AudioChannel::Voice;
}
