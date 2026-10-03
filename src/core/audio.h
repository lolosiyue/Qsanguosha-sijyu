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

    // Runtime controls and diagnostics.
    // Active backend name: "fmod", "qt", "android", or "null".
    static QString backendName();
    static bool isInitialized();
    // Whether a usable output device really exists. No device is not an error; there is
    // simply no sound.
    static bool hasOutputDevice();
    // Read master/effect/voice/mute from Config and apply them to the backend.
    // Call after the settings dialog accepts its changes.
    static void applyConfigVolumes();
    // Structured status for --multimedia-smoke and the About dialog.
    static QJsonObject diagnostics();
};

#endif
