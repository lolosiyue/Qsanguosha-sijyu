#ifndef QSAN_QT_AUDIO_BACKEND_H
#define QSAN_QT_AUDIO_BACKEND_H

#include "audio-backend.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

QT_BEGIN_NAMESPACE
class QAudioOutput;
class QMediaPlayer;
class QSoundEffect;
QT_END_NAMESPACE

// Qt Multimedia backend for Linux GUI.

// Keep short UI sounds, voice playback and BGM on separate resource paths.

// Short UI sounds use preloaded QSoundEffect; voice assets are not transcoded or loaded wholesale.

// Voice playback uses a bounded reusable player/output pool.

// BGM has its own player and output.

// Missing devices, files and unsupported codecs degrade to warnings rather than crashes.
class QtMediaAudioBackend final : public IAudioBackend
{
public:
    QtMediaAudioBackend();
    ~QtMediaAudioBackend() override;

    // Short UI sounds preloaded for low-latency playback.
    static QStringList preloadedEffectNames();
    static int maxConcurrentVoices();
    static int maxConcurrentEffects();

    QString name() const override;
    bool initialize() override;
    void shutdown() override;
    bool hasOutputDevice() const override;
    void play(const QString &filename, bool superpose, AudioChannel channel) override;
    void stopAll() override;
    void playBGM(const QString &filename) override;
    void setBGMVolume(float volume) override;
    void stopBGM() override;
    void setApplicationSuspended(bool suspended) override;
    void applyVolumes(const AudioVolumes &volumes) override;
    QString version() const override;
    QJsonObject diagnostics() const override;

private:
    // A reusable player+output. Effect and voice each have their own fixed-size set,
    // so playback never news a fresh pair that could leak.
    struct PlayerSlot
    {
        QMediaPlayer *player = nullptr;
        QAudioOutput *output = nullptr;
        QString source;
        qint64 startedAt = 0;
        bool suspendedByApplication = false;
    };

    // Create resources; return true immediately if already created. Playback requests
    // arriving after shutdown() recreate them, so at the backend level "play after shutdown" is a safe no-crash path, not UB.
    // (When quit() is invoked through the Audio facade the backend itself is deleted, so that path
    // remains terminal as on Windows - this guard only covers direct backend use.)
    bool ensureReady();
    void teardown();

    QSoundEffect *effectFor(const QString &filename);
    void buildPool(QVector<PlayerSlot> &pool, int size, const QString &what);
    bool playPooled(QVector<PlayerSlot> &pool, const QString &path, bool superpose,
        float gain, int *started, int *evicted);
    void applyEffectVolumes();
    void applyPoolVolumes(QVector<PlayerSlot> &pool, float gain);
    void noteError(const QString &what, const QString &detail);
    static QString resolve(const QString &filename);

    QObject *m_root = nullptr;
    QHash<QString, QSoundEffect *> m_effects;
    // Sounds that QSoundEffect cannot load (e.g. unsupported codec) switch to the
    // player pool; retries do not log again every time.
    QSet<QString> m_effectFallback;
    // Short sounds QSoundEffect cannot handle fall into their own small pool and
    // never occupy voice slots: one button press should not interrupt a voice line.
    QVector<PlayerSlot> m_effectSlots;
    QVector<PlayerSlot> m_voices;
    QMediaPlayer *m_bgm = nullptr;
    QAudioOutput *m_bgmOutput = nullptr;
    QString m_bgmSource;
    bool m_bgmSuspendedByApplication = false;
    bool m_applicationSuspended = false;

    AudioVolumes m_volumes;
    bool m_ready = false;
    bool m_hasOutputDevice = false;
    int m_preloadedEffects = 0;
    int m_missingFiles = 0;
    int m_effectStarted = 0;
    int m_effectEvicted = 0;
    int m_voiceStarted = 0;
    int m_voiceEvicted = 0;
    int m_errors = 0;
    QString m_lastError;
};

#endif
