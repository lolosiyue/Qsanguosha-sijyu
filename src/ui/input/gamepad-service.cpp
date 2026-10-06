#include "gamepad-service.h"

#include <QElapsedTimer>

namespace {
// Direction indices shared by the D-pad, stick and repeat state.
enum { DirUp, DirDown, DirLeft, DirRight, DirCount };

const UiAction kDirectionActions[DirCount] = {
    UiAction::Up, UiAction::Down, UiAction::Left, UiAction::Right
};

// A different stick direction must lead the current one by this much before
// the service switches, so a diagonal does not alternate between two axes.
const double kStickSwitchMargin = 0.15;

int dpadIndex(GamepadButton button)
{
    switch (button) {
    case GamepadButton::DpadUp: return DirUp;
    case GamepadButton::DpadDown: return DirDown;
    case GamepadButton::DpadLeft: return DirLeft;
    case GamepadButton::DpadRight: return DirRight;
    default: return -1;
    }
}

bool buttonAction(GamepadButton button, UiAction *action)
{
    switch (button) {
    case GamepadButton::South: *action = UiAction::Accept; return true;
    case GamepadButton::East: *action = UiAction::Back; return true;
    case GamepadButton::West: *action = UiAction::Secondary; return true;
    case GamepadButton::North: *action = UiAction::Details; return true;
    case GamepadButton::Start: *action = UiAction::Confirm; return true;
    case GamepadButton::Back: *action = UiAction::View; return true;
    case GamepadButton::Guide: *action = UiAction::Menu; return true;
    case GamepadButton::LeftShoulder: *action = UiAction::PreviousGroup; return true;
    case GamepadButton::RightShoulder: *action = UiAction::NextGroup; return true;
    default: return false; // Stick clicks are reserved for the virtual cursor.
    }
}
}

GlyphStyle glyphStyleForGamepadType(GamepadType type)
{
    switch (type) {
    case GamepadType::PlayStation3:
    case GamepadType::PlayStation4:
    case GamepadType::PlayStation5:
        return GlyphStyle::PlayStation;
    case GamepadType::NintendoSwitchPro:
    case GamepadType::NintendoJoyCon:
        return GlyphStyle::Nintendo;
    default:
        return GlyphStyle::Xbox;
    }
}

GamepadService::GamepadService(GamepadBackend *backend, QObject *parent)
    : QObject(parent), m_backend(backend)
{
    if (m_backend) {
        m_backend->setParent(nullptr); // Owned through unique_ptr.
        connect(m_backend.get(), &GamepadBackend::deviceAdded, this, &GamepadService::onDeviceAdded);
        connect(m_backend.get(), &GamepadBackend::deviceRemoved, this, &GamepadService::onDeviceRemoved);
        connect(m_backend.get(), &GamepadBackend::buttonChanged, this, &GamepadService::onButton);
        connect(m_backend.get(), &GamepadBackend::axisChanged, this, &GamepadService::onAxis);
    }
    connect(&m_timer, &QTimer::timeout, this, &GamepadService::tick);
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    m_clock = [elapsed]() { return elapsed->elapsed(); };
}

GamepadService::~GamepadService()
{
    stop();
}

void GamepadService::setClock(std::function<qint64()> clock)
{
    if (clock) m_clock = std::move(clock);
}

qint64 GamepadService::now() const
{
    return m_clock();
}

QString GamepadService::backendName() const
{
    return m_backend ? m_backend->name() : QString();
}

bool GamepadService::start(QString *error)
{
    if (m_running) return true;
    if (!m_backend) {
        if (error) *error = QStringLiteral("no gamepad backend is available in this build");
        return false;
    }
    QString backendError;
    if (!m_backend->start(&backendError)) {
        if (error) *error = backendError;
        return false;
    }
    m_running = true;
    m_timer.start(qMax(1, m_tuning.pollIntervalMs));
    return true;
}

void GamepadService::stop()
{
    m_timer.stop();
    if (m_running && m_backend) m_backend->stop();
    m_running = false;
    releaseAll();
}

void GamepadService::tick()
{
    if (m_running && m_backend) m_backend->poll();
    if (m_repeatDirection < 0) return;
    const qint64 current = now();
    if (current < m_nextRepeatAt) return;
    emitAction(kDirectionActions[m_repeatDirection], true);
    m_nextRepeatAt += qMax(1, m_tuning.repeatIntervalMs);
    // After a stall (debugger, modal loop) resume the cadence instead of bursting.
    if (m_nextRepeatAt <= current) m_nextRepeatAt = current + qMax(1, m_tuning.repeatIntervalMs);
}

