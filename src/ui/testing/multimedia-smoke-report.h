#ifndef MULTIMEDIA_SMOKE_REPORT_H
#define MULTIMEDIA_SMOKE_REPORT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Linux GUI M2B-A 的 multimedia smoke 契約。
//
// 同 M1／M2 一樣，呢個 header 只依賴 Qt Core：marker schema、exit code 對照、
// 參數解析都可以喺 CTest 直接驗，唔使開 QApplication 或者真音訊裝置。實際驅動
// GUI／audio backend 的部分喺 MultimediaSmokeController。
class MultimediaSmokeReport
{
public:
    static const char *const StageMarker;  // "MULTIMEDIA_STAGE"
    static const char *const ResultMarker; // "MULTIMEDIA_RESULT"
    static const char *const VideoMarker;  // "VIDEO_BACKEND_RESULT"

    static int schemaVersion();

    enum ExitCode {
        Passed = 0,
        SetupFailed = 1,       // QApplication／engine／MainWindow 未能建立
        AudioStageFailed = 2,  // backend／ui_effect／voice／bgm／missing_asset
        VideoStageFailed = 3,  // QML media component 初始化失敗且冇靜態 fallback
        Timeout = 4,           // app 內部 timeout
        InvalidArguments = 5,
        InternalError = 6
    };

    static QStringList stageOrder();
    static bool isKnownStage(const QString &stage);

    static const char *const FlagMultimediaSmoke; // "--multimedia-smoke"
    static const char *const FlagReportPath;      // "--multimedia-report"
    static const char *const FlagTimeoutMs;       // "--multimedia-timeout-ms"
    // 強制首頁背景用指定的檔案（可以係一個唔存在或者解唔到的影片），令 CI 可以
    // 真係行一次影片失敗 → 靜態背景的降級路徑，而唔使入庫一段影片。
    static const char *const FlagVideoSource;    // "--multimedia-video-source"
    // 音訊 fixture 的所在目錄。M3 之後遊戲會 chdir 去解析出嚟嘅 asset root，
    // 所以喺一個安裝／可攜／AppImage bundle 度跑呢個 smoke 時，fixture 唔會
    // 喺 CWD 下面，一定要明確指出佢喺邊。
    static const char *const FlagFixtureRoot;    // "--multimedia-fixtures"

    static int defaultTimeoutMs();
    static int minimumTimeoutMs();
    static int maximumTimeoutMs();

    static bool isRequested(const QStringList &arguments);
    static bool parseTimeoutMs(const QStringList &arguments, int *timeoutMs, QString *error);
    static QString parseReportPath(const QStringList &arguments);
    static QString parseVideoSource(const QStringList &arguments);
    static QString parseFixtureRoot(const QStringList &arguments);

    // 影片背景的結果分類。「冇 console error」唔可以當成功，所以要明確分開
    // 「資產缺失」「後端唔喺度」「codec 唔支援」「播放出錯」「已成功降級」。
    static const char *const VideoOk;                 // "ok"
    static const char *const VideoNotRequested;       // "not_requested"
    static const char *const VideoDisabled;           // "disabled"
    static const char *const VideoAssetMissing;       // "asset_missing"
    static const char *const VideoBackendUnavailable; // "backend_unavailable"
    static const char *const VideoCodecUnsupported;   // "codec_unsupported"
    static const char *const VideoPlaybackError;      // "playback_error"
    static const char *const VideoFallbackOk;         // "fallback_ok"

    static QStringList videoReasons();
    static bool isKnownVideoReason(const QString &reason);
    // 影片播唔到唔一定係失敗：只要靜態背景頂得上，M2B-A 就當通過。
    static bool isAcceptableVideoReason(const QString &reason);

    static const char *const ReasonOk;          // "ok"
    static const char *const ReasonStageFailed; // "stage_failed"
    static const char *const ReasonTimeout;     // "timeout"

    static QString stageLine(const QString &stage, bool ok,
        const QJsonObject &details = QJsonObject());
    static QString resultLine(bool ok, const QString &stage, const QString &error,
        ExitCode exitCode, const QJsonObject &details = QJsonObject());
    static QString videoLine(const QJsonObject &video);

    static QJsonObject stagePayload(const QString &stage, bool ok,
        const QJsonObject &details = QJsonObject());
    static QJsonObject resultPayload(bool ok, const QString &stage, const QString &error,
        ExitCode exitCode, const QJsonObject &details = QJsonObject());
    static QJsonObject videoPayload(bool available, bool loaded, bool fallback,
        const QString &reason, const QString &error);

    static ExitCode exitCodeForFailedStage(const QString &stage);
};

#endif
