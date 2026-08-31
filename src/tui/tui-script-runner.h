#ifndef TUI_SCRIPT_RUNNER_H
#define TUI_SCRIPT_RUNNER_H

#include <QObject>
#include <QVariant>
#include <QStringList>
#include <QTimer>

#include <functional>

class ClientCore;

class TuiScriptRunner final : public QObject
{
    Q_OBJECT

public:
    using LineSink = std::function<void(const QString &)>;
    // Renders a stored presentation event the way the player sees it. Injected
    // so the runner itself stays free of engine and translation dependencies.
    using EventFormatter
        = std::function<QString(int command, const QString &fallbackText,
                                const QVariant &payload)>;

    explicit TuiScriptRunner(ClientCore *core, LineSink sink,
                             QObject *parent = nullptr);
    bool load(const QString &path, QString *error = nullptr);
    void start();
    void notifyStateChanged();
    void setEventFormatter(EventFormatter formatter);

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
    EventFormatter m_eventFormatter;
    QStringList m_lines;
    int m_index = 0;
    QStringList m_waitCondition;
    QTimer m_waitTimer;
};

#endif
