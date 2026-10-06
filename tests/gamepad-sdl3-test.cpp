// SDL3 backend integration: SDL virtual joysticks stand in for hardware, so
// hotplug, labels, buttons and sticks go through the real SDL event path.
#include "gamepad-service.h"
#include "sdl3-gamepad-backend.h"

#include <QSignalSpy>
#include <QTest>

#include <SDL3/SDL.h>

namespace {
SDL_JoystickID attachPad(Uint16 vendor, Uint16 product, const char *name)
{
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = vendor;
    desc.product_id = product;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = name;
    return SDL_AttachVirtualJoystick(&desc);
}
}

class GamepadSdl3Test : public QObject
{
    Q_OBJECT

private:
    GamepadService *m_service = nullptr;
    QList<UiAction> m_actions;

    void pump(int rounds = 6)
    {
        for (int i = 0; i < rounds; ++i) {
            m_service->tick();
            QTest::qWait(5);
        }
    }

private slots:
    void init()
    {
        m_actions.clear();
        m_service = new GamepadService(new Sdl3GamepadBackend);
        connect(m_service, &GamepadService::actionTriggered, this,
                [this](UiAction action, bool repeat) { if (!repeat) m_actions << action; });
        QString error;
        QVERIFY2(m_service->start(&error), qPrintable(error));
        pump();
        QVERIFY(m_service->devices().isEmpty());
    }

    void cleanup()
    {
        delete m_service;
        m_service = nullptr;
    }

    void hotplugButtonsAndStick()
    {
        QSignalSpy devices(m_service, &GamepadService::devicesChanged);
        // DualSense VID/PID: SDL reports a PS5 pad, so glyphs switch to PlayStation.
        const SDL_JoystickID id = attachPad(0x054c, 0x0ce6, "Virtual DualSense");
        QVERIFY2(id != 0, SDL_GetError());
        pump();
        QCOMPARE(m_service->devices().size(), 1);
        QCOMPARE(m_service->devices().first().type, GamepadType::PlayStation5);
        QCOMPARE(m_service->glyphStyle(), GlyphStyle::PlayStation);

        SDL_Joystick *joystick = SDL_OpenJoystick(id);
        QVERIFY(joystick);
        SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true);
        pump();
        SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, false);
        SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_START, true);
        pump();
        SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_START, false);
        SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 30000);
        pump();
        SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0);
        SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
        pump();
        QCOMPARE(m_actions, (QList<UiAction>{UiAction::Accept, UiAction::Confirm, UiAction::Right, UiAction::NextPage}));

        SDL_CloseJoystick(joystick);
        QVERIFY(SDL_DetachVirtualJoystick(id));
        pump();
        QVERIFY(m_service->devices().isEmpty());
        QCOMPARE(devices.count(), 2);
    }

    void secondPadSwitchesGlyphs()
    {
        const SDL_JoystickID ps = attachPad(0x054c, 0x0ce6, "Virtual DualSense");
        const SDL_JoystickID xbox = attachPad(0x045e, 0x0b12, "Virtual Xbox Series");
        pump();
        QCOMPARE(m_service->devices().size(), 2);
        SDL_Joystick *joystick = SDL_OpenJoystick(ps);
        SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, true);
        pump();
        QCOMPARE(m_service->glyphStyle(), GlyphStyle::PlayStation);
        QCOMPARE(m_actions, QList<UiAction>{UiAction::Back});
        SDL_CloseJoystick(joystick);
        SDL_DetachVirtualJoystick(ps);
        SDL_DetachVirtualJoystick(xbox);
        pump();
        QVERIFY(m_service->devices().isEmpty());
    }
};

QTEST_GUILESS_MAIN(GamepadSdl3Test)
#include "gamepad-sdl3-test.moc"
