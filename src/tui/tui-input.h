#ifndef TUI_INPUT_H
#define TUI_INPUT_H

#include <QByteArray>
#include <QObject>

class TuiInput final : public QObject
{
    Q_OBJECT

public:
    explicit TuiInput(QObject *parent = nullptr);
    ~TuiInput() override;

    bool start(QString *error = nullptr);
    void stop();

signals:
    void lineReady(const QString &line);
    void endOfInput();
    void interruptRequested();
    void inputError(const QString &message);

private:
    void appendBytes(const QByteArray &bytes);
    void emitBufferedLine(const QByteArray &line);
#ifdef Q_OS_WIN
    void readWindowsInput();
    void *m_inputHandle = nullptr;
    unsigned long m_originalConsoleMode = 0;
    QString m_consoleLine;
    bool m_consoleInput = false;
#else
    void readUnixInput(int descriptor);
#endif
    QObject *m_notifier = nullptr;
    QByteArray m_buffer;
    bool m_running = false;
};

#endif
