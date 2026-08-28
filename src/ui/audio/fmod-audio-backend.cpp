#include "fmod-audio-backend.h"

#include <QCache>
#include <QList>
#include <QString>

#include <fmod.h>

// 由 src/core/audio.cpp 搬過嚟。FMOD 的呼叫次序、參數同 cache 策略全部保持
// 原樣:M2B-A 只係將佢放入 IAudioBackend,唔改 Windows 的播放行為。
//
// 唯一新增的係音量來源:以前每次播放都直接讀 Config.EffectVolume,而家由
// facade 推落嚟的 AudioVolumes 提供。預設值(master=1、voice=1、mute=false)
// 之下 effectGain()/voiceGain() 都等於 Config.EffectVolume,行為不變。
namespace {

class Sound;

FMOD_SOUND *BGM = nullptr;
FMOD_SYSTEM *System = nullptr;
FMOD_CHANNEL *BGMChannel = nullptr;
QCache<QString, Sound> SoundCache;
QString bgmPlaying;

class Sound
{
public:
    Sound(const QString &filename)
        : sound(nullptr), channel(nullptr)
    {
        FMOD_System_CreateSound(System, filename.toLatin1(), FMOD_DEFAULT, nullptr, &sound);
    }

    ~Sound()
    {
        if (sound) FMOD_Sound_Release(sound);
    }

    void play(float volume)
    {
        if (sound) {
            if (FMOD_System_PlaySound(System, FMOD_CHANNEL_FREE, sound, false, &channel) == FMOD_OK) {
                FMOD_Channel_SetVolume(channel, volume);
                FMOD_System_Update(System);
            }
        }
    }

    bool isPlaying() const
    {
        if (channel == nullptr) return false;

        FMOD_BOOL is_playing = false;
        FMOD_Channel_IsPlaying(channel, &is_playing);
        return is_playing;
    }

private:
    FMOD_SOUND *sound;
    FMOD_CHANNEL *channel;
};

} // namespace

QString FmodAudioBackend::name() const
{
    return QStringLiteral("fmod");
}

bool FmodAudioBackend::initialize()
{
    if (FMOD_System_Create(&System) != FMOD_OK) {
        System = nullptr;
        return false;
    }

    FMOD_System_Init(System, 100, 0, nullptr);
    return true;
}

void FmodAudioBackend::shutdown()
{
    if (System) {
        SoundCache.clear();
        FMOD_System_Release(System);

        System = nullptr;
        BGM = nullptr;
        BGMChannel = nullptr;
        bgmPlaying.clear();
    }
}

bool FmodAudioBackend::hasOutputDevice() const
{
    if (System == nullptr)
        return false;
    int drivers = 0;
    if (FMOD_System_GetNumDrivers(System, &drivers) != FMOD_OK)
        return false;
    return drivers > 0;
}

void FmodAudioBackend::play(const QString &filename, bool superpose, AudioChannel channel)
{
    if (System == nullptr) return;

    Sound *sound = SoundCache[filename];
    if (sound) {
        if (!superpose && sound->isPlaying())
            return;
    } else {
        sound = new Sound(filename);
        SoundCache.insert(filename, sound);
    }

    sound->play(m_volumes.gainFor(channel));
}

void FmodAudioBackend::stopAll()
{
    if (System == nullptr) return;

    int n;
    FMOD_System_GetChannelsPlaying(System, &n);

    QList<FMOD_CHANNEL *> channels;
    for (int i = 0; i < n; i++) {
        FMOD_CHANNEL *channel;
        if (FMOD_System_GetChannel(System, i, &channel) == FMOD_OK)
            channels << channel;
    }

    foreach (FMOD_CHANNEL *channel, channels)
        FMOD_Channel_Stop(channel);

    stopBGM();

    FMOD_System_Update(System);
}

void FmodAudioBackend::playBGM(const QString &filename)
{
    if (System == nullptr) return;
    if (bgmPlaying == filename) return;
    if (FMOD_System_CreateStream(System, filename.toLocal8Bit(), FMOD_LOOP_NORMAL, nullptr, &BGM) == FMOD_OK) {
        bgmPlaying = filename;
        FMOD_Sound_SetLoopCount(BGM, -1);
        FMOD_System_PlaySound(System, FMOD_CHANNEL_FREE, BGM, false, &BGMChannel);

        FMOD_System_Update(System);
    }
}

void FmodAudioBackend::setBGMVolume(float volume)
{
    m_volumes.bgm = volume;
    if (BGMChannel) FMOD_Channel_SetVolume(BGMChannel, m_volumes.bgmGain());
}

void FmodAudioBackend::stopBGM()
{
    bgmPlaying.clear();
    if (BGMChannel) FMOD_Channel_Stop(BGMChannel);
}

void FmodAudioBackend::applyVolumes(const AudioVolumes &volumes)
{
    m_volumes = volumes;
    if (BGMChannel) FMOD_Channel_SetVolume(BGMChannel, m_volumes.bgmGain());
}

QString FmodAudioBackend::version() const
{
    unsigned int version = 0;
    FMOD_System_GetVersion(System, &version); // convert it to QString
    return QString("%1.%2.%3").arg((version & 0xFFFF0000) >> 16, 0, 16)
        .arg((version & 0xFF00) >> 8, 2, 16, QChar('0'))
        .arg((version & 0xFF), 2, 16, QChar('0'));
}

QJsonObject FmodAudioBackend::diagnostics() const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("backend"), name());
    payload.insert(QStringLiteral("initialized"), System != nullptr);
    payload.insert(QStringLiteral("output_device"), hasOutputDevice());
    payload.insert(QStringLiteral("version"), version());
    payload.insert(QStringLiteral("bgm_source"), bgmPlaying);
    return payload;
}
