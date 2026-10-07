#include "core/engine-bootstrap.h"
#include "core/engine.h"
#include "core/runtime-paths.h"
#include "core/settings.h"
#include "core/battle-statistics.h"
#include "server/managed-rewind-lab.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQueue>
#include <QSocketNotifier>
#include <QTextStream>
#include <functional>
#include <unistd.h>

// Separate opt-in POSIX console: standard input is local operator authority.
// No server listener, lobby publication, or remotely callable rewind command.
int main(int argc, char **argv)
{
    qSetGlobalQHashSeed(0);
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qsanguosha-rewind-lab"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Private restricted 02p rewind lab. No generals/skills, native TrustAI, basic cards only.\n"
        "Omniscient local debug console; no networking, lobby or GUI; statistics are excluded debug data.\n"
        "Commands: step | rewind turn | rewind round | retry stats | status | quit\n"
        "Rewind pauses at the selected start; step continues play. Limits: 128 advances / 128 restores."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("seed"), QStringLiteral("Deterministic game seed."), QStringLiteral("number"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("asset-root"), QStringLiteral("Engine asset directory."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("user-data-root"), QStringLiteral("Local runtime data directory."), QStringLiteral("path")});
    parser.process(app);
    bool seedOk = false;
    const quint64 seed = parser.value(QStringLiteral("seed")).toULongLong(&seedOk);
    QString error;
    if (!seedOk || !QSanRuntimePaths::resolve(app.arguments(), &error)) {
        QTextStream(stderr) << (seedOk ? error : QStringLiteral("Invalid seed")) << Qt::endl;
        return 64;
    }
    Config.setValueOverrides({{"EnableAI", false}, {"Enable2ndGeneral", false},
        {"EnableHegemony", false}, {"EnableMeleeMode", false}, {"EnableLuckCard", false}});
    if (!EngineBootstrap::initialize(false, &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 1;
    }
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    Config.init();
    Config.EnableAI = false;
    Config.Enable2ndGeneral = false;
    Config.EnableHegemony = false;
    Config.AIDelay = Config.OriginAIDelay = 0;
    int result = 0;
    {
        ManagedRewindLab lab;
        QTextStream out(stdout);
        QByteArray input;
        QQueue<QString> pending;
        bool busy = true;
        bool closed = false;
        bool readyReceived = false;
        bool stopRequested = false;
        QSocketNotifier notifier(STDIN_FILENO, QSocketNotifier::Read);
        std::function<void()> dispatch;
        dispatch = [&] {
            if (busy) return;
            while (!pending.isEmpty()) {
                const QString line = pending.dequeue().trimmed();
                if (line.isEmpty()) continue;
                if (line == QStringLiteral("quit")) {
                    notifier.setEnabled(false);
                    stopRequested = true;
                    lab.stop();
                    busy = true;
                    return;
                }
                QString why;
                if (lab.submit(line, &why)) { busy = true; return; }
                out << QJsonDocument::fromVariant(QVariantMap{{"command", line}, {"ok", false}, {"error", why}})
                    .toJson(QJsonDocument::Compact) << Qt::endl;
            }
            if (closed) { stopRequested = true; lab.stop(); busy = true; }
        };
        QObject::connect(&lab, &ManagedRewindLab::lineReady, &app, [&](const QString &json) {
            out << json << Qt::endl;
            const auto response = QJsonDocument::fromJson(json.toUtf8()).object();
            if (response.value("command") == "ready") readyReceived = true;
            if (response.value("command") == "ready" && !response.value("ok").toBool()) {
                result = 1;
                pending.clear();
                closed = true;
            }
            busy = false;
            dispatch();
        });
        QObject::connect(&lab, &ManagedRewindLab::stopped, &app, [&] {
            if (!readyReceived || !stopRequested) {
                QTextStream(stderr) << "Restricted worker exited unexpectedly" << Qt::endl;
                result = 1;
            }
            app.quit();
        });
        QObject::connect(&notifier, &QSocketNotifier::activated, &app, [&] {
            char buffer[4096];
            const auto count = ::read(STDIN_FILENO, buffer, sizeof buffer);
            if (count <= 0) { closed = true; notifier.setEnabled(false); }
            else input.append(buffer, int(count));
            if (input.size() > 65536) {
                result = 64; pending.clear(); input.clear(); closed = true; notifier.setEnabled(false);
            }
            for (;;) {
                const int end = input.indexOf('\n');
                if (end < 0) break;
                pending.enqueue(QString::fromUtf8(input.left(end)));
                input.remove(0, end + 1);
                if (pending.size() > 256) { result = 64; pending.clear(); input.clear(); closed = true; notifier.setEnabled(false); break; }
            }
            if (closed && !input.isEmpty()) { pending.enqueue(QString::fromUtf8(input)); input.clear(); }
            dispatch();
        });
        if (!lab.start(seed, &error)) {
            QTextStream(stderr) << error << Qt::endl;
            result = 1;
        } else {
            app.exec();
        }
    }
    BattleStatistics::waitForPendingWrites();
    EngineBootstrap::shutdown();
    return result;
}
