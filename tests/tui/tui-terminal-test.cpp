// Windows exercises a private hidden native console. On Unix, raw mode,
// SIGWINCH and alternate screen restoration are covered by the pty-based
// tools/autotest/tui_board_smoke.py. The
// SIGINT-sharing test below is registration-level for the same reason: it
// checks which handler sigaction() reports installed, and never raises a
// real signal or touches a real terminal.
#include "tui-terminal.h"

#include <QCoreApplication>
#include <QSize>

#include <cstdio>
#include <cstring>

#if defined(Q_OS_WIN)
#include "tui-input.h"
#include "tui-line-editor.h"
#include "tui-screen.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <qt_windows.h>
#include <string>
#endif

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
QStringList failureMessages;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        failureMessages.append(QString::fromUtf8(what));
        std::printf("[FAIL] %s\n", what);
    }
}

#if defined(Q_OS_WIN)
void pumpConsoleEvents(int milliseconds = 250)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
}

void sendKey(WORD code, WCHAR character = 0, DWORD modifiers = 0, WORD repeat = 1,
             bool pressed = true)
{
    INPUT_RECORD record{};
    record.EventType = KEY_EVENT;
    record.Event.KeyEvent = {pressed, repeat, code,
        static_cast<WORD>(MapVirtualKeyW(code, MAPVK_VK_TO_VSC)), {}, modifiers};
    record.Event.KeyEvent.uChar.UnicodeChar = character;
    DWORD written = 0;
    check(WriteConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), &record, 1, &written)
              && written == 1,
          "a native key record enters the console input queue");
}

