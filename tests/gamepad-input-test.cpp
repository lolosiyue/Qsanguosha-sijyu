// Fake-backend tests for the BP-1a gamepad input layer: no SDL, no device.
#include "gamepad-backend.h"
#include "gamepad-service.h"
#include "input-mode-tracker.h"
#include "ui-action-dispatcher.h"

#include <QApplication>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

namespace {
class FakeBackend final : public GamepadBackend
{
public:
    bool startOk = true;
    int polls = 0;
    int stops = 0;

    QString name() const override { return QStringLiteral("fake"); }
    bool start(QString *error) override
    {
        if (!startOk && error) *error = QStringLiteral("fake failure");
        return startOk;
    }
    void stop() override { ++stops; }
    void poll() override { ++polls; }

    void add(int id, GamepadType type = GamepadType::XboxOne) { emit deviceAdded(id, QStringLiteral("Pad %1").arg(id), type); }
    void remove(int id) { emit deviceRemoved(id); }
    void press(int id, GamepadButton button) { emit buttonChanged(id, button, true); }
    void release(int id, GamepadButton button) { emit buttonChanged(id, button, false); }
    void click(int id, GamepadButton button) { press(id, button); release(id, button); }
    void axis(int id, GamepadAxis axis, double value) { emit axisChanged(id, axis, value); }
};

struct Recorded
{
    UiAction action;
    bool repeat;
};

class KeyRecorder final : public QWidget
{
public:
    QList<QPair<QEvent::Type, int>> keys;
    QList<Qt::KeyboardModifiers> modifiers;
protected:
    void keyPressEvent(QKeyEvent *event) override { record(event); }
    void keyReleaseEvent(QKeyEvent *event) override { record(event); }
private:
    void record(QKeyEvent *event)
    {
        keys.append({event->type(), event->key()});
        modifiers.append(event->modifiers());
        event->accept();
    }
};
}

class GamepadInputTest : public QObject
{
    Q_OBJECT

private:
    // Service wired to a fake backend and a manual clock.
    struct Rig
    {
        FakeBackend *backend = new FakeBackend;
        GamepadService service{backend};
        qint64 now = 0;
        QList<Recorded> actions;
        Rig()
        {
            service.setClock([this]() { return now; });
            QObject::connect(&service, &GamepadService::actionTriggered, &service,
                             [this](UiAction action, bool repeat) { actions.append({action, repeat}); });
        }
        void at(qint64 ms) { now = ms; service.tick(); }
        QList<UiAction> take()
        {
            QList<UiAction> out;
            for (const auto &r : actions) out << r.action;
            actions.clear();
            return out;
        }
    };

private slots:
    void buttonsMapToPositionalActions()
    {
        Rig rig;
        rig.backend->add(1);
        const QList<QPair<GamepadButton, UiAction>> table = {
            {GamepadButton::South, UiAction::Accept},
            {GamepadButton::East, UiAction::Back},
            {GamepadButton::West, UiAction::Secondary},
            {GamepadButton::North, UiAction::Details},
            {GamepadButton::Start, UiAction::Confirm},
            {GamepadButton::Back, UiAction::View},
            {GamepadButton::Guide, UiAction::Menu},
            {GamepadButton::LeftShoulder, UiAction::PreviousGroup},
            {GamepadButton::RightShoulder, UiAction::NextGroup},
        };
        for (const auto &row : table) {
            rig.backend->click(1, row.first);
            QCOMPARE(rig.take(), QList<UiAction>{row.second});
        }
        rig.backend->click(1, GamepadButton::LeftStick);
        rig.backend->click(1, GamepadButton::RightStick);
        QVERIFY(rig.take().isEmpty());
    }

