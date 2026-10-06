#include "sdl3-gamepad-backend.h"

#include <QDebug>

#include <SDL3/SDL.h>

namespace {
GamepadType typeOf(SDL_Gamepad *pad)
{
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_XBOX360: return GamepadType::Xbox360;
    case SDL_GAMEPAD_TYPE_XBOXONE: return GamepadType::XboxOne;
    case SDL_GAMEPAD_TYPE_PS3: return GamepadType::PlayStation3;
    case SDL_GAMEPAD_TYPE_PS4: return GamepadType::PlayStation4;
    case SDL_GAMEPAD_TYPE_PS5: return GamepadType::PlayStation5;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return GamepadType::NintendoSwitchPro;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return GamepadType::NintendoJoyCon;
    default:
        break;
    }
    // Unlisted pads: fall back to what SDL says is printed on the bottom button.
    switch (SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_SOUTH)) {
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return GamepadType::PlayStation4;
    case SDL_GAMEPAD_BUTTON_LABEL_B: return GamepadType::NintendoSwitchPro;
    default: return GamepadType::Generic;
    }
}

bool buttonOf(Uint8 button, GamepadButton *out)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: *out = GamepadButton::South; return true;
    case SDL_GAMEPAD_BUTTON_EAST: *out = GamepadButton::East; return true;
    case SDL_GAMEPAD_BUTTON_WEST: *out = GamepadButton::West; return true;
    case SDL_GAMEPAD_BUTTON_NORTH: *out = GamepadButton::North; return true;
    case SDL_GAMEPAD_BUTTON_BACK: *out = GamepadButton::Back; return true;
    case SDL_GAMEPAD_BUTTON_GUIDE: *out = GamepadButton::Guide; return true;
    case SDL_GAMEPAD_BUTTON_START: *out = GamepadButton::Start; return true;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: *out = GamepadButton::LeftStick; return true;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: *out = GamepadButton::RightStick; return true;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: *out = GamepadButton::LeftShoulder; return true;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: *out = GamepadButton::RightShoulder; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: *out = GamepadButton::DpadUp; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: *out = GamepadButton::DpadDown; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: *out = GamepadButton::DpadLeft; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: *out = GamepadButton::DpadRight; return true;
    default: return false;
    }
}

bool axisOf(Uint8 axis, GamepadAxis *out)
{
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX: *out = GamepadAxis::LeftX; return true;
    case SDL_GAMEPAD_AXIS_LEFTY: *out = GamepadAxis::LeftY; return true;
    case SDL_GAMEPAD_AXIS_RIGHTX: *out = GamepadAxis::RightX; return true;
    case SDL_GAMEPAD_AXIS_RIGHTY: *out = GamepadAxis::RightY; return true;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: *out = GamepadAxis::LeftTrigger; return true;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: *out = GamepadAxis::RightTrigger; return true;
    default: return false;
    }
}
}

Sdl3GamepadBackend::Sdl3GamepadBackend(QObject *parent)
    : GamepadBackend(parent)
{
}

Sdl3GamepadBackend::~Sdl3GamepadBackend()
{
    stop();
}

QString Sdl3GamepadBackend::name() const
{
    return QStringLiteral("SDL3 %1.%2.%3").arg(SDL_MAJOR_VERSION).arg(SDL_MINOR_VERSION).arg(SDL_MICRO_VERSION);
}

bool Sdl3GamepadBackend::start(QString *error)
{
    if (m_started) return true;
    // Qt owns signals, windows and focus. SDL has no window, so it must be told
    // that background input is wanted; the dispatcher drops it while unfocused.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        if (error) *error = QString::fromUtf8(SDL_GetError());
        return false;
    }
    m_started = true;
    // Already-connected pads arrive as SDL_EVENT_GAMEPAD_ADDED on the first poll.
    return true;
}

void Sdl3GamepadBackend::stop()
{
    if (!m_started) return;
    const auto ids = m_pads.keys();
    for (unsigned int id : ids) closeDevice(id);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    m_started = false;
}

void Sdl3GamepadBackend::poll()
{
    if (!m_started) return;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            openDevice(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            closeDevice(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            GamepadButton button;
            if (m_pads.contains(event.gbutton.which) && buttonOf(event.gbutton.button, &button))
                emit buttonChanged(int(event.gbutton.which), button, event.gbutton.down);
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            GamepadAxis axis;
            if (!m_pads.contains(event.gaxis.which) || !axisOf(event.gaxis.axis, &axis)) break;
            const double value = event.gaxis.value < 0 ? event.gaxis.value / 32768.0 : event.gaxis.value / 32767.0;
            emit axisChanged(int(event.gaxis.which), axis, value);
            break;
        }
        default:
            break;
        }
    }
}

void Sdl3GamepadBackend::openDevice(unsigned int instanceId)
{
    if (m_pads.contains(instanceId)) return;
    SDL_Gamepad *pad = SDL_OpenGamepad(instanceId);
    if (!pad) {
        qWarning() << "Gamepad: cannot open device" << instanceId << SDL_GetError();
        return;
    }
    m_pads.insert(instanceId, pad);
    const QString padName = QString::fromUtf8(SDL_GetGamepadName(pad));
    const GamepadType type = typeOf(pad);
    qInfo().noquote() << "Gamepad connected:" << padName << "id" << instanceId
                      << "glyphs" << QSanInput::glyphStyleName(glyphStyleForGamepadType(type));
    emit deviceAdded(int(instanceId), padName, type);
}

void Sdl3GamepadBackend::closeDevice(unsigned int instanceId)
{
    SDL_Gamepad *pad = m_pads.take(instanceId);
    if (!pad) return;
    SDL_CloseGamepad(pad);
    qInfo() << "Gamepad disconnected: id" << instanceId;
    emit deviceRemoved(int(instanceId));
}
