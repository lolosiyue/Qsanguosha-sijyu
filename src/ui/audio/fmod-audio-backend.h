#ifndef QSAN_FMOD_AUDIO_BACKEND_H
#define QSAN_FMOD_AUDIO_BACKEND_H

#include "audio-backend.h"

// Windows Release backend. FMOD headers stay out of this file because they are unavailable to Linux and Debug builds.




class FmodAudioBackend final : public IAudioBackend
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
    AudioVolumes m_volumes;
};

#endif
