#ifndef QSAN_SDL3_GAMEPAD_BACKEND_H
#define QSAN_SDL3_GAMEPAD_BACKEND_H

#include "gamepad-backend.h"

#include <QHash>

struct SDL_Gamepad;

// Desktop backend over SDL3's gamepad subsystem only (no SDL video, no SDL
// window). Qt keeps the event loop; poll() drains SDL's queue from a QTimer.
class Sdl3GamepadBackend final : public GamepadBackend
{
    Q_OBJECT
public:
    explicit Sdl3GamepadBackend(QObject *parent = nullptr);
    ~Sdl3GamepadBackend() override;

    QString name() const override;
    bool start(QString *error) override;
    void stop() override;
    void poll() override;

private:
    void openDevice(unsigned int instanceId);
    void closeDevice(unsigned int instanceId);

    bool m_started = false;
    QHash<unsigned int, SDL_Gamepad *> m_pads;
};

#endif
