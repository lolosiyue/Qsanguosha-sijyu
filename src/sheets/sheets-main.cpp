#include "../excel/excel-bridge.h"
#include "audio.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "runtime-paths.h"
#include "version.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QUuid>

#include <atomic>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr int UsageError = 2;
constexpr int StartupError = 6;
constexpr int OwnerLostExitCode = 86;

struct LaunchIdentity {
    QString session;
    QString token;
};

bool validToken(const QString &token)
{
    if (token.size() != 64) return false;
    for (const QChar c : token) {
        if (!c.isDigit() && !(c >= QLatin1Char('a') && c <= QLatin1Char('f'))
            && !(c >= QLatin1Char('A') && c <= QLatin1Char('F'))) return false;
    }
    return true;
}

bool readLaunchIdentity(LaunchIdentity *identity)
{
    QString text;
#ifdef Q_OS_WIN
    const HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) return false;
    QByteArray line;
    for (;;) {
        char c = 0;
        DWORD read = 0;
        if (!ReadFile(handle, &c, 1, &read, nullptr) || read == 0) return false;
        if (c == '\n') break;
        if (line.size() >= 4096) return false;
        line.append(c);
    }
    text = QString::fromUtf8(line).trimmed();
#else
    Q_UNUSED(identity);
    return false;
#endif
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) return false;
    identity->session = doc.object().value(QStringLiteral("session")).toString();
    identity->token = doc.object().value(QStringLiteral("token")).toString();
    return !QUuid(identity->session).isNull() && validToken(identity->token);
}

class StdinSupervisor final : public QThread
{
public:
    explicit StdinSupervisor(std::atomic_bool *eof, QObject *parent = nullptr)
        : QThread(parent), m_eof(eof) {}
    ~StdinSupervisor() override
    {
        requestInterruption();
        // The worker only peeks/reads available pipe bytes and sleeps 50 ms.
        // Join before QThread destruction; never destroy a still-running thread.
        wait();
    }

protected:
    void run() override
    {
#ifdef Q_OS_WIN
        const HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
            m_eof->store(true, std::memory_order_release);
            return;
        }
        for (;;) {
            if (isInterruptionRequested()) return;
            DWORD available = 0;
            if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) break;
            if (available == 0) { Sleep(50); continue; }
            char buffer[1024];
            DWORD read = 0;
            if (!ReadFile(handle, buffer, qMin<DWORD>(available, sizeof(buffer)), &read, nullptr) || read == 0) break;
            Q_UNUSED(buffer);
        }
#endif
        m_eof->store(true, std::memory_order_release);
    }

private:
    std::atomic_bool *m_eof;
};

bool writeReady(const QString &path, const QString &session, quint16 port,
    const QString &status)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QJsonObject ready{{QStringLiteral("api_version"), 1},
        {QStringLiteral("session"), session}, {QStringLiteral("port"), int(port)},
        {QStringLiteral("status"), status},
        {QStringLiteral("pid"), qint64(QCoreApplication::applicationPid())}};
    file.write(QJsonDocument(ready).toJson(QJsonDocument::Compact));
    file.write("\n");
    return file.commit();
}

bool scopedPaths(const QString &readyFile)
{
    const QDir privateDir(QFileInfo(readyFile).absolutePath());
    const QString settings = QFileInfo(qEnvironmentVariable("QSAN_SESSION_SETTINGS"))
        .absoluteFilePath();
    const QString data = QFileInfo(qEnvironmentVariable("QSAN_USER_DATA_ROOT"))
        .absoluteFilePath();
    return QFileInfo(settings).absoluteFilePath().compare(
               privateDir.filePath(QStringLiteral("config.ini")), Qt::CaseInsensitive) == 0
        && QFileInfo(data).absoluteFilePath().compare(
               privateDir.filePath(QStringLiteral("data")), Qt::CaseInsensitive) == 0;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QSanguoshaSheetsBridge"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(QSanVersion::Number));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QSanguosha Google Sheets bridge"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("ready-file"),
        QStringLiteral("Private ready file"), QStringLiteral("file")));
    parser.addOption(QCommandLineOption(QStringLiteral("asset-root"),
        QStringLiteral("Packaged runtime root"), QStringLiteral("directory")));
    parser.process(application);
    const QString readyFile = parser.value(QStringLiteral("ready-file"));
    if (!QFileInfo(readyFile).isAbsolute() || !parser.isSet(QStringLiteral("asset-root")))
        return UsageError;

    LaunchIdentity identity;
    if (!readLaunchIdentity(&identity) || !scopedPaths(readyFile)) {
        writeReady(readyFile, identity.session, 0, QStringLiteral("error"));
        return StartupError;
    }

    std::atomic_bool stdinEof{false};
    StdinSupervisor supervisor(&stdinEof);
    supervisor.start();
    if (stdinEof.load(std::memory_order_acquire)) {
        writeReady(readyFile, identity.session, 0, QStringLiteral("error"));
        return StartupError;
    }

    QString error;
    if (!QSanRuntimePaths::resolve(application.arguments(), &error)
        || !EngineBootstrap::initialize(false, &error) || !EngineBootstrap::hasLuaState()
        || stdinEof.load(std::memory_order_acquire)) {
        EngineBootstrap::shutdown();
        writeReady(readyFile, identity.session, 0, QStringLiteral("error"));
        return StartupError;
    }

    int exitCode = StartupError;
    {
        ExcelBridgeOptions options;
        options.session = identity.session;
        options.token = identity.token;
        options.assetRoot = QSanRuntimePaths::assetRoot();
        options.userDataRoot = qEnvironmentVariable("QSAN_USER_DATA_ROOT");
        options.suppressPresentationAudio = true;
        ExcelBridge bridge(options);
        QObject::disconnect(&application, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
        QObject::connect(&supervisor, &QThread::finished, &bridge, &ExcelBridge::stop);
        QObject::connect(&bridge, &ExcelBridge::stopped, &supervisor, &QThread::requestInterruption);
        QObject::connect(&bridge, &ExcelBridge::stopped, &application,
            [&application, &stdinEof](bool graceful) {
                // Owner loss is cleanup, not successful user-requested exit.
                application.exit(graceful && !stdinEof.load(std::memory_order_acquire)
                    ? 0 : OwnerLostExitCode);
            });
        if (stdinEof.load(std::memory_order_acquire)) {
            // Do not reopen an already stopped bridge or emit exit() before
            // exec(). A queued EOF arriving later is handled inside exec().
            writeReady(readyFile, identity.session, 0, QStringLiteral("error"));
        } else if (!bridge.start(&error) || !writeReady(readyFile, identity.session, bridge.port(),
            QStringLiteral("ready"))) {
            bridge.stop();
            writeReady(readyFile, identity.session, 0, QStringLiteral("error"));
        } else {
            exitCode = application.exec();
        }
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QFile::remove(readyFile);
    EngineBootstrap::shutdown();
    return exitCode;
}
