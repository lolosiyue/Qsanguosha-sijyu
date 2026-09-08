#include "tui-terminal.h"

#include <QCoreApplication>
#include <QSocketNotifier>
#include <QTimer>

#include <cstring>
#include <csignal>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <qt_windows.h>
#include <io.h>
#endif

#if defined(Q_OS_WIN)
// Only Win32/POD state is accessed by console-control and fatal callbacks.
// The SRW lock prevents leave() from deleting it under a control-handler thread.
struct TuiWindowsTerminalState {
    HANDLE input = INVALID_HANDLE_VALUE;
    HANDLE output = INVALID_HANDLE_VALUE;
    DWORD inputMode = 0;
    DWORD outputMode = 0;
    UINT inputCodePage = 0;
    UINT outputCodePage = 0;
    CONSOLE_CURSOR_INFO cursor{};
    WORD attributes = 0;
    bool alternateScreen = false;
    bool restored = false;
    std::atomic<bool> interruptPending{false};
    using SignalHandler = void (*)(int);
    SignalHandler previousAbort = SIG_DFL;
    SignalHandler previousSegv = SIG_DFL;
};
#endif

namespace {

#if defined(Q_OS_WIN)
SRWLOCK g_windowsTerminalLock = SRWLOCK_INIT;
TuiWindowsTerminalState *g_windowsTerminal = nullptr;

HANDLE windowsHandle(int fd)
{
    // Negative descriptors otherwise invoke the CRT invalid-parameter handler.
    return fd < 0 ? INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(_get_osfhandle(fd));
}

bool writeWindowsBytes(HANDLE handle, const char *bytes, qsizetype length)
{
    while (length > 0) {
        DWORD written = 0;
        DWORD chunk = static_cast<DWORD>(qMin<qsizetype>(length, 32768));
        // Each console write must end on a UTF-8 boundary even for large
        // frames. Do not leave half a CJK/emoji character in another call.
        if (chunk < length) {
            while (chunk > 0 && (static_cast<unsigned char>(bytes[chunk]) & 0xc0) == 0x80)
                --chunk;
            if (chunk == 0)
                return false;
        }
        if (!WriteFile(handle, bytes, chunk, &written, nullptr) || written == 0)
            return false;
        bytes += written;
        length -= written;
    }
    return true;
}

DWORD windowsOutputMode(DWORD mode)
{
    // Full frames contain explicit CRLF between exactly-full rows. Delayed
    // wrapping keeps the bottom-right cell from scrolling the board away.
    return mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING
        | ENABLE_WRAP_AT_EOL_OUTPUT | DISABLE_NEWLINE_AUTO_RETURN;
}

void restoreWindowsTerminal(TuiWindowsTerminalState *state)
{
    if (state->restored)
        return;
    state->restored = true;
    if (state->alternateScreen) {
        static const char restore[] = "\x1b[?1049l\x1b[?25h\x1b[0m";
        writeWindowsBytes(state->output, restore, sizeof(restore) - 1);
    }
    // Leave the alternate buffer while VT is still enabled, then restore the
    // original buffer's attributes, cursor, modes and code pages exactly.
    SetConsoleTextAttribute(state->output, state->attributes);
    SetConsoleCursorInfo(state->output, &state->cursor);
    SetConsoleMode(state->input, state->inputMode);
    SetConsoleMode(state->output, state->outputMode);
    SetConsoleCP(state->inputCodePage);
    SetConsoleOutputCP(state->outputCodePage);
}

BOOL WINAPI windowsControlHandler(DWORD event)
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && event != CTRL_CLOSE_EVENT
        && event != CTRL_LOGOFF_EVENT && event != CTRL_SHUTDOWN_EVENT)
        return FALSE;
    AcquireSRWLockExclusive(&g_windowsTerminalLock);
    const bool owned = g_windowsTerminal != nullptr;
    if (owned) {
        if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT)
            g_windowsTerminal->interruptPending.store(true);
        else
            restoreWindowsTerminal(g_windowsTerminal);
    }
    ReleaseSRWLockExclusive(&g_windowsTerminalLock);
    // C/Break are handed to Qt by the poll timer. Close/logoff must restore
    // synchronously: Windows need not give the Qt event loop another turn.
    return owned && (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT);
}

