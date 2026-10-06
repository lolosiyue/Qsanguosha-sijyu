#include "controller-service.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QSettings>
#include <QHash>
#include <QDebug>
#include <array>
#include <cmath>
#include <SDL3/SDL.h>

namespace {
struct Binding { SDL_GamepadButton button; ControllerAction action; };
const Binding bindings[] = {
    {SDL_GAMEPAD_BUTTON_DPAD_UP, ControllerAction::Up},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, ControllerAction::Down},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, ControllerAction::Left},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, ControllerAction::Right},
    {SDL_GAMEPAD_BUTTON_SOUTH, ControllerAction::Activate},
    {SDL_GAMEPAD_BUTTON_WEST, ControllerAction::Submit},
    {SDL_GAMEPAD_BUTTON_EAST, ControllerAction::Back},
    {SDL_GAMEPAD_BUTTON_NORTH, ControllerAction::Inspect},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, ControllerAction::PreviousGroup},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, ControllerAction::NextGroup},
    {SDL_GAMEPAD_BUTTON_START, ControllerAction::Menu},
    {SDL_GAMEPAD_BUTTON_BACK, ControllerAction::Recover},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, ControllerAction::MoveEarlier},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, ControllerAction::MoveLater}
};
bool repeatable(ControllerAction a)
{
    return a == ControllerAction::Up || a == ControllerAction::Down
        || a == ControllerAction::Left || a == ControllerAction::Right
        || a == ControllerAction::PreviousPage || a == ControllerAction::NextPage;
}
}

struct ControllerService::Impl {
    SDL_Gamepad *pad = nullptr;
    SDL_Joystick *virtualJoystick = nullptr;
    SDL_JoystickID virtualId = 0;
    bool initialized = false;
    bool neutralRequired = true;
    bool diagnosticOwnership = false;
    quint64 epoch = 0;
    QString failure;
    QElapsedTimer clock;
    QHash<int, qint64> repeatAt;
    QHash<int, ControllerAction> held;
    QHash<int, ControllerAction> configured;
    double deadzone = 0.25;
    int axisDirection[2] = {0, 0};
    int axes[SDL_GAMEPAD_AXIS_COUNT] = {};
};

ControllerService::ControllerService(QObject *parent) : QObject(parent), d(new Impl)
{
    // Controller input must never enter a suspended application. We deliberately
    // require neutral after activation/hotplug instead of replaying held buttons.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    d->initialized = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (!d->initialized) {
        d->failure = QString::fromUtf8(SDL_GetError());
        qWarning() << "Controller initialization failed:" << d->failure;
        return;
    }
    QSettings settings;
    d->deadzone = qBound(0.15, settings.value("Controller/Deadzone", 0.25).toDouble(), 0.6);
    const QString mappings = settings.value("Controller/MappingFile").toString();
    if (!mappings.isEmpty()) SDL_AddGamepadMappingsFromFile(mappings.toUtf8().constData());
    for (const auto &binding : bindings) {
        ControllerAction mapped = binding.action;
        const QString key = QStringLiteral("Controller/Bindings/")
            + QString::fromUtf8(SDL_GetGamepadStringForButton(binding.button));
        controllerActionFromName(settings.value(key).toString(), &mapped);
        d->configured.insert(binding.button, mapped);
    }
    d->clock.start();
    auto *timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(16);
    connect(timer, &QTimer::timeout, this, &ControllerService::poll);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState) {
        d->neutralRequired = true;
        d->held.clear(); d->repeatAt.clear();
    });
    timer->start();
}

