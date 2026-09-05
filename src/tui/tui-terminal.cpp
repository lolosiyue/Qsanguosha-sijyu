#include "tui-terminal.h"

#include <QCoreApplication>
#include <QSocketNotifier>

#include <cstring>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

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
    // First priority, unconditionally: get the terminal back. If a resize
    // races a crash, or two fatal signals race each other, worst case is a
    // handful of redundant writes of the same bytes.
    if (g_restoreLength > 0 && g_restoreFd >= 0)
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

// --- The separate, lighter-weight SIGINT exit for tuiInstallInterruptHandler ---
//
// Classic mode never takes the terminal into raw mode or the alternate
// screen, so it has nothing in common with the state above: no restore
// sequence, no SIGWINCH/SIGSEGV/SIGABRT handling, just Ctrl+C mapped to a
// callback. Kept as its own self-pipe rather than folded into TuiTerminal's
// so classic mode never needs a TuiTerminal instance at all.
int g_interruptWakePipe[2] = { -1, -1 };
std::function<void()> g_interruptCallback;
QSocketNotifier *g_interruptNotifier = nullptr;

extern "C" void tuiInterruptSignalHandler(int /* number */)
{
    const char token = 'i';
    if (g_interruptWakePipe[1] >= 0)
        ::write(g_interruptWakePipe[1], &token, 1);
}

#endif // Q_OS_UNIX

} // namespace

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
    const int handledSignals[] = { SIGWINCH, SIGINT, SIGTERM, SIGHUP, SIGSEGV, SIGABRT };
    for (int signalNumber : handledSignals)
        ::sigaction(signalNumber, &action, nullptr);

    m_notifier = new QSocketNotifier(m_wakePipe[0], QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this,
        [this](QSocketDescriptor, QSocketNotifier::Type) { drainWakePipe(); });

    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &TuiTerminal::leave);

    m_left.store(false);
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
    const int handledSignals[] = { SIGWINCH, SIGINT, SIGTERM, SIGHUP, SIGSEGV, SIGABRT };
    for (int signalNumber : handledSignals)
        ::sigaction(signalNumber, &action, nullptr);
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
#endif
}

QSize TuiTerminal::size() const
{
#if defined(Q_OS_UNIX)
    struct winsize ws;
    std::memset(&ws, 0, sizeof(ws));
    if (::ioctl(m_outFd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0)
        return QSize(int(ws.ws_col), int(ws.ws_row)); // QSize(width, height) == (cols, rows)
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
    char token = 0;
    // O_NONBLOCK on the read end makes the last read() in the loop fail with
    // EAGAIN instead of blocking once the pipe runs dry.
    while (::read(m_wakePipe[0], &token, 1) == 1) {
        if (token == 'w')
            emit resized();
        else
            emit interrupted();
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
            char token = 0;
            while (::read(g_interruptWakePipe[0], &token, 1) == 1) {
                if (g_interruptCallback)
                    g_interruptCallback();
            }
        });

    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = tuiInterruptSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    ::sigaction(SIGINT, &action, nullptr);
#else
    // Windows gets a graceful Ctrl+C exit from TuiInput's own console event
    // loop (ReadConsoleInputW watching for a control-key chord), which is
    // wired up separately. There is no self-pipe/sigaction primitive to hang
    // this on there, so this is intentionally a no-op.
    (void)callback;
#endif
}
