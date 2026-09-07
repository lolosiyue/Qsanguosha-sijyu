#ifndef TUI_PRESENTER_H
#define TUI_PRESENTER_H

#include <QString>

#include <functional>

class ClientGameState;
struct InteractionRequest;

// Where the controller's output goes. The controller sanitizes and writes the
// log file itself, so --log-file content stays identical no matter which
// presenter is installed; a presenter only decides where text lands on screen.
class TuiPresenter
{
public:
    using Sink = std::function<void(const QString &)>;

    virtual ~TuiPresenter() = default;
    virtual void writeOutput(const QString &text) = 0;
    // Exactly the six long-dump commands' output (/players, /log, /hand,
    // /skills, /piles, /equip -- spec §5.2): the one kind of output board
    // mode is allowed to take the whole screen for. Everything else --
    // interaction prompts included, which routinely run longer than any of
    // these dumps -- goes through writeOutput() and must never trigger an
    // overlay on its own; only the caller naming one of those six commands
    // may call this instead of writeOutput().
    virtual void writeDump(const QString &text) = 0;
    virtual void writeError(const QString &text) = 0;
    // Called once before the process exits, on every path that unwinds. A
    // presenter that took the terminal over gives it back here.
    virtual void shutdown() = 0;

    // The two board-mode hooks. The controller calls both unconditionally,
    // whichever presenter is installed -- it does not (and must not) know
    // that a board mode exists; see TuiStreamPresenter's own no-op bodies for
    // why that presenter has nothing to do with either call. TuiBoardPresenter
    // is the one implementation that repaints from them (docs/tui-board-ui.md
    // §3.6).
    virtual void stateChanged(const ClientGameState &state) = 0;
    // nullptr means no request is in flight right now -- answered, cancelled,
    // superseded, or none has ever arrived.
    virtual void interactionChanged(const InteractionRequest *request) = 0;
};

#endif
