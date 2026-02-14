#include "audio.h"
#include "settings.h"

class Sound;

static FMOD_SOUND *BGM;
static FMOD_SYSTEM *System;
static FMOD_CHANNEL *BGMChannel;
static QCache<QString, Sound> SoundCache;

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

    void play()
    {
        if (sound) {
            if (FMOD_System_PlaySound(System, FMOD_CHANNEL_FREE, sound, false, &channel) == FMOD_OK) {
                FMOD_Channel_SetVolume(channel, Config.EffectVolume);
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

void Audio::init()
{
    if (FMOD_System_Create(&System) != FMOD_OK) return;

    FMOD_System_Init(System, 100, 0, nullptr);
}

void Audio::quit()
{
    if (System) {
        SoundCache.clear();
        FMOD_System_Release(System);

        System = nullptr;
    }
}

void Audio::play(const QString &filename, bool superpose)
{
    Sound *sound = SoundCache[filename];
	if(sound){
		if (!superpose && sound->isPlaying())
			return;
	}else{
        sound = new Sound(filename);
        SoundCache.insert(filename, sound);
	}

    sound->play();
}

void Audio::stop()
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

static QString bgmPlaying;

void Audio::playBGM(const QString &filename)
{
    if (bgmPlaying == filename) return;
    if (FMOD_System_CreateStream(System, filename.toLocal8Bit(), FMOD_LOOP_NORMAL, nullptr, &BGM) == FMOD_OK) {
		bgmPlaying = filename;
        FMOD_Sound_SetLoopCount(BGM, -1);
        FMOD_System_PlaySound(System, FMOD_CHANNEL_FREE, BGM, false, &BGMChannel);

        FMOD_System_Update(System);
    }
}

void Audio::setBGMVolume(float volume)
{
    if (BGMChannel) FMOD_Channel_SetVolume(BGMChannel, volume);
}

void Audio::stopBGM()
{
	bgmPlaying = "";
    if (BGMChannel) FMOD_Channel_Stop(BGMChannel);
}

QString Audio::getVersion()
{
    unsigned int version = 0;
    FMOD_System_GetVersion(System, &version);// convert it to QString
    return QString("%1.%2.%3").arg((version & 0xFFFF0000) >> 16, 0, 16)
        .arg((version & 0xFF00) >> 8, 2, 16, QChar('0'))
        .arg((version & 0xFF), 2, 16, QChar('0'));
}