void windowsFatalSignalHandler(int number)
{
    // Best effort if a fault occurs during enter/leave itself; never deadlock
    // a fatal callback on the lock that its interrupted thread already owns.
    if (TryAcquireSRWLockExclusive(&g_windowsTerminalLock)) {
        if (g_windowsTerminal != nullptr)
            restoreWindowsTerminal(g_windowsTerminal);
        ReleaseSRWLockExclusive(&g_windowsTerminalLock);
    }
    std::signal(number, SIG_DFL);
    std::raise(number);
}
#endif

#if defined(Q_OS_UNIX)

// --- State the fatal-signal path needs, laid out for async-signal-safety ---
//
// A signal handler may only touch plain data with async-signal-safe
// functions: write(), and reads/writes of a sig_atomic_t. It must not
// allocate, must not touch a QByteArray or QString, and must not call into
// Qt. That rules out building the restore sequence, or even looking it up
// via TuiTerminal::restoreSequence(), from inside the handler -- so enter()
// builds it once, up front, into this plain buffer, and the handler only
// ever writes bytes that already exist.
//
// There is exactly one controlling terminal per process, so this state is
// process-global rather than per-TuiTerminal-instance on purpose: a second
// TuiTerminal entering raw mode on top of the first would not make sense
// either, and the crash handler only ever needs to undo the one real
// takeover.
constexpr int kMaxRestoreBytes = 64;
char g_restoreBytes[kMaxRestoreBytes];
volatile sig_atomic_t g_restoreLength = 0;
volatile sig_atomic_t g_restoreFd = -1;

// Read end lives with the TuiTerminal (behind a QSocketNotifier); the
// handler only ever needs the write end, and only as a bare fd -- writing to
// it is the entire hand-off from signal context back to the event loop.
volatile sig_atomic_t g_wakeWriteFd = -1;

extern "C" void tuiTerminalSignalHandler(int number)
{
    // Get the terminal back for every signal here that actually ends the
    // session -- the fatal ones below, and SIGTERM/SIGHUP, which fall
    // through to the same "please shut down" path afterwards. SIGWINCH is
    // deliberately excluded: a plain resize does not end anything, and
    // writing the restore sequence for it would leave the alternate screen
    // (and re-show the cursor) on every window resize, with nothing after it
    // ever re-entering the alternate screen -- the resize repaint below only
    // ever emits a fresh frame, never `\x1b[?1049h` again. If a resize
    // happens to race a real fatal signal, that signal's own invocation of
    // this same handler still writes the restore bytes; the only thing lost
    // by excluding SIGWINCH is a redundant write of bytes the fatal signal
    // was going to write anyway.
    if (number != SIGWINCH && g_restoreLength > 0 && g_restoreFd >= 0)
        ::write(int(g_restoreFd), g_restoreBytes, size_t(g_restoreLength));

    if (number == SIGSEGV || number == SIGABRT) {
        // The terminal is back to normal; do not try to keep the process
        // alive on top of a corrupted heap or a failed assertion. Restore
        // the default disposition and re-raise so the crash proceeds
        // normally -- core dump, exit status, whatever the shell expects --
        // instead of us pretending we handled it.
        ::signal(number, SIG_DFL);
        ::raise(number);
        return;
    }

    // Everything else (SIGWINCH aside) funnels into the same "please shut
    // down" path; the notifier slot decides what interrupted() means.
    const char token = (number == SIGWINCH) ? 'w' : 'i';
    if (g_wakeWriteFd >= 0)
        ::write(int(g_wakeWriteFd), &token, 1);
}

