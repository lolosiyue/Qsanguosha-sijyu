#ifndef QSAN_SERVER_CONSOLE_H
#define QSAN_SERVER_CONSOLE_H

#include <QByteArray>
#include <QObject>
#include <QTextStream>
#include <QThread>

class QSocketNotifier;
class Server;

#if defined(Q_OS_WIN)
#include <atomic>

// Reads raw console/pipe input on a worker thread because stdin has no
// QSocketNotifier support on Windows; every parsed line is queued back to
// ServerConsole instead of being executed here.
class ConsoleInputThread final : public QThread
{
    Q_OBJECT

public:
    explicit ConsoleInputThread(bool interactive, QObject *parent = nullptr);

    void run() override;
    unsigned long nativeThreadId() const { return m_threadId.load(); }

signals:
    void lineReceived(const QString &line);
    void inputClosed();

private:
    void readFromConsole();
    void readFromPipe();

    bool m_interactive = false;
    std::atomic<unsigned long> m_threadId{0};
};
#endif

class ServerConsole final : public QObject
{
public:
    explicit ServerConsole(Server *server, QObject *parent = nullptr);
    ~ServerConsole() override;

    void start();
    void writeLog(const QString &message);
    bool isInteractive() const { return m_interactive; }

private:
    void readStandardInput();
    void processBufferedInput();
    void executeCommand(const QString &line);
    void printHelp();
    void printStatus();
    void printPlayers();
    void printRooms();
    void writeLine(const QString &line = QString());
    void showPrompt();
    void disableInput();

#if defined(Q_OS_WIN)
    void stopInputThread();
    void handleConsoleLine(const QString &line);
    void handleInputClosed();
#endif

    Server *m_server;
    QTextStream m_output;
    QByteArray m_inputBuffer;
    bool m_started = false;
    bool m_acceptingInput = false;
    bool m_interactive = false;
    bool m_promptVisible = false;
    bool m_handlingCommand = false;

#if defined(Q_OS_UNIX)
    QSocketNotifier *m_stdinNotifier = nullptr;
    int m_originalStdinFlags = -1;
    bool m_restoreStdinFlags = false;
#endif
#if defined(Q_OS_WIN)
    ConsoleInputThread *m_inputThread = nullptr;
#endif
};

#endif
