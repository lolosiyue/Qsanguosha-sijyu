#ifndef QSAN_FMOD_AUDIO_BACKEND_H
#define QSAN_FMOD_AUDIO_BACKEND_H

#include "audio-backend.h"

// Windows GUI Release 的 backend。實作係由 src/core/audio.cpp 原封搬過嚟,
// FMOD 的呼叫次序同參數冇改動 —— M2B-A 唔改 Windows 的播放行為。
//
// 呢個 header 唔會 include 任何 FMOD header:bundled FMOD header 只喺 Windows
// Release 的 include path,放喺呢度會令 Linux／Debug build 斷。
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