// --- Shared SIGINT state, and the one handler that unifies its two owners ---
//
// Classic mode never takes the terminal into raw mode or the alternate
// screen, so it has nothing in common with the restore-sequence state above
// except SIGINT itself: both TuiTerminal::enter() and
// tuiInstallInterruptHandler() (the latter installed unconditionally by
// TuiInput on Unix, in both classic and board mode) need to own it. This
// self-pipe stays separate from TuiTerminal's wake pipe so classic mode
// never needs a TuiTerminal instance at all -- but, unlike that pipe, the
// *signal handler* for SIGINT below is shared rather than duplicated, so
// installing one of these two owners can never silently disable the other.
int g_interruptWakePipe[2] = { -1, -1 };
std::function<void()> g_interruptCallback;
QSocketNotifier *g_interruptNotifier = nullptr;

// sigaction() only ever keeps the *last* installed handler for a given
// signal. Before this function existed, TuiTerminal::enter() and
// tuiInstallInterruptHandler() each installed their own SIGINT handler, so
// whichever ran second silently disabled the first: depending on install
// order, Ctrl+C then either restored the terminal without disconnecting, or
// disconnected without restoring the terminal -- and board mode runs both.
// Routing both installers through this one function instead makes the
// install idempotent (reinstalling the same function changes nothing), so
// the order stops mattering. Each half below is inert when its owner was
// never installed: g_restoreLength stays 0 with no TuiTerminal entered, and
// g_interruptWakePipe[1] stays -1 with no tuiInstallInterruptHandler call.
extern "C" void tuiSigintSignalHandler(int /* number */)
{
    if (g_restoreLength > 0 && g_restoreFd >= 0)
        ::write(int(g_restoreFd), g_restoreBytes, size_t(g_restoreLength));
    const char token = 'i';
    if (g_wakeWriteFd >= 0)
        ::write(int(g_wakeWriteFd), &token, 1);
    if (g_interruptWakePipe[1] >= 0)
        ::write(g_interruptWakePipe[1], &token, 1);
}

#endif // Q_OS_UNIX

} // namespace

#if defined(Q_OS_UNIX)
// Deliberately not `static` and not inside the anonymous namespace above:
// this is the one sigaction(SIGINT, ...) call that both TuiTerminal::enter()
// and tuiInstallInterruptHandler() make (see tuiSigintSignalHandler for why
// sharing it matters), and giving it external linkage lets
// tests/tui/tui-terminal-test.cpp forward-declare and call it directly --
// the only way to exercise TuiTerminal's half of the ordering invariant
// without a real terminal to carry enter() past its isatty() gate.
void tuiInstallSharedSigintHandler()
{
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = tuiSigintSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    ::sigaction(SIGINT, &action, nullptr);
}
#endif

TuiTerminal::TuiTerminal(int inFd, int outFd, QObject *parent)
    : QObject(parent)
    , m_inFd(inFd)
    , m_outFd(outFd)
{
}

TuiTerminal::~TuiTerminal()
{
    leave();
}

