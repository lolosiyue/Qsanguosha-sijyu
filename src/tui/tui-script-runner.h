#ifndef TUI_SCRIPT_RUNNER_H
#define TUI_SCRIPT_RUNNER_H

#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

class ClientCore;

class TuiScriptRunner final : public QObject
{
    Q_OBJECT

public:
    using LineSink = std::function<void(const QString &)>;

    explicit TuiScriptRunner(ClientCore *core, LineSink sink,
                             QObject *parent = nullptr);
    bool load(const QString &path, QString *error = nullptr);
    void start();
    void notifyStateChanged();

signals:
    void finished();
    void scriptError(const QString &message);

private:
    bool conditionMatches(const QStringList &tokens) const;
    bool stateValueMatches(const QStringList &tokens) const;
    bool logContains(const QStringList &tokens) const;
    bool assertCondition(const QStringList &tokens, QString *error) const;
    void advance();
    void fail(const QString &message);

    ClientCore *m_core = nullptr;
    LineSink m_sink;
    QStringList m_lines;
    int m_index = 0;
    QStringList m_waitCondition;
    QTimer m_waitTimer;
};

#endif
