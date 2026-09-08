#include "local-server-controller.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "runtime-paths.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <functional>
#include <windows.h>

namespace {
bool expect(bool condition, const QString &message)
{
    if (!condition) QTextStream(stderr) << "FAIL: " << message << '\n';
    return condition;
}

bool waitUntil(const std::function<bool()> &condition, int milliseconds)
{
    if (condition()) return true;
    QEventLoop loop;
    QTimer poll, deadline;
    poll.setInterval(10);
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (condition()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    deadline.start(milliseconds);
    loop.exec();
    return condition();
}

QString argument(const QStringList &arguments, const QString &name, const QString &fallback = QString())
{
    const int index = arguments.indexOf(name);
    return index < 0 ? fallback : arguments.value(index + 1);
}

QStringList events(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll()).split('\n', QString::SkipEmptyParts);
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void stopProcess(QProcess &process)
{
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
}

QProcessEnvironment isolatedEnvironment(const QString &dataRoot, const QString &runtimeDirectory)
{
    QDir().mkpath(dataRoot);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QSAN_USER_DATA_ROOT", dataRoot);
    environment.insert("QSAN_XP_SETTINGS", QDir(dataRoot).filePath("config.ini"));
    environment.insert("PATH", QDir::toNativeSeparators(runtimeDirectory) + ';' + environment.value("PATH"));
    environment.insert("QSAN_XP_FIXTURE_OWNER_PID", QString::number(QCoreApplication::applicationPid()));
    return environment;
}

bool initializeEngine(QCoreApplication &app)
{
    const QString configured = QString::fromLocal8Bit(qgetenv("QSAN_XP_SETTINGS"));
    const QString root = QString::fromLocal8Bit(qgetenv("QSAN_USER_DATA_ROOT"));
    if (!expect(!configured.isEmpty() && QFileInfo(configured).isAbsolute()
                    && !root.isEmpty() && QFileInfo(root).isAbsolute()
                    && QDir::cleanPath(configured).startsWith(QDir::cleanPath(root) + '/', Qt::CaseInsensitive)
                    && QFileInfo(Config.fileName()).absoluteFilePath().compare(
                        QFileInfo(configured).absoluteFilePath(), Qt::CaseInsensitive) == 0,
                "fixture test uses isolated global Config")) return false;
    Config.setValue("GameMode", "03_1v2");
    Config.setValue("ServerPort", 0);
    Config.setValue("BindAddress", "127.0.0.1");
    Config.setValue("AutoAddRobots", false);
    Config.setValue("EnableUPnP", false);
    Config.setValue("EnableListServer", false);
    Config.sync();
    QString error;
    if (!QSanRuntimePaths::resolve(app.arguments(), &error)) return expect(false, error);
    if (!EngineBootstrap::initialize(false, &error)) return expect(false, error);
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    Config.init();
    return true;
}

struct FailureCase {
    const char *name;
    const char *failure;
    bool ready;
    bool graceful;
};

bool runCase(LocalServerController &controller, const FailureCase &test, QProcess &sentinel)
{
    const QString scenario = QString::fromLatin1(test.name);
    const QString eventPath = QSanRuntimePaths::userDataPath(scenario + "-events.txt");
    qputenv("QSAN_XP_FIXTURE_CASE", scenario.toLatin1());
    qputenv("QSAN_XP_FIXTURE_EVENTS", eventPath.toLocal8Bit());
    qputenv("QSAN_XP_FIXTURE_OWNER_PID", QByteArray::number(QCoreApplication::applicationPid()));
    QStringList failures;
    QString requestId, resultId, resultCode;
    QStringList requestIds;
    QHash<QString, QStringList> completions;
    int readyCount = 0, stopCount = 0, resultCount = 0;
    bool graceful = false, forced = false, acceptedResult = false;
    QElapsedTimer elapsed;
    elapsed.start();
    QObject callbacks;
    QObject::connect(&controller, &LocalServerController::failed, &callbacks,
                     [&](const QString &code) { failures << code; });
    QObject::connect(&controller, &LocalServerController::logMessage, &callbacks,
                     [&](const QString &message) { if (message.startsWith("forced_shutdown:")) forced = true; });
    QObject::connect(&controller, &LocalServerController::ready, &callbacks, [&]() {
        ++readyCount;
        if (!test.ready || scenario == "forced_hang") controller.stop();
        else {
            requestId = controller.request("status");
            requestIds << requestId;
            if (scenario == "request_timeout") {
                requestIds << controller.request("status") << controller.request("status");
            }
            if (scenario == "pending_cancel") controller.stop();
        }
    });
    QObject::connect(&controller, &LocalServerController::commandResult, &callbacks,
                     [&](const QString &id, bool success, const QJsonObject &body) {
        ++resultCount;
        resultId = id;
        resultCode = body.value("code").toString();
        completions[id] << resultCode;
        acceptedResult = success;
    });
    QObject::connect(&controller, &LocalServerController::stopped, &callbacks,
                     [&](bool normal) { ++stopCount; graceful = normal; });
    if (!expect(controller.start(LocalServerController::Ownership::OwnedPrivate, false,
                                  GameSessionConfig(Q_UINT64_C(20260908))), scenario + ": start")) return false;
    QPointer<QProcess> child = controller.findChild<QProcess *>();
    const QString session = child ? child->processEnvironment().value("QSAN_XP_SESSION") : QString();
    const QString diagnosticStem = QDir(QSanRuntimePaths::userDataPath("logs")).filePath(
        QStringLiteral("xp-server-") + session.left(16));
    const bool stopped = waitUntil([&]() { return stopCount > 0; }, 23000);
    if (!stopped) {
        controller.stop();
        waitUntil([&]() { return !controller.active(); }, 11000);
    }
    bool result = expect(stopped && stopCount == 1 && !controller.active()
                             && (!child || child->state() == QProcess::NotRunning), scenario + ": owned child reaped once");
    result = expect(readyCount == (test.ready ? 1 : 0), scenario + ": ready count") && result;
    result = expect(graceful == test.graceful, scenario + ": cleanup classification") && result;
    const QString expectedFailure = QString::fromLatin1(test.failure);
    result = expect(expectedFailure.isEmpty() ? failures.isEmpty()
                        : failures.size() == 1 && failures.first().startsWith(expectedFailure),
                    scenario + ": failure codes = " + failures.join(" | ")) && result;
    const QStringList observed = events(eventPath);
    result = expect(observed.contains("started"), scenario + ": fixture actually executed") && result;
    result = expect(fileBytes(diagnosticStem + ".stdout.log").contains(
                        (QStringLiteral("XP_FIXTURE_STDOUT ") + scenario).toUtf8()),
                    scenario + ": helper stdout was preserved") && result;
    result = expect(fileBytes(diagnosticStem + ".stderr.log").contains(
                        (QStringLiteral("XP_FIXTURE_STDERR ") + scenario).toUtf8()),
                    scenario + ": helper stderr was preserved") && result;
    if (scenario.startsWith("wrong_") && scenario != "wrong_build" && scenario != "wrong_ready")
        result = expect(observed.contains("rejected_before_initialize") && !observed.contains("initialized"),
                        scenario + ": invalid hello was not adopted") && result;
    if (scenario == "pending_cancel") {
        result = expect(!requestId.isEmpty() && resultCount == 1 && resultId == requestId
                            && !acceptedResult && resultCode == "cancelled",
                        scenario + ": pending command completes exactly once") && result;
    }
    if (scenario == "request_timeout") {
        int timeouts = 0, cancellations = 0;
        for (const QString &id : requestIds) {
            const QStringList codes = completions.value(id);
            result = expect(!id.isEmpty() && codes.size() == 1,
                            "each timed-out batch request completes exactly once: " + id) && result;
            timeouts += codes.count("request_timeout");
            cancellations += codes.count("cancelled");
        }
        result = expect(requestIds.size() == 3 && completions.size() == 3 && resultCount == 3
                            && !acceptedResult && timeouts == 1 && cancellations == 2,
                        "one timeout cancels the remaining batch without duplicate completion") && result;
        result = expect(elapsed.elapsed() >= 4900, "request timeout uses the real five second deadline") && result;
    }
    if (scenario == "forced_hang")
        result = expect(forced && !graceful && elapsed.elapsed() >= 9900,
                        "stuck helper uses real ten second forced shutdown deadline") && result;
    result = expect(sentinel.state() == QProcess::Running,
                    scenario + ": unrelated process with identical executable name survives") && result;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTextStream(stdout) << "FIXTURE_CASE " << scenario << ' ' << (result ? "PASS" : "FAIL")
                        << " graceful=" << graceful << " elapsed_ms=" << elapsed.elapsed() << '\n';
    return result;
}

bool fixtureSuite(QCoreApplication &app, const QString &fixtureSource)
{
    if (!initializeEngine(app)) return false;
    bool result = true;
    {
        LocalServerController controller;
        QString missingError;
        QObject missingCallbacks;
        QObject::connect(&controller, &LocalServerController::failed, &missingCallbacks,
                         [&](const QString &error) { missingError = error; });
        result = expect(!controller.start(LocalServerController::Ownership::OwnedPrivate, false,
                                          GameSessionConfig(Q_UINT64_C(20260908)))
                            && !controller.active() && missingError.startsWith("helper_missing:")
                            && !controller.findChild<QProcess *>(), "missing helper fails without process adoption");
        QObject::disconnect(&controller, nullptr, &missingCallbacks, nullptr);
        const QString helper = QDir(app.applicationDirPath()).filePath("QSanguoshaXPServer.exe");
        result = expect(!QFileInfo::exists(helper) && QFile::copy(fixtureSource, helper)
                            && XpControl::fileHash(helper) == XpControl::fileHash(fixtureSource),
                        "stage test-only helper after missing-file case") && result;
        if (result) {
            QProcess sentinel;
            QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
            environment.insert("QSAN_XP_FIXTURE_OWNER_PID", QString::number(app.applicationPid()));
            environment.insert("QSAN_XP_FIXTURE_EVENTS", QSanRuntimePaths::userDataPath("sentinel-events.txt"));
            sentinel.setProcessEnvironment(environment);
            sentinel.start(helper, QStringList({"--sentinel"}));
            QByteArray output;
            result = expect(waitUntil([&]() {
                output += sentinel.readAllStandardOutput();
                return output.contains("SENTINEL_READY");
            }, 5000), "unrelated same-name sentinel started") && result;
            const FailureCase cases[] = {
                {"wrong_build", "helper_build_mismatch", false, true},
                {"wrong_version", "helper_exited: 42", false, false},
                {"wrong_session", "helper_exited: 42", false, false},
                {"wrong_generation", "helper_exited: 42", false, false},
                {"early_exit", "helper_exited: 42", false, false},
                {"wrong_ready", "ready_mismatch", false, true},
                {"postauth_session", "control_identity_mismatch", true, true},
                {"postauth_generation", "control_identity_mismatch", true, true},
                {"postauth_version", "control_identity_mismatch", true, true},
                {"channel_loss", "control_disconnected", true, false},
                {"request_timeout", "request_timeout", true, true},
                {"pending_cancel", "", true, true},
                {"forced_hang", "", true, false}
            };
            for (const FailureCase &test : cases) {
                if (!result) break;
                result = runCase(controller, test, sentinel);
            }
            stopProcess(sentinel);
        }
    }
    EngineBootstrap::shutdown();
    return result;
}

bool parentDeath(const QString &ownerExecutable, const QString &assetRoot,
                 const QString &dataRoot, const QString &runtimeDirectory)
{
    QProcess owner;
    owner.setProcessEnvironment(isolatedEnvironment(dataRoot, runtimeDirectory));
    owner.setProcessChannelMode(QProcess::MergedChannels);
    owner.start(ownerExecutable, QStringList({"--asset-root", assetRoot, "--hold-owner"}));
    QByteArray output;
    quint64 pid = 0;
    const bool ready = waitUntil([&]() {
        output += owner.readAll();
        const QByteArray marker("OWNER_READY helper_pid=");
        const int start = output.indexOf(marker);
        if (start < 0) return false;
        const int end = output.indexOf('\n', start);
        return end >= 0 && XpControl::decimal(QString::fromLatin1(output.mid(
            start + marker.size(), end - start - marker.size()).trimmed()), &pid);
    }, 40000);
    if (!expect(ready && pid > 0 && pid <= 0xffffffffULL && owner.state() == QProcess::Running,
                "production owner reports its live helper before parent death")) {
        QTextStream(stderr) << QString::fromLocal8Bit(output) << '\n';
        stopProcess(owner);
        return false;
    }
    // Retain the concrete process handle before killing the owner, so PID reuse
    // cannot masquerade as a successfully reaped child.
    HANDLE helper = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, DWORD(pid));
    if (!expect(helper && WaitForSingleObject(helper, 0) == WAIT_TIMEOUT, "retain real owned helper handle")) {
        stopProcess(owner);
        if (helper) CloseHandle(helper);
        return false;
    }
    stopProcess(owner);
    const DWORD waitResult = WaitForSingleObject(helper, 5000);
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(helper, &exitCode);
    const bool result = expect(waitResult == WAIT_OBJECT_0 && exitCode == 86,
                              QStringLiteral("production parent-death watchdog exit 86; actual=%1").arg(exitCode));
    if (waitResult != WAIT_OBJECT_0) {
        TerminateProcess(helper, 125);
        WaitForSingleObject(helper, 3000);
    }
    CloseHandle(helper);
    if (result) QTextStream(stdout) << "PASS: production parent death reaps exact helper, emergency exit=86\n";
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const QString assetRoot = argument(arguments, "--asset-root");
    const QString fixture = argument(arguments, "--fixture-helper",
        QDir(app.applicationDirPath()).filePath("qsan_xp_controller_fixture.exe"));
    const QString owner = argument(arguments, "--owner-test",
        QDir(app.applicationDirPath()).filePath("qsan_xp_controller_tests.exe"));
    if (!expect(QFileInfo(assetRoot).isAbsolute() && QFileInfo(fixture).isFile(),
                "provide absolute --asset-root and existing --fixture-helper")) return 2;
    if (arguments.contains("--exercise-fixture")) return fixtureSuite(app, fixture) ? 0 : 1;
    QTemporaryDir staging(QDir::tempPath() + "/qsan-xp-failure-XXXXXX");
    if (!expect(staging.isValid(), "create isolated failure test staging")) return 2;
    const QString binary = QDir(staging.path()).filePath("controller-failure-test.exe");
    if (!expect(QFile::copy(app.applicationFilePath(), binary), "stage isolated test binary")) return 2;
    QProcess child;
    child.setProcessEnvironment(isolatedEnvironment(staging.path() + "/fixture-data", app.applicationDirPath()));
    child.setProcessChannelMode(QProcess::ForwardedChannels);
    child.start(binary, QStringList({"--exercise-fixture", "--asset-root", assetRoot, "--fixture-helper", fixture}));
    if (!expect(child.waitForStarted(5000), "start isolated fixture test process")) return 1;
    const bool finished = waitUntil([&]() { return child.state() == QProcess::NotRunning; }, 180000);
    const bool fixturesPassed = expect(finished && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                                       "isolated production-controller fixture suite");
    stopProcess(child);
    if (!fixturesPassed) return 1;
    return parentDeath(owner, assetRoot, staging.path() + "/parent-data", app.applicationDirPath()) ? 0 : 1;
}
