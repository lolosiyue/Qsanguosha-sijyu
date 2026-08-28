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

// Linux GUI 的 backend，行 Qt Multimedia。
//
// 資源策略（三條路徑刻意分開，唔會共用同一個 player）：
//
//   * 短 UI 音效：少量常用音效預載成 QSoundEffect（低延遲、可重播）。
//     只預載 preloadedEffectNames() 嗰幾個，唔會將武將語音轉檔或者全部載入記憶體。
//   * 武將語音：固定大小的 QMediaPlayer + QAudioOutput pool。同時播放有上限，
//     播完自動回收；pool 滿就搶最舊嗰個，永遠唔會每次播放都 new 一對。
//   * BGM：獨立一個 player／output，唔會同語音 pool 混用。
//
// 冇音訊裝置、冇檔案、codec 唔支援都只係 warning + 降級，唔會 crash。
class QtMediaAudioBackend final : public IAudioBackend
{
public:
    QtMediaAudioBackend();
    ~QtMediaAudioBackend() override;

    // 預載的短音效名（audio/system/<name>.ogg）。
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
    void applyVolumes(const AudioVolumes &volumes) override;
    QString version() const override;
    QJsonObject diagnostics() const override;

private:
    // 一個可重用的 player+output。effect 同 voice 各有自己一組，數量固定，
    // 所以播放永遠唔會 new 一對新的落去 leak。
    struct PlayerSlot
    {
        QMediaPlayer *player = nullptr;
        QAudioOutput *output = nullptr;
        QString source;
        qint64 startedAt = 0;
    };

    // shutdown() 之後再有播放請求時重新建起資源。StartScene::switchToServer()
    // 會喺開房時 Audio::quit()，Linux 唔應該因此永久收聲。
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
    // QSoundEffect 載入唔到（例如 codec 唔支援）的音效改行 player pool，
    // 唔會每次重試都再 log 一次。
    QSet<QString> m_effectFallback;
    // QSoundEffect 撐唔到的短音效跌落自己嗰個細 pool，唔會佔用語音 slot：
    // 撳一下按鈕唔應該打斷一句武將台詞。
    QVector<PlayerSlot> m_effectSlots;
    QVector<PlayerSlot> m_voices;
    QMediaPlayer *m_bgm = nullptr;
    QAudioOutput *m_bgmOutput = nullptr;
    QString m_bgmSource;

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
