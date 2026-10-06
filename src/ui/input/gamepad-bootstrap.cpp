#include "gamepad-bootstrap.h"

#include "gamepad-service.h"
#include "input-mode-tracker.h"
#include "settings.h"
#include "ui-action-dispatcher.h"

#if QSAN_HAS_SDL3
#include "sdl3-gamepad-backend.h"
#endif

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QPointer>

namespace {
QPointer<GamepadService> g_service;
}

namespace QSanInput {

bool bigPictureActive()
{
    static const bool active = [] {
        if (QCoreApplication::instance()
            && QCoreApplication::arguments().contains(QStringLiteral("--big-picture")))
            return true;
        return Config.value(QStringLiteral("BigPicture/Enabled"), false).toBool();
    }();
    return active;
}

GamepadService *gamepadService()
{
    return g_service;
}

void installGamepadInput(QObject *parent)
{
    if (g_service) return;
    const bool bigPicture = bigPictureActive();
    if (!Config.value(QStringLiteral("Gamepad/Enabled"), bigPicture).toBool()) return;

    InputModeTracker::instance()->setHideCursorInGamepadMode(bigPicture);
    UiActionDispatcher::instance();

#if QSAN_HAS_SDL3
    auto *service = new GamepadService(new Sdl3GamepadBackend, parent);
#else
    auto *service = new GamepadService(nullptr, parent);
#endif
    QObject::connect(service, &GamepadService::actionTriggered, service, [](UiAction action, bool) {
        // SDL keeps reporting while another application has focus; never act then.
        if (QGuiApplication::applicationState() != Qt::ApplicationActive) return;
        UiActionDispatcher::instance()->dispatch(action);
    });
    QObject::connect(service, &GamepadService::glyphStyleChanged, service,
                     [](GlyphStyle style) { setCurrentGlyphStyle(style); });
    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, service,
                     [service](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) service->releaseAll();
    });
    QString error;
    if (!service->start(&error)) {
        qWarning().noquote() << "Gamepad input unavailable:" << error;
        delete service;
        return;
    }
    g_service = service;
    qInfo().noquote() << "Gamepad input started:" << service->backendName();
}

} // namespace QSanInput
