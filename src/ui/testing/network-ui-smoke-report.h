#ifndef NETWORK_UI_SMOKE_REPORT_H
#define NETWORK_UI_SMOKE_REPORT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// The network UI smoke contract for Linux GUI M2.
//
// Like the M1 UiStartupSmokeReport, this header depends on Qt Core only
// (protocol.h contributes enums only), so the marker schema / exit code /
// command mapping can be verified directly by CTest under a server-only
// configure, without launching QApplication or a real network game.
//
// The part that actually drives the GUI lives in NetworkUiSmokeController / NetworkUiSmokeResponder.
class NetworkUiSmokeReport
{
public:
    // stdout marker 前綴。CI 靠呢兩個 token 解析結果，改動即係改契約。
    static const char *const StageMarker;  // "NETWORK_UI_STAGE"
    static const char *const ResultMarker; // "NETWORK_UI_RESULT"

    // marker payload 的 schema version；欄位語意有 breaking change 先加。
    static int schemaVersion();

    // Failure classification must be fine-grained enough that the CI artifact
    // shows at a glance which layer blew up.
    // server not started / server died midway → the client never gets connected
    // or disconnects midway; both are separated here by stage + reason. A
    // client crash / shutdown hang leaves no result marker and is judged by the
    // runner via the exit code.
    enum ExitCode {
        Passed = 0,
        InvalidArguments = 1,
        ConnectFailed = 2,          // TCP connect failed (server not started / wrong port)
        SignupFailed = 3,           // signup／setup 未完成
        RoomSceneFailed = 4,        // RoomScene 未建立
        DashboardFailed = 5,        // Dashboard 未建立
        GeneralSelectionFailed = 6, // 選將請求未回覆
        GameStartFailed = 7,        // 未開局
        InteractionFailed = 8,      // askFor 請求無法經 UI 回覆
        GameOverNotReached = 9,     // game started but game over never arrived
        Disconnected = 10,          // 局中被 server 斷線
        Timeout = 11,               // app 內部總 timeout
        InternalError = 12
    };

    // Stage names, in the order they actually occur.
    //
    // The order differs slightly from the task spec: RoomScene is created by
    // MainWindow::enterRoom immediately on Client::server_connected (setup
    // received), earlier than the general-selection request, so room_scene and
    // dashboard come before general_selected. This is the product's real order,
    // not something rearranged to suit the test.
    static QStringList stageOrder();
    static bool isKnownStage(const QString &stage);

    static const char *const StageConnected;        // "connected"
    static const char *const StageSignedUp;         // "signed_up"
    static const char *const StageRoomScene;        // "room_scene"
    static const char *const StageDashboard;        // "dashboard"
    static const char *const StageGeneralSelected;  // "general_selected"
    static const char *const StageGameStarted;      // "game_started"
    static const char *const StageGameOver;         // "game_over"
    static const char *const StageShutdown;         // "shutdown"

    static const char *const FlagNetworkUiSmoke;    // "--network-ui-smoke"
    static const char *const FlagResultPath;        // "--network-ui-smoke-result"
    static const char *const FlagTimeoutMs;         // "--network-ui-smoke-timeout-ms"
    static const char *const FlagStallMs;           // "--network-ui-smoke-stall-ms"
    static const char *const FlagScreenshotPath;    // "--network-ui-smoke-screenshot"

    static int defaultTimeoutMs();
    static int minimumTimeoutMs();
    static int maximumTimeoutMs();
    static int defaultStallMs();
    static int minimumStallMs();
    static int maximumStallMs();

    // 只認完全相符的 flag 或者 "--flag=value" 形式，避免 "--network-ui-smoke-xxx"
    // 之類的前綴誤判成 "--network-ui-smoke"。
    static bool isRequested(const QStringList &arguments);
    static bool parseTimeoutMs(const QStringList &arguments, int *timeoutMs, QString *error);
    static bool parseStallMs(const QStringList &arguments, int *stallMs, QString *error);
    static QString parseResultPath(const QStringList &arguments);
    static QString parseScreenshotPath(const QStringList &arguments);

    // Failure reason. timeout / disconnect share the stage name with "stage itself failed"; this field tells them apart.
    static const char *const ReasonOk;                  // "ok"
    static const char *const ReasonStageFailed;         // "stage_failed"
    static const char *const ReasonTimeout;             // "timeout"
    static const char *const ReasonDisconnected;        // "disconnected"
    static const char *const ReasonInteractionStalled;  // "interaction_stalled"

// Protocol V2 server request → contract interaction name. The runner and CI
    // use these names as the askFor coverage gate, so the names must not be
    // renamed casually.
    //
    // Commands not in this table return an empty string: they are
    // notifications/replies, not interactions.
    static QString interactionName(int commandType);
    static QStringList knownInteractionNames();

    // Names of UI actions the responder performed through the real
    // RoomScene/Dashboard (not server commands but "what the player did"),
    // recorded separately from interactions.
    static const char *const ActionPlayCard;    // "play_card"
    static const char *const ActionSelectTarget;// "select_target"
    static const char *const ActionFinishPhase; // "finish_phase"
    static const char *const ActionChooseOption;// "choose_option"
    static const char *const ActionChooseCard;  // "choose_card"
    static const char *const ActionChoosePlayer;// "choose_player"
    static const char *const ActionInvokeSkill; // "invoke_skill"
    static const char *const ActionDecline;     // "decline"
    static const char *const ActionTrusteeFallback; // "trustee_fallback"
    static QStringList knownActionNames();

    static QString stageLine(const QString &stage, bool ok,
        const QJsonObject &details = QJsonObject());
    static QString resultLine(bool ok, const QString &stage, const QString &error,
        ExitCode exitCode, const QJsonObject &details = QJsonObject());

    static QJsonObject stagePayload(const QString &stage, bool ok,
        const QJsonObject &details = QJsonObject());
    static QJsonObject resultPayload(bool ok, const QString &stage, const QString &error,
        ExitCode exitCode, const QJsonObject &details = QJsonObject());

    // 失敗 stage → exit code 的固定對照（timeout／斷線除外，由 caller 直接傳）。
    static ExitCode exitCodeForFailedStage(const QString &stage);
};

#endif
