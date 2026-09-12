#ifndef NETWORK_UI_SMOKE_CONTROLLER_H
#define NETWORK_UI_SMOKE_CONTROLLER_H

#include "network-ui-smoke-report.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>

class MainWindow;
class NetworkUiSmokeResponder;
class RoomScene;
class QTimer;

// The network UI smoke for Linux GUI M2.
//
// Unlike the M1 UiStartupSmokeController, this controller does not start
// MainWindow itself: what M2 verifies is the product's normal network startup
// path
//
//     QApplication → engine → MainWindow → -connect: → Client(real TCP)
//         → signup/setup → enterRoom() → RoomScene/Dashboard → general
//         selection → game start → askFor interactions → game over → normal exit
//
// So main.cpp does everything it normally does; the controller only hooks into
// the product's existing signals afterwards (MainWindow::roomSceneCreated,
// Client::socket_connected/server_connected/game_started/game_over), records
// each step as a NETWORK_UI_STAGE marker, and finally emits one NETWORK_UI_RESULT
// line and quits actively, making "client clean exit" itself a verified step.
//
// The part that actually stands in for a human lives in NetworkUiSmokeResponder.
class NetworkUiSmokeController final : public QObject
{
    Q_OBJECT

public:
    explicit NetworkUiSmokeController(QObject *parent = nullptr);
    ~NetworkUiSmokeController() override;

    static bool isRequested(const QStringList &arguments);

    // Call after MainWindow was created and -connect: has already triggered
    // startConnection().
    // Returning false means the arguments are invalid or there is no client at
    // all (a failure result has been output); the caller should exit directly
    // with *exitCode.
    static bool begin(const QStringList &arguments, MainWindow *mainWindow, int *exitCode);

    // qApp->exec() 之後呼叫,回傳 smoke 的 process exit code。
    static int finish(int applicationExitCode);

    // atexit 兜底:任何未經 finish 的退出路徑都補一行 failure result。
    static void reportUnfinishedAtExit();

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onDisconnectVerdict();
    void onErrorMessage(const QString &message);
    void onServerConnected();
    void onRoomSceneCreated(RoomScene *scene);
    void onServerReply(int commandType);
    void onGameStarted();
    void onGameOver();
    void onSettled();
    void onTimeout();

private:
    bool configure(const QStringList &arguments, QString *error);
    void attach(MainWindow *mainWindow);
    void emitStage(const QString &stage, bool ok, const QJsonObject &details = QJsonObject());
    void failStage(const QString &stage, const QString &error,
        NetworkUiSmokeReport::ExitCode exitCode);
    void complete(bool ok, const QString &stage, const QString &error,
        NetworkUiSmokeReport::ExitCode exitCode);
    void captureFailureEvidence();
    void writeResultFile();
    QJsonObject environmentDetails() const;

    static NetworkUiSmokeController *s_active;

    QStringList m_arguments;
    int m_timeoutMs = NetworkUiSmokeReport::defaultTimeoutMs();
    int m_stallMs = NetworkUiSmokeReport::defaultStallMs();
    QString m_resultPath;
    QString m_screenshotPath;

    QPointer<MainWindow> m_mainWindow;
    QPointer<RoomScene> m_roomScene;
    NetworkUiSmokeResponder *m_responder = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QElapsedTimer m_elapsed;

    bool m_connected = false;
    bool m_signedUp = false;
    bool m_roomSceneReady = false;
    bool m_generalSelected = false;
    bool m_gameStarted = false;
    bool m_gameOver = false;
    bool m_finished = false;
    int m_exitCode = NetworkUiSmokeReport::Passed;

    QJsonArray m_stages;
    QJsonArray m_errors;
    QJsonObject m_result;
    QJsonObject m_lastUiState;
};

#endif
