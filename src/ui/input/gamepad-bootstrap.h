#ifndef QSAN_GAMEPAD_BOOTSTRAP_H
#define QSAN_GAMEPAD_BOOTSTRAP_H

class QObject;
class GamepadService;

namespace QSanInput {

// True when --big-picture was passed or BigPicture/Enabled is set. The switch
// itself belongs to BP-1b; this only reads the shared key/argument.
bool bigPictureActive();

// Starts gamepad input when big-picture mode is on, or when Gamepad/Enabled is
// set explicitly (it defaults to the big-picture state). Otherwise does nothing,
// so a normal desktop session installs no device polling or event filter.
void installGamepadInput(QObject *parent);

// Null unless installGamepadInput() started a service.
GamepadService *gamepadService();

} // namespace QSanInput

#endif
