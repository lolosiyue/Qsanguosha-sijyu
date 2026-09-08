#include "server-console.h"

#include "server-core.h"
#include "version.h"

#include <QCoreApplication>
#if !defined(QSAN_XP_LEGACY)
#include <QDeadlineTimer>
#endif
#include <QSocketNotifier>

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#endif

namespace
{
constexpr qsizetype MaximumInputBufferSize = 64 * 1024;

QString enabledText(bool enabled)
{
    return enabled ? QStringLiteral("enabled") : QStringLiteral("disabled");
}

QString durationText(qint64 milliseconds)
{
    if (milliseconds < 0)
        return QStringLiteral("-");

    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString tableRow(const QStringList &columns, const QList<int> &widths)
{
    QString result;
    for (qsizetype i = 0; i < columns.size(); ++i) {
        if (i > 0)
            result += QStringLiteral("  ");
        result += i + 1 < columns.size()
            ? columns.at(i).leftJustified(widths.at(i))
            : columns.at(i);
    }
    return result;
}
}

#if defined(Q_OS_WIN)
ConsoleInputThread::ConsoleInputThread(bool interactive, QObject *parent)
    : QThread(parent), m_interactive(interactive)
{
}

void ConsoleInputThread::run()
{
    // The owner needs the native thread to CancelSynchronousIo() the blocking
    // read below when the console is torn down.
    m_threadId.store(::GetCurrentThreadId(), std::memory_order_release);

    if (m_interactive)
        readFromConsole();
    else
        readFromPipe();
}

void ConsoleInputThread::readFromConsole()
{
    const HANDLE stdinHandle = ::GetStdHandle(STD_INPUT_HANDLE);
#ifdef QSAN_XP_LEGACY
    // XP cannot cancel a synchronous ReadConsoleW. Read individual input
    // records only after readiness, so interruption always has a bounded join.
    QString pending;
    while (!isInterruptionRequested()) {
        if (::WaitForSingleObject(stdinHandle, 25) != WAIT_OBJECT_0)
            continue;
        INPUT_RECORD record;
        DWORD count = 0;
        if (!::ReadConsoleInputW(stdinHandle, &record, 1, &count)) break;
        if (!count || record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) continue;
        const wchar_t ch = record.Event.KeyEvent.uChar.UnicodeChar;
        if (ch == L'\r') { emit lineReceived(pending); pending.clear(); }
        else if (ch == L'\b') { if (!pending.isEmpty()) pending.chop(1); }
        else if (ch && pending.size() < MaximumInputBufferSize) pending.append(QChar(ch));
        DWORD written = 0;
        if (ch) ::WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), &ch, 1, &written, nullptr);
    }
    emit inputClosed();
#else
    // In line-input mode ReadConsoleW returns CR LF terminated lines unless the
    // 4 KiB chunk fills first; scanning for '\n' and keeping the remainder also
    // covers pasted lines longer than one chunk. UTF-16 arrives directly, so
    // "say 中文" is unaffected by the console codepage.
    wchar_t chunk[4096];
    QString pending;
    bool endOfInput = false;
    while (!isInterruptionRequested()) {
        DWORD count = 0;
        if (!::ReadConsoleW(stdinHandle, chunk,
                DWORD(sizeof(chunk) / sizeof(chunk[0])), &count, nullptr)) {
            // Aborted (Ctrl+C or CancelSynchronousIo) or console error.
            break;
        }
        if (count == 0) {
            endOfInput = true; // Ctrl+Z.
            break;
        }
        pending.append(reinterpret_cast<const QChar *>(chunk), int(count));
        for (;;) {
            const qsizetype newline = pending.indexOf(QLatin1Char('\n'));
            if (newline < 0)
                break;
            QString line = pending.left(newline);
            pending.remove(0, newline + 1);
            if (line.endsWith(QLatin1Char('\r')))
                line.chop(1);
            emit lineReceived(line);
        }
    }
    if (endOfInput && !pending.isEmpty()) {
        if (pending.endsWith(QLatin1Char('\r')))
            pending.chop(1);
        emit lineReceived(pending);
    }
    emit inputClosed();
