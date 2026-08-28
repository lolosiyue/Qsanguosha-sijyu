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
    // 唔係錯誤:呢個 backend 就係設計成收咗請求之後乜都唔做。計數留低,
    // smoke 先至可以證明 call site 真係行過,而唔係靜靜地冇 call。
    payload.insert(QStringLiteral("effect_requests"), m_effectRequests);
    payload.insert(QStringLiteral("voice_requests"), m_voiceRequests);
    payload.insert(QStringLiteral("bgm_requests"), m_bgmRequests);
    return payload;
}
