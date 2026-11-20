#include "controller-service.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHash>
#include <QJniObject>
#include <QKeyEvent>
#include <QTimer>

namespace {
bool remoteAction(int key, ControllerAction *action)
{
    switch (key) {
    case Qt::Key_Up: *action = ControllerAction::Up; break;
    case Qt::Key_Down: *action = ControllerAction::Down; break;
    case Qt::Key_Left: *action = ControllerAction::Left; break;
    case Qt::Key_Right: *action = ControllerAction::Right; break;
    // Qt's Android plugin and Bluetooth remotes use different select key codes.
    case Qt::Key_Select: case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Space:
        *action = ControllerAction::Activate; break;
    case Qt::Key_Back: case Qt::Key_Escape:
        *action = ControllerAction::Back; break;
    default: return false;
    }
    return true;
}
bool direction(ControllerAction action)
{
    return action == ControllerAction::Up || action == ControllerAction::Down
        || action == ControllerAction::Left || action == ControllerAction::Right;
}
}

struct ControllerService::Impl {
    bool enabled = false;
    quint64 epoch = 1;
    QElapsedTimer clock;
    QHash<int, qint64> held;
};

bool ControllerService::isTelevision()
{
    if (qEnvironmentVariableIsSet("QSAN_ANDROID_TV"))
        return qEnvironmentVariableIntValue("QSAN_ANDROID_TV") != 0;
    static const bool television = [] {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return false;
        const auto packages = context.callObjectMethod("getPackageManager", "()Landroid/content/pm/PackageManager;");
        for (const char *name : {"android.software.leanback", "android.hardware.type.television"}) {
            const auto feature = QJniObject::fromString(QString::fromLatin1(name));
            if (packages.isValid() && packages.callMethod<jboolean>("hasSystemFeature", "(Ljava/lang/String;)Z", feature.object()))
                return true;
        }
        return false;
    }();
    return television;
}

ControllerService::ControllerService(QObject *parent) : QObject(parent), d(new Impl)
{
    d->enabled = isTelevision();
    if (!d->enabled) return; // Preserve touch/keyboard behavior on phones and tablets.
    d->clock.start();
    qApp->installEventFilter(this);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState) {
        // Discard queued actions and held keys across the Android activity boundary.
        ++d->epoch;
        d->held.clear();
        emit deviceChanged(QStringLiteral("Android TV remote"), d->epoch);
    });
    QTimer::singleShot(0, this, [this] {
        emit deviceChanged(QStringLiteral("Android TV remote"), d->epoch);
    });
}

ControllerService::~ControllerService()
{
    if (qApp) qApp->removeEventFilter(this);
}

bool ControllerService::isAvailable() const { return d->enabled; }
bool ControllerService::hasConnectedDevice() const { return d->enabled; }
QString ControllerService::error() const { return {}; }
quint64 ControllerService::deviceEpoch() const { return d->epoch; }
bool ControllerService::isDirectionalRemote() const { return d->enabled; }
QVariantMap ControllerService::buttonLabels() const
{
    if (!d->enabled) return {};
    return {{"remote", true}, {"south", QStringLiteral("OK")}, {"east", QStringLiteral("Back")}};
}

bool ControllerService::acceptsNativeKeyEvent(const QEvent *event) const
{
    if (!d->enabled || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)) return false;
    const auto *key = static_cast<const QKeyEvent *>(event);
    ControllerAction mapped;
    return !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier | Qt::ShiftModifier))
        && remoteAction(key->key(), &mapped);
}

bool ControllerService::eventFilter(QObject *, QEvent *event)
{
    // A synchronous Qt activation can open exec(). Native remote events inside
    // that nested loop remain physical even while a synthetic scope exists.
    if (!event->spontaneous() || !acceptsNativeKeyEvent(event)) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    ControllerAction mapped;
    remoteAction(key->key(), &mapped);
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        d->held.clear();
        return true;
    }
    if (key->type() == QEvent::KeyRelease) {
        // Qt can pair autorepeat presses with synthetic autorepeat releases.
        if (!key->isAutoRepeat() && d->held.remove(key->key()))
            emit deviceInput(controllerActionName(mapped), 0, d->epoch);
        return true;
    }
    const qint64 now = d->clock.elapsed();
    if (!key->isAutoRepeat()) {
        // Android can lose the matching release when a modal window takes
        // focus. A fresh non-repeat press is still a new physical edge.
        d->held.insert(key->key(), now + 360);
        emit deviceInput(controllerActionName(mapped), 1, d->epoch);
        emit action(mapped, d->epoch, false);
    } else if (d->held.contains(key->key()) && direction(mapped) && now >= d->held.value(key->key())) {
        d->held[key->key()] = now + 150;
        emit action(mapped, d->epoch, true);
    }
    return true; // Never let the same physical key also enter the legacy keyboard path.
}

// SDL diagnostic devices are desktop-only. Android acceptance uses OS key events.
bool ControllerService::attachVirtualDevice() { return false; }
bool ControllerService::setVirtualButton(const QString &, bool) { return false; }
bool ControllerService::setVirtualAxis(const QString &, int) { return false; }
void ControllerService::detachVirtualDevice() {}
