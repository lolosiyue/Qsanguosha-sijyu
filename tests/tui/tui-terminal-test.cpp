// Only the parts that are verifiable without a pty live here. Raw mode,
// SIGWINCH and alternate screen restoration need a real terminal and are
// covered by tools/autotest/tui_board_smoke.py, which is a local gate.
#include "tui-terminal.h"

#include <QCoreApplication>
#include <QSize>

#include <unistd.h>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    const QByteArray restore = TuiTerminal::restoreSequence();
    check(restore.contains("\x1b[?1049l"), "restoring leaves the alternate screen");
    check(restore.contains("\x1b[?25h"), "restoring shows the cursor again");
    check(restore.contains("\x1b[0m"), "restoring drops any attribute still in force");
    check(!restore.isEmpty() && restore.size() < 64,
          "the sequence stays small enough to write from a signal handler");

    // A pipe, never the real terminal: a test that entered raw mode and the
    // alternate screen for real would wreck the shell it was launched from.
    int pipeFds[2] = {-1, -1};
    check(::pipe(pipeFds) == 0, "the test opens a pipe to stand in for a terminal");
    TuiTerminal terminal(pipeFds[0], pipeFds[1]);
    QString error;
    const bool entered = terminal.enter(&error);
    check(!entered, "entering on something that is not a terminal fails");
    check(!error.isEmpty(), "and reports a reason a player can act on");

    const QSize fallback = terminal.size();
    check(fallback.height() == 24 && fallback.width() == 80,
          "an unknown size falls back to 80x24 rather than zero");

    terminal.leave();
    terminal.leave();
    check(true, "leaving twice is a no-op and does not crash");

    std::printf("[AUTOTEST] TUI_TERMINAL_RESULT status=%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
