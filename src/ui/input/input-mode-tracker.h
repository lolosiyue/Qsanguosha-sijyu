#ifndef QSAN_INPUT_MODE_TRACKER_H
#define QSAN_INPUT_MODE_TRACKER_H

#include <QObject>
#include <QPointF>

// Remembers whether the player last used the mouse, keyboard or a gamepad.
// Gamepad mode may hide the pointer; a deliberate mouse movement restores it.
// The tracker is created on first use, so a session that never sees a gamepad
// or big-picture mode installs no application event filter.
class InputModeTracker : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Mouse, Keyboard, Gamepad };
    Q_ENUM(Mode)

    // Pointer travel (device-independent pixels) that switches back to the mouse.
    static constexpr double MouseSwitchDistance = 8.0;

    explicit InputModeTracker(QObject *parent = nullptr);
    ~InputModeTracker() override;

    // Application-wide instance; instance() creates and installs it on demand.
    static InputModeTracker *instance();
    static InputModeTracker *existing();

    Mode mode() const { return m_mode; }
    void setHideCursorInGamepadMode(bool hide);
    bool hidesCursorInGamepadMode() const { return m_hideCursor; }
    bool cursorHidden() const { return m_cursorHidden; }

    void noteGamepadInput();
    void noteKeyboardInput();
    void noteMouseMove(const QPointF &globalPos);
    void noteMouseButton();

    // Key events synthesized for gamepad actions must not flip the mode to keyboard.
    class SyntheticInputScope
    {
    public:
        SyntheticInputScope();
        ~SyntheticInputScope();
        static bool active();
    };

signals:
    void modeChanged(InputModeTracker::Mode mode);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setMode(Mode mode);
    void applyCursor();

    Mode m_mode = Mode::Mouse;
    bool m_hideCursor = false;
    bool m_cursorHidden = false;
    bool m_anchorValid = false;
    QPointF m_anchor;
};

#endif
