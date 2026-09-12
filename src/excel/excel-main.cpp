#include "excel-bridge.h"
#include "excel-process-guard.h"
#include "audio.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "runtime-paths.h"
#include "version.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>
#include <cmath>

namespace {
constexpr int UsageError = 2;
constexpr int StartupError = 6;
constexpr int ForcedShutdown = 86;

bool sessionBootstrapPath(const QString &path)
{
    const QFileInfo file(path);
    const QFileInfo session(file.absolutePath());
    const QString root = QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("QSanguoshaExcel"));
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    return !canonicalRoot.isEmpty() && file.isAbsolute()
        && file.fileName() == QLatin1String("bootstrap.json")
        && !QUuid(session.fileName()).isNull() && !session.isSymLink() && !file.isSymLink()
        && QFileInfo(session.absolutePath()).canonicalFilePath()
            .compare(canonicalRoot, Qt::CaseInsensitive) == 0;
}

// Recovery runs in a short-lived hidden helper, never a blocking VBA callback.
int stopSession(QCoreApplication &application, const QString &path, qint64 parentPid, int waitReadyMs)
{
    if (!sessionBootstrapPath(path)) return UsageError;
    if (waitReadyMs > 0) {
        // Cancellation can precede Engine startup. The marker prevents a late
        // ready publication even after this bounded recovery helper has exited.
        QFile cancellation(QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral("cancel.request")));
        if (!cancellation.open(QIODevice::WriteOnly)) return StartupError;
        cancellation.close();
        if (!QFileInfo::exists(path)) {
            QEventLoop waiting;
            QTimer poll, deadline;
            poll.setInterval(100);
            deadline.setSingleShot(true);
            QObject::connect(&poll, &QTimer::timeout, &waiting, [&]() {
                if (QFileInfo::exists(path)) waiting.quit();
            });
            QObject::connect(&deadline, &QTimer::timeout, &waiting, &QEventLoop::quit);
            poll.start();
            deadline.start(waitReadyMs);
            waiting.exec();
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) return UsageError;
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return UsageError;
    const QJsonObject ready = document.object();
    const double rawPort = ready.value(QStringLiteral("port")).toDouble(-1);
    const QString session = ready.value(QStringLiteral("session")).toString();
    const QString token = ready.value(QStringLiteral("token")).toString();
    if (ready.value(QStringLiteral("api_version")) != QJsonValue(1)
        || ready.value(QStringLiteral("status")) != QLatin1String("ready")
        || rawPort < 1 || rawPort > 65535 || std::floor(rawPort) != rawPort
        || QUuid(session).isNull() || token.size() != 64
        || (parentPid > 0 && ready.value(QStringLiteral("parent_pid")).toDouble() != parentPid))
        return UsageError;
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/v1/shutdown").arg(int(rawPort))));
    request.setRawHeader("Authorization", "Bearer " + token.toLatin1());
    request.setRawHeader("X-QSan-Session", session.toLatin1());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
#endif
    QNetworkReply *reply = network.post(request, QByteArrayLiteral("{}"));
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &application, [&]() {
        reply->abort();
        application.exit(StartupError);
    });
    QObject::connect(reply, &QNetworkReply::finished, &application, [&]() {
        deadline.stop();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        application.exit(reply->error() == QNetworkReply::NoError && status == 200 ? 0 : StartupError);
    });
    deadline.start(5000);
    return application.exec();
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QSanguoshaExcelBridge"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(QSanVersion::Number));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QSanguosha worksheet client bridge"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("bootstrap"), QStringLiteral("Private ready file"), QStringLiteral("file")));
    parser.addOption(QCommandLineOption(QStringLiteral("nonce"), QStringLiteral("Workbook launch nonce"), QStringLiteral("uuid")));
    parser.addOption(QCommandLineOption(QStringLiteral("parent-pid"), QStringLiteral("Owning Excel process"), QStringLiteral("pid")));
    parser.addOption(QCommandLineOption(QStringLiteral("asset-root"), QStringLiteral("Packaged runtime root"), QStringLiteral("directory")));
    parser.addOption(QCommandLineOption(QStringLiteral("stop-session"), QStringLiteral("Stop one owned bootstrap session"), QStringLiteral("file")));
    parser.addOption(QCommandLineOption(QStringLiteral("wait-ready"), QStringLiteral("Cancel pending startup and await ready (milliseconds)"), QStringLiteral("milliseconds"), QStringLiteral("0")));
    parser.process(application);

    bool parentOk = false;
    const qint64 parentPid = parser.value(QStringLiteral("parent-pid")).toLongLong(&parentOk);
    if (parser.isSet(QStringLiteral("stop-session"))) {
        bool waitOk = false;
        const int waitReady = parser.value(QStringLiteral("wait-ready")).toInt(&waitOk);
        if (!waitOk || waitReady < 0 || waitReady > 180000) return UsageError;
        return stopSession(application, parser.value(QStringLiteral("stop-session")), parentOk ? parentPid : 0, waitReady);
    }
    const QString bootstrap = parser.value(QStringLiteral("bootstrap"));
    const QString nonce = parser.value(QStringLiteral("nonce"));
    if (!parentOk || parentPid <= 0 || !QFileInfo(bootstrap).isAbsolute()
        || QUuid(nonce).isNull() || !parser.isSet(QStringLiteral("asset-root"))) return UsageError;
