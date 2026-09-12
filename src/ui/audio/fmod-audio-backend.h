#ifndef QSAN_FMOD_AUDIO_BACKEND_H
#define QSAN_FMOD_AUDIO_BACKEND_H

#include "audio-backend.h"

// Backend for Windows GUI Release. The implementation was moved verbatim from
// src/core/audio.cpp: FMOD call order and parameters are unchanged - M2B-A does not alter Windows playback behavior.
//
// This header never includes any FMOD header: the bundled FMOD header is only on
// the Windows Release include path; putting it here would break Linux / Debug builds.
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
