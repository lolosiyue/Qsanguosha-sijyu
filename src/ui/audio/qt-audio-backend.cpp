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

// Cap on simultaneous playback. Once exceeded, the oldest slot is evicted; players are never spawned without limit.
const int kMaxVoices = 8;
// The short-UI-sound fallback pool is much smaller: rapid button presses must not steal voice slots.
const int kMaxEffects = 4;

// Qt volume is linear amplitude; the UI slider is perceptual, so the two must be
// converted, otherwise half a slider step would barely sound quieter.
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
    // The list shares one source with classifyAudioFile(), so the inconsistency of
    // "played as a short sound but never preloaded" cannot happen.
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

    // No output device does not count as initialize failure: the GUI keeps running,
    // just silent. This flag lets multimedia smoke tell "backend broken" from "this machine / CI runner has no sound card".
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

    // Preload short sounds. A missing file is not an error: a clean checkout has no committed audio assets to begin with.
    foreach (const QString &effect, preloadedEffectNames()) {
        QString path = resolve(QStringLiteral("audio/system/%1.ogg").arg(effect));
#ifdef Q_OS_ANDROID
        // Android ships tiny PCM copies in the APK because its QSoundEffect
        // backend may reject the external OGG codec. Keep OGG as fallback.
        const QString androidWav = resolve(
            QStringLiteral("resource/android/%1.wav").arg(effect));
        if (QFileInfo::exists(androidWav))
            path = androidWav;
#endif
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
    // QSoundEffect goes through QAudioDecoder, which not every codec supports. On
    // load failure the file is permanently marked to run on the player pool, with no retry or repeated log on every playback.
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
    if (filename.isEmpty() || m_applicationSuspended)
        return;
    if (!ensureReady())
        return;

    QString path = resolve(filename);
#ifdef Q_OS_ANDROID
    if (channel == AudioChannel::Effect) {
        // Prefer the APK-provided PCM asset by basename, then retain the
        // caller's OGG path for media-import and non-Android fallbacks.
        const QString androidWav = resolve(QStringLiteral("resource/android/%1.wav")
                                                .arg(QFileInfo(filename).completeBaseName()));
        if (QFileInfo::exists(androidWav))
            path = androidWav;
    }
#endif
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
        // When QSoundEffect cannot handle it (e.g. this machine's QAudioDecoder
        // cannot decode .ogg), fall back to the short-sound pool, not the voice pool.
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

    // Old semantics of superpose=false: while the same file is still playing, do not overlap it with another play.
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
        // Pool full: evict the oldest one. The cap is fixed, so no player/output leak.
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
    chosen->suspendedByApplication = false;
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
            slot.suspendedByApplication = false;
        }
    }
    stopBGM();
}

void QtMediaAudioBackend::playBGM(const QString &filename)
{
    if (filename.isEmpty() || m_applicationSuspended)
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
    m_bgmSuspendedByApplication = false;
    if (m_bgm) {
        m_bgm->stop();
        m_bgm->setSource(QUrl());
    }
}

void QtMediaAudioBackend::setApplicationSuspended(bool suspended)
{
    if (m_applicationSuspended == suspended)
        return;

    m_applicationSuspended = suspended;
    if (!m_ready)
        return;

    if (suspended) {
        // QSoundEffect has no pause API.  Stop short effects; they are not
        // marked, so resume cannot replay an effect the user already heard.
        for (QSoundEffect *effect : std::as_const(m_effects)) {
            if (effect)
                effect->stop();
        }
        for (QVector<PlayerSlot> *pool : {&m_effectSlots, &m_voices}) {
            for (PlayerSlot &slot : *pool) {
                if (slot.player
                    && slot.player->playbackState() == QMediaPlayer::PlayingState) {
                    slot.player->pause();
                    slot.suspendedByApplication = true;
                }
            }
        }
        if (m_bgm && m_bgm->playbackState() == QMediaPlayer::PlayingState) {
            m_bgm->pause();
            m_bgmSuspendedByApplication = true;
        }
        return;
    }

    // Resume only players paused by this lifecycle transition and still
    // holding the same source.  stopAll()/stopBGM() clear the markers, while
    // a replacement cannot be queued during suspension.
    for (QVector<PlayerSlot> *pool : {&m_effectSlots, &m_voices}) {
        for (PlayerSlot &slot : *pool) {
            if (!slot.suspendedByApplication)
                continue;
            const bool resumable = slot.player && !slot.source.isEmpty()
                && slot.player->playbackState() == QMediaPlayer::PausedState;
            slot.suspendedByApplication = false;
            if (resumable)
                slot.player->play();
        }
    }
    if (m_bgmSuspendedByApplication) {
        const bool resumable = m_bgm && !m_bgmSource.isEmpty()
            && m_bgm->playbackState() == QMediaPlayer::PausedState;
        m_bgmSuspendedByApplication = false;
        if (resumable)
            m_bgm->play();
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

    // Stop first, then tear down the object: never delete the player while its decoder is still running.
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
    m_bgmSuspendedByApplication = false;

    // m_root is the parent of every player / output / effect; one delete cleans
    // everything up, leaving no active QObject or decoder thread behind.
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
