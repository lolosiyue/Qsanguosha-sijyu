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

// Linux GUI M2 的 network UI smoke。
//
// 同 M1 的 UiStartupSmokeController 唔同,呢個 controller 唔會自己起 MainWindow:
// M2 要驗證的係產品正常的網絡啟動路徑
//
//     QApplication → engine → MainWindow → -connect: → Client(真 TCP)
//         → signup/setup → enterRoom() → RoomScene/Dashboard → 選將 → 開局
//         → askFor 互動 → game over → 正常退出
//
// 所以 main.cpp 照常做晒佢平時做的嘢,controller 只係喺後面接駁產品已有的訊號
// (MainWindow::roomSceneCreated、Client::socket_connected/server_connected/
// game_started/game_over),把每一步記成 NETWORK_UI_STAGE marker,最後輸出一行
// NETWORK_UI_RESULT 並且主動 quit,令「client clean exit」本身都係被驗證的一步。
//
// 真正代替真人操作的部分喺 NetworkUiSmokeResponder。
class NetworkUiSmokeController final : public QObject
{
    Q_OBJECT

public:
    explicit NetworkUiSmokeController(QObject *parent = nullptr);
    ~NetworkUiSmokeController() override;

    static bool isRequested(const QStringList &arguments);

    // 喺 MainWindow 建立、-connect: 已經觸發 startConnection() 之後呼叫。
    // 回傳 false 代表參數不合法或者根本冇 client(已輸出 failure result),
    // caller 應該直接用 *exitCode 退出。
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
