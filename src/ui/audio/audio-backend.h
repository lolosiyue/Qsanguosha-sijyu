#ifndef QSAN_AUDIO_BACKEND_H
#define QSAN_AUDIO_BACKEND_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// M2B-A 的 audio backend 抽象。
//
// 產品一直只有 `Audio` 一個 facade（src/core/audio.h），呢度唔會另開第二個
// facade：`Audio` 保持係唯一入口，只係將實作轉交畀下面其中一個 backend。
//
//     Audio  ──►  IAudioBackend
//                   ├── FmodAudioBackend      Windows GUI Release
//                   ├── QtMediaAudioBackend   Linux GUI（Qt Multimedia）
//                   └── NullAudioBackend      dedicated server / 測試 / 降級
//
// backend 的選擇淨係喺 CMake（QSAN_AUDIO_BACKEND）同 audio-backend-factory.cpp
// 一個地方發生，call site 唔會散落 #ifdef Q_OS_LINUX。

// 短 UI 音效同武將語音喺 Qt backend 行兩條唔同的資源路徑（QSoundEffect 對
// player pool），所以 facade 要話畀 backend 知呢一次係邊一類。
enum class AudioChannel
{
    Effect,
    Voice
};

struct AudioVolumes
{
    float master = 1.0f;
    float effect = 1.0f;
    // 語音係音效的一個 sub-trim,而唔係另一條獨立通道:預設 1.0 時
    // voiceGain() == effectGain(),Windows 舊有「語音跟 EffectVolume」的行為
    // 原樣保留。
    float voice = 1.0f;
    // BGM 音量由 call site 明確傳入（Audio::setBGMVolume），呢度只記住最後
    // 一次的值,方便 master／mute 改變時重新套用。
    float bgm = 1.0f;
    bool muted = false;

    float effectGain() const { return muted ? 0.0f : master * effect; }
    float voiceGain() const { return muted ? 0.0f : master * effect * voice; }
    float bgmGain() const { return muted ? 0.0f : master * bgm; }

    float gainFor(AudioChannel channel) const
    {
        return channel == AudioChannel::Voice ? voiceGain() : effectGain();
    }
};

class IAudioBackend
{
public:
    virtual ~IAudioBackend() = default;

    // "fmod" / "qt" / "null"。會出現喺 multimedia smoke 的 report,所以係契約。
    virtual QString name() const = 0;

    // 建立底層資源。回傳 false 代表呢個 backend 喺呢部機用唔到,facade 會轉用
    // NullAudioBackend,而唔係讓 GUI 掛咗。
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // 有冇真正可用的輸出裝置。無音訊裝置唔算 initialize() 失敗:GUI 照跑,
    // 只係聽唔到聲,呢個 flag 令 smoke／診斷可以分辨兩者。
    virtual bool hasOutputDevice() const = 0;

    virtual void play(const QString &filename, bool superpose, AudioChannel channel) = 0;
    virtual void stopAll() = 0;

    virtual void playBGM(const QString &filename) = 0;
    virtual void setBGMVolume(float volume) = 0;
    virtual void stopBGM() = 0;

    // master／effect／voice／mute 改變時由 facade 推落嚟。
    virtual void applyVolumes(const AudioVolumes &volumes) = 0;

    virtual QString version() const = 0;

    // 畀 --multimedia-smoke 同 about dialog 用的結構化狀態。
    virtual QJsonObject diagnostics() const = 0;
};

// 由 CMake 的 QSAN_AUDIO_BACKEND 決定編入邊個實作。實作喺
// audio-backend-factory.cpp,係全個 codebase 唯一做 backend 選擇的地方。
IAudioBackend *createConfiguredAudioBackend();
IAudioBackend *createNullAudioBackend();

// 會預載成低延遲短音效的 audio/system/<name>.ogg 名單。
QStringList shortUiEffectNames();

// 一個播放請求屬於短 UI 音效定係語音／一次性長音效。呢個係唯一的分類點,
// call site 唔需要自己知。
AudioChannel classifyAudioFile(const QString &filename);

#endif
