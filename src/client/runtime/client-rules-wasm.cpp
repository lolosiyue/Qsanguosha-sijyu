#include "client-rules-host.h"
#include "protocol/rules-bundle-identity.h"
#include "rules-bundle-exporter.h"
#include <QFile>
#include <QJsonDocument>
#include <QtLogging>
#include <cstdio>

#include <emscripten/emscripten.h>

namespace {
ClientRulesHost host(QStringLiteral("/assets"), QStringLiteral("/work"),
                     QStringLiteral("/userdata"));

void logToWorker(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // Qt's default WASM sink bypasses Module.printErr; keep native failures in
    // the Worker's bounded log, including errors immediately before exit().
    const QByteArray text = qFormatLogMessage(type, context, message).toUtf8();
    std::fprintf(stderr, "%s\n", text.constData());
    std::fflush(stderr);
}
}

extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_bridge_schema()
{
    return QSanRules::BridgeSchema;
}

// Read the code seal before any Engine or Lua content is initialized.
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_code_identity()
{
    QFile file(QStringLiteral("/work/code.json"));
    const QByteArray bytes = QJsonDocument(QSanRules::exportCodeIdentity()).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() ? 0 : 1;
}

// Keep the production ABI unchanged. Native tests compile the same host;
// there is no test implementation of initialization, evaluation or shutdown.
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_initialize()
{
    qInstallMessageHandler(logToWorker);
    return host.initialize();
}
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_evaluate() { return host.evaluate(); }
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_shutdown() { return host.shutdown(); }

// Raw-frame ingress is additive and opt-in; its own envelope is versioned.
extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_stream() { return host.stream(); }
