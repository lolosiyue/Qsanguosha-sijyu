#ifndef LOCAL_RESPONSE_UI_CONTROLLER_H
#define LOCAL_RESPONSE_UI_CONTROLLER_H

#include "local-response-ui-case.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>

#include <functional>

class Client;
class LocalResponseUiProbe;
class QMainWindow;
class RoomScene;
class TestClientSocket;

class LocalResponseUiController final : public QObject
{
    Q_OBJECT

public:
    enum ExitCode
    {
        Passed = 0,
        AssertionFailed = 1,
        InvalidCase = 2,
        SetupFailed = 3,
        PresentationTimeout = 4,
        ReplyTimeout = 5,
        InternalError = 6
    };

    explicit LocalResponseUiController(QObject *parent = nullptr);
    ~LocalResponseUiController() override;

    static int run(const QStringList &arguments);

private slots:
    void execute();

private:
    bool configure(const QStringList &arguments, QString *error);
    bool bootstrap(QString *error);
    bool resolveCards(QString *error);
    bool runActions(QString *error);
    bool validateReply(QString *error);
    bool validateSnapshot(const QJsonObject &expected, const QJsonObject &actual,
        const QString &path);
    bool validateJsonValue(const QJsonValue &expected, const QJsonValue &actual,
        const QString &path);
    bool validatePrompt(const QJsonObject &expected, const QJsonObject &snapshot,
        const QString &path);
    bool validateNamedStates(const QJsonObject &expected, const QJsonArray &actual,
        const QString &nameKey, const QString &path);

    void injectNotification(QSanProtocol::CommandType command, const QVariant &body);
    void recordAssertion(const QString &path, const QJsonValue &expected,
        const QJsonValue &actual, bool passed);
    void flushEvents();
    bool waitForCondition(const std::function<bool()> &condition, int timeoutMs);
    QString captureScreenshot(const QString &name);
    void finish(ExitCode code, const QString &stage = QString(),
        const QString &message = QString());
    bool writeReport(QString *error);

    LocalResponseUiCase m_case;
    QString m_casePath;
    QString m_reportPath;
    QString m_screenshotDir;
    int m_timeoutMs;
    bool m_showUi;
    bool m_screenshotOnFailure;

    Client *m_client;
    TestClientSocket *m_socket;
    QMainWindow *m_mainWindow;
    RoomScene *m_scene;
    LocalResponseUiProbe *m_probe;

    QMap<QString, int> m_aliasToId;
    QJsonObject m_report;
    QJsonObject m_snapshots;
    QJsonArray m_actionResults;
    QJsonArray m_assertions;
    QJsonArray m_capturedPackets;
    QJsonArray m_qtMessages;
};

#endif
