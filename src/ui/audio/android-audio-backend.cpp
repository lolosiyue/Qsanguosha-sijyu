#include "android-audio-backend.h"

#ifndef Q_OS_ANDROID
#error AndroidAudioBackend is Android-only
#endif

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace {

const char *const kBridge = "org/qsanguosha/game/AudioBridge";

bool jniFailed()
{
    QJniEnvironment environment;
    if (!environment->ExceptionCheck())
        return false;
    environment->ExceptionDescribe();
    environment->ExceptionClear();
    return true;
}

} // namespace

AndroidAudioBackend::~AndroidAudioBackend()
{
    shutdown();
}

QString AndroidAudioBackend::name() const
{
    return QStringLiteral("android");
}

bool AndroidAudioBackend::initialize()
{
    if (m_ready)
        return true;

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        m_lastError = QStringLiteral("no android context");
        return false;
    }

    const jboolean started = QJniObject::callStaticMethod<jboolean>(
        kBridge, "start", "(Landroid/content/Context;)Z", context.object<jobject>());
    if (jniFailed() || !started) {
        m_lastError = bridgeError();
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("AudioBridge.start failed");
        return false;
    }

    m_ready = true;
    m_hasOutputDevice = QJniObject::callStaticMethod<jboolean>(
        kBridge, "hasOutputDevice", "()Z");
    if (jniFailed())
        m_hasOutputDevice = false;
    return true;
}

void AndroidAudioBackend::shutdown()
{
    if (!m_ready)
        return;
    QJniObject::callStaticMethod<void>(kBridge, "shutdown", "()V");
    jniFailed();
    m_ready = false;
    m_hasOutputDevice = false;
}

bool AndroidAudioBackend::hasOutputDevice() const
{
    return m_hasOutputDevice;
}

QString AndroidAudioBackend::resolve(const QString &filename) const
{
    if (filename.isEmpty())
        return QString();
    const QFileInfo info(filename);
    return info.isAbsolute() ? info.absoluteFilePath()
                             : QDir::current().absoluteFilePath(filename);
}

void AndroidAudioBackend::play(const QString &filename, bool superpose, AudioChannel channel)
{
    if (!m_ready || filename.isEmpty())
        return;
    const QString path = resolve(filename);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        qWarning().noquote() << "AndroidAudioBackend: missing audio file" << path;
        return;
    }
    const QJniObject jpath = QJniObject::fromString(path);
    QJniObject::callStaticMethod<void>(
        kBridge, "play", "(Ljava/lang/String;ZZ)V",
        jpath.object<jstring>(), jboolean(superpose),
        jboolean(channel == AudioChannel::Effect));
    jniFailed();
}

void AndroidAudioBackend::stopAll()
{
    if (!m_ready)
        return;
    QJniObject::callStaticMethod<void>(kBridge, "stopAll", "()V");
    jniFailed();
}

void AndroidAudioBackend::playBGM(const QString &filename)
{
    if (!m_ready || filename.isEmpty())
        return;
    const QString path = resolve(filename);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        qWarning().noquote() << "AndroidAudioBackend: missing BGM file" << path;
        return;
    }
    const QJniObject jpath = QJniObject::fromString(path);
    QJniObject::callStaticMethod<void>(
        kBridge, "playBgm", "(Ljava/lang/String;)V", jpath.object<jstring>());
    jniFailed();
}

void AndroidAudioBackend::setBGMVolume(float volume)
{
    m_volumes.bgm = qBound(0.0f, volume, 1.0f);
    pushVolumes();
}

void AndroidAudioBackend::stopBGM()
{
    if (!m_ready)
        return;
    QJniObject::callStaticMethod<void>(kBridge, "stopBgm", "()V");
    jniFailed();
}

void AndroidAudioBackend::setApplicationSuspended(bool suspended)
{
    if (!m_ready)
        return;
    QJniObject::callStaticMethod<void>(
        kBridge, "setSuspended", "(Z)V", jboolean(suspended));
    jniFailed();
}

void AndroidAudioBackend::applyVolumes(const AudioVolumes &volumes)
{
    m_volumes = volumes;
    pushVolumes();
}

void AndroidAudioBackend::pushVolumes() const
{
    if (!m_ready)
        return;
    QJniObject::callStaticMethod<void>(
        kBridge, "setVolumes", "(FFF)V",
        jfloat(m_volumes.effectGain()), jfloat(m_volumes.voiceGain()),
        jfloat(m_volumes.bgmGain()));
    jniFailed();
}

QString AndroidAudioBackend::version() const
{
    if (!m_ready)
        return QStringLiteral("n/a");
    const jint sdk = QJniObject::callStaticMethod<jint>(kBridge, "sdk", "()I");
    if (jniFailed())
        return QStringLiteral("android");
    return QString::number(sdk);
}

QString AndroidAudioBackend::bridgeError() const
{
    const QJniObject error = QJniObject::callStaticObjectMethod(
        kBridge, "lastError", "()Ljava/lang/String;");
    if (jniFailed() || !error.isValid())
        return QString();
    return error.toString();
}

QJsonObject AndroidAudioBackend::diagnostics() const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("backend"), name());
    payload.insert(QStringLiteral("initialized"), m_ready);
    payload.insert(QStringLiteral("output_device"), m_hasOutputDevice);
    payload.insert(QStringLiteral("sdk"), version());
    payload.insert(QStringLiteral("last_error"), m_ready ? bridgeError() : m_lastError);
    return payload;
}