#ifdef QSAN_XP_LEGACY
    const bool legacy = true;
#else
    const bool legacy = false;
#endif
    const QString session = ExcelProcessGuard::generateSession();
    const QString token = ExcelProcessGuard::generateToken();
    if (session.isEmpty() || token.isEmpty()) return StartupError;
    ExcelProcessGuard parent;
    QString error;
    if (!parent.initialize(parentPid, bootstrap, nonce, session, token,
        legacy ? QStringLiteral("legacy") : QStringLiteral("modern"), legacy ? 10 : 0, &error))
        return StartupError;
    // Watch process death before potentially slow engine/Lua initialization.
    if (!parent.startMonitoring()) {
        parent.writeError(QStringLiteral("parent_monitor_unavailable"));
        return StartupError;
    }
    const QDir privateDirectory(QFileInfo(bootstrap).absolutePath());
    const QString cancelPath = privateDirectory.filePath(QStringLiteral("cancel.request"));
    if (QFileInfo::exists(cancelPath)) {
        parent.writeError(QStringLiteral("startup_cancelled"));
        return StartupError;
    }
    const QString profile = privateDirectory.absoluteFilePath(QStringLiteral("config.ini"));
    const QString userData = privateDirectory.absoluteFilePath(QStringLiteral("data"));
    // Config is constructed before main. Only the scoped launcher can supply
    // this path early enough; never initialize using the user's normal config.
    if (QFileInfo(qEnvironmentVariable("QSAN_SESSION_SETTINGS")).absoluteFilePath()
            .compare(QFileInfo(profile).absoluteFilePath(), Qt::CaseInsensitive) != 0
        || QFileInfo(qEnvironmentVariable("QSAN_USER_DATA_ROOT")).absoluteFilePath()
            .compare(QFileInfo(userData).absoluteFilePath(), Qt::CaseInsensitive) != 0) {
        parent.writeError(QStringLiteral("use_scoped_launcher"));
        return StartupError;
    }
    if (!QSanRuntimePaths::resolve(application.arguments(), &error)) {
        parent.writeError(QStringLiteral("runtime_paths_invalid"));
        return StartupError;
    }
    if (!EngineBootstrap::initialize(false, &error) || !EngineBootstrap::hasLuaState()) {
        parent.writeError(QStringLiteral("engine_initialization_failed"));
        EngineBootstrap::shutdown();
        return StartupError;
    }
    QObject::disconnect(&application, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    if (QFileInfo::exists(cancelPath)) {
        parent.writeError(QStringLiteral("startup_cancelled"));
        EngineBootstrap::shutdown();
        return StartupError;
    }
#ifdef AUDIO_SUPPORT
    Audio::init();
#endif
    int exitCode = StartupError;
    {
        ExcelBridgeOptions options;
        options.session = session;
        options.token = token;
        options.assetRoot = QSanRuntimePaths::assetRoot();
        options.userDataRoot = userData;
        options.legacy = legacy;
        ExcelBridge bridge(options);
        QObject::connect(&bridge, &ExcelBridge::stopped, &application,
            [&application](bool graceful) { application.exit(graceful ? 0 : ForcedShutdown); });
        QObject::connect(&parent, &ExcelProcessGuard::parentDied, &bridge, &ExcelBridge::stop);
        if (!bridge.start(&error) || !parent.writeReady(bridge.port(), &error)) {
            parent.writeError(QStringLiteral("bridge_start_failed"));
        } else exitCode = application.exec();
    }
#ifdef AUDIO_SUPPORT
    Audio::quit();
#endif
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EngineBootstrap::shutdown();
    // Delete only this session's credential; retain diagnostic files.
    if (exitCode == 0 || exitCode == ForcedShutdown) QFile::remove(bootstrap);
    return exitCode;
}
