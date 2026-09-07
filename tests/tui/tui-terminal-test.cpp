// Only the parts that are verifiable without a pty live here. Raw mode,
// SIGWINCH and alternate screen restoration need a real terminal and are
// covered by tools/autotest/tui_board_smoke.py, which is a local gate. The
// SIGINT-sharing test below is registration-level for the same reason: it
// checks which handler sigaction() reports installed, and never raises a
// real signal or touches a real terminal.
#include "tui-terminal.h"

#include <QCoreApplication>
#include <QSize>

#include <cstdio>
#include <cstring>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <unistd.h>

// Not declared in tui-terminal.h -- tuiInstallSharedSigintHandler() is the
// internal sigaction(SIGINT, ...) call shared by TuiTerminal::enter() and
// tuiInstallInterruptHandler() (see its definition in tui-terminal.cpp for
// why sharing it matters). Forward-declared here, matching its actual
// external-linkage signature, purely so this suite can call it directly:
// that is the only way to exercise TuiTerminal's half of the ordering
// invariant without a real terminal to carry enter() past its isatty() gate.
extern void tuiInstallSharedSigintHandler();
#endif

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

    int pipeFds[2] = {-1, -1};
#if defined(Q_OS_UNIX)
    // A pipe, never the real terminal: entering raw mode for real would
    // wreck the shell the test was launched from.
    check(::pipe(pipeFds) == 0, "the test opens a pipe to stand in for a terminal");
#endif
    // Other platforms reject raw mode before inspecting the descriptors.
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

#if defined(Q_OS_UNIX)
    if (pipeFds[0] >= 0)
        ::close(pipeFds[0]);
    if (pipeFds[1] >= 0)
        ::close(pipeFds[1]);
#else
    check(error.contains(QStringLiteral("not implemented")),
          "unsupported platforms explain why raw mode is unavailable");
#endif

    // enter()'s re-entry guard (skip straight to the isatty() failure when
    // already entered, rather than re-running tcgetattr()) only has an
    // observable effect once entered, and nothing in this suite can reach
    // "entered" without a real tty -- enter() always fails at the isatty()
    // check first on a pipe, exactly like the case above. That guard is
    // exercised for real by tools/autotest/tui_board_smoke.py's pty smoke,
    // not here; see task-5-report.md for why a pty-based unit test was
    // rejected for this suite.

#if defined(Q_OS_UNIX)
    {
        // Registration-level check for the SIGINT-sharing invariant: after
        // both TuiTerminal::enter() and tuiInstallInterruptHandler() have
        // run, in either order, exactly one handler must be installed for
        // SIGINT -- never two independent ones fighting over the slot. This
        // suite never touches a real terminal and never raises a real
        // signal, so it drives the two installers' shared sigaction() call
        // directly rather than getting TuiTerminal::enter() past a real tty.
        bool interruptCallbackRegistered = false;
        tuiInstallInterruptHandler([&interruptCallbackRegistered]() {
            interruptCallbackRegistered = true; // never actually invoked: no signal is raised
        });

        struct sigaction afterInterruptHandler;
        std::memset(&afterInterruptHandler, 0, sizeof(afterInterruptHandler));
        check(::sigaction(SIGINT, nullptr, &afterInterruptHandler) == 0,
              "querying SIGINT's disposition after tuiInstallInterruptHandler succeeds");
        check(afterInterruptHandler.sa_handler != SIG_DFL
                  && afterInterruptHandler.sa_handler != SIG_IGN,
              "tuiInstallInterruptHandler installs a real handler for SIGINT");

        // Stand in for TuiTerminal::enter() also having run: enter() cannot
        // get this far on a pipe (it fails at isatty() first), so call the
        // exact same shared installer enter() calls once it is past that
        // gate, and check it does not disturb what tuiInstallInterruptHandler
        // just installed.
        tuiInstallSharedSigintHandler();

        struct sigaction afterTuiTerminal;
        std::memset(&afterTuiTerminal, 0, sizeof(afterTuiTerminal));
        check(::sigaction(SIGINT, nullptr, &afterTuiTerminal) == 0,
              "querying SIGINT's disposition after the TuiTerminal-side install succeeds");
        check(afterTuiTerminal.sa_handler == afterInterruptHandler.sa_handler,
              "TuiTerminal's SIGINT install does not replace tuiInstallInterruptHandler's");

        // And the reverse order: running tuiInstallInterruptHandler() again
        // (its early-return path, since the pipe/notifier already exist)
        // must not undo the TuiTerminal-side install either.
        tuiInstallInterruptHandler([&interruptCallbackRegistered]() {
            interruptCallbackRegistered = true;
        });
        struct sigaction afterBoth;
        std::memset(&afterBoth, 0, sizeof(afterBoth));
        check(::sigaction(SIGINT, nullptr, &afterBoth) == 0,
              "querying SIGINT's disposition after both installers have run a second time "
              "succeeds");
        check(afterBoth.sa_handler == afterTuiTerminal.sa_handler,
              "a single handler stays installed for SIGINT no matter which installer runs last");

        (void)interruptCallbackRegistered;
    }

    {
        // Minor fix from the 2026-09 review: tuiInstallInterruptHandler()'s
        // callback is a bare capture with no lifetime tracking of its own
        // (typically `[this]() { emit interruptRequested(); }` from a
        // TuiInput -- see tui-input.cpp), and the global that holds it used
        // to never get cleared. This repo has a documented history of
        // exactly this shape of teardown use-after-free elsewhere, so this
        // proves the actual mechanism meant to prevent it here: raise a
        // real SIGINT and pump the event loop to prove the self-pipe ->
        // QSocketNotifier -> callback path genuinely fires (not just that
        // sigaction() reports a handler installed, which the block above
        // already covers), then clear it and prove a second SIGINT does
        // NOT fire the (now-cleared) callback again.
        int callCount = 0;
        tuiInstallInterruptHandler([&callCount]() { ++callCount; });

        check(::raise(SIGINT) == 0, "a SIGINT can be raised for this test");
        QCoreApplication::processEvents();
        check(callCount == 1, "a raised SIGINT actually reaches the installed callback");

        tuiClearInterruptHandler();
        check(::raise(SIGINT) == 0, "a second SIGINT can be raised after clearing");
        QCoreApplication::processEvents();
        check(callCount == 1,
              "tuiClearInterruptHandler() stops a cleared callback from firing again "
              "-- this is what keeps a SIGINT delivered after a TuiInput is destroyed "
              "from invoking a dangling pointer");
    }
#endif

    std::printf("[AUTOTEST] TUI_TERMINAL_RESULT status=%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
