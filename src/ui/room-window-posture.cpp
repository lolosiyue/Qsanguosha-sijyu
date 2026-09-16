#include "room-window-posture.h"

#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QScreen>
#include <QWindow>
#include <QMetaObject>
#include <QStringList>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace {
QMutex postureInstanceMutex;
QPointer<RoomWindowPosture> postureInstance;

#ifdef Q_OS_ANDROID
void onAndroidPostureChanged(JNIEnv *, jclass, jint generation, jint mode,
                             jint left, jint top,
                             jint right, jint bottom, jboolean separating,
                             jboolean occluding)
{
    QMutexLocker locker(&postureInstanceMutex);
    QPointer<RoomWindowPosture> receiver = postureInstance;
    if (!receiver)
        return;

    const auto decodedMode = mode == 1 ? RoomWindowPosture::Mode::Book
        : mode == 2 ? RoomWindowPosture::Mode::Tabletop
                    : RoomWindowPosture::Mode::None;
    QMetaObject::invokeMethod(receiver.data(), [receiver, generation, decodedMode,
                                                 left, top, right, bottom,
                                                 separating, occluding]() {
        if (!receiver)
            return;
        QScreen *screen = nullptr;
        if (QWindow *window = QGuiApplication::focusWindow())
            screen = window->screen();
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        qreal scale = screen ? screen->devicePixelRatio() : 1.0;
        if (scale <= 0.0)
            scale = 1.0;
        const QRectF bounds(left / scale, top / scale,
                            (right - left) / scale, (bottom - top) / scale);
        receiver->updateFromPlatform(generation,
            {decodedMode, bounds, bool(separating), bool(occluding)});
    }, Qt::QueuedConnection);
}

bool registerAndroidPostureCallback()
{
    static bool registered = false;
    if (registered)
        return true;

    const JNINativeMethod methods[] = {
        {const_cast<char *>("nativeOnPostureChanged"),
         const_cast<char *>("(IIIIIIZZ)V"),
         reinterpret_cast<void *>(onAndroidPostureChanged)}
    };
    QJniEnvironment environment;
    registered = environment.registerNativeMethods(
        "org/qsanguosha/game/RoomWindowPostureBridge", methods, 1);
    return registered;
}
#endif

bool parseBoolean(const QString &text, bool *ok)
{
    if (text == QLatin1String("1")
            || text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
        *ok = true;
        return true;
    }
    if (text == QLatin1String("0")
            || text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
        *ok = true;
        return false;
    }
    *ok = false;
    return false;
}
}

RoomWindowPosture::RoomWindowPosture(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<RoomWindowPosture::Value>();
    {
        QMutexLocker locker(&postureInstanceMutex);
        postureInstance = this;
    }
    m_injected = loadEnvironmentInjection();

    if (!qGuiApp)
        return;

    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
        if (m_injected)
            return;
        if (state == Qt::ApplicationActive)
            startPlatformListener();
        else
            stopPlatformListener();
    });

    if (!m_injected && qGuiApp->applicationState() == Qt::ApplicationActive)
        startPlatformListener();
}

RoomWindowPosture::~RoomWindowPosture()
{
    stopPlatformListener();
    QMutexLocker locker(&postureInstanceMutex);
    if (postureInstance == this)
        postureInstance = nullptr;
}

void RoomWindowPosture::inject(Mode mode, const QRectF &bounds,
                               bool separating, bool occluding)
{
    m_injected = true;
    setValue({mode, bounds, separating, occluding});
    stopPlatformListener();
}

void RoomWindowPosture::clearInjection()
{
    m_injected = false;
    setValue({});
    if (qGuiApp && qGuiApp->applicationState() == Qt::ApplicationActive)
        startPlatformListener();
}

void RoomWindowPosture::setResponsivePreview(bool enabled)
{
#ifdef Q_OS_ANDROID
    const QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        "org/qsanguosha/game/RoomWindowPostureBridge", "setResponsivePreview",
        "(Landroid/app/Activity;Z)V", activity.object<jobject>(),
        jboolean(enabled));
#else
    Q_UNUSED(enabled);
#endif
}

void RoomWindowPosture::setValue(const Value &value)
{
    if (m_value == value)
        return;
    m_value = value;
    emit postureChanged(m_value);
}

void RoomWindowPosture::updateFromPlatform(int generation, const Value &value)
{
    if (!m_injected && generation == m_platformGeneration)
        setValue(value);
}

bool RoomWindowPosture::loadEnvironmentInjection()
{
    const QString specification = qEnvironmentVariable("QSAN_WINDOW_POSTURE").trimmed();
    if (specification.isEmpty())
        return false;

    const QStringList parts = specification.split(QLatin1Char(','));
    if (parts.size() != 7)
        return false;

    Mode mode = Mode::None;
    if (parts[0].compare(QLatin1String("book"), Qt::CaseInsensitive) == 0)
        mode = Mode::Book;
    else if (parts[0].compare(QLatin1String("tabletop"), Qt::CaseInsensitive) == 0)
        mode = Mode::Tabletop;
    else if (parts[0].compare(QLatin1String("none"), Qt::CaseInsensitive) != 0)
        return false;

    bool ok[6] = {};
    const qreal left = parts[1].toDouble(&ok[0]);
    const qreal top = parts[2].toDouble(&ok[1]);
    const qreal right = parts[3].toDouble(&ok[2]);
    const qreal bottom = parts[4].toDouble(&ok[3]);
    const bool separating = parseBoolean(parts[5], &ok[4]);
    const bool occluding = parseBoolean(parts[6], &ok[5]);
    for (bool valid : ok) {
        if (!valid)
            return false;
    }
    if (right < left || bottom < top)
        return false;

    m_value = {mode, QRectF(left, top, right - left, bottom - top),
               separating, occluding};
    return true;
}

void RoomWindowPosture::startPlatformListener()
{
#ifdef Q_OS_ANDROID
    if (!registerAndroidPostureCallback())
        return;
    const QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return;
    const int generation = ++m_platformGeneration;
    QJniObject::callStaticMethod<void>(
        "org/qsanguosha/game/RoomWindowPostureBridge", "attach",
        "(Landroid/app/Activity;I)V", activity.object<jobject>(), generation);
#endif
}

void RoomWindowPosture::stopPlatformListener()
{
#ifdef Q_OS_ANDROID
    ++m_platformGeneration;
    QJniObject::callStaticMethod<void>(
        "org/qsanguosha/game/RoomWindowPostureBridge", "detach", "()V");
#endif
}