    void releaseAndUnknownDevicesEmitNothing()
    {
        Rig rig;
        rig.backend->press(7, GamepadButton::South); // Never announced.
        QVERIFY(rig.take().isEmpty());
        rig.backend->add(1);
        rig.backend->press(1, GamepadButton::South);
        rig.backend->release(1, GamepadButton::South);
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Accept});
    }

    void heldDirectionRepeatsAfterDelay()
    {
        Rig rig;
        rig.backend->add(1);
        rig.at(0);
        rig.backend->press(1, GamepadButton::DpadDown);
        QCOMPARE(rig.actions.size(), 1);
        QCOMPARE(rig.actions.first().action, UiAction::Down);
        QVERIFY(!rig.actions.first().repeat);
        rig.actions.clear();
        rig.at(399);
        QVERIFY(rig.actions.isEmpty());
        rig.at(400);
        QCOMPARE(rig.actions.size(), 1);
        QVERIFY(rig.actions.first().repeat);
        rig.at(509);
        QCOMPARE(rig.actions.size(), 1);
        rig.at(510);
        QCOMPARE(rig.actions.size(), 2);
        rig.backend->release(1, GamepadButton::DpadDown);
        rig.actions.clear();
        rig.at(5000);
        QVERIFY(rig.actions.isEmpty());
    }

    void repeatDoesNotBurstAfterStall()
    {
        Rig rig;
        rig.backend->add(1);
        rig.backend->press(1, GamepadButton::DpadRight);
        rig.actions.clear();
        rig.at(10000); // E.g. a modal loop starved the timer.
        QCOMPARE(rig.actions.size(), 1);
        rig.at(10109);
        QCOMPARE(rig.actions.size(), 1);
        rig.at(10110);
        QCOMPARE(rig.actions.size(), 2);
    }

    void customTuningIsUsed()
    {
        Rig rig;
        GamepadService::Tuning tuning;
        tuning.repeatDelayMs = 250;
        tuning.repeatIntervalMs = 50;
        rig.service.setTuning(tuning);
        rig.backend->add(1);
        rig.backend->press(1, GamepadButton::DpadUp);
        rig.actions.clear();
        rig.at(249);
        QVERIFY(rig.actions.isEmpty());
        rig.at(250);
        rig.at(300);
        QCOMPARE(rig.actions.size(), 2);
    }

    void stickDeadzoneAndHysteresis()
    {
        Rig rig;
        rig.backend->add(1);
        rig.backend->axis(1, GamepadAxis::LeftX, 0.3);   // Inside the dead zone.
        QVERIFY(rig.take().isEmpty());
        rig.backend->axis(1, GamepadAxis::LeftX, 0.6);
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Right});
        rig.backend->axis(1, GamepadAxis::LeftX, 0.4);   // Between release and press: still held.
        rig.backend->axis(1, GamepadAxis::LeftX, 0.55);
        QVERIFY(rig.take().isEmpty());
        rig.backend->axis(1, GamepadAxis::LeftX, 0.2);   // Released.
        rig.backend->axis(1, GamepadAxis::LeftX, -0.7);
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Left});
        rig.backend->axis(1, GamepadAxis::LeftX, 0.0);
        rig.backend->axis(1, GamepadAxis::LeftY, -0.9);  // Up is negative Y.
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Up});
    }

    void stickDiagonalDoesNotChatter()
    {
        Rig rig;
        rig.backend->add(1);
        rig.backend->axis(1, GamepadAxis::LeftX, 0.6);
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Right});
        rig.backend->axis(1, GamepadAxis::LeftY, 0.65);  // Diagonal: keep the current axis.
        rig.backend->axis(1, GamepadAxis::LeftY, 0.6);
        QVERIFY(rig.take().isEmpty());
        rig.backend->axis(1, GamepadAxis::LeftY, 0.8);   // Clearly down now.
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Down});
    }

    void dpadAndStickMergeIntoOneDirection()
    {
        Rig rig;
        rig.backend->add(1);
        rig.backend->press(1, GamepadButton::DpadUp);
        rig.backend->axis(1, GamepadAxis::LeftY, -0.9);
        rig.backend->release(1, GamepadButton::DpadUp);  // Stick still holds Up.
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Up});
        rig.at(400);
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Up}); // Repeat continues.
        rig.backend->axis(1, GamepadAxis::LeftY, 0.0);
        rig.at(2000);
        QVERIFY(rig.take().isEmpty());
    }

    void triggersUseThresholds()
    {
        Rig rig;
        rig.backend->add(1);
        rig.backend->axis(1, GamepadAxis::LeftTrigger, 0.6);
        rig.backend->axis(1, GamepadAxis::LeftTrigger, 0.4);
        rig.backend->axis(1, GamepadAxis::LeftTrigger, 0.7);
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::PreviousPage});
        rig.backend->axis(1, GamepadAxis::LeftTrigger, 0.1);
        rig.backend->axis(1, GamepadAxis::LeftTrigger, 0.9);
        rig.backend->axis(1, GamepadAxis::RightTrigger, 1.0);
        QCOMPARE(rig.take(), (QList<UiAction>{UiAction::PreviousPage, UiAction::NextPage}));
        rig.at(5000);
        QVERIFY(rig.take().isEmpty()); // Triggers never repeat.
    }

    void rightStickIsIgnored()
    {
        Rig rig;
        rig.backend->add(1);
        rig.backend->axis(1, GamepadAxis::RightX, 1.0);
        rig.backend->axis(1, GamepadAxis::RightY, -1.0);
        QVERIFY(rig.take().isEmpty());
    }

    void hotplugRemovalStopsRepeat()
    {
        Rig rig;
        QSignalSpy devices(&rig.service, &GamepadService::devicesChanged);
        rig.backend->add(1);
        rig.backend->add(2, GamepadType::PlayStation5);
        QCOMPARE(rig.service.devices().size(), 2);
        rig.backend->press(1, GamepadButton::DpadRight);
        rig.take();
        rig.backend->remove(1);
        QCOMPARE(rig.service.devices().size(), 1);
        QCOMPARE(rig.service.devices().first().id, 2);
        rig.at(5000);
        QVERIFY(rig.take().isEmpty());
        // The surviving pad still works, the removed one is ignored.
        rig.backend->click(1, GamepadButton::South);
        rig.backend->click(2, GamepadButton::South);
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Accept});
        // Reconnecting starts from a clean state.
        rig.backend->add(1);
        rig.backend->press(1, GamepadButton::DpadRight);
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Right});
        QCOMPARE(devices.count(), 4);
        rig.backend->remove(9); // Unknown id: no signal.
        QCOMPARE(devices.count(), 4);
    }

    void releaseAllDropsHeldState()
    {
        Rig rig;
        rig.backend->add(1);
        rig.backend->press(1, GamepadButton::DpadLeft);
        rig.take();
        rig.service.releaseAll();
        rig.at(5000);
        QVERIFY(rig.take().isEmpty());
        // The button is still physically down; the next press is a fresh edge.
        rig.backend->release(1, GamepadButton::DpadLeft);
        rig.backend->press(1, GamepadButton::DpadLeft);
        QCOMPARE(rig.take(), QList<UiAction>{UiAction::Left});
    }

    void glyphStyleFollowsLastUsedDevice()
    {
        Rig rig;
        QSignalSpy styles(&rig.service, &GamepadService::glyphStyleChanged);
        QCOMPARE(rig.service.glyphStyle(), GlyphStyle::Xbox);
        rig.backend->add(1, GamepadType::Xbox360);
        QCOMPARE(styles.count(), 0);
        rig.backend->add(2, GamepadType::PlayStation4);
        QCOMPARE(rig.service.glyphStyle(), GlyphStyle::PlayStation);
        rig.backend->click(1, GamepadButton::South);
        QCOMPARE(rig.service.glyphStyle(), GlyphStyle::Xbox);
        rig.backend->add(3, GamepadType::NintendoSwitchPro);
        QCOMPARE(rig.service.glyphStyle(), GlyphStyle::Nintendo);
        rig.backend->axis(2, GamepadAxis::LeftX, 0.9);
        QCOMPARE(rig.service.glyphStyle(), GlyphStyle::PlayStation);
        QCOMPARE(styles.count(), 4);
    }

    void glyphStyleForEveryType()
    {
        QCOMPARE(glyphStyleForGamepadType(GamepadType::Unknown), GlyphStyle::Xbox);
        QCOMPARE(glyphStyleForGamepadType(GamepadType::Generic), GlyphStyle::Xbox);
        QCOMPARE(glyphStyleForGamepadType(GamepadType::Xbox360), GlyphStyle::Xbox);
        QCOMPARE(glyphStyleForGamepadType(GamepadType::XboxOne), GlyphStyle::Xbox);
        QCOMPARE(glyphStyleForGamepadType(GamepadType::PlayStation3), GlyphStyle::PlayStation);
        QCOMPARE(glyphStyleForGamepadType(GamepadType::PlayStation4), GlyphStyle::PlayStation);
        QCOMPARE(glyphStyleForGamepadType(GamepadType::PlayStation5), GlyphStyle::PlayStation);
        QCOMPARE(glyphStyleForGamepadType(GamepadType::NintendoSwitchPro), GlyphStyle::Nintendo);
        QCOMPARE(glyphStyleForGamepadType(GamepadType::NintendoJoyCon), GlyphStyle::Nintendo);
    }

    void glyphLabels()
    {
        using QSanInput::uiActionGlyph;
        QCOMPARE(uiActionGlyph(UiAction::Accept, GlyphStyle::Xbox), QStringLiteral("A"));
        QCOMPARE(uiActionGlyph(UiAction::Accept, GlyphStyle::PlayStation), QStringLiteral("✕"));
        QCOMPARE(uiActionGlyph(UiAction::Accept, GlyphStyle::Nintendo), QStringLiteral("B"));
        QCOMPARE(uiActionGlyph(UiAction::Accept, GlyphStyle::Keyboard), QStringLiteral("Space"));
        QCOMPARE(uiActionGlyph(UiAction::Back, GlyphStyle::PlayStation), QStringLiteral("○"));
        QCOMPARE(uiActionGlyph(UiAction::Back, GlyphStyle::Nintendo), QStringLiteral("A"));
        QCOMPARE(uiActionGlyph(UiAction::Confirm, GlyphStyle::Keyboard), QStringLiteral("Enter"));
        QCOMPARE(uiActionGlyph(UiAction::NextGroup, GlyphStyle::PlayStation), QStringLiteral("R1"));
        // Every action has a gamepad label in every controller style.
        for (int i = int(UiAction::Up); i <= int(UiAction::Menu); ++i)
            for (GlyphStyle style : {GlyphStyle::Xbox, GlyphStyle::PlayStation, GlyphStyle::Nintendo})
                QVERIFY2(!uiActionGlyph(UiAction(i), style).isEmpty(), qPrintable(QSanInput::uiActionName(UiAction(i))));
    }

    void actionNamesRoundTrip()
    {
        for (int i = int(UiAction::Up); i <= int(UiAction::Menu); ++i) {
            const QString name = QSanInput::uiActionName(UiAction(i));
            QVERIFY(!name.isEmpty());
            UiAction parsed = UiAction::Menu;
            QVERIFY(QSanInput::uiActionFromName(name, &parsed));
            QCOMPARE(int(parsed), i);
        }
        QVERIFY(!QSanInput::uiActionFromName(QStringLiteral("jump"), nullptr));
    }

    void keyMapsFollowTheSharedConvention()
    {
        using QSanInput::uiActionKey;
        Qt::KeyboardModifiers mods;
        // Generic: arrows, Enter = confirm, Esc = back, PageUp/PageDown = LB/RB.
        QCOMPARE(uiActionKey(UiAction::Up, UiKeyMap::Generic), int(Qt::Key_Up));
        QCOMPARE(uiActionKey(UiAction::Down, UiKeyMap::Generic), int(Qt::Key_Down));
        QCOMPARE(uiActionKey(UiAction::Left, UiKeyMap::Generic), int(Qt::Key_Left));
        QCOMPARE(uiActionKey(UiAction::Right, UiKeyMap::Generic), int(Qt::Key_Right));
        QCOMPARE(uiActionKey(UiAction::Accept, UiKeyMap::Generic), int(Qt::Key_Return));
        QCOMPARE(uiActionKey(UiAction::Confirm, UiKeyMap::Generic), int(Qt::Key_Return));
        QCOMPARE(uiActionKey(UiAction::Back, UiKeyMap::Generic), int(Qt::Key_Escape));
        QCOMPARE(uiActionKey(UiAction::PreviousGroup, UiKeyMap::Generic, &mods), int(Qt::Key_PageUp));
        QCOMPARE(mods, Qt::KeyboardModifiers(Qt::NoModifier));
        QCOMPARE(uiActionKey(UiAction::NextGroup, UiKeyMap::Generic), int(Qt::Key_PageDown));
        QCOMPARE(uiActionKey(UiAction::Details, UiKeyMap::Generic), 0);
        // Table: the existing native keys (Space toggles, Enter submits, Tab groups).
        QCOMPARE(uiActionKey(UiAction::Accept, UiKeyMap::Table), int(Qt::Key_Space));
        QCOMPARE(uiActionKey(UiAction::Confirm, UiKeyMap::Table), int(Qt::Key_Return));
        QCOMPARE(uiActionKey(UiAction::Back, UiKeyMap::Table), int(Qt::Key_Escape));
        QCOMPARE(uiActionKey(UiAction::NextGroup, UiKeyMap::Table), int(Qt::Key_Tab));
        QCOMPARE(uiActionKey(UiAction::PreviousGroup, UiKeyMap::Table, &mods), int(Qt::Key_Backtab));
        QCOMPARE(mods, Qt::KeyboardModifiers(Qt::ShiftModifier));
        QCOMPARE(uiActionKey(UiAction::Secondary, UiKeyMap::Table), 0);
        QCOMPARE(uiActionKey(UiAction::PreviousPage, UiKeyMap::Table), int(Qt::Key_Home));
        QCOMPARE(uiActionKey(UiAction::NextPage, UiKeyMap::Table), int(Qt::Key_End));
    }

    void serviceStartAndPolling()
    {
        GamepadService inert(nullptr);
        QString error;
        QVERIFY(!inert.start(&error));
        QVERIFY(!error.isEmpty());

        auto *failing = new FakeBackend;
        failing->startOk = false;
        GamepadService broken(failing);
        QVERIFY(!broken.start(&error));
        QCOMPARE(error, QStringLiteral("fake failure"));
        QVERIFY(!broken.isRunning());

        auto *backend = new FakeBackend;
        GamepadService service(backend);
        QVERIFY(service.start(&error));
        QVERIFY(service.isRunning());
        QCOMPARE(service.backendName(), QStringLiteral("fake"));
        service.tick();
        QVERIFY(backend->polls >= 1);
        QTRY_VERIFY(backend->polls >= 3); // Timer-driven polling.
        service.stop();
        QCOMPARE(backend->stops, 1);
        const int polls = backend->polls;
        service.tick();
        QCOMPARE(backend->polls, polls);
    }

    void trackerModesAndCursor()
    {
        InputModeTracker tracker;
        QSignalSpy modes(&tracker, &InputModeTracker::modeChanged);
        QCOMPARE(tracker.mode(), InputModeTracker::Mode::Mouse);
        tracker.setHideCursorInGamepadMode(true);
        tracker.noteGamepadInput();
        QCOMPARE(tracker.mode(), InputModeTracker::Mode::Gamepad);
        QVERIFY(tracker.cursorHidden());
        QVERIFY(QGuiApplication::overrideCursor());
        QCOMPARE(QGuiApplication::overrideCursor()->shape(), Qt::BlankCursor);
        // Jitter around the resting point keeps gamepad mode.
        tracker.noteMouseMove(QPointF(100, 100));
        tracker.noteMouseMove(QPointF(104, 103));
        QCOMPARE(tracker.mode(), InputModeTracker::Mode::Gamepad);
        tracker.noteMouseMove(QPointF(112, 100));
        QCOMPARE(tracker.mode(), InputModeTracker::Mode::Mouse);
        QVERIFY(!tracker.cursorHidden());
        QVERIFY(!QGuiApplication::overrideCursor());
        {
            InputModeTracker::SyntheticInputScope synthetic;
            tracker.noteKeyboardInput();
        }
        QCOMPARE(tracker.mode(), InputModeTracker::Mode::Mouse);
        tracker.noteKeyboardInput();
        QCOMPARE(tracker.mode(), InputModeTracker::Mode::Keyboard);
        tracker.noteGamepadInput();
        tracker.noteMouseButton();
        QCOMPARE(tracker.mode(), InputModeTracker::Mode::Mouse);
        QCOMPARE(modes.count(), 5);
        // Without cursor hiding (big-picture off) the pointer is never touched.
        tracker.setHideCursorInGamepadMode(false);
        tracker.noteGamepadInput();
        QVERIFY(!QGuiApplication::overrideCursor());
    }

    void dispatcherSendsSharedKeysToFocusWindow()
    {
        KeyRecorder window;
        window.setFocusPolicy(Qt::StrongFocus);
        window.resize(200, 120);
        window.show();
        window.activateWindow();
        window.setFocus();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        UiActionDispatcher *dispatcher = UiActionDispatcher::instance();
        QSignalSpy dispatched(dispatcher, &UiActionDispatcher::actionDispatched);
        QVERIFY(dispatcher->dispatch(UiAction::Up));
        QVERIFY(dispatcher->dispatch(UiAction::Accept));
        QVERIFY(dispatcher->dispatch(UiAction::Back));
        QVERIFY(dispatcher->dispatch(UiAction::NextGroup));
        QVERIFY(!dispatcher->dispatch(UiAction::Details)); // No key equivalent.
        QCOMPARE(dispatched.count(), 5);
        const QList<QPair<QEvent::Type, int>> expected = {
            {QEvent::KeyPress, Qt::Key_Up}, {QEvent::KeyRelease, Qt::Key_Up},
            {QEvent::KeyPress, Qt::Key_Return}, {QEvent::KeyRelease, Qt::Key_Return},
            {QEvent::KeyPress, Qt::Key_Escape}, {QEvent::KeyRelease, Qt::Key_Escape},
            {QEvent::KeyPress, Qt::Key_PageDown}, {QEvent::KeyRelease, Qt::Key_PageDown},
        };
        QCOMPARE(window.keys, expected);
        // Dispatch marks gamepad mode; the synthesized keys do not flip it to keyboard.
        QVERIFY(InputModeTracker::existing());
        QCOMPARE(InputModeTracker::existing()->mode(), InputModeTracker::Mode::Gamepad);
    }

    void dispatcherPrefersTheTableHandler()
    {
        KeyRecorder window;
        window.setFocusPolicy(Qt::StrongFocus);
        window.show();
        window.activateWindow();
        window.setFocus();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        UiActionDispatcher *dispatcher = UiActionDispatcher::instance();
        QList<UiAction> tableActions;
        auto *owner = new QObject;
        dispatcher->setTableHandler(owner, [&tableActions](UiAction action) {
            tableActions << action;
            return action != UiAction::Back; // Back: "not my surface", fall through.
        });
        QVERIFY(dispatcher->dispatch(UiAction::Right));
        QVERIFY(dispatcher->dispatch(UiAction::Back));
        QCOMPARE(tableActions, (QList<UiAction>{UiAction::Right, UiAction::Back}));
        QCOMPARE(window.keys, (QList<QPair<QEvent::Type, int>>{
            {QEvent::KeyPress, Qt::Key_Escape}, {QEvent::KeyRelease, Qt::Key_Escape}}));
        delete owner; // Handler goes with its owner.
        window.keys.clear();
        QVERIFY(dispatcher->dispatch(UiAction::Right));
        QCOMPARE(tableActions.size(), 2);
        QCOMPARE(window.keys.size(), 2);
        QCOMPARE(window.keys.first().second, int(Qt::Key_Right));
    }
};

QTEST_MAIN(GamepadInputTest)
#include "gamepad-input-test.moc"