bool TuiTerminal::enter(QString *error)
{
    // A second enter() while the terminal is still taken must not re-run
    // tcgetattr(): that would capture the *already-raw*, unechoed termios as
    // the new "original", so leave() would then restore the terminal to
    // that raw state instead of the user's real shell settings -- silently
    // wrecking the shell, the exact failure this class exists to prevent.
    // Nothing calls enter() twice today, but a later caller retrying after a
    // failed board-mode start is a reasonable thing to write, so this guard
    // needs to exist before that caller does. Mirrors leave()'s own
    // compare_exchange re-entrancy guard rather than leaving enter()
    // asymmetric with it.
    if (!m_left.load()) {
        if (error != nullptr)
            *error = QStringLiteral("tui: terminal already entered; call leave() first");
        return false;
    }

#if defined(Q_OS_UNIX)
    if (::isatty(m_outFd) != 1 || ::isatty(m_inFd) != 1) {
        if (error != nullptr)
            *error = QStringLiteral("tui: fd %1/%2 is not a terminal").arg(m_inFd).arg(m_outFd);
        return false;
    }

    auto *saved = new struct termios;
    if (::tcgetattr(m_outFd, saved) != 0) {
        if (error != nullptr)
            *error = QStringLiteral("tui: tcgetattr failed: %1")
                .arg(QString::fromLocal8Bit(std::strerror(errno)));
        delete saved;
        return false;
    }

    struct termios raw = *saved;
    // ICANON/ECHO come off so keystrokes reach the board UI one at a time,
    // unechoed. ISIG stays on: that is what keeps Ctrl+C a real SIGINT
    // instead of a keystroke the raw-mode reader would otherwise have to
    // notice by scanning for 0x03 in the input stream, and what keeps Ctrl+\
    // (SIGQUIT) working as a last-resort escape hatch too.
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (::tcsetattr(m_outFd, TCSAFLUSH, &raw) != 0) {
        if (error != nullptr)
            *error = QStringLiteral("tui: tcsetattr failed: %1")
                .arg(QString::fromLocal8Bit(std::strerror(errno)));
        delete saved;
        return false;
    }
    m_savedTermios = saved;

    static const char kEnterSequence[] = "\x1b[?1049h\x1b[?25l"; // alt screen, hide cursor
    ssize_t written = ::write(m_outFd, kEnterSequence, sizeof(kEnterSequence) - 1);
    (void)written; // best-effort: a torn write here is not worth unwinding raw mode over

    if (::pipe(m_wakePipe) != 0) {
        if (error != nullptr)
            *error = QStringLiteral("tui: pipe() failed: %1")
                .arg(QString::fromLocal8Bit(std::strerror(errno)));
        // Unwind both of the changes already made above: the terminal must
        // not be left in the alternate screen with the cursor hidden just
        // because the self-pipe could not be created.
        const QByteArray restore = restoreSequence();
        ssize_t written = ::write(m_outFd, restore.constData(), size_t(restore.size()));
        (void)written;
        ::tcsetattr(m_outFd, TCSAFLUSH, saved);
        delete saved;
        m_savedTermios = nullptr;
        return false;
    }
    // Non-blocking on both ends: the read side is drained in a loop by the
    // notifier slot and must not block once it runs dry, and the write side
    // must never block *inside the signal handler* even in the (practically
    // impossible, single bytes at a time) case that the pipe's buffer fills.
    ::fcntl(m_wakePipe[0], F_SETFL, O_NONBLOCK);
    ::fcntl(m_wakePipe[1], F_SETFL, O_NONBLOCK);

    // Build the handler's restore buffer now, while we can still allocate:
    // the handler itself cannot touch a QByteArray. Publish g_restoreLength
    // last -- it is the flag the handler checks before trusting the bytes.
    const QByteArray restore = restoreSequence();
    const int length = qMin<int>(restore.size(), kMaxRestoreBytes);
    g_restoreLength = 0;
    std::memcpy(g_restoreBytes, restore.constData(), size_t(length));
    g_restoreFd = m_outFd;
    g_restoreLength = length;
    g_wakeWriteFd = m_wakePipe[1];

    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = tuiTerminalSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    const int handledSignals[] = { SIGWINCH, SIGTERM, SIGHUP, SIGSEGV, SIGABRT };
    for (int signalNumber : handledSignals)
        ::sigaction(signalNumber, &action, nullptr);
    // SIGINT is installed separately, through the same function
    // tuiInstallInterruptHandler() also calls, so the two never fight over
    // which handler wins -- see tuiSigintSignalHandler's comment.
    tuiInstallSharedSigintHandler();

    m_notifier = new QSocketNotifier(m_wakePipe[0], QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this,
        [this](QSocketDescriptor, QSocketNotifier::Type) { drainWakePipe(); });

    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &TuiTerminal::leave);

    m_left.store(false);
    return true;
