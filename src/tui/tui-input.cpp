#include "tui-input.h"

#include <QCoreApplication>
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

QString tr(const char *source)
{
    return QCoreApplication::translate("QSanguoshaTui", source);
}

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

bool TuiInput::start(QString *error)
{
    if (m_running)
        return true;
#ifdef Q_OS_WIN
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE)
        return fail(error, tr("標準輸入控制代碼不可用"));
    m_inputHandle = input;
    DWORD mode = 0;
    m_consoleInput = GetConsoleMode(input, &mode) != 0;
    if (m_consoleInput) {
        m_originalConsoleMode = mode;
        mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        if (!SetConsoleMode(input, mode))
            return fail(error, tr("無法啟用非同步終端輸入"));
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
#endif
    m_running = true;
    return true;
}

void TuiInput::stop()
{
    if (!m_running)
        return;
    if (m_notifier != nullptr) {
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
#ifdef Q_OS_WIN
    if (m_consoleInput && m_inputHandle != nullptr)
        SetConsoleMode(static_cast<HANDLE>(m_inputHandle), m_originalConsoleMode);
    m_inputHandle = nullptr;
    m_consoleInput = false;
#endif
    m_running = false;
}

void TuiInput::appendBytes(const QByteArray &bytes)
{
    m_buffer.append(bytes);
    while (true) {
        const qsizetype newline = m_buffer.indexOf('\n');
        if (newline < 0)
            break;
        if (newline > 16384) {
            m_buffer.clear();
            emit inputError(tr("輸入行超過 16384 bytes"));
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
        emit inputError(tr("輸入行超過 16384 bytes"));
    }
}

void TuiInput::emitBufferedLine(const QByteArray &line)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder.decode(line);
    if (decoder.hasError()) {
        emit inputError(tr("輸入不是有效的 UTF-8"));
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
            emit inputError(tr("無法檢查終端輸入"));
            return;
        }
        while (available > 0) {
            INPUT_RECORD records[64];
            DWORD read = 0;
            if (!ReadConsoleInputW(input, records, qMin<DWORD>(available, DWORD(64)), &read)) {
                emit inputError(tr("無法讀取終端輸入"));
                return;
            }
            for (DWORD i = 0; i < read; ++i) {
                if (records[i].EventType != KEY_EVENT || !records[i].Event.KeyEvent.bKeyDown)
                    continue;
                const KEY_EVENT_RECORD &key = records[i].Event.KeyEvent;
                const bool control = (key.dwControlKeyState
                    & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
                if (control && key.wVirtualKeyCode == 'C') {
                    emit interruptRequested();
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
                            emit inputError(tr("輸入行超過 16384 字元"));
                            return;
                        }
                        m_consoleLine.append(character);
                        QTextStream(stdout) << character << Qt::flush;
                    }
                }
            }
            if (!GetNumberOfConsoleInputEvents(input, &available))
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
        emit inputError(tr("無法讀取重新導向的標準輸入"));
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
    emit inputError(tr("無法讀取標準輸入"));
}
#endif
