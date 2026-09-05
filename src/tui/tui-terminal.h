#ifndef TUI_TERMINAL_H
#define TUI_TERMINAL_H

#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QString>

#include <atomic>
#include <functional>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#else
// Pulled in unconditionally so the constructor's default arguments below
// compile on every platform qsanguosha_tui targets, even though raw-mode
// entry itself is Unix-only for now (see tui-terminal.cpp). <unistd.h> is not
// available under MSVC, so these mirror its well-known values.
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#endif

class QSocketNotifier;

// Takes the terminal into raw mode and the alternate screen for the ASCII
// board UI, and hands it back on every path that unwinds -- normal shutdown,
// SIGINT/SIGTERM/SIGHUP, and a crash (SIGSEGV/SIGABRT). See tui-terminal.cpp
// for why the signal handler has to be written the way it is.
//
// Constructed with fds rather than reaching for STDIN_FILENO/STDOUT_FILENO
// itself so a test can hand it a pipe: entering raw mode and the alternate
// screen for real on whatever terminal launched the test process would wreck
// that shell.
class TuiTerminal final : public QObject
{
    Q_OBJECT

public:
    explicit TuiTerminal(int inFd = STDIN_FILENO, int outFd = STDOUT_FILENO,
                          QObject *parent = nullptr);
    ~TuiTerminal() override;

    // Fails (returning false and filling `error`) unless both fds are a real
    // terminal. On success the terminal stays taken until leave() runs --
    // from here, from the destructor, from QCoreApplication::aboutToQuit, or
    // (best-effort) from a fatal signal.
    bool enter(QString *error = nullptr);

    // Restores the terminal. Safe to call any number of times, including
    // before enter() ever succeeded: only the first call after a successful
    // enter() does anything.
    void leave();

    // Current rows x cols, queried fresh every call. Falls back to 80x24 --
    // never 0x0 -- when the fd is not a terminal or the ioctl fails, so
    // callers can size a layout without a special no-terminal case.
    QSize size() const;

    // The exact bytes written to leave the alternate screen, show the cursor
    // and drop any SGR attribute still in force. leave() and the signal
    // handler both write this same sequence; it exists as its own function
    // so the test can check its shape without going through a real signal.
    static QByteArray restoreSequence();

signals:
    // Emitted from the QSocketNotifier slot that drains the self-pipe --
    // never from the signal handler itself.
    void resized();
    void interrupted();

private:
#if defined(Q_OS_UNIX)
    void drainWakePipe();
#endif

    int m_inFd;
    int m_outFd;

    // false only between a successful enter() and the leave() that undoes
    // it; leave() flips it with compare_exchange so a second call (or a call
    // that races the destructor and aboutToQuit) is a plain no-op rather than
    // restoring termios twice or double-closing the self-pipe.
    std::atomic<bool> m_left { true };

#if defined(Q_OS_UNIX)
    // Opaque `struct termios *` saved by enter(); kept out of the header so
    // <termios.h> does not leak into every translation unit that includes
    // this file.
    void *m_savedTermios = nullptr;
    int m_wakePipe[2] = { -1, -1 };
    QSocketNotifier *m_notifier = nullptr;
#endif
};

// The SIGINT-to-callback exit shared by classic and board mode. Installs a
// self-pipe-backed SIGINT handler (once per process) and invokes `callback`
// from the Qt event loop, never from the handler itself. Classic mode uses
// this directly: unlike TuiTerminal it never takes the terminal into raw mode
// or the alternate screen, so it has no restore sequence to write and no
// SIGWINCH/SIGSEGV handling to do -- Ctrl+C is its only reason to touch a
// signal at all.
void tuiInstallInterruptHandler(std::function<void()> callback);

#endif
