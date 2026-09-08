#include "client-rules-session.h"
#include "engine-bootstrap.h"
#include "runtime-paths.h"

#include <emscripten/emscripten.h>

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include <memory>
#include <stdexcept>

namespace {
std::unique_ptr<QCoreApplication> application;
ClientRulesSession session;
bool initialized = false;
bool closed = false;
int argumentCount = 1;
char programName[] = "qsanguosha_client";
char *arguments[] = {programName, nullptr};

bool writeJson(const QString &path, const QJsonObject &value)
{
    QSaveFile output(path);
    const QByteArray bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return output.open(QIODevice::WriteOnly)
        && output.write(bytes) == bytes.size() && output.commit();
}

int fail(const QString &path, const QString &reason, int status)
{
    // A failed operation never leaves a previous successful result available.
    QFile::remove(path);
    writeJson(path, {{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("known"), false},
                    {QStringLiteral("reason"), reason}});
    return status;
}

void releaseEngine()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EngineBootstrap::shutdown();
    initialized = false;
    application.reset();
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_initialize()
{
    const QString path = QStringLiteral("/work/init.json");
    QFile::remove(path);
    if (closed)
        return fail(path, QStringLiteral("runtime_closed"), 64);
    try {
        if (!initialized) {
            if (QCoreApplication::instance() != nullptr)
                return fail(path, QStringLiteral("application_already_exists"), 3);
            application = std::make_unique<QCoreApplication>(argumentCount, arguments);
            QCoreApplication::setApplicationName(QStringLiteral("qsanguosha_client"));
            qputenv("QSAN_ASSET_ROOT", "/assets");
            qputenv("QSAN_USER_DATA_ROOT", "/userdata");
            if (!QDir().mkpath(QStringLiteral("/work"))
                || !QDir().mkpath(QStringLiteral("/userdata")))
                throw std::runtime_error("runtime_directories_unavailable");
            QString error;
            if (!QSanRuntimePaths::resolve(application->arguments(), &error)
                || !EngineBootstrap::initialize(false, &error)
                || !EngineBootstrap::hasLuaState())
                throw std::runtime_error((QStringLiteral("engine_initialization_failed:") + error).toStdString());
            initialized = true;
        }
        if (!writeJson(path, session.registry()))
            throw std::runtime_error("cannot_write_registry");
        return 0;
    } catch (const std::exception &error) {
        releaseEngine();
        closed = true;
        return fail(path, QString::fromUtf8(error.what()), 3);
    } catch (...) {
        releaseEngine();
        closed = true;
        return fail(path, QStringLiteral("engine_initialization_failed"), 3);
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_evaluate()
{
    const QString outputPath = QStringLiteral("/work/result.json");
    QFile::remove(outputPath);
    if (!initialized || closed)
        return fail(outputPath, QStringLiteral("engine_unavailable"), 3);
    QFile input(QStringLiteral("/work/request.json"));
    if (!input.open(QIODevice::ReadOnly))
        return fail(outputPath, QStringLiteral("cannot_read_request"), 2);
    constexpr qint64 limit = 4 * 1024 * 1024;
    const QByteArray bytes = input.read(limit + 1);
    QJsonParseError error;
    const QJsonDocument request = QJsonDocument::fromJson(bytes, &error);
    if (bytes.size() > limit || error.error != QJsonParseError::NoError || !request.isObject())
        return fail(outputPath, QStringLiteral("invalid_request_json"), 2);
    // Incomplete selections and unsupported content are successful queries
    // with known=false/can_confirm=false. They do not poison this Engine.
    const QJsonObject result = session.evaluate(request.object());
    if (!writeJson(outputPath, result))
        return fail(outputPath, QStringLiteral("cannot_write_result"), 5);
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int qsan_client_shutdown()
{
    if (closed)
        return 0;
    closed = true;
    releaseEngine();
    return 0;
}

