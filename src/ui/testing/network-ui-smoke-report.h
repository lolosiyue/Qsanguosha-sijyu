#ifndef NETWORK_UI_SMOKE_REPORT_H
#define NETWORK_UI_SMOKE_REPORT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Linux GUI M2 的 network UI smoke 契約。
//
// 同 M1 的 UiStartupSmokeReport 一樣，呢個 header 只依賴 Qt Core（protocol.h 只
// 用到 enum），令 marker schema／exit code／command 對照可以喺 server-only
// configure 下用 CTest 直接驗，唔使開 QApplication，亦唔使起真正的網絡局。
//
// 真正驅動 GUI 的部分喺 NetworkUiSmokeController／NetworkUiSmokeResponder。
class NetworkUiSmokeReport
{
public:
    // stdout marker 前綴。CI 靠呢兩個 token 解析結果，改動即係改契約。
    static const char *const StageMarker;  // "NETWORK_UI_STAGE"
    static const char *const ResultMarker; // "NETWORK_UI_RESULT"

    // marker payload 的 schema version；欄位語意有 breaking change 先加。
    static int schemaVersion();

    // 失敗分類必須夠細，令 CI artifact 一眼睇得出係邊一層爆。
    // server 未啟動／server 中途死 → client 收唔到 connected 或者中途 disconnect，
    // 兩者都由呢度嘅 stage + reason 區分；client crash／shutdown hang 冇 result
    // marker，由 runner 靠 exit code 判定。
    enum ExitCode {
        Passed = 0,
        InvalidArguments = 1,
        ConnectFailed = 2,          // TCP 連唔上（server 未啟動／port 錯）
        SignupFailed = 3,           // signup／setup 未完成
        RoomSceneFailed = 4,        // RoomScene 未建立
        DashboardFailed = 5,        // Dashboard 未建立
        GeneralSelectionFailed = 6, // 選將請求未回覆
        GameStartFailed = 7,        // 未開局
        InteractionFailed = 8,      // askFor 請求無法經 UI 回覆
        GameOverNotReached = 9,     // 開咗局但冇 game over
        Disconnected = 10,          // 局中被 server 斷線
        Timeout = 11,               // app 內部總 timeout
        InternalError = 12
    };

    // stage 名稱，按實際發生次序。
    //
    // 次序同任務書列出嘅稍有不同：RoomScene 係喺 Client::server_connected
    // （setup 收到）即刻由 MainWindow::enterRoom 建立，早過選將請求，所以
    // room_scene／dashboard 排喺 general_selected 之前。呢個係產品真實次序，
    // 唔係為咗遷就測試而改。
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

    // 失敗原因。timeout／斷線同「stage 本身失敗」共用 stage 名，靠呢個欄位分辨。
    static const char *const ReasonOk;                  // "ok"
    static const char *const ReasonStageFailed;         // "stage_failed"
    static const char *const ReasonTimeout;             // "timeout"
    static const char *const ReasonDisconnected;        // "disconnected"
    static const char *const ReasonInteractionStalled;  // "interaction_stalled"

    // server request（S_TYPE_REQUEST）→ 契約互動名。runner 同 CI 用呢啲名做
    // askFor 覆蓋率 gate，所以名唔可以隨便改。
    //
    // 唔喺對照表內的 command 回傳空字串：呢啲係 notification／reply，唔算互動。
    static QString interactionName(int commandType);
    static QStringList knownInteractionNames();

    // Responder 經真正 RoomScene／Dashboard 做出嘅 UI 動作名（唔係 server
    // command，而係「玩家做咗乜」），同 interaction 分開記。
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
