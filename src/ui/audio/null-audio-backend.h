#ifndef QSAN_NULL_AUDIO_BACKEND_H
#define QSAN_NULL_AUDIO_BACKEND_H

#include "audio-backend.h"

// The silent backend. Used in three cases:
//   * QSAN_AUDIO_BACKEND=NULL builds (dedicated server / CI);
//   * Windows Debug (FMOD links only in Release - silent, same as the old behavior);
//   * fallback when a real backend's initialize() fails.
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