#endif
}

void ConsoleInputThread::readFromPipe()
{
    const HANDLE stdinHandle = ::GetStdHandle(STD_INPUT_HANDLE);
    // Piped input is assumed to be UTF-8, matching the Unix reader.
    char chunk[4096];
    QByteArray pending;
    bool endOfInput = false;
    while (!isInterruptionRequested()) {
        DWORD count = 0;
#ifdef QSAN_XP_LEGACY
        // Anonymous QProcess stdin pipes support PeekNamedPipe on XP. Never
        // enter ReadFile until bytes exist; a live empty pipe remains stoppable.
        if (::GetFileType(stdinHandle) == FILE_TYPE_PIPE) {
            DWORD available = 0;
            if (!::PeekNamedPipe(stdinHandle, nullptr, 0, nullptr, &available, nullptr)) {
                endOfInput = true; break;
            }
            if (!available) { msleep(25); continue; }
        }
#endif
        if (!::ReadFile(stdinHandle, chunk, DWORD(sizeof(chunk)), &count, nullptr)) {
            const DWORD error = ::GetLastError();
            endOfInput = error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF;
            break;
        }
        if (count == 0) {
            endOfInput = true;
            break;
        }
        pending.append(chunk, int(count));
        for (;;) {
            const qsizetype newline = pending.indexOf('\n');
            if (newline < 0)
                break;
            QByteArray bytes = pending.left(newline);
            pending.remove(0, newline + 1);
            if (bytes.endsWith('\r'))
                bytes.chop(1);
            emit lineReceived(QString::fromUtf8(bytes));
        }
        if (pending.size() > MaximumInputBufferSize)
            pending.clear(); // Same discard rule as the Unix path.
    }
    if (endOfInput && !pending.isEmpty())
        emit lineReceived(QString::fromUtf8(pending));
    emit inputClosed();
}
#endif

ServerConsole::ServerConsole(Server *server, QObject *parent)
    : QObject(parent), m_server(server), m_output(stdout)
{
}

ServerConsole::~ServerConsole()
{
#if defined(Q_OS_UNIX)
    if (m_restoreStdinFlags)
        ::fcntl(STDIN_FILENO, F_SETFL, m_originalStdinFlags);
#elif defined(Q_OS_WIN)
    stopInputThread();
#endif
}

void ServerConsole::start()
{
    if (m_started || !m_server)
        return;
    m_started = true;

#if defined(Q_OS_UNIX)
    m_interactive = ::isatty(STDIN_FILENO) == 1;
#else
    // _isatty rather than GetFileType: a NUL device reports FILE_TYPE_CHAR but
    // is not a terminal, and piped stdin must take the non-interactive path.
    m_interactive = ::_isatty(_fileno(stdin)) != 0;
#endif

    const ServerStatusSnapshot snapshot = m_server->statusSnapshot();
    if (m_interactive) {
        writeLine(QStringLiteral("QSanguosha Server %1")
            .arg(QString::fromLatin1(QSanVersion::Number)));
        writeLine(QStringLiteral("Listening on %1:%2")
            .arg(snapshot.bindAddress)
            .arg(snapshot.port));
        if (snapshot.websocketPort != 0)
            writeLine(QStringLiteral("WebSocket listening on %1:%2")
                .arg(snapshot.bindAddress)
                .arg(snapshot.websocketPort));
        writeLine(QStringLiteral("Mode: %1").arg(snapshot.gameMode));
        writeLine();
    }

#if defined(Q_OS_UNIX)
    m_originalStdinFlags = ::fcntl(STDIN_FILENO, F_GETFL, 0);
    if (m_originalStdinFlags >= 0
        && ::fcntl(STDIN_FILENO, F_SETFL, m_originalStdinFlags | O_NONBLOCK) == 0) {
        m_restoreStdinFlags = true;
    }

    m_stdinNotifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
    connect(m_stdinNotifier, &QSocketNotifier::activated, this,
        [this](QSocketDescriptor, QSocketNotifier::Type) { readStandardInput(); });
    m_acceptingInput = true;
    showPrompt();
#elif defined(Q_OS_WIN)
    const HANDLE stdinHandle = ::GetStdHandle(STD_INPUT_HANDLE);
    if (!stdinHandle || stdinHandle == INVALID_HANDLE_VALUE)
        return;

    m_inputThread = new ConsoleInputThread(m_interactive, this);
    connect(m_inputThread, &ConsoleInputThread::lineReceived,
        this, &ServerConsole::handleConsoleLine, Qt::QueuedConnection);
    connect(m_inputThread, &ConsoleInputThread::inputClosed,
        this, &ServerConsole::handleInputClosed, Qt::QueuedConnection);
    m_acceptingInput = true;
    m_inputThread->start();
    showPrompt();
#endif
}

