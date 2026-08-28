#include "network-ui-smoke-controller.h"

#include "network-ui-smoke-responder.h"

#include "client.h"
#include "clientstruct.h"
#include "dashboard.h"
#include "engine.h"
#include "local-response-ui-probe.h"
#include "mainwindow.h"
#include "protocol.h"
#include "roomscene.h"
#include "settings.h"
#include "effects/effects-completion.h"
#include "effects/effects-policy.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPixmap>
#include <QSaveFile>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

namespace {

// game over 之後留一格時間畀 RoomScene 收尾(結算畫面、log flush),再退出。
// 太短會喺 UI 未安頓時斬,太長只係拖慢 CI。
const int kSettleMs = 1500;

// 收到 socket disconnected 之後,等幾耐先判定係「中途斷線」而唔係完局收尾。
const int kDisconnectVerdictMs = 2000;

void writeMarker(const QString &line)
{
    const QByteArray utf8 = line.toUtf8();
    fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

QString environmentValue(const char *name)
{
    return qEnvironmentVariableIsSet(name) ? qEnvironmentVariable(name) : QString();
}

} // namespace

NetworkUiSmokeController *NetworkUiSmokeController::s_active = nullptr;

NetworkUiSmokeController::NetworkUiSmokeController(QObject *parent)
    : QObject(parent)
{
}

NetworkUiSmokeController::~NetworkUiSmokeController()
{
    if (s_active == this)
        s_active = nullptr;
}

bool NetworkUiSmokeController::isRequested(const QStringList &arguments)
{
    return NetworkUiSmokeReport::isRequested(arguments);
}

bool NetworkUiSmokeController::begin(const QStringList &arguments, MainWindow *mainWindow,
    int *exitCode)
{
    if (exitCode)
        *exitCode = NetworkUiSmokeReport::Passed;
    if (!isRequested(arguments))
        return true;

    NetworkUiSmokeController *controller = new NetworkUiSmokeController(qApp);
    controller->m_arguments = arguments;
    controller->m_elapsed.start();
    s_active = controller;
    // 任何唔經 complete() 的退出路徑(engine exit(1)、qFatal、未處理例外)都要留低
    // 一行 result marker,CI 先分得出「client crash」同「契約壞咗」。
    std::atexit(&NetworkUiSmokeController::reportUnfinishedAtExit);

    QString error;
    if (!controller->configure(arguments, &error)) {
        controller->complete(false, QStringLiteral("arguments"), error,
            NetworkUiSmokeReport::InvalidArguments);
        if (exitCode)
            *exitCode = controller->m_exitCode;
        return false;
    }

    if (ClientInstance == nullptr) {
        controller->failStage(QLatin1String(NetworkUiSmokeReport::StageConnected),
            QStringLiteral("no Client was created; --network-ui-smoke requires "
                           "-connect:<host>[:<port>]"),
            NetworkUiSmokeReport::ConnectFailed);
        if (exitCode)
            *exitCode = controller->m_exitCode;
        return false;
    }

    controller->attach(mainWindow);
    return true;
}

bool NetworkUiSmokeController::configure(const QStringList &arguments, QString *error)
{
    if (!NetworkUiSmokeReport::parseTimeoutMs(arguments, &m_timeoutMs, error))
        return false;
    if (!NetworkUiSmokeReport::parseStallMs(arguments, &m_stallMs, error))
        return false;
    m_resultPath = NetworkUiSmokeReport::parseResultPath(arguments);
    m_screenshotPath = NetworkUiSmokeReport::parseScreenshotPath(arguments);
    return true;
}

void NetworkUiSmokeController::attach(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;

    connect(ClientInstance, &Client::socket_connected,
        this, &NetworkUiSmokeController::onSocketConnected);
    connect(ClientInstance, &Client::socket_disconnected,
        this, &NetworkUiSmokeController::onSocketDisconnected);
    connect(ClientInstance, &Client::error_message,
        this, &NetworkUiSmokeController::onErrorMessage);
    connect(ClientInstance, &Client::server_connected,
        this, &NetworkUiSmokeController::onServerConnected);
    connect(ClientInstance, &Client::server_reply,
        this, &NetworkUiSmokeController::onServerReply);
    connect(ClientInstance, &Client::game_started,
        this, &NetworkUiSmokeController::onGameStarted);
    connect(ClientInstance, &Client::game_over,
        this, &NetworkUiSmokeController::onGameOver);
    // 平局(standoff)一樣係「局行完咗」,唔可以當 game over 未到。
    connect(ClientInstance, &Client::standoff,
        this, &NetworkUiSmokeController::onGameOver);

    if (mainWindow) {
        connect(mainWindow, &MainWindow::roomSceneCreated,
            this, &NetworkUiSmokeController::onRoomSceneCreated);
    }

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(m_timeoutMs);
    connect(m_timeoutTimer, &QTimer::timeout, this, &NetworkUiSmokeController::onTimeout);
    m_timeoutTimer->start();
}

void NetworkUiSmokeController::onSocketConnected()
{
    if (m_connected)
        return;
    m_connected = true;
    emitStage(QLatin1String(NetworkUiSmokeReport::StageConnected), true, QJsonObject{
        {QStringLiteral("host_address"), Config.HostAddress}
    });
}

void NetworkUiSmokeController::onSocketDisconnected()
{
    // Client::gameOver() 自己第一件事就係 disconnectFromHost(),之後先 emit
    // game_over();即係正常完局都會先見到呢個訊號。所以唔可以即刻判失敗,要留一格
    // 畀 game_over 到達;真正的中途斷線唔會有後續 game_over。
    if (m_finished || m_gameOver)
        return;
    QTimer::singleShot(kDisconnectVerdictMs, this,
        &NetworkUiSmokeController::onDisconnectVerdict);
}

void NetworkUiSmokeController::onDisconnectVerdict()
{
    if (m_finished || m_gameOver)
        return;
    failStage(m_gameStarted ? QLatin1String(NetworkUiSmokeReport::StageGameOver)
                            : QLatin1String(NetworkUiSmokeReport::StageSignedUp),
        QStringLiteral("the server closed the connection before the game was over"),
        NetworkUiSmokeReport::Disconnected);
}

void NetworkUiSmokeController::onErrorMessage(const QString &message)
{
    m_errors.append(message);
    if (m_finished || m_connected)
        return;
    failStage(QLatin1String(NetworkUiSmokeReport::StageConnected), message,
        NetworkUiSmokeReport::ConnectFailed);
}

void NetworkUiSmokeController::onServerConnected()
{
    if (m_signedUp)
        return;
    m_signedUp = true;
    emitStage(QLatin1String(NetworkUiSmokeReport::StageSignedUp), true, QJsonObject{
        {QStringLiteral("game_mode"), ServerInfo.GameMode},
        {QStringLiteral("player_count"), Sanguosha ? Sanguosha->getPlayerCount(ServerInfo.GameMode) : 0},
        {QStringLiteral("operation_timeout"), ServerInfo.OperationTimeout},
        {QStringLiteral("second_general"), ServerInfo.Enable2ndGeneral}
    });
}

void NetworkUiSmokeController::onRoomSceneCreated(RoomScene *scene)
{
    if (m_roomSceneReady || scene == nullptr)
        return;
    m_roomScene = scene;
    m_roomSceneReady = true;
    emitStage(QLatin1String(NetworkUiSmokeReport::StageRoomScene), true, QJsonObject{
        {QStringLiteral("photo_count"), scene->photos.size()},
        {QStringLiteral("scene_width"), scene->width()},
        {QStringLiteral("scene_height"), scene->height()}
    });

    // Dashboard 係 RoomScene 的一部分,但係獨立驗一次:M2 要分得出「RoomScene 起到
    // 但 Dashboard 構造失敗」呢種情況。
    Dashboard *dashboard = scene->dashboard;
    if (dashboard == nullptr || dashboard->scene() != scene) {
        failStage(QLatin1String(NetworkUiSmokeReport::StageDashboard),
            QStringLiteral("RoomScene was created without a Dashboard in the scene"),
            NetworkUiSmokeReport::DashboardFailed);
        return;
    }
    emitStage(QLatin1String(NetworkUiSmokeReport::StageDashboard), true, QJsonObject{
        {QStringLiteral("width"), dashboard->boundingRect().width()},
        {QStringLiteral("height"), dashboard->boundingRect().height()}
    });

    // Responder 一定要喺 RoomScene 之後建立:佢接的 status_changed 要行喺
    // RoomScene::updateStatus 之後,先至見到 RoomScene 佈置好的按鈕狀態。
    m_responder = new NetworkUiSmokeResponder(scene, m_stallMs, this);
}

void NetworkUiSmokeController::onServerReply(int commandType)
{
    if (m_generalSelected || commandType != QSanProtocol::S_COMMAND_CHOOSE_GENERAL)
        return;
    m_generalSelected = true;
    emitStage(QLatin1String(NetworkUiSmokeReport::StageGeneralSelected), true, QJsonObject{
        {QStringLiteral("general"), Self ? Self->getGeneralName() : QString()}
    });
}

void NetworkUiSmokeController::onGameStarted()
{
    if (m_gameStarted)
        return;
    m_gameStarted = true;
    if (!m_generalSelected) {
        // 開局之前一定會問過選將;冇 reply 記錄即係 UI 冇覆過。
        failStage(QLatin1String(NetworkUiSmokeReport::StageGeneralSelected),
            QStringLiteral("the game started without the client replying to a "
                           "general-selection request"),
            NetworkUiSmokeReport::GeneralSelectionFailed);
        return;
    }
    QJsonObject details;
    details.insert(QStringLiteral("game_mode"), ServerInfo.GameMode);
    if (Self) {
        details.insert(QStringLiteral("self"), Self->objectName());
        details.insert(QStringLiteral("general"), Self->getGeneralName());
        details.insert(QStringLiteral("role"), Self->getRole());
    }
    if (ClientInstance)
        details.insert(QStringLiteral("players"), ClientInstance->getPlayers().size());
    emitStage(QLatin1String(NetworkUiSmokeReport::StageGameStarted), true, details);
}

void NetworkUiSmokeController::onGameOver()
{
    if (m_gameOver || m_finished)
        return;
    m_gameOver = true;

    QJsonObject details;
    if (Self)
        details.insert(QStringLiteral("self_won"), Self->property("win").toBool());
    if (m_responder)
        details.insert(QStringLiteral("responder"), m_responder->summary());
    emitStage(QLatin1String(NetworkUiSmokeReport::StageGameOver), true, details);

    if (!m_gameStarted) {
        failStage(QLatin1String(NetworkUiSmokeReport::StageGameStarted),
            QStringLiteral("the game ended without ever starting"),
            NetworkUiSmokeReport::GameStartFailed);
        return;
    }

    // 正常離開:留少少時間畀結算 UI 安頓,然後主動 quit。client clean exit 本身
    // 就係 M2 要驗的一步,所以唔靠 runner 去 kill。
    QTimer::singleShot(kSettleMs, this, &NetworkUiSmokeController::onSettled);
}

void NetworkUiSmokeController::onSettled()
{
    if (m_finished)
        return;
    emitStage(QLatin1String(NetworkUiSmokeReport::StageShutdown), true);
    complete(true, QLatin1String(NetworkUiSmokeReport::StageShutdown), QString(),
        NetworkUiSmokeReport::Passed);
}

void NetworkUiSmokeController::onTimeout()
{
    if (m_finished)
        return;
    // 最遠去到邊一步就 blame 邊一步的下一步,令 artifact 一眼睇得出卡喺邊。
    QString stage = QLatin1String(NetworkUiSmokeReport::StageConnected);
    if (m_gameStarted)
        stage = QLatin1String(NetworkUiSmokeReport::StageGameOver);
    else if (m_generalSelected)
        stage = QLatin1String(NetworkUiSmokeReport::StageGameStarted);
    else if (m_roomSceneReady)
        stage = QLatin1String(NetworkUiSmokeReport::StageGeneralSelected);
    else if (m_signedUp)
        stage = QLatin1String(NetworkUiSmokeReport::StageRoomScene);
    else if (m_connected)
        stage = QLatin1String(NetworkUiSmokeReport::StageSignedUp);

    emitStage(stage, false, QJsonObject{{QStringLiteral("error"),
        QStringLiteral("timed out after %1ms").arg(m_timeoutMs)}});
    complete(false, stage,
        QStringLiteral("the network UI smoke timed out after %1ms").arg(m_timeoutMs),
        NetworkUiSmokeReport::Timeout);
}

void NetworkUiSmokeController::failStage(const QString &stage, const QString &error,
    NetworkUiSmokeReport::ExitCode exitCode)
{
    if (m_finished)
        return;
    emitStage(stage, false, QJsonObject{{QStringLiteral("error"), error}});
    complete(false, stage, error, exitCode);
}

void NetworkUiSmokeController::emitStage(const QString &stage, bool ok,
    const QJsonObject &details)
{
    QJsonObject payload = details;
    payload.insert(QStringLiteral("elapsed_ms"), static_cast<int>(m_elapsed.elapsed()));
    m_stages.append(NetworkUiSmokeReport::stagePayload(stage, ok, payload));
    writeMarker(NetworkUiSmokeReport::stageLine(stage, ok, payload));
}

void NetworkUiSmokeController::captureFailureEvidence()
{
    // 失敗時的 UI state 同截圖只作診斷,唔係 pixel gate。
    //
    // RoomState 喺 Client::gameOver() 已經 unregisterRoom(),probe 會經
    // Engine::getCurrentCardUsePattern() 摸落去,所以冇 RoomState 就唔影快照 —
    // 診斷資料唔值得為咗佢喺失敗路徑再炸多一次。
    if (!m_roomScene.isNull() && ClientInstance && Sanguosha
        && Sanguosha->currentRoomState() != nullptr) {
        LocalResponseUiProbe probe(ClientInstance, m_roomScene, QMap<QString, int>());
        m_lastUiState = probe.snapshot();
    }
    if (m_screenshotPath.isEmpty() || m_mainWindow.isNull())
        return;
    const QFileInfo info(m_screenshotPath);
    QDir().mkpath(info.absolutePath());
    const QPixmap shot = m_mainWindow->grab();
    if (!shot.isNull())
        shot.save(m_screenshotPath, "PNG");
}

void NetworkUiSmokeController::complete(bool ok, const QString &stage, const QString &error,
    NetworkUiSmokeReport::ExitCode exitCode)
{
    if (m_finished)
        return;
    m_finished = true;
    m_exitCode = exitCode;
    if (m_timeoutTimer)
        m_timeoutTimer->stop();
    if (!ok)
        captureFailureEvidence();

    QJsonObject details = environmentDetails();
    details.insert(QStringLiteral("connected"), m_connected);
    details.insert(QStringLiteral("signed_up"), m_signedUp);
    details.insert(QStringLiteral("room_scene"), m_roomSceneReady);
    details.insert(QStringLiteral("general_selected"), m_generalSelected);
    details.insert(QStringLiteral("game_started"), m_gameStarted);
    details.insert(QStringLiteral("game_over"), m_gameOver);
    details.insert(QStringLiteral("elapsed_ms"), static_cast<int>(m_elapsed.elapsed()));
    if (m_responder) {
        details.insert(QStringLiteral("trustee_engaged"), m_responder->trusteeEngaged());
        details.insert(QStringLiteral("interactions"),
            QJsonArray::fromStringList(m_responder->coveredInteractions()));
        details.insert(QStringLiteral("ui_actions"),
            QJsonArray::fromStringList(m_responder->coveredActions()));
    }

    m_result = NetworkUiSmokeReport::resultPayload(ok, stage, error, exitCode, details);
    writeMarker(NetworkUiSmokeReport::resultLine(ok, stage, error, exitCode, details));
    writeResultFile();

    if (qApp)
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void NetworkUiSmokeController::writeResultFile()
{
    if (m_resultPath.isEmpty())
        return;
    QJsonObject report;
    report.insert(QStringLiteral("schema_version"), NetworkUiSmokeReport::schemaVersion());
    report.insert(QStringLiteral("ok"), m_exitCode == NetworkUiSmokeReport::Passed);
    report.insert(QStringLiteral("exit_code"), m_exitCode);
    report.insert(QStringLiteral("result"), m_result);
    report.insert(QStringLiteral("stages"), m_stages);
    report.insert(QStringLiteral("environment"), environmentDetails());
    report.insert(QStringLiteral("errors"), m_errors);
    if (m_responder)
        report.insert(QStringLiteral("responder"), m_responder->summary());
    if (!m_lastUiState.isEmpty())
        report.insert(QStringLiteral("last_ui_state"), m_lastUiState);

    const QFileInfo info(m_resultPath);
    QDir().mkpath(info.absolutePath());
    QSaveFile file(m_resultPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    file.commit();
}

QJsonObject NetworkUiSmokeController::environmentDetails() const
{
    QJsonObject details;
    details.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));
    details.insert(QStringLiteral("qt_build_version"), QStringLiteral(QT_VERSION_STR));
    details.insert(QStringLiteral("platform"), qApp ? qApp->platformName() : QString());
    details.insert(QStringLiteral("qt_qpa_platform"), environmentValue("QT_QPA_PLATFORM"));
    details.insert(QStringLiteral("qt_quick_backend"), environmentValue("QT_QUICK_BACKEND"));
    details.insert(QStringLiteral("display"), environmentValue("DISPLAY"));
    details.insert(QStringLiteral("working_directory"), QDir::currentPath());
    details.insert(QStringLiteral("host_address"), Config.HostAddress);
    details.insert(QStringLiteral("game_mode"), ServerInfo.GameMode);
    details.insert(QStringLiteral("timeout_ms"), m_timeoutMs);
    details.insert(QStringLiteral("stall_ms"), m_stallMs);
    // M2B-B：完整一局要證明「三個 profile 有完全相同嘅遊戲規則同網絡回覆」，
    // 所以每次網絡 smoke 都記低佢實際行緊邊個 profile、建立咗幾多高成本
    // 物件、同埋派咗幾多次 completion。
    details.insert(QStringLiteral("effects"), G_EFFECTS.describe());
    details.insert(QStringLiteral("effects_counters"), G_EFFECTS.countersJson());
    QJsonObject completion;
    completion.insert(QStringLiteral("delivered"),
        static_cast<double>(EffectsCompletion::deliveredCount()));
    completion.insert(QStringLiteral("cancelled"),
        static_cast<double>(EffectsCompletion::cancelledCount()));
    details.insert(QStringLiteral("effects_completion"), completion);
    return details;
}

int NetworkUiSmokeController::finish(int applicationExitCode)
{
    NetworkUiSmokeController *controller = s_active;
    if (!controller)
        return applicationExitCode;
    if (!controller->m_finished) {
        // event loop 行完但 smoke 未有結論:例如有人關咗窗,或者 MainWindow
        // closeEvent 直接 quit。呢個唔算 PASS。
        controller->complete(false, QLatin1String(NetworkUiSmokeReport::StageShutdown),
            QStringLiteral("the Qt event loop exited before the network UI smoke "
                           "reached a conclusion"),
            NetworkUiSmokeReport::InternalError);
    }
    if (controller->m_exitCode == NetworkUiSmokeReport::Passed && applicationExitCode != 0)
        return applicationExitCode;
    return controller->m_exitCode;
}

void NetworkUiSmokeController::reportUnfinishedAtExit()
{
    NetworkUiSmokeController *controller = s_active;
    if (!controller || controller->m_finished)
        return;
    controller->m_finished = true;
    writeMarker(NetworkUiSmokeReport::resultLine(false,
        QLatin1String(NetworkUiSmokeReport::StageShutdown),
        QStringLiteral("the process exited without completing the network UI smoke"),
        NetworkUiSmokeReport::InternalError));
}