void windowsConsoleChecks()
{
    HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    // This worker owns a hidden console. Fix its viewport so corner assertions
    // cover full-width frames and the bottom-right wrap boundary deterministically.
    SMALL_RECT initialWindow{0, 0, 79, 23};
    check(SetConsoleWindowInfo(outputHandle, TRUE, &initialWindow), "set 80x24 viewport");
    check(SetConsoleScreenBufferSize(outputHandle, COORD{80, 24}), "set 80x24 buffer");
    DWORD originalInput = 0, originalOutput = 0;
    GetConsoleMode(inputHandle, &originalInput);
    GetConsoleMode(outputHandle, &originalOutput);
    const UINT originalInputCP = GetConsoleCP(), originalOutputCP = GetConsoleOutputCP();
    CONSOLE_CURSOR_INFO originalCursor{};
    GetConsoleCursorInfo(outputHandle, &originalCursor);
    check(TuiTerminal::supportsWindowsConsole(), "a native modern console supports board");
    DWORD mode = 0;
    GetConsoleMode(outputHandle, &mode);
    check(mode == originalOutput, "capability probing restores the original output mode");

    TuiTerminal terminal;
    QString error;
    if (!terminal.enter(&error)) {
        check(false, "native Windows terminal entry succeeds");
        failureMessages.append(error);
        return;
    }
    check(!terminal.enter(&error), "re-entry cannot overwrite original console state");
    TuiTerminal second;
    check(!second.enter(&error), "another terminal cannot claim the same console");
    check(terminal.size() == QSize(80, 24), "size reports the viewport in columns and rows");
    GetConsoleMode(inputHandle, &mode);
    check(!(mode & (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT
                      | ENABLE_QUICK_EDIT_MODE | ENABLE_VIRTUAL_TERMINAL_INPUT)),
          "board reads unechoed native key records without QuickEdit or VT input");
    check(GetConsoleOutputCP() == CP_UTF8, "board output uses UTF-8");

    TuiScreen screen;
    screen.resize(24, 80);
    screen.drawBox({0, 0, 24, 80});
    screen.putText(1, 1, QStringLiteral("\u4e2d\u6587"));
    check(terminal.write(screen.flush().toUtf8()), "a real board frame reaches the console");
    auto cell = [outputHandle](SHORT x, SHORT y) {
        // Read the whole double-width character; a one-cell request can be
        // returned as padding when it clips a CJK glyph's trailing cell.
        WCHAR characters[2]{};
        DWORD read = 0;
        check(ReadConsoleOutputCharacterW(outputHandle, characters, 2, COORD{x, y}, &read)
                  && read >= 1, "the native screen buffer can be read");
        return characters[0];
    };
    check(cell(0, 0) == 0x250c && cell(79, 23) == 0x2518,
          "a full frame preserves both corners instead of scrolling the first row away");
    check(cell(1, 1) == 0x4e2d, "UTF-8 Chinese survives the console write without CRT recoding");
    QByteArray largeWrite;
    for (int i = 0; i < 8191; ++i)
        largeWrite += "\x1b[0m";
    largeWrite += "\x1b[H"; // first CJK byte lands at offset 32767
    largeWrite += QStringLiteral("\u4e2d\u6587").toUtf8();
    check(terminal.write(largeWrite) && cell(0, 0) == 0x4e2d && cell(2, 0) == 0x6587,
          "large writes preserve Unicode across the output chunk boundary");
    if (failures != 0)
        failureMessages << QStringLiteral("console cell (1,1)=U+%1").arg(uint(cell(1, 1)), 4, 16, QLatin1Char('0'));

    TuiInput input;
    input.setRawMode(true);
    TuiKeyDecoder decoder;
    TuiLineEditor editor;
    editor.setCompleter([](const QString &, QStringList *) { return QStringLiteral("/help"); });
    QStringList submitted;
    int directLines = 0, interrupts = 0, resized = 0;
    QVector<TuiKey> keys;
    QObject::connect(&input, &TuiInput::lineReady, &input, [&](const QString &) { ++directLines; });
    QObject::connect(&input, &TuiInput::inputError, &input,
        [&](const QString &message) { check(false, "native input has no errors"); failureMessages << message; });
    QObject::connect(&input, &TuiInput::interruptRequested, &input, [&]() { ++interrupts; });
    QObject::connect(&terminal, &TuiTerminal::interrupted, &input, [&]() { ++interrupts; });
    QObject::connect(&terminal, &TuiTerminal::resized, &input, [&]() { ++resized; });
    QObject::connect(&input, &TuiInput::rawBytes, &input, [&](const QByteArray &bytes) {
        for (const TuiKeyEvent &event : decoder.feed(bytes)) {
            keys << event.key;
            QString line;
            editor.handle(event, &line);
            if (event.key == TuiKey::Enter)
                submitted << line;
        }
    });
    check(input.start(&error), "QWinEventNotifier starts on the native input queue");
    sendKey('A', L'a'); sendKey('B', L'b'); sendKey(VK_LEFT);
    sendKey(0, 0x4e2d); sendKey(VK_DELETE); sendKey(VK_HOME);
    sendKey(0, 0xd83d); pumpConsoleEvents(20); sendKey(0, 0xde00);
    sendKey(VK_END); sendKey(VK_RETURN, L'\r');
    sendKey('Z', L'z', 0, 3); sendKey(VK_RETURN, L'\r');
    sendKey(VK_TAB, L'\t'); sendKey(VK_RETURN, L'\r');
    sendKey('X', L'x', 0, 1, false); // key-up must not insert text
    sendKey('A', L'a'); sendKey('B', L'b'); sendKey(VK_BACK);
    sendKey(VK_RETURN, L'\r');
    sendKey(VK_PRIOR); sendKey(VK_NEXT);
    sendKey('U', 0x15, LEFT_CTRL_PRESSED); sendKey('K', 0x0b, LEFT_CTRL_PRESSED);
    sendKey('W', 0x17, LEFT_CTRL_PRESSED); sendKey('A', 0x01, LEFT_CTRL_PRESSED);
    sendKey('E', 0x05, LEFT_CTRL_PRESSED);
    sendKey('C', 0x03, LEFT_CTRL_PRESSED);
    pumpConsoleEvents();
    check(submitted == QStringList{QStringLiteral("\U0001f600a\u4e2d"), QStringLiteral("zzz"),
              QStringLiteral("/help"), QStringLiteral("a")},
          "native text, split surrogates, repeats, editing and Tab use one line editor");
    if (failures != 0) {
        for (const QString &line : submitted) {
            QStringList points;
            for (char32_t point : line.toUcs4())
                points << QString::number(uint(point), 16);
            failureMessages << QStringLiteral("submitted codepoints: %1").arg(points.join(QLatin1Char(' ')));
        }
        QStringList decoded;
        for (TuiKey key : keys)
            decoded << QString::number(int(key));
        failureMessages << QStringLiteral("decoded keys: %1").arg(decoded.join(QLatin1Char(' ')));
    }
    check(directLines == 0, "raw input never also submits classic lineReady");
    check(interrupts == 1, "Ctrl+C requests exactly one graceful exit");
    check(keys.contains(TuiKey::PageUp) && keys.contains(TuiKey::PageDown)
              && keys.contains(TuiKey::KillToLineStart) && keys.contains(TuiKey::KillToLineEnd)
              && keys.contains(TuiKey::KillPreviousWord), "paging and kill keys reach the shared decoder");
    sendKey(VK_ESCAPE, 0x1b);
    pumpConsoleEvents(30);
    check(decoder.resolvePendingEscape().value(0).key == TuiKey::Escape,
          "a lone Escape remains available to the existing escape timer");

    check(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0), "Ctrl+Break reaches the native handler");
    pumpConsoleEvents();
    check(interrupts == 2, "Ctrl+Break is handed back to the Qt event loop");
    // Resize the actual owned window: SetConsoleWindowInfo can silently do
    // nothing in conhost's alternate screen (microsoft/terminal#13741).
    HWND window = GetConsoleWindow();
    RECT bounds{};
    CONSOLE_FONT_INFO font{};
    check(GetWindowRect(window, &bounds) && GetCurrentConsoleFont(outputHandle, FALSE, &font),
          "query the private console's window and cell dimensions");
    check(SetWindowPos(window, nullptr, 0, 0,
              bounds.right - bounds.left - 20 * font.dwFontSize.X,
              bounds.bottom - bounds.top - 6 * font.dwFontSize.Y,
              SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), "shrink the hidden console window");
    pumpConsoleEvents();
    const QSize smallerSize = terminal.size();
    check(smallerSize.width() < 80 && smallerSize.height() < 24
              && smallerSize.width() > 0 && smallerSize.height() > 0 && resized == 1,
          "a viewport change emits one resize and reports the new size");
    if (smallerSize == QSize(80, 24) || resized != 1)
        failureMessages << QStringLiteral("viewport=%1x%2 resize events=%3")
            .arg(terminal.size().width()).arg(terminal.size().height()).arg(resized);

    // Deliberately reverse normal shutdown: Terminal can die before TuiInput
    // during constructor/startup unwind, and input must not resurrect raw mode.
    terminal.leave();
    terminal.leave();
    input.stop();
    GetConsoleMode(inputHandle, &mode);
    check(mode == originalInput, "input mode survives terminal-first teardown");
    GetConsoleMode(outputHandle, &mode);
    check(mode == originalOutput, "output mode is restored exactly");
    check(GetConsoleCP() == originalInputCP && GetConsoleOutputCP() == originalOutputCP,
          "both console code pages are restored");
    CONSOLE_CURSOR_INFO cursor{};
    GetConsoleCursorInfo(outputHandle, &cursor);
    check(cursor.bVisible == originalCursor.bVisible && cursor.dwSize == originalCursor.dwSize,
          "cursor visibility and shape are restored");
    check(terminal.enter(&error), "a restored console can be entered again");
    terminal.leave();
}

