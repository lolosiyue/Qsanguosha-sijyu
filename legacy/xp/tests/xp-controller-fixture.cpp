// A protocol peer used only by xp-controller-failure-test. It never loads game
// Engine or claims gameplay coverage, and is staged away from production EXEs.
#include "xp-control-protocol.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QTcpServer>
#include <QTextStream>
#include <QTimer>
#include <windows.h>

namespace {
void record(const QString &event)
{
    QFile file(QString::fromLocal8Bit(qgetenv("QSAN_XP_FIXTURE_EVENTS")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream output(&file);
        output << event << '\n';
    }
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString scenario = QString::fromLatin1(qgetenv("QSAN_XP_FIXTURE_CASE"));
    const QString session = QString::fromLatin1(qgetenv("QSAN_XP_SESSION"));
    const QString generation = QString::fromLatin1(qgetenv("QSAN_XP_GENERATION"));
    const QString token = QString::fromLatin1(qgetenv("QSAN_XP_TOKEN"));
    const DWORD ownerPid = QString::fromLatin1(qgetenv("QSAN_XP_FIXTURE_OWNER_PID")).toUInt();
    if (!scenario.isEmpty()) {
        QTextStream output(stdout);
        output << "XP_FIXTURE_STDOUT " << scenario << '\n';
        output.flush();
        QTextStream error(stderr);
        error << "XP_FIXTURE_STDERR " << scenario << '\n';
        error.flush();
    }
    HANDLE owner = OpenProcess(SYNCHRONIZE, FALSE, ownerPid);
    if (!owner) return 80;
    // A failed harness must not leave its test-only helper or sentinel behind.
    QTimer ownerTimer;
    QObject::connect(&ownerTimer, &QTimer::timeout, &app, [&]() {
        if (WaitForSingleObject(owner, 0) != WAIT_TIMEOUT) app.exit(86);
    });
    ownerTimer.start(100);
    QTimer::singleShot(app.arguments().contains("--sentinel") ? 240000 : 30000,
                       &app, [&]() { app.exit(124); });
    record("started");
    if (app.arguments().contains("--sentinel")) {
        QTextStream output(stdout);
        output << "SENTINEL_READY\n";
        output.flush();
        const int result = app.exec();
        CloseHandle(owner);
        return result;
    }
    if (scenario == "early_exit") {
        record("early_exit");
        CloseHandle(owner);
        return 42;
    }
    QLocalSocket socket;
    XpControl::Channel channel(&socket);
    QTcpServer endpoint;
    bool initialized = false;
    auto send = [&](const QString &id, const QString &type, const QJsonObject &body = QJsonObject()) {
        return channel.send(XpControl::envelope(session, generation, id, type, body));
    };
    auto finish = [&]() {
        record("shutdown_request");
        send("0", "shutdown_complete", {{"exitCode", 0}});
        socket.flush();
        QTimer::singleShot(30, &app, [&]() { app.quit(); });
    };
    QObject::connect(&socket, &QLocalSocket::connected, &app, [&]() {
        record("connected");
        QJsonObject hello = XpControl::envelope(session, generation, "0", "hello",
            {{"token", token}, {"build", scenario == "wrong_build" ? "wrong-fixture-build" : XpControl::buildIdentity()}});
        if (scenario == "wrong_version") hello["version"] = XpControl::Version + 1;
        if (scenario == "wrong_session") hello["session"] = "wrong-session";
        if (scenario == "wrong_generation") hello["generation"] = "0";
        channel.send(hello);
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, &app, [&]() {
        record(initialized ? "disconnected_after_initialize" : "rejected_before_initialize");
        if (!initialized) app.exit(42);
    });
    QObject::connect(&channel, &XpControl::Channel::message, &app, [&](const QJsonObject &message) {
        if (!XpControl::validEnvelope(message, session, generation)) { app.exit(81); return; }
        const QString type = message.value("type").toString();
        if (type == "shutdown_request") {
            if (scenario != "forced_hang" && scenario != "channel_loss") finish();
            return;
        }
        if (type == "initialize") {
            initialized = true;
            record("initialized");
            if (!endpoint.listen(QHostAddress::LocalHost, 0)) { app.exit(82); return; }
            const QJsonObject initialization = message.value("body").toObject();
            QJsonObject ready{{"host", "127.0.0.1"}, {"port", endpoint.serverPort()},
                {"build", initialization.value("build")}, {"runtime", initialization.value("runtime")},
                {"settingsHash", initialization.value("settingsHash")}, {"messages", QJsonArray()},
                {"pid", QString::number(QCoreApplication::applicationPid())}};
            if (scenario == "wrong_ready") ready["settingsHash"] = "wrong-settings-hash";
            send("1", "ready", ready);
            return;
        }
        record("command:" + type);
        if (scenario == "channel_loss") {
            socket.abort();
            QTimer::singleShot(250, &app, [&]() { app.exit(43); });
        } else if (scenario == "postauth_session" || scenario == "postauth_generation"
                   || scenario == "postauth_version") {
            QJsonObject reply = XpControl::envelope(session, generation, "0", "status");
            if (scenario == "postauth_session") reply["session"] = "stale-session";
            if (scenario == "postauth_generation") reply["generation"] = "0";
            if (scenario == "postauth_version") reply["version"] = XpControl::Version + 1;
            channel.send(reply);
        }
        // request_timeout, pending_cancel and forced_hang deliberately withhold
        // command acknowledgements; the real controller decides the outcome.
    });
    socket.connectToServer(QString::fromLocal8Bit(qgetenv("QSAN_XP_CONTROL")));
    const int result = app.exec();
    CloseHandle(owner);
    return result;
}