void GamepadService::releaseAll()
{
    for (auto it = m_state.begin(); it != m_state.end(); ++it) it.value() = DeviceState();
    m_repeatDevice = -1;
    m_repeatDirection = -1;
}

void GamepadService::onDeviceAdded(int deviceId, const QString &name, GamepadType type)
{
    for (int i = 0; i < m_devices.size(); ++i)
        if (m_devices.at(i).id == deviceId) m_devices.removeAt(i--);
    m_devices.append({ deviceId, name, type });
    m_state.insert(deviceId, DeviceState());
    // A newly connected pad is usually the one about to be used.
    noteDeviceUsed(deviceId);
    emit devicesChanged();
}

void GamepadService::onDeviceRemoved(int deviceId)
{
    bool known = false;
    for (int i = 0; i < m_devices.size(); ++i)
        if (m_devices.at(i).id == deviceId) { m_devices.removeAt(i--); known = true; }
    m_state.remove(deviceId);
    if (m_repeatDevice == deviceId) {
        m_repeatDevice = -1;
        m_repeatDirection = -1;
    }
    if (known) emit devicesChanged();
}

void GamepadService::onButton(int deviceId, GamepadButton button, bool pressed)
{
    auto it = m_state.find(deviceId);
    if (it == m_state.end()) return; // Events for unannounced devices are ignored.
    if (pressed) noteDeviceUsed(deviceId);
    const int dir = dpadIndex(button);
    if (dir >= 0) {
        it->dpad[dir] = pressed;
        updateDirections(deviceId, *it);
        return;
    }
    UiAction action;
    if (pressed && buttonAction(button, &action)) emitAction(action);
}

void GamepadService::onAxis(int deviceId, GamepadAxis axis, double value)
{
    auto it = m_state.find(deviceId);
    if (it == m_state.end()) return;
    switch (axis) {
    case GamepadAxis::LeftX:
        it->stickX = qBound(-1.0, value, 1.0);
        updateStick(deviceId, *it);
        break;
    case GamepadAxis::LeftY:
        it->stickY = qBound(-1.0, value, 1.0);
        updateStick(deviceId, *it);
        break;
    case GamepadAxis::LeftTrigger:
    case GamepadAxis::RightTrigger: {
        bool &held = axis == GamepadAxis::LeftTrigger ? it->leftTrigger : it->rightTrigger;
        if (!held && value >= m_tuning.triggerPress) {
            held = true;
            noteDeviceUsed(deviceId);
            emitAction(axis == GamepadAxis::LeftTrigger ? UiAction::PreviousPage : UiAction::NextPage);
        } else if (held && value <= m_tuning.triggerRelease) {
            held = false;
        }
        break;
    }
    default:
        break; // The right stick drives the future virtual cursor only.
    }
}

void GamepadService::updateStick(int deviceId, DeviceState &state)
{
    const double along[DirCount] = { -state.stickY, state.stickY, -state.stickX, state.stickX };
    int current = state.stickDirection;
    if (current >= 0 && along[current] < m_tuning.stickRelease) current = -1;
    int best = -1;
    double bestValue = 0;
    for (int i = 0; i < DirCount; ++i)
        if (along[i] > bestValue) { best = i; bestValue = along[i]; }
    if (best >= 0 && bestValue >= m_tuning.stickPress
        && (current < 0 || (best != current && bestValue > along[current] + kStickSwitchMargin)))
        current = best;
    if (current == state.stickDirection) return;
    state.stickDirection = current;
    if (current >= 0) noteDeviceUsed(deviceId);
    updateDirections(deviceId, state);
}

void GamepadService::updateDirections(int deviceId, DeviceState &state)
{
    for (int i = 0; i < DirCount; ++i) {
        const bool held = state.dpad[i] || state.stickDirection == i;
        if (held == state.held[i]) continue;
        state.held[i] = held;
        if (held) {
            emitAction(kDirectionActions[i]);
            m_repeatDevice = deviceId;
            m_repeatDirection = i;
            m_nextRepeatAt = now() + qMax(0, m_tuning.repeatDelayMs);
        } else if (m_repeatDevice == deviceId && m_repeatDirection == i) {
            m_repeatDevice = -1;
            m_repeatDirection = -1;
        }
    }
}

void GamepadService::noteDeviceUsed(int deviceId)
{
    for (const auto &device : m_devices) {
        if (device.id != deviceId) continue;
        const GlyphStyle style = glyphStyleForGamepadType(device.type);
        if (style != m_glyphStyle) {
            m_glyphStyle = style;
            emit glyphStyleChanged(style);
        }
        return;
    }
}

void GamepadService::emitAction(UiAction action, bool repeat)
{
    emit actionTriggered(action, repeat);
}