void runWindowsConsoleWorker()
{
    // Never take over the caller's terminal. A fresh hidden console also makes
    // this gate runnable from redirected build/CI output without skipping it.
    QTemporaryDir directory;
    check(directory.isValid(), "create private console report directory");
    const QString reportPath = directory.filePath(QStringLiteral("console.txt"));
    std::wstring command = QStringLiteral("\"%1\" --suite terminal --windows-console \"%2\"")
        .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
             QDir::toNativeSeparators(reportPath)).toStdWString();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
        CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process);
    check(started, "start a worker in its own hidden Windows console");
    if (!started)
        return;
    const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 1000);
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    check(wait == WAIT_OBJECT_0 && code == 0, "native Windows console regression passes");
    QFile report(reportPath);
    check(report.open(QIODevice::ReadOnly), "native worker writes its completion evidence");
    const QByteArray details = report.readAll();
    std::fwrite(details.constData(), 1, static_cast<size_t>(details.size()), stdout);
    check(details.contains("WINDOWS_CONSOLE_RESULT status=PASS"), "native worker completed all checks");
}
#endif

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

#if defined(Q_OS_WIN)
    const QStringList arguments = application.arguments();
    const qsizetype worker = arguments.indexOf(QStringLiteral("--windows-console"));
    if (worker >= 0) {
        windowsConsoleChecks();
        QFile report(arguments.value(worker + 1));
        if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 1;
        report.write(failureMessages.join(QLatin1Char('\n')).toUtf8());
        report.write(failures == 0 ? "\nWINDOWS_CONSOLE_RESULT status=PASS\n"
                                  : "\nWINDOWS_CONSOLE_RESULT status=FAIL\n");
        return failures == 0 ? 0 : 1;
    }
#endif

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
#elif defined(Q_OS_WIN)
    check(error.contains(QStringLiteral("Windows console")),
          "Windows rejects invalid descriptors without invoking the CRT abort handler");
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

#if defined(Q_OS_WIN)
    runWindowsConsoleWorker();
#endif
    std::printf("[AUTOTEST] TUI_TERMINAL_RESULT status=%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
