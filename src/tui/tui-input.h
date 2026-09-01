#ifndef TUI_INPUT_H
#define TUI_INPUT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class TuiInput final : public QObject
{
    Q_OBJECT

public:
    explicit TuiInput(QObject *parent = nullptr);
    ~TuiInput() override;

    bool start(QString *error = nullptr);
    void stop();
    void setCompleter(std::function<QString(const QString &, QStringList *)> completer);

signals:
    void lineReady(const QString &line);
    void endOfInput();
    void interruptRequested();
    void inputError(const QString &message);
    void completionChoices(const QStringList &matches);

private:
    void appendBytes(const QByteArray &bytes);
    void emitBufferedLine(const QByteArray &line);
#ifdef Q_OS_WIN
    void readWindowsInput();
    void completeWindowsLine();
    void rewriteWindowsLine(const QString &next);
    void *m_inputHandle = nullptr;
    unsigned long m_originalConsoleMode = 0;
    QString m_consoleLine;
    bool m_consoleInput = false;
#else
    void readUnixInput(int descriptor);
#endif
    std::function<QString(const QString &, QStringList *)> m_completer;
    QObject *m_notifier = nullptr;
    QByteArray m_buffer;
    bool m_running = false;
};

#endif
