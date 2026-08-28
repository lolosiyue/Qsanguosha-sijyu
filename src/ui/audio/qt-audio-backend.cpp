#include "qt-audio-backend.h"

#include <QAudio>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QSoundEffect>
#include <QStringList>
#include <QUrl>

#include <utility>

namespace {

// 同時播放的上限。超過就搶最舊嗰個 slot，唔會無限開 player。
const int kMaxVoices = 8;
// 短 UI 音效的 fallback pool 細好多：連續撳掣唔應該搶走語音 slot。
const int kMaxEffects = 4;

// Qt 的音量係線性振幅；UI 的 slider 係感知刻度，兩者要換算，否則
// 半格 slider 聽落幾乎冇細過。
float toLinear(float perceptual)
{
    const float clamped = qBound(0.0f, perceptual, 1.0f);
    if (clamped <= 0.0f)
        return 0.0f;
    return static_cast<float>(
        QAudio::convertVolume(clamped, QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale));
}

} // namespace

QtMediaAudioBackend::QtMediaAudioBackend() = default;

QtMediaAudioBackend::~QtMediaAudioBackend()
{
    teardown();
}

QStringList QtMediaAudioBackend::preloadedEffectNames()
{
    // 名單同 classifyAudioFile() 共用同一個來源,唔會出現「當成短音效播,但係
    // 冇預載」嘅唔一致。
    return shortUiEffectNames();
}

int QtMediaAudioBackend::maxConcurrentVoices()
{
    return kMaxVoices;
}

int QtMediaAudioBackend::maxConcurrentEffects()
{
    return kMaxEffects;
}

QString QtMediaAudioBackend::name() const
{
    return QStringLiteral("qt");
}

QString QtMediaAudioBackend::resolve(const QString &filename)
{
    if (filename.isEmpty())
        return QString();
    const QFileInfo info(filename);
    return info.isAbsolute() ? info.absoluteFilePath()
                             : QDir::current().absoluteFilePath(filename);
}

bool QtMediaAudioBackend::initialize()
{
    return ensureReady();
}

bool QtMediaAudioBackend::ensureReady()
{
    if (m_ready)
        return true;

    m_root = new QObject;
    m_root->setObjectName(QStringLiteral("QtMediaAudioBackend"));

    // 無輸出裝置唔算 initialize 失敗:GUI 照跑,只係聽唔到聲。呢個 flag 令
    // multimedia smoke 分辨到「backend 壞咗」同「呢部機／CI runner 冇音效卡」。
    m_hasOutputDevice = !QMediaDevices::defaultAudioOutput().isNull();
    if (!m_hasOutputDevice)
        qWarning("QtMediaAudioBackend: no default audio output device; running silently");

    buildPool(m_voices, kMaxVoices, QStringLiteral("voice"));
    buildPool(m_effectSlots, kMaxEffects, QStringLiteral("effect"));

    m_bgmOutput = new QAudioOutput(m_root);
    m_bgm = new QMediaPlayer(m_root);
    m_bgm->setAudioOutput(m_bgmOutput);
    m_bgm->setLoops(QMediaPlayer::Infinite);
    QObject::connect(m_bgm, &QMediaPlayer::errorOccurred, m_root,
        [this](QMediaPlayer::Error error, const QString &message) {
            Q_UNUSED(error);
            noteError(QStringLiteral("bgm"), message);
        });

    // 預載短音效。缺檔案唔係錯誤:clean checkout 本身就冇入庫音訊資產。
    foreach (const QString &effect, preloadedEffectNames()) {
        const QString path = resolve(QStringLiteral("audio/system/%1.ogg").arg(effect));
        if (path.isEmpty() || !QFileInfo::exists(path))
            continue;
        if (effectFor(path))
            ++m_preloadedEffects;
    }

    m_ready = true;
    applyEffectVolumes();
    applyPoolVolumes(m_effectSlots, m_volumes.effectGain());
    applyPoolVolumes(m_voices, m_volumes.voiceGain());
    if (m_bgmOutput)
        m_bgmOutput->setVolume(toLinear(m_volumes.bgmGain()));
    return true;
}

void QtMediaAudioBackend::buildPool(QVector<PlayerSlot> &pool, int size, const QString &what)
{
    pool.resize(size);
    for (int i = 0; i < size; ++i) {
        PlayerSlot &slot = pool[i];
        slot.output = new QAudioOutput(m_root);
        slot.player = new QMediaPlayer(m_root);
        slot.player->setAudioOutput(slot.output);
        QObject::connect(slot.player, &QMediaPlayer::errorOccurred, m_root,
            [this, what](QMediaPlayer::Error error, const QString &message) {
                Q_UNUSED(error);
                noteError(what, message);
            });
    }
}