#elif defined(Q_OS_WIN)
    AcquireSRWLockExclusive(&g_windowsTerminalLock);
    if (g_windowsTerminal != nullptr) {
        ReleaseSRWLockExclusive(&g_windowsTerminalLock);
        if (error != nullptr)
            *error = QStringLiteral("tui: another terminal already owns this console");
        return false;
    }
    auto *state = new TuiWindowsTerminalState;
    state->input = windowsHandle(m_inFd);
    state->output = windowsHandle(m_outFd);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleMode(state->input, &state->inputMode)
        || !GetConsoleMode(state->output, &state->outputMode)
        || !GetConsoleCursorInfo(state->output, &state->cursor)
        || !GetConsoleScreenBufferInfo(state->output, &info)) {
        delete state;
        ReleaseSRWLockExclusive(&g_windowsTerminalLock);
        if (error != nullptr)
            *error = QStringLiteral("tui: board needs a Windows console for stdin and stdout");
        return false;
    }
    state->attributes = info.wAttributes;
    state->inputCodePage = GetConsoleCP();
    state->outputCodePage = GetConsoleOutputCP();
    // ReadConsoleInputW remains the reader. Disable VT INPUT so it keeps
    // delivering INPUT_RECORDs, and QuickEdit so selection cannot pause IO.
    const DWORD inputMode = (state->inputMode | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT)
        & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT
            | ENABLE_QUICK_EDIT_MODE | ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT);
    bool entered = SetConsoleMode(state->output, windowsOutputMode(state->outputMode))
        && SetConsoleMode(state->input, inputMode)
        && SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8);
    if (entered) {
        state->alternateScreen = true; // also unwind a partially written enter sequence
        static const char enter[] = "\x1b[?1049h\x1b[?25l";
        entered = writeWindowsBytes(state->output, enter, sizeof(enter) - 1);
    }
    if (entered)
        entered = SetConsoleCtrlHandler(windowsControlHandler, TRUE) != FALSE;
    if (!entered) {
        const DWORD code = GetLastError();
        restoreWindowsTerminal(state);
        delete state;
        ReleaseSRWLockExclusive(&g_windowsTerminalLock);
        if (error != nullptr)
            *error = QStringLiteral("tui: Windows console setup failed (error %1)").arg(code);
        return false;
    }
    state->previousAbort = std::signal(SIGABRT, windowsFatalSignalHandler);
    state->previousSegv = std::signal(SIGSEGV, windowsFatalSignalHandler);
    g_windowsTerminal = m_windowsState = state;
    m_left.store(false);
    ReleaseSRWLockExclusive(&g_windowsTerminalLock);

    m_lastSize = size();
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() {
        if (m_windowsState->interruptPending.exchange(false)) {
            emit interrupted();
            return;
        }
        const QSize current = size();
        if (current != m_lastSize) {
            m_lastSize = current;
            emit resized();
        }
    });
    m_pollTimer->start(100);
    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &TuiTerminal::leave);
    return true;
#else
    if (error != nullptr)
        *error = QStringLiteral("tui: raw-mode terminal support is not implemented on this "
                                 "platform");
    return false;
#endif
}

