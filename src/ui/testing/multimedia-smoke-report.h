#ifndef MULTIMEDIA_SMOKE_REPORT_H
#define MULTIMEDIA_SMOKE_REPORT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// The multimedia smoke contract for Linux GUI M2B-A.
//
// Like M1/M2, this header depends on Qt Core only: the marker schema, exit code
// mapping, and argument parsing can all be verified directly by CTest without
// launching QApplication or a real audio device. The part that actually drives
// the GUI and audio backend lives in MultimediaSmokeController.
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
        VideoStageFailed = 3,  // QML media component initialization failed with no static fallback
        Timeout = 4,           // app 內部 timeout
        InvalidArguments = 5,
        InternalError = 6
    };

    static QStringList stageOrder();
    static bool isKnownStage(const QString &stage);

    static const char *const FlagMultimediaSmoke; // "--multimedia-smoke"
    static const char *const FlagReportPath;      // "--multimedia-report"
    static const char *const FlagTimeoutMs;       // "--multimedia-timeout-ms"
    // Forces the home background to use the given file (which may be a
    // nonexistent or undecodable video) so CI can actually exercise the
    // video-failure → static-background degradation path without committing a
    // video asset to the repo.
    static const char *const FlagVideoSource;    // "--multimedia-video-source"
    // Directory containing the audio fixtures. Since M3 the game chdirs into
    // the resolved asset root, so when this smoke runs inside an installed,
    // portable, or AppImage bundle the fixtures are not under the CWD and the
    // location must be given explicitly.
    static const char *const FlagFixtureRoot;    // "--multimedia-fixtures"

    static int defaultTimeoutMs();
    static int minimumTimeoutMs();
    static int maximumTimeoutMs();

    static bool isRequested(const QStringList &arguments);
    static bool parseTimeoutMs(const QStringList &arguments, int *timeoutMs, QString *error);
    static QString parseReportPath(const QStringList &arguments);
    static QString parseVideoSource(const QStringList &arguments);
    static QString parseFixtureRoot(const QStringList &arguments);

    // Result classification for the video background. "No console error" must
    // not be treated as success, so these cases are distinguished explicitly:
    // missing asset, backend absent, unsupported codec, playback error, and
    // successfully degraded.
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
    // A video that cannot play is not necessarily a failure: as long as the static background holds, M2B-A counts it as a pass.
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