QSoundEffect *QtMediaAudioBackend::effectFor(const QString &path)
{
    if (m_effectFallback.contains(path))
        return nullptr;

    const auto it = m_effects.constFind(path);
    if (it != m_effects.constEnd())
        return it.value();

    QSoundEffect *effect = new QSoundEffect(m_root);
    effect->setSource(QUrl::fromLocalFile(path));
    effect->setVolume(toLinear(m_volumes.effectGain()));
    // QSoundEffect 走 QAudioDecoder,唔係所有 codec 都撐。載入失敗就永久標記
    // 呢個檔案改行 player pool,唔會每次播放都重試同重複 log。
    QObject::connect(effect, &QSoundEffect::statusChanged, m_root, [this, effect, path]() {
        if (effect->status() != QSoundEffect::Error)
            return;
        qWarning().noquote() << "QtMediaAudioBackend: QSoundEffect cannot decode" << path
                             << "- falling back to the media player pool";
        m_effectFallback.insert(path);
        m_effects.remove(path);
        effect->deleteLater();
    });
    m_effects.insert(path, effect);
    return effect;
}

void QtMediaAudioBackend::play(const QString &filename, bool superpose, AudioChannel channel)
{
    if (filename.isEmpty())
        return;
    if (!ensureReady())
        return;

    const QString path = resolve(filename);
    if (!QFileInfo::exists(path)) {
        // 缺檔案只係 warning。呢條路本身就會發生:語音資產係 optional。
        ++m_missingFiles;
        qWarning().noquote() << "QtMediaAudioBackend: missing audio file" << path;
        return;
    }

    if (channel == AudioChannel::Effect) {
        if (QSoundEffect *effect = effectFor(path)) {
            if (!superpose && effect->isPlaying())
                return;
            effect->setVolume(toLinear(m_volumes.effectGain()));
            effect->play();
            return;
        }
        // QSoundEffect 撐唔到（例如呢部機的 QAudioDecoder 解唔到 .ogg）就跌落
        // 短音效自己嗰個 pool，而唔係語音 pool。
        playPooled(m_effectSlots, path, superpose, m_volumes.effectGain(),
            &m_effectStarted, &m_effectEvicted);
        return;
    }

    playPooled(m_voices, path, superpose, m_volumes.voiceGain(),
        &m_voiceStarted, &m_voiceEvicted);
}

bool QtMediaAudioBackend::playPooled(QVector<PlayerSlot> &pool, const QString &path,
    bool superpose, float gain, int *started, int *evicted)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // superpose=false 的舊語義:同一個檔案仲響緊就唔重疊播多次。
    if (!superpose) {
        for (const PlayerSlot &slot : std::as_const(pool)) {
            if (slot.source == path && slot.player
                && slot.player->playbackState() == QMediaPlayer::PlayingState)
                return false;
        }
    }

    PlayerSlot *chosen = nullptr;
    for (PlayerSlot &slot : pool) {
        if (slot.player && slot.player->playbackState() == QMediaPlayer::StoppedState) {
            chosen = &slot;
            break;
        }
    }
    if (!chosen) {
        // pool 滿:搶最舊嗰個。上限固定,所以唔會 leak player/output。
        for (PlayerSlot &slot : pool) {
            if (!chosen || slot.startedAt < chosen->startedAt)
                chosen = &slot;
        }
        if (chosen && chosen->player) {
            chosen->player->stop();
            ++*evicted;
        }
    }
    if (!chosen || !chosen->player)
        return false;

    chosen->source = path;
    chosen->startedAt = now;
    chosen->output->setVolume(toLinear(gain));
    chosen->player->setSource(QUrl::fromLocalFile(path));
    chosen->player->play();
    ++*started;
    return true;
}

void QtMediaAudioBackend::stopAll()
{
    if (!m_ready)
        return;
    for (QSoundEffect *effect : std::as_const(m_effects)) {
        if (effect)
            effect->stop();
    }
    for (QVector<PlayerSlot> *pool : {&m_effectSlots, &m_voices}) {
        for (PlayerSlot &slot : *pool) {
            if (slot.player)
                slot.player->stop();
            slot.source.clear();
        }
    }
    stopBGM();
}

void QtMediaAudioBackend::playBGM(const QString &filename)
{
    if (filename.isEmpty())
        return;
    if (!ensureReady())
        return;

    const QString path = resolve(filename);
    if (!QFileInfo::exists(path)) {
        ++m_missingFiles;
        qWarning().noquote() << "QtMediaAudioBackend: missing BGM file" << path;
        return;
    }
    if (m_bgmSource == path && m_bgm->playbackState() == QMediaPlayer::PlayingState)
        return;

    m_bgmSource = path;
    m_bgm->setLoops(QMediaPlayer::Infinite);
    m_bgm->setSource(QUrl::fromLocalFile(path));
    m_bgmOutput->setVolume(toLinear(m_volumes.bgmGain()));
    m_bgm->play();
}

