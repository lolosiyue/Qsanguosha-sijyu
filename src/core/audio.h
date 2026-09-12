#ifndef _AUDIO_H
#define _AUDIO_H

#include <QJsonObject>
#include <QString>

// The client's single audio facade. M2B-A adds no second facade: this class keeps the
// original static API (zero call-site changes) and merely forwards the implementation to
// IAudioBackend (src/ui/audio/audio-backend.h).
//
// The implementation is compiled into the GUI target (QSanguosha) only.
// qsanguosha_engine/qsanguosha_server never define AUDIO_SUPPORT, so the dedicated server
// never pulls in Qt Multimedia.
class Audio
{
public:
    static void init();
    static void quit();

    // Whether filename is a short UI sound effect or a general voice is decided by
    // classifyAudioFile(); call sites need not know. superpose=false keeps the old
    // semantics: a file already playing is not overlapped.
    static void play(const QString &filename, bool superpose = true);
    static void stop();

    static void playBGM(const QString &filename);
    static void setBGMVolume(float volume);
    static void stopBGM();

    // Android application lifecycle hook.  Background suspension is a backend
    // concern; unsupported backends keep their existing no-op behaviour.
    static void setApplicationSuspended(bool suspended);

    static QString getVersion();

    // ── M2B-A 新增的觀測／設定接口 ──────────────────────────────────────
    // 目前生效的 backend 名（"fmod" / "qt" / "null"）。
    static QString backendName();
    static bool isInitialized();
    // Whether a usable output device really exists. No device is not an error; there is
    // simply no sound.
    static bool hasOutputDevice();
    // 由 Config 讀 master／effect／voice／mute 並推落 backend。設定畫面按確定
    // 之後呼叫一次即可。
    static void applyConfigVolumes();
    // --multimedia-smoke 同 about dialog 用的結構化狀態。
    static QJsonObject diagnostics();
};

#endif
