#include "audio.h"

#include "audio-backend.h"
#include "settings.h"

#include <QDebug>
#ifdef Q_OS_ANDROID
#include <QGuiApplication>
#endif
#include <QJsonArray>

// Implementation of the Audio facade. It only does three things:
//   * pick the backend (once, via createConfiguredAudioBackend());
//   * remember volume state so master / mute can be applied to every channel;
//   * guarantee that no call site crashes when init() has not run or quit() already has.
namespace {

IAudioBackend *g_backend = nullptr;
AudioVolumes g_volumes;
bool g_initialized = false;
bool g_applicationSuspended = false;

AudioVolumes volumesFromConfig()
{
    AudioVolumes volumes;
    volumes.master = qBound(0.0f, Config.MasterVolume, 1.0f);
    volumes.effect = qBound(0.0f, Config.EffectVolume, 1.0f);
    volumes.voice = qBound(0.0f, Config.VoiceVolume, 1.0f);
    volumes.muted = Config.AudioMuted;
    volumes.bgm = g_volumes.bgm;
    return volumes;
}

} // namespace

void Audio::init()
{
    if (g_initialized)
        return;

#ifdef Q_OS_ANDROID
    // MainWindow can be constructed before Audio::init(), and the first
    // application-state callback may therefore arrive before a backend exists.
    // Recover the actual Android state here instead of losing that transition.
    if (qGuiApp) {
        const Qt::ApplicationState state = qGuiApp->applicationState();
        g_applicationSuspended = state != Qt::ApplicationActive;
    }
#endif

    g_backend = createConfiguredAudioBackend();
    if (g_backend && !g_backend->initialize()) {
        // An unusable backend (no FMOD system, missing Qt Multimedia plugin, ...)
        // must not hang the GUI: degrade to the null backend; the game keeps running, just without sound.
        qWarning().noquote() << "Audio: backend" << g_backend->name()
                             << "failed to initialize; falling back to the null backend";
        delete g_backend;
        g_backend = createNullAudioBackend();
        g_backend->initialize();
    }
    g_initialized = g_backend != nullptr;
    if (g_backend)
        g_backend->setApplicationSuspended(g_applicationSuspended);
    applyConfigVolumes();
}

void Audio::quit()
{
    if (!g_backend)
        return;
    g_backend->stopAll();
    g_backend->shutdown();
    delete g_backend;
    g_backend = nullptr;
    g_initialized = false;
}

void Audio::play(const QString &filename, bool superpose)
{
    if (!g_backend)
        return;
    g_backend->play(filename, superpose, classifyAudioFile(filename));
}

void Audio::stop()
{
    if (g_backend)
        g_backend->stopAll();
}

void Audio::playBGM(const QString &filename)
{
    if (g_backend)
        g_backend->playBGM(filename);
}

void Audio::setBGMVolume(float volume)
{
    g_volumes.bgm = qBound(0.0f, volume, 1.0f);
    if (g_backend)
        g_backend->setBGMVolume(g_volumes.bgm);
}

void Audio::stopBGM()
{
    if (g_backend)
        g_backend->stopBGM();
}

void Audio::setApplicationSuspended(bool suspended)
{
    g_applicationSuspended = suspended;
    if (g_backend)
        g_backend->setApplicationSuspended(suspended);
}

QString Audio::getVersion()
{
    return g_backend ? g_backend->version() : QStringLiteral("n/a");
}

QString Audio::backendName()
{
    return g_backend ? g_backend->name() : QStringLiteral("none");
}

bool Audio::isInitialized()
{
    return g_initialized;
}

bool Audio::hasOutputDevice()
{
    return g_backend && g_backend->hasOutputDevice();
}

void Audio::applyConfigVolumes()
{
    g_volumes = volumesFromConfig();
    if (g_backend)
        g_backend->applyVolumes(g_volumes);
}

QJsonObject Audio::diagnostics()
{
    QJsonObject payload;
    payload.insert(QStringLiteral("schema_version"), 1);
    payload.insert(QStringLiteral("backend"), backendName());
    payload.insert(QStringLiteral("initialized"), g_initialized);
    payload.insert(QStringLiteral("master_volume"), g_volumes.master);
    payload.insert(QStringLiteral("effect_volume"), g_volumes.effect);
    payload.insert(QStringLiteral("voice_volume"), g_volumes.voice);
    payload.insert(QStringLiteral("bgm_volume"), g_volumes.bgm);
    payload.insert(QStringLiteral("muted"), g_volumes.muted);
    payload.insert(QStringLiteral("ui_effect_names"),
        QJsonArray::fromStringList(shortUiEffectNames()));
    if (g_backend)
        payload.insert(QStringLiteral("details"), g_backend->diagnostics());
    return payload;
}
