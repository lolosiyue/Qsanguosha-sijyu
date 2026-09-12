#include "null-audio-backend.h"

QString NullAudioBackend::name() const
{
    return QStringLiteral("null");
}

bool NullAudioBackend::initialize()
{
    return true;
}

void NullAudioBackend::shutdown()
{
}

bool NullAudioBackend::hasOutputDevice() const
{
    return false;
}

void NullAudioBackend::play(const QString &filename, bool superpose, AudioChannel channel)
{
    Q_UNUSED(filename);
    Q_UNUSED(superpose);
    if (channel == AudioChannel::Voice)
        ++m_voiceRequests;
    else
        ++m_effectRequests;
}

void NullAudioBackend::stopAll()
{
}

void NullAudioBackend::playBGM(const QString &filename)
{
    Q_UNUSED(filename);
    ++m_bgmRequests;
}

void NullAudioBackend::setBGMVolume(float volume)
{
    Q_UNUSED(volume);
}

void NullAudioBackend::stopBGM()
{
}

void NullAudioBackend::applyVolumes(const AudioVolumes &volumes)
{
    Q_UNUSED(volumes);
}

QString NullAudioBackend::version() const
{
    return QStringLiteral("null");
}

QJsonObject NullAudioBackend::diagnostics() const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("backend"), name());
    payload.insert(QStringLiteral("output_device"), false);
    // Not an error: this backend is designed to do nothing after accepting a request.
    // The counters are kept so smoke tests can prove the call site really ran, instead of silently never calling.
    payload.insert(QStringLiteral("effect_requests"), m_effectRequests);
    payload.insert(QStringLiteral("voice_requests"), m_voiceRequests);
    payload.insert(QStringLiteral("bgm_requests"), m_bgmRequests);
    return payload;
}
