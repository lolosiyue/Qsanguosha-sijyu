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
    // Off by default, so the classic client keeps assembling lines itself
    // and its behaviour is untouched. When on, TuiInput stops decoding
    // bytes itself and only forwards them via rawBytes(); see that signal.
    void setRawMode(bool enabled);

signals:
    void lineReady(const QString &line);
    void endOfInput();
    void interruptRequested();
    void inputError(const QString &message);
    void completionChoices(const QStringList &matches);
    // Raw mode only: terminal bytes on Unix, or UTF-8/CSI encoded from native
    // key records on Windows, including split sequences and code points --
    // TuiKeyDecoder owns reassembling those, not TuiInput. TuiInput must
    // never also emit lineReady while raw mode is on: grammar, ClientCore
    // and the reply encoder all assume a single line-assembly path, and a
    // second one racing the line editor's own lineReady (fired later by the
    // presenter once a line is actually finished) would let a half-typed
    // line reach the wire.
    void rawBytes(const QByteArray &bytes);

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
    QChar m_highSurrogate;
    unsigned short m_surrogateRepeat = 0;
#else
    void readUnixInput(int descriptor);
#endif
    std::function<QString(const QString &, QStringList *)> m_completer;
    QObject *m_notifier = nullptr;
    QByteArray m_buffer;
    bool m_running = false;
    bool m_rawMode = false;
};

#endif
