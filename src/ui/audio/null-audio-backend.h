#ifndef QSAN_NULL_AUDIO_BACKEND_H
#define QSAN_NULL_AUDIO_BACKEND_H

#include "audio-backend.h"

// Silent backend for NULL builds, Windows Debug and failed real-backend initialization.



class NullAudioBackend final : public IAudioBackend
{
public:
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
    int m_effectRequests = 0;
    int m_voiceRequests = 0;
    int m_bgmRequests = 0;
};

#endif