void ServerConsole::writeLog(const QString &message)
{
    if (m_interactive && m_promptVisible)
        m_output << Qt::endl;
    m_promptVisible = false;
    m_output << message << Qt::endl;
    if (m_acceptingInput && !m_handlingCommand)
        showPrompt();
}

void ServerConsole::readStandardInput()
{
#if defined(Q_OS_UNIX)
    char buffer[4096];
    do {
        const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count > 0) {
            m_inputBuffer.append(buffer, count);
            if (m_inputBuffer.size() > MaximumInputBufferSize) {
                writeLine(QStringLiteral("Console input exceeded 64 KiB and was discarded."));
                m_inputBuffer.clear();
            }
            processBufferedInput();
            if (!m_restoreStdinFlags || !m_acceptingInput)
                return;
            continue;
        }
        if (count == 0) {
            if (!m_inputBuffer.isEmpty()) {
                m_inputBuffer.append('\n');
                processBufferedInput();
            }
            if (m_interactive && m_promptVisible)
                m_output << Qt::endl;
            disableInput();
            return;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            writeLine(QStringLiteral("Console input failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            disableInput();
            return;
        }
        if (errno == EINTR)
            continue;
        return;
    } while (m_acceptingInput);
#endif
}

void ServerConsole::processBufferedInput()
{
    while (m_acceptingInput) {
        const qsizetype newline = m_inputBuffer.indexOf('\n');
        if (newline < 0)
            return;

        QByteArray bytes = m_inputBuffer.left(newline);
        m_inputBuffer.remove(0, newline + 1);
        if (bytes.endsWith('\r'))
            bytes.chop(1);

        m_promptVisible = false;
        m_handlingCommand = true;
        executeCommand(QString::fromUtf8(bytes));
        m_handlingCommand = false;
        if (m_acceptingInput)
            showPrompt();
    }
}

#if defined(Q_OS_WIN)
void ServerConsole::stopInputThread()
{
    if (!m_inputThread)
        return;

    if (m_inputThread->isRunning()) {
        m_inputThread->requestInterruption();
#ifdef QSAN_XP_LEGACY
        if (!m_inputThread->wait(1000))
            qFatal("XP console reader failed its bounded shutdown");
#else
        // The worker blocks in a synchronous console read; cancel that pending
        // I/O so it can observe the interruption instead of hanging the join.
        const DWORD threadId = static_cast<DWORD>(m_inputThread->nativeThreadId());
        if (threadId != 0) {
            if (const HANDLE handle = ::OpenThread(THREAD_TERMINATE, FALSE, threadId)) {
                ::CancelSynchronousIo(handle);
                ::CloseHandle(handle);
            }
        }
        if (!m_inputThread->wait(QDeadlineTimer(2000))) {
            // Still blocked: leak the object instead of destroying a running
            // QThread; the OS thread ends with the process.
            m_inputThread->setParent(nullptr);
            return;
        }
#endif
    }
    delete m_inputThread;
    m_inputThread = nullptr;
}

void ServerConsole::handleConsoleLine(const QString &line)
{
    if (!m_acceptingInput)
        return;
    m_promptVisible = false;
    m_handlingCommand = true;
    executeCommand(line);
    m_handlingCommand = false;
    if (m_acceptingInput)
        showPrompt();
}

void ServerConsole::handleInputClosed()
{
    if (m_interactive && m_promptVisible)
        m_output << Qt::endl;
    disableInput();
}
#endif

void ServerConsole::executeCommand(const QString &line)
{
    const QString input = line.trimmed();
    if (input.isEmpty())
        return;

    qsizetype separator = 0;
    while (separator < input.size() && !input.at(separator).isSpace())
        ++separator;
    const QString command = input.left(separator).toLower();
    const QString arguments = input.mid(separator).trimmed();

    if (command == QLatin1String("help")) {
        if (!arguments.isEmpty())
            writeLine(QStringLiteral("Usage: help"));
        else
            printHelp();
    } else if (command == QLatin1String("status")) {
        if (!arguments.isEmpty())
            writeLine(QStringLiteral("Usage: status"));
        else
            printStatus();
    } else if (command == QLatin1String("players")) {
        if (!arguments.isEmpty())
            writeLine(QStringLiteral("Usage: players"));
        else
            printPlayers();
    } else if (command == QLatin1String("rooms")) {
        if (!arguments.isEmpty())
            writeLine(QStringLiteral("Usage: rooms"));
        else
            printRooms();
    } else if (command == QLatin1String("say")) {
        if (arguments.isEmpty()) {
            writeLine(QStringLiteral("Usage: say <message>"));
        } else {
            m_server->broadcastAdminMessage(arguments);
            writeLine(QStringLiteral("Broadcast sent."));
        }
    } else if (command == QLatin1String("kick")) {
        if (arguments.isEmpty()) {
            writeLine(QStringLiteral("Usage: kick <player-id>"));
        } else if (m_server->kickPlayer(arguments)) {
            writeLine(QStringLiteral("Player kicked: %1").arg(arguments));
        } else {
            writeLine(QStringLiteral("Player not found: %1").arg(arguments));
        }
    } else if (command == QLatin1String("shutdown")) {
        if (!arguments.isEmpty()) {
            writeLine(QStringLiteral("Usage: shutdown"));
        } else {
            writeLine(QStringLiteral("Shutdown requested by console."));
            disableInput();
            QCoreApplication::quit();
        }
    } else {
        writeLine(QStringLiteral("Unknown command '%1'. Type 'help'.").arg(command));
    }
}

void ServerConsole::printHelp()
{
    writeLine(QStringLiteral("Available commands:"));
    writeLine(QStringLiteral("  help                 Show this help."));
    writeLine(QStringLiteral("  status               Show server status."));
    writeLine(QStringLiteral("  players              List connected players."));
    writeLine(QStringLiteral("  rooms                List rooms."));
    writeLine(QStringLiteral("  say <message>         Broadcast an administrator message."));
    writeLine(QStringLiteral("  kick <player-id>      Disconnect a player by exact ID."));
    writeLine(QStringLiteral("  shutdown             Shut down the server cleanly."));
}

void ServerConsole::printStatus()
{
    const ServerStatusSnapshot snapshot = m_server->statusSnapshot();
    const int labelWidth = 15;
    auto item = [this, labelWidth](const QString &label, const QString &value) {
        writeLine(label.leftJustified(labelWidth) + value);
    };

    writeLine(QStringLiteral("QSanguosha Server %1")
        .arg(QString::fromLatin1(QSanVersion::Number)));
    item(QStringLiteral("Uptime:"), durationText(snapshot.uptimeMs));
    item(QStringLiteral("Listening:"), QStringLiteral("%1:%2")
        .arg(snapshot.bindAddress).arg(snapshot.port));
    item(QStringLiteral("Game mode:"), snapshot.gameMode);
    item(QStringLiteral("Rooms:"), QString::number(snapshot.roomCount));
    item(QStringLiteral("Games running:"), QString::number(snapshot.gamesRunning));
    item(QStringLiteral("Players:"), QString::number(snapshot.playerCount));
    item(QStringLiteral("Online:"), QString::number(snapshot.onlineCount));
    item(QStringLiteral("Robots:"), QString::number(snapshot.robotCount));
    item(QStringLiteral("AI:"), enabledText(snapshot.aiEnabled));
    item(QStringLiteral("Lua:"), enabledText(snapshot.luaEnabled));
}

void ServerConsole::printPlayers()
{
    const QList<PlayerStatusSnapshot> players = m_server->playerSnapshots();
    if (players.isEmpty()) {
        writeLine(QStringLiteral("No players connected."));
        return;
    }

    QList<int> widths { 2, 4, 4, 5 };
    for (const PlayerStatusSnapshot &player : players) {
        widths[0] = qMax(widths[0], int(player.id.size()));
        widths[1] = qMax(widths[1], int(player.name.size()));
        widths[2] = qMax(widths[2], int(QString::number(player.roomId).size()));
        widths[3] = qMax(widths[3], int(player.state.size()));
    }
    writeLine(tableRow({ QStringLiteral("ID"), QStringLiteral("NAME"),
        QStringLiteral("ROOM"), QStringLiteral("STATE") }, widths));
    for (const PlayerStatusSnapshot &player : players) {
        writeLine(tableRow({ player.id, player.name,
            player.roomId >= 0 ? QString::number(player.roomId) : QStringLiteral("-"),
            player.state }, widths));
    }
}

void ServerConsole::printRooms()
{
    const QList<RoomStatusSnapshot> rooms = m_server->roomSnapshots();
    if (rooms.isEmpty()) {
        writeLine(QStringLiteral("No rooms."));
        return;
    }

    QList<int> widths { 2, 5, 4, 7, 6 };
    for (const RoomStatusSnapshot &room : rooms) {
        const QString occupancy = QStringLiteral("%1/%2")
            .arg(room.playerCount).arg(room.playerCapacity);
        widths[0] = qMax(widths[0], int(QString::number(room.id).size()));
        widths[1] = qMax(widths[1], int(room.state.size()));
        widths[2] = qMax(widths[2], int(room.gameMode.size()));
        widths[3] = qMax(widths[3], int(occupancy.size()));
        widths[4] = qMax(widths[4], int(durationText(room.uptimeMs).size()));
    }
    writeLine(tableRow({ QStringLiteral("ID"), QStringLiteral("STATE"),
        QStringLiteral("MODE"), QStringLiteral("PLAYERS"),
        QStringLiteral("UPTIME") }, widths));
    for (const RoomStatusSnapshot &room : rooms) {
        writeLine(tableRow({ QString::number(room.id), room.state, room.gameMode,
            QStringLiteral("%1/%2").arg(room.playerCount).arg(room.playerCapacity),
            durationText(room.uptimeMs) }, widths));
    }
}

void ServerConsole::writeLine(const QString &line)
{
    m_output << line << Qt::endl;
}

void ServerConsole::showPrompt()
{
    if (!m_interactive || !m_acceptingInput || m_promptVisible)
        return;
    m_output << "server> " << Qt::flush;
    m_promptVisible = true;
}

void ServerConsole::disableInput()
{
    m_acceptingInput = false;
    m_promptVisible = false;
#if defined(Q_OS_UNIX)
    if (m_stdinNotifier)
        m_stdinNotifier->setEnabled(false);
#endif
}
