#include "tui-input.h"
#include "tui-terminal.h"
#include "tui-text.h"

#include <QStringDecoder>
#include <QTextStream>

#include <utility>

#ifdef Q_OS_WIN
#include <QWinEventNotifier>
#include <windows.h>
#else
#include <QSocketNotifier>
#include <unistd.h>
#endif

namespace {

bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

} // namespace

TuiInput::TuiInput(QObject *parent)
    : QObject(parent)
{
}

TuiInput::~TuiInput()
{
    stop();
}

void TuiInput::setCompleter(std::function<QString(const QString &, QStringList *)> completer)
{
    m_completer = std::move(completer);
}

void TuiInput::setRawMode(bool enabled)
{
    m_rawMode = enabled;
}

bool TuiInput::start(QString *error)
{
    if (m_running)
        return true;
#ifdef Q_OS_WIN
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE)
        return fail(error, tuiText("tui_input_no_handle"));
    m_inputHandle = input;
    DWORD mode = 0;
    m_consoleInput = GetConsoleMode(input, &mode) != 0;
    if (m_consoleInput && !m_rawMode) {
        m_originalConsoleMode = mode;
        mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        if (!SetConsoleMode(input, mode))
            return fail(error, tuiText("tui_input_async_failed"));
    }
    auto *notifier = new QWinEventNotifier(input, this);
    m_notifier = notifier;
    connect(notifier, &QWinEventNotifier::activated, this,
        [this](HANDLE) { readWindowsInput(); });
#else
    const int descriptor = STDIN_FILENO;
    auto *notifier = new QSocketNotifier(descriptor, QSocketNotifier::Read, this);
    m_notifier = notifier;
    connect(notifier, &QSocketNotifier::activated, this,
        [this](QSocketDescriptor descriptor, QSocketNotifier::Type) {
            readUnixInput(static_cast<int>(descriptor));
        });
    // Ctrl+C used to be the terminal driver's business on Unix, which killed the
    // process without a graceful disconnect -- docs/tui-client.md claimed
    // otherwise. Both platforms now leave through the same signal.
    tuiInstallInterruptHandler([this]() { emit interruptRequested(); });
#endif
    m_running = true;
    return true;
}

void TuiInput::stop()
{
    if (!m_running)
        return;
    if (m_notifier != nullptr) {
#ifdef Q_OS_WIN
        static_cast<QWinEventNotifier *>(m_notifier)->setEnabled(false);
#endif
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
#ifdef Q_OS_WIN
    // Board console modes belong to TuiTerminal. Restoring the mode observed
    // AFTER its enter() here would put the shell back into raw mode on unwind.
    if (m_consoleInput && !m_rawMode && m_inputHandle != nullptr)
        SetConsoleMode(static_cast<HANDLE>(m_inputHandle), m_originalConsoleMode);
    m_inputHandle = nullptr;
    m_consoleInput = false;
    m_highSurrogate = QChar();
    m_surrogateRepeat = 0;
#else
    // start()'s tuiInstallInterruptHandler() call handed the shared
    // self-pipe a `[this]() { emit interruptRequested(); }` closure. That
    // global has no idea this TuiInput is about to be destroyed, so drop it
    // here rather than leaving a dangling `this` behind for a SIGINT
    // delivered (or already queued) after this point to call into.
    tuiClearInterruptHandler();
#endif
    m_running = false;
}

void TuiInput::appendBytes(const QByteArray &bytes)
{
    if (m_rawMode) {
        emit rawBytes(bytes);
        return;
    }
    m_buffer.append(bytes);
    while (true) {
        const qsizetype newline = m_buffer.indexOf('\n');
        if (newline < 0)
            break;
        if (newline > 16384) {
            m_buffer.clear();
            emit inputError(tuiText("tui_input_line_too_long_bytes"));
            return;
        }
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        emitBufferedLine(line);
    }
    if (m_buffer.size() > 16384) {
        m_buffer.clear();
        emit inputError(tuiText("tui_input_line_too_long_bytes"));
    }
}

void TuiInput::emitBufferedLine(const QByteArray &line)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder.decode(line);
    if (decoder.hasError()) {
        emit inputError(tuiText("tui_input_invalid_utf8"));
        return;
    }
    emit lineReady(decoded);
}