void TuiTerminal::leave()
{
#if defined(Q_OS_UNIX)
    bool expected = false;
    if (!m_left.compare_exchange_strong(expected, true))
        return; // never entered, or already left: nothing to undo

    // Drop back to the default disposition before touching anything else, so
    // a signal arriving mid-teardown cannot fire into a handler that is
    // about to have the state out from under it (pipe closed, fd reused).
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    const int handledSignals[] = { SIGWINCH, SIGTERM, SIGHUP, SIGSEGV, SIGABRT };
    for (int signalNumber : handledSignals)
        ::sigaction(signalNumber, &action, nullptr);
    // SIGINT is shared with tuiInstallInterruptHandler() (see
    // tuiSigintSignalHandler): only drop it to the default disposition here
    // if that installer never ran. If it did, SIGINT must keep pointing at
    // the shared handler -- clearing g_restoreLength/g_wakeWriteFd below
    // already makes TuiTerminal's half of that handler inert, so leaving the
    // sigaction in place costs nothing and keeps Ctrl+C still reaching
    // interruptRequested() after this TuiTerminal goes away.
    if (g_interruptNotifier == nullptr)
        ::sigaction(SIGINT, &action, nullptr);
    g_restoreLength = 0;
    g_restoreFd = -1;
    g_wakeWriteFd = -1;

    if (m_savedTermios != nullptr) {
        ::tcsetattr(m_outFd, TCSAFLUSH, static_cast<const struct termios *>(m_savedTermios));
        delete static_cast<struct termios *>(m_savedTermios);
        m_savedTermios = nullptr;
    }

    const QByteArray restore = restoreSequence();
    ssize_t written = ::write(m_outFd, restore.constData(), size_t(restore.size()));
    (void)written;

    delete m_notifier;
    m_notifier = nullptr;
    if (m_wakePipe[0] >= 0) {
        ::close(m_wakePipe[0]);
        m_wakePipe[0] = -1;
    }
    if (m_wakePipe[1] >= 0) {
        ::close(m_wakePipe[1]);
        m_wakePipe[1] = -1;
    }
#elif defined(Q_OS_WIN)
    bool expected = false;
    if (!m_left.compare_exchange_strong(expected, true))
        return;
    delete m_pollTimer;
    m_pollTimer = nullptr;
    AcquireSRWLockExclusive(&g_windowsTerminalLock);
    auto *state = m_windowsState;
    restoreWindowsTerminal(state);
    g_windowsTerminal = nullptr;
    if (state->previousAbort != SIG_ERR)
        std::signal(SIGABRT, state->previousAbort);
    if (state->previousSegv != SIG_ERR)
        std::signal(SIGSEGV, state->previousSegv);
    m_windowsState = nullptr;
    SetConsoleCtrlHandler(windowsControlHandler, FALSE);
    ReleaseSRWLockExclusive(&g_windowsTerminalLock);
    delete state;
#endif
}

bool TuiTerminal::write(const QByteArray &bytes) const
{
#if defined(Q_OS_WIN)
    return writeWindowsBytes(windowsHandle(m_outFd), bytes.constData(), bytes.size());
#else
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written = ::write(m_outFd, bytes.constData() + offset,
            static_cast<size_t>(bytes.size() - offset));
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;
        offset += written;
    }
    return true;
#endif
}

#if defined(Q_OS_WIN)
bool TuiTerminal::supportsWindowsConsole()
{
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(output, &mode))
        return false;
    if (!SetConsoleMode(output, windowsOutputMode(mode)))
        return false;
    return SetConsoleMode(output, mode) != FALSE;
}
#endif

QSize TuiTerminal::size() const
{
#if defined(Q_OS_UNIX)
    struct winsize ws;
    std::memset(&ws, 0, sizeof(ws));
    if (::ioctl(m_outFd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0)
        return QSize(int(ws.ws_col), int(ws.ws_row)); // QSize(width, height) == (cols, rows)
#elif defined(Q_OS_WIN)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(windowsHandle(m_outFd), &info)) {
        const QSize viewport(info.srWindow.Right - info.srWindow.Left + 1,
            info.srWindow.Bottom - info.srWindow.Top + 1);
        if (viewport.width() > 0 && viewport.height() > 0)
            return viewport;
    }
#endif
    return QSize(80, 24); // width, height -- i.e. 80 cols x 24 rows
}

