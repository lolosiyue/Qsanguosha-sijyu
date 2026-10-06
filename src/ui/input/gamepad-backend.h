#ifndef QSAN_GAMEPAD_BACKEND_H
#define QSAN_GAMEPAD_BACKEND_H

#include "ui-action.h"

#include <QObject>
#include <QString>

// Positional buttons, matching SDL3's south/east/west/north naming.
enum class GamepadButton : int
{
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight
};

enum class GamepadAxis : int
{
    LeftX,
    LeftY,      // Positive is down, as in SDL.
    RightX,
    RightY,
    LeftTrigger,  // 0..1
    RightTrigger  // 0..1
};

enum class GamepadType : int
{
    Unknown,
    Xbox360,
    XboxOne,
    PlayStation3,
    PlayStation4,
    PlayStation5,
    NintendoSwitchPro,
    NintendoJoyCon,
    Generic
};

GlyphStyle glyphStyleForGamepadType(GamepadType type);

// Device source for GamepadService. Implementations deliver every signal from
// poll(), on the thread that owns the service, so the service needs no locking.
class GamepadBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~GamepadBackend() override = default;

    virtual QString name() const = 0;
    virtual bool start(QString *error) = 0;
    virtual void stop() = 0;
    virtual void poll() = 0;

signals:
    void deviceAdded(int deviceId, const QString &name, GamepadType type);
    void deviceRemoved(int deviceId);
    void buttonChanged(int deviceId, GamepadButton button, bool pressed);
    // Sticks are -1..1, triggers 0..1.
    void axisChanged(int deviceId, GamepadAxis axis, double value);
};

#endif