#ifdef Q_OS_WIN
void TuiInput::readWindowsInput()
{
    HANDLE input = static_cast<HANDLE>(m_inputHandle);
    if (input == nullptr)
        return;
    if (m_consoleInput) {
        DWORD available = 0;
        if (!GetNumberOfConsoleInputEvents(input, &available)) {
            emit inputError(tuiText("tui_input_peek_failed"));
            return;
        }
        while (available > 0) {
            INPUT_RECORD records[64];
            DWORD read = 0;
            if (!ReadConsoleInputW(input, records, qMin<DWORD>(available, DWORD(64)), &read)) {
                emit inputError(tuiText("tui_input_read_failed"));
                return;
            }
            for (DWORD i = 0; i < read && m_running; ++i) {
                if (records[i].EventType != KEY_EVENT || !records[i].Event.KeyEvent.bKeyDown)
                    continue;
                const KEY_EVENT_RECORD &key = records[i].Event.KeyEvent;
                const bool control = (key.dwControlKeyState
                    & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
                const bool alt = (key.dwControlKeyState
                    & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
                if (control && key.wVirtualKeyCode == 'C' && (!m_rawMode || !alt)) {
                    emit interruptRequested();
                    continue;
                }
                if (m_rawMode) {
                    // Windows supplies UTF-16 key records, not VT input bytes.
                    // Encode only the existing decoder's vocabulary; all line
                    // editing and submission still happen in TuiLineEditor.
                    QByteArray bytes;
                    const QChar character(static_cast<ushort>(key.uChar.UnicodeChar));
                    if (control && !alt && key.wVirtualKeyCode >= 'A'
                        && key.wVirtualKeyCode <= 'Z') {
                        bytes.append(char(key.wVirtualKeyCode - 'A' + 1));
                    } else {
                        switch (key.wVirtualKeyCode) {
                        case VK_LEFT: bytes = "\x1b[D"; break;
                        case VK_RIGHT: bytes = "\x1b[C"; break;
                        case VK_UP: bytes = "\x1b[A"; break;
                        case VK_DOWN: bytes = "\x1b[B"; break;
                        case VK_HOME: bytes = "\x1b[H"; break;
                        case VK_END: bytes = "\x1b[F"; break;
                        case VK_DELETE: bytes = "\x1b[3~"; break;
                        case VK_PRIOR: bytes = "\x1b[5~"; break;
                        case VK_NEXT: bytes = "\x1b[6~"; break;
                        case VK_BACK: bytes = "\x7f"; break;
                        case VK_RETURN: bytes = "\r"; break;
                        case VK_TAB: bytes = "\t"; break;
                        case VK_ESCAPE: bytes = "\x1b"; break;
                        default: break;
                        }
                    }
                    unsigned short repeat = qMax<WORD>(1, key.wRepeatCount);
                    if (!bytes.isEmpty()) {
                        m_highSurrogate = QChar();
                    } else if (character.isHighSurrogate()) {
                        m_highSurrogate = character;
                        m_surrogateRepeat = repeat;
                        continue;
                    } else if (character.isLowSurrogate()) {
                        if (m_highSurrogate.isNull())
                            continue;
                        bytes = (QString(m_highSurrogate) + character).toUtf8();
                        repeat = qMin(repeat, m_surrogateRepeat);
                        m_highSurrogate = QChar();
                    } else if (!character.isNull()) {
                        m_highSurrogate = QChar();
                        bytes = QString(character).toUtf8();
                    }
                    // Modifier-only and IME intermediate records carry no text.
                    if (!bytes.isEmpty())
                        emit rawBytes(bytes.repeated(repeat));
                    continue;
                }
                if (key.wVirtualKeyCode == VK_TAB) {
                    completeWindowsLine();
                    continue;
                }
                if (key.wVirtualKeyCode == VK_RETURN) {
                    QTextStream(stdout) << '\n' << Qt::flush;
                    emit lineReady(m_consoleLine);
                    m_consoleLine.clear();
                } else if (key.wVirtualKeyCode == VK_BACK) {
                    if (!m_consoleLine.isEmpty()) {
                        m_consoleLine.chop(1);
                        QTextStream(stdout) << "\b \b" << Qt::flush;
                    }
                } else if (key.uChar.UnicodeChar != 0) {
                    const QChar character(static_cast<ushort>(key.uChar.UnicodeChar));
                    if (!character.isNull()) {
                        if (m_consoleLine.size() >= 16384) {
                            emit inputError(tuiText("tui_input_line_too_long_chars"));
                            return;
                        }
                        m_consoleLine.append(character);
                        QTextStream(stdout) << character << Qt::flush;
                    }
                }
            }
            if (!m_running || !GetNumberOfConsoleInputEvents(input, &available))
                break;
        }
        return;
    }

    DWORD available = 0;
    if (PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr) && available == 0)
        return;
    char bytes[4096];
    DWORD read = 0;
    if (!ReadFile(input, bytes, sizeof(bytes), &read, nullptr)) {
        const DWORD code = GetLastError();
        if (code == ERROR_BROKEN_PIPE || code == ERROR_HANDLE_EOF) {
            if (!m_buffer.isEmpty()) {
                emitBufferedLine(m_buffer);
                m_buffer.clear();
            }
            emit endOfInput();
            stop();
            return;
        }
        emit inputError(tuiText("tui_input_redirect_failed"));
        return;
    }
    if (read == 0) {
        emit endOfInput();
        stop();
        return;
    }
    appendBytes(QByteArray(bytes, static_cast<qsizetype>(read)));
}

void TuiInput::completeWindowsLine()
{
    if (!m_completer)
        return;
    QStringList matches;
    const QString next = m_completer(m_consoleLine, &matches);
    if (matches.size() > 1) {
        QTextStream(stdout) << '\n' << Qt::flush;
        emit completionChoices(matches);
        m_consoleLine = next;
        QTextStream(stdout) << m_consoleLine << Qt::flush;
        return;
    }
    if (matches.isEmpty() && next == m_consoleLine)
        return;
    rewriteWindowsLine(next);
}

void TuiInput::rewriteWindowsLine(const QString &next)
{
    QTextStream out(stdout);
    const int oldWidth = int(m_consoleLine.size());
    out << '\r' << next;
    const int pad = oldWidth - int(next.size());
    if (pad > 0) {
        out << QString(pad, QLatin1Char(' '));
        out << QString(pad, QLatin1Char('\b'));
    }
    out << Qt::flush;
    m_consoleLine = next;
}
#else
void TuiInput::readUnixInput(int descriptor)
{
    char bytes[4096];
    const ssize_t count = ::read(descriptor, bytes, sizeof(bytes));
    if (count > 0) {
        appendBytes(QByteArray(bytes, static_cast<qsizetype>(count)));
        return;
    }
    if (count == 0) {
        if (!m_buffer.isEmpty()) {
            emitBufferedLine(m_buffer);
            m_buffer.clear();
        }
        emit endOfInput();
        stop();
        return;
    }
    emit inputError(tuiText("tui_input_stdin_failed"));
}
#endif