QByteArray TuiTerminal::restoreSequence()
{
    // Leave the alternate screen, show the cursor again, and drop any SGR
    // attribute still in force. Shared verbatim by leave() and by the signal
    // handler's pre-built buffer -- see the comment above
    // tuiTerminalSignalHandler for why the handler cannot build this itself.
    return QByteArrayLiteral("\x1b[?1049l\x1b[?25h\x1b[0m");
}

#if defined(Q_OS_UNIX)
void TuiTerminal::drainWakePipe()
{
    // O_NONBLOCK on the read end makes the last read() in the loop fail with
    // EAGAIN instead of blocking once the pipe runs dry; EINTR is retried
    // rather than treated as "done" so a signal landing mid-drain cannot
    // make this return before the pipe is actually empty. QSocketNotifier is
    // level-triggered so a premature EINTR exit would not hang -- the
    // notifier would just fire again -- but there is no reason to leave
    // bytes sitting in the pipe until the next event-loop turn either.
    for (;;) {
        char token = 0;
        const ssize_t n = ::read(m_wakePipe[0], &token, 1);
        if (n == 1) {
            if (token == 'w')
                emit resized();
            else
                emit interrupted();
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break; // EAGAIN (drained) or an unexpected error: nothing more to read
    }
}
#endif

void tuiInstallInterruptHandler(std::function<void()> callback)
{
#if defined(Q_OS_UNIX)
    g_interruptCallback = std::move(callback);
    if (g_interruptNotifier != nullptr)
        return; // pipe and signal handler already installed; only the callback changes

    if (::pipe(g_interruptWakePipe) != 0)
        return; // best effort: classic mode still runs, just without a graceful Ctrl+C
    ::fcntl(g_interruptWakePipe[0], F_SETFL, O_NONBLOCK);
    ::fcntl(g_interruptWakePipe[1], F_SETFL, O_NONBLOCK);

    // No parent, and never deleted: this is a once-per-process install meant
    // to live exactly as long as the process does, same as the callback
    // itself needing its target (typically TuiInput) to outlive it.
    g_interruptNotifier = new QSocketNotifier(g_interruptWakePipe[0], QSocketNotifier::Read);
    QObject::connect(g_interruptNotifier, &QSocketNotifier::activated, g_interruptNotifier,
        [](QSocketDescriptor, QSocketNotifier::Type) {
            // See TuiTerminal::drainWakePipe() for why EINTR is retried
            // rather than treated as end-of-data.
            for (;;) {
                char token = 0;
                const ssize_t n = ::read(g_interruptWakePipe[0], &token, 1);
                if (n == 1) {
                    // Call a copy, not the global itself. The callback's own
                    // work reaches TuiInput::stop(), which calls
                    // tuiClearInterruptHandler() -- destroying the
                    // std::function while it is executing. That survives
                    // today only because the installed lambda captures a bare
                    // `this` small enough to live in the function's inline
                    // buffer and touches nothing after the emit; a capture one
                    // pointer larger is heap-allocated and this becomes a
                    // use-after-free, which is a shape this repo has been
                    // bitten by before.
                    std::function<void()> callback = g_interruptCallback;
                    if (callback)
                        callback();
                    continue;
                }
                if (n < 0 && errno == EINTR)
                    continue;
                break;
            }
        });

    // Installed through the same function TuiTerminal::enter() uses for
    // SIGINT, not a separate sigaction() call -- see tuiSigintSignalHandler.
    tuiInstallSharedSigintHandler();
#else
    // Windows gets a graceful Ctrl+C exit from TuiInput's own console event
    // loop (ReadConsoleInputW watching for a control-key chord), which is
    // wired up separately. There is no self-pipe/sigaction primitive to hang
    // this on there, so this is intentionally a no-op.
    (void)callback;
#endif
}

void tuiClearInterruptHandler()
{
#if defined(Q_OS_UNIX)
    g_interruptCallback = std::function<void()>();
#endif
}
