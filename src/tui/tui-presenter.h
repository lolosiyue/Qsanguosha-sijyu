#ifndef TUI_PRESENTER_H
#define TUI_PRESENTER_H

#include <QString>

#include <functional>

// Where the controller's output goes. The controller sanitizes and writes the
// log file itself, so --log-file content stays identical no matter which
// presenter is installed; a presenter only decides where text lands on screen.
class TuiPresenter
{
public:
    using Sink = std::function<void(const QString &)>;

    virtual ~TuiPresenter() = default;
    virtual void writeOutput(const QString &text) = 0;
    virtual void writeError(const QString &text) = 0;
    // Called once before the process exits, on every path that unwinds. A
    // presenter that took the terminal over gives it back here.
    virtual void shutdown() = 0;
};

#endif
