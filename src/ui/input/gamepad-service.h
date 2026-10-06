#ifndef QSAN_GAMEPAD_SERVICE_H
#define QSAN_GAMEPAD_SERVICE_H

#include "gamepad-backend.h"
#include "ui-action.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>

struct GamepadDeviceInfo
{
    int id = -1;
    QString name;
    GamepadType type = GamepadType::Unknown;
};

// Turns raw device state into UiActions: merges D-pad and left stick, applies
// stick/trigger dead zones with hysteresis, auto-repeats held directions and
// tracks hotplugged devices. It never touches widgets; see UiActionDispatcher.
class GamepadService : public QObject
{
    Q_OBJECT
public:
    struct Tuning
    {
        double stickPress = 0.5;    // Stick magnitude that starts a direction.
        double stickRelease = 0.35; // Lower release threshold avoids chatter.
        double triggerPress = 0.5;
        double triggerRelease = 0.3;
        int repeatDelayMs = 400;
        int repeatIntervalMs = 110;
        int pollIntervalMs = 8;
    };

    // Takes ownership of backend; a null backend yields an inert service.
    explicit GamepadService(GamepadBackend *backend, QObject *parent = nullptr);
    ~GamepadService() override;

    void setTuning(const Tuning &tuning) { m_tuning = tuning; }
    const Tuning &tuning() const { return m_tuning; }
    // Milliseconds; tests inject a manual clock.
    void setClock(std::function<qint64()> clock);

    bool start(QString *error = nullptr);
    void stop();
    bool isRunning() const { return m_running; }
    QString backendName() const;

    // Polls the backend and emits due repeats. Driven by a timer when running.
    void tick();
    // Drops held state, e.g. when the application loses focus.
    void releaseAll();

    QList<GamepadDeviceInfo> devices() const { return m_devices; }
    GlyphStyle glyphStyle() const { return m_glyphStyle; }

signals:
    void actionTriggered(UiAction action, bool repeat);
    void devicesChanged();
    void glyphStyleChanged(GlyphStyle style);

private:
    struct DeviceState
    {
        bool dpad[4] = { false, false, false, false };
        int stickDirection = -1; // Index into the direction table, -1 none.
        double stickX = 0;
        double stickY = 0;
        bool held[4] = { false, false, false, false }; // Combined D-pad + stick.
        bool leftTrigger = false;
        bool rightTrigger = false;
    };

    void onDeviceAdded(int deviceId, const QString &name, GamepadType type);
    void onDeviceRemoved(int deviceId);
    void onButton(int deviceId, GamepadButton button, bool pressed);
    void onAxis(int deviceId, GamepadAxis axis, double value);
    void updateStick(int deviceId, DeviceState &state);
    void updateDirections(int deviceId, DeviceState &state);
    void noteDeviceUsed(int deviceId);
    void emitAction(UiAction action, bool repeat = false);
    qint64 now() const;

    std::unique_ptr<GamepadBackend> m_backend;
    QTimer m_timer;
    Tuning m_tuning;
    std::function<qint64()> m_clock;
    bool m_running = false;
    QList<GamepadDeviceInfo> m_devices;
    QHash<int, DeviceState> m_state;
    GlyphStyle m_glyphStyle = GlyphStyle::Xbox;
    int m_repeatDevice = -1;
    int m_repeatDirection = -1;
    qint64 m_nextRepeatAt = 0;
};

#endif