ControllerService::~ControllerService()
{
    if (d->pad) SDL_CloseGamepad(d->pad);
    d->pad = nullptr;
    detachVirtualDevice();
    if (d->initialized) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

bool ControllerService::isAvailable() const { return d->initialized; }
bool ControllerService::hasConnectedDevice() const { return d->pad && SDL_GamepadConnected(d->pad); }
QString ControllerService::error() const { return d->failure; }
quint64 ControllerService::deviceEpoch() const { return d->epoch; }

void ControllerService::poll()
{
    SDL_PumpEvents();
    SDL_Event event;
    // SDL owns only its gamepad/joystick subsystem; Qt owns window/keyboard input.
    while (SDL_PollEvent(&event)) {}
    if (d->pad && !SDL_GamepadConnected(d->pad)) {
        SDL_CloseGamepad(d->pad); d->pad = nullptr;
        d->held.clear(); d->repeatAt.clear(); d->neutralRequired = true;
        ++d->epoch;
        emit deviceChanged({}, d->epoch);
    }
    if (!d->pad) {
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (d->virtualId) {
            for (int i = 0; i < count; ++i) if (ids[i] == d->virtualId) d->pad = SDL_OpenGamepad(ids[i]);
        } else if (count && !d->diagnosticOwnership) d->pad = SDL_OpenGamepad(ids[0]); // One owner, no duplicate source.
        SDL_free(ids);
        if (!d->pad) return;
        ++d->epoch;
        d->neutralRequired = true;
        emit deviceChanged(QString::fromUtf8(SDL_GetGamepadName(d->pad)), d->epoch);
    }
    const bool active = QGuiApplication::applicationState() == Qt::ApplicationActive;
    QHash<int, ControllerAction> current;
    for (const auto &binding : bindings)
        if (SDL_GetGamepadButton(d->pad, binding.button))
            current.insert(binding.button, d->configured.value(binding.button));
    const auto axisValue = [this](SDL_GamepadAxis axis) {
        const int value = SDL_GetGamepadAxis(d->pad, axis);
        if (d->axes[axis] != value) {
            d->axes[axis] = value;
            emit deviceInput(QString::fromUtf8(SDL_GetGamepadStringForAxis(axis)), value, d->epoch);
        }
        return value;
    };
    for (int axis = 0; axis < 2; ++axis) {
        const double value = axisValue(static_cast<SDL_GamepadAxis>(axis)) / 32768.0;
        int &direction = d->axisDirection[axis];
        if (std::abs(value) < d->deadzone) direction = 0;
        else if (std::abs(value) > qMin(0.85, d->deadzone + 0.15)) direction = value < 0 ? -1 : 1;
        if (direction) current.insert(100 + axis, axis == 0
            ? (direction < 0 ? ControllerAction::Left : ControllerAction::Right)
            : (direction < 0 ? ControllerAction::Up : ControllerAction::Down));
    }
    for (SDL_GamepadAxis axis : {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}) {
        const int value = axisValue(axis);
        if (value > 18000) current.insert(100 + axis, axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER
            ? ControllerAction::PreviousPage : ControllerAction::NextPage);
    }
    // Right stick scrolls by pages, without firing activation or confirmation.
    const int scroll = axisValue(SDL_GAMEPAD_AXIS_RIGHTY);
    if (std::abs(scroll) > 18000) current.insert(120, scroll < 0
        ? ControllerAction::PreviousPage : ControllerAction::NextPage);
    if (!active) d->neutralRequired = true;
    if (d->neutralRequired) {
        d->held = current; d->repeatAt.clear();
        if (active && current.isEmpty()) d->neutralRequired = false;
        return;
    }
    const qint64 now = d->clock.elapsed();
    // Publish releases before actions; an action may open a nested dialog loop.
    for (auto it = d->held.cbegin(); it != d->held.cend(); ++it)
        if (!current.contains(it.key())) {
            emit deviceInput(QString::number(it.key()), 0, d->epoch);
            d->repeatAt.remove(it.key());
        }
    const auto previous = d->held;
    d->held = current;
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        const bool edge = !previous.contains(it.key()) || previous.value(it.key()) != it.value();
        if (edge) {
            d->repeatAt.insert(it.key(), now + 360);
            emit deviceInput(QString::number(it.key()), 1, d->epoch);
            emit action(it.value(), d->epoch, false);
        } else if (repeatable(it.value()) && d->repeatAt.value(it.key(), now + 1) <= now) {
            d->repeatAt.insert(it.key(), now + 150);
            emit action(it.value(), d->epoch, true);
        }
    }
}

bool ControllerService::attachVirtualDevice()
{
    d->diagnosticOwnership = true;
    if (!d->initialized || d->virtualJoystick) return false;
    if (d->pad) {
        SDL_CloseGamepad(d->pad); d->pad = nullptr;
        ++d->epoch;
    }
    d->held.clear(); d->repeatAt.clear(); d->neutralRequired = true;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "QSanguosha diagnostic virtual gamepad";
    d->virtualId = SDL_AttachVirtualJoystick(&desc);
    if (!d->virtualId) return false;
    d->virtualJoystick = SDL_OpenJoystick(d->virtualId);
    return d->virtualJoystick != nullptr;
}

bool ControllerService::setVirtualButton(const QString &button, bool down)
{
    const QHash<QString, QString> positions{{"south", "a"}, {"east", "b"}, {"west", "x"}, {"north", "y"}};
    const QString name = positions.value(button, button);
    const auto value = SDL_GetGamepadButtonFromString(name.toUtf8().constData());
    return d->virtualJoystick && value != SDL_GAMEPAD_BUTTON_INVALID
        && SDL_SetJoystickVirtualButton(d->virtualJoystick, value, down);
}

bool ControllerService::setVirtualAxis(const QString &axis, int value)
{
    const auto parsed = SDL_GetGamepadAxisFromString(axis.toUtf8().constData());
    return d->virtualJoystick && parsed != SDL_GAMEPAD_AXIS_INVALID
        && value >= -32768 && value <= 32767
        && SDL_SetJoystickVirtualAxis(d->virtualJoystick, parsed, static_cast<Sint16>(value));
}

void ControllerService::detachVirtualDevice()
{
    if (d->pad && SDL_GetGamepadID(d->pad) == d->virtualId) {
        SDL_CloseGamepad(d->pad); d->pad = nullptr;
        d->held.clear(); d->repeatAt.clear(); d->neutralRequired = true;
        ++d->epoch;
        emit deviceChanged({}, d->epoch);
    }
    if (d->virtualJoystick) SDL_CloseJoystick(d->virtualJoystick);
    d->virtualJoystick = nullptr;
    if (d->virtualId) SDL_DetachVirtualJoystick(d->virtualId);
    d->virtualId = 0;
}
