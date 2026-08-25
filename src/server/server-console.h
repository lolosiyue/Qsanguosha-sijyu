#ifndef QSAN_SERVER_CONSOLE_H
#define QSAN_SERVER_CONSOLE_H

#include <QByteArray>
#include <QObject>
#include <QTextStream>

class QSocketNotifier;
class Server;

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
};

#endif