void QtMediaAudioBackend::setBGMVolume(float volume)
{
    m_volumes.bgm = qBound(0.0f, volume, 1.0f);
    if (m_bgmOutput)
        m_bgmOutput->setVolume(toLinear(m_volumes.bgmGain()));
}

void QtMediaAudioBackend::stopBGM()
{
    m_bgmSource.clear();
    if (m_bgm) {
        m_bgm->stop();
        m_bgm->setSource(QUrl());
    }
}

void QtMediaAudioBackend::applyVolumes(const AudioVolumes &volumes)
{
    m_volumes = volumes;
    applyEffectVolumes();
    applyPoolVolumes(m_effectSlots, m_volumes.effectGain());
    applyPoolVolumes(m_voices, m_volumes.voiceGain());
    if (m_bgmOutput)
        m_bgmOutput->setVolume(toLinear(m_volumes.bgmGain()));
}

void QtMediaAudioBackend::applyEffectVolumes()
{
    const float gain = toLinear(m_volumes.effectGain());
    for (QSoundEffect *effect : std::as_const(m_effects)) {
        if (effect)
            effect->setVolume(gain);
    }
}

void QtMediaAudioBackend::applyPoolVolumes(QVector<PlayerSlot> &pool, float gain)
{
    const float linear = toLinear(gain);
    for (PlayerSlot &slot : pool) {
        if (slot.output)
            slot.output->setVolume(linear);
    }
}

void QtMediaAudioBackend::noteError(const QString &what, const QString &detail)
{
    ++m_errors;
    m_lastError = QStringLiteral("%1: %2").arg(what, detail);
    qWarning().noquote() << "QtMediaAudioBackend:" << m_lastError;
}

void QtMediaAudioBackend::shutdown()
{
    teardown();
}

void QtMediaAudioBackend::teardown()
{
    if (!m_ready && !m_root)
        return;

    // 先停低,再拆 object:唔可以喺 decoder 仲行緊嗰陣直接刪 player。
    for (QSoundEffect *effect : std::as_const(m_effects)) {
        if (effect)
            effect->stop();
    }
    for (QVector<PlayerSlot> *pool : {&m_effectSlots, &m_voices}) {
        for (PlayerSlot &slot : *pool) {
            if (slot.player) {
                slot.player->stop();
                slot.player->setSource(QUrl());
            }
        }
    }
    if (m_bgm) {
        m_bgm->stop();
        m_bgm->setSource(QUrl());
    }

    m_effects.clear();
    m_effectFallback.clear();
    m_effectSlots.clear();
    m_voices.clear();
    m_bgm = nullptr;
    m_bgmOutput = nullptr;
    m_bgmSource.clear();

    // m_root 係所有 player／output／effect 的 parent,一 delete 就全部收乾淨,
    // 唔會留低 active QObject 或者 decoder thread。
    delete m_root;
    m_root = nullptr;
    m_ready = false;
    m_preloadedEffects = 0;
}

bool QtMediaAudioBackend::hasOutputDevice() const
{
    return m_hasOutputDevice;
}

QString QtMediaAudioBackend::version() const
{
    return QString::fromLatin1(qVersion());
}

QJsonObject QtMediaAudioBackend::diagnostics() const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("backend"), name());
    payload.insert(QStringLiteral("initialized"), m_ready);
    payload.insert(QStringLiteral("output_device"), m_hasOutputDevice);
    payload.insert(QStringLiteral("output_device_name"),
        QMediaDevices::defaultAudioOutput().description());
    payload.insert(QStringLiteral("qt_media_backend"),
        qEnvironmentVariableIsSet("QT_MEDIA_BACKEND")
            ? qEnvironmentVariable("QT_MEDIA_BACKEND") : QString());
    payload.insert(QStringLiteral("preloaded_effects"), m_preloadedEffects);
    payload.insert(QStringLiteral("effect_fallbacks"), int(m_effectFallback.size()));
    payload.insert(QStringLiteral("effect_pool_size"), int(m_effectSlots.size()));
    payload.insert(QStringLiteral("effect_started"), m_effectStarted);
    payload.insert(QStringLiteral("effect_evicted"), m_effectEvicted);
    payload.insert(QStringLiteral("voice_pool_size"), int(m_voices.size()));
    payload.insert(QStringLiteral("voice_started"), m_voiceStarted);
    payload.insert(QStringLiteral("voice_evicted"), m_voiceEvicted);
    payload.insert(QStringLiteral("missing_files"), m_missingFiles);
    payload.insert(QStringLiteral("bgm_source"), m_bgmSource);
    payload.insert(QStringLiteral("bgm_playing"),
        m_bgm && m_bgm->playbackState() == QMediaPlayer::PlayingState);
    payload.insert(QStringLiteral("errors"), m_errors);
    payload.insert(QStringLiteral("last_error"), m_lastError);
    return payload;
}
