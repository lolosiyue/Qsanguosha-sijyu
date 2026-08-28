#ifndef QSAN_NULL_AUDIO_BACKEND_H
#define QSAN_NULL_AUDIO_BACKEND_H

#include "audio-backend.h"

// 冇聲音的 backend。三種情況會用到:
//   * QSAN_AUDIO_BACKEND=NULL 的 build(dedicated server／CI);
//   * Windows Debug(FMOD 只喺 Release 連結,同舊行為一樣冇聲);
//   * 真 backend initialize() 失敗時的降級。
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
