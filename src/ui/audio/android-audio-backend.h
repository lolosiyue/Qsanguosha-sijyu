#ifndef QSAN_ANDROID_AUDIO_BACKEND_H
#define QSAN_ANDROID_AUDIO_BACKEND_H

#include "audio-backend.h"

// Android playback goes through org.qsanguosha.game.AudioBridge (SoundPool +
// MediaPlayer). Qt Multimedia stays available for video, but this backend does
// not open an AAudio stream.
class AndroidAudioBackend final : public IAudioBackend
{
public:
    AndroidAudioBackend() = default;
    ~AndroidAudioBackend() override;

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
    QString resolve(const QString &filename) const;
    void pushVolumes() const;
    QString bridgeError() const;

    AudioVolumes m_volumes;
    bool m_ready = false;
    bool m_hasOutputDevice = false;
    QString m_lastError;
};

#endif
