#include "client-rules-host.h"
#include "card.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "runtime-paths.h"
#include "server-info.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <stdexcept>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
bool writeJson(const QString &path, const QJsonObject &value)
{
    QSaveFile output(path);
    const QByteArray bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return output.open(QIODevice::WriteOnly)
        && output.write(bytes) == bytes.size() && output.commit();
}

// loadScene writes this desktop-compatible singleton, including on paths
// that later reject malformed state. Restore the complete value, not a list
// of fields which would drift when ServerInfoStruct grows.
struct ServerInfoScope
{
    const ServerInfoStruct saved = ServerInfo;
    ~ServerInfoScope() { ServerInfo = saved; }
};
}

ClientRulesHost::ClientRulesHost(const QString &assets, const QString &work,
                               const QString &userData)
    : m_assets(assets), m_work(work), m_userData(userData)
{
}

ClientRulesHost::~ClientRulesHost() { shutdown(); }

QString ClientRulesHost::file(const char *name) const
{
    return QDir(m_work).filePath(QString::fromLatin1(name));
}

int ClientRulesHost::fail(const QString &path, const QString &reason, int status)
{
    QFile::remove(path);
    writeJson(path, {{QStringLiteral("schema_version"), 1},
        {QStringLiteral("known"), false}, {QStringLiteral("can_confirm"), false},
        {QStringLiteral("wire"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("reason"), reason}});
    return status;
}

void ClientRulesHost::releaseEngine()
{
    // Do not drain or destroy an application/Engine owned by another caller
    // when initialize() rejected the ownership check.
    if (m_application)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (m_ownsEngine) {
        EngineBootstrap::shutdown();
        m_ownsEngine = false;
    }
    m_application.reset();
}

int ClientRulesHost::initialize()
{
    const QString path = file("init.json");
    QFile::remove(path);
    if (m_phase == Phase::Closed)
        return fail(path, QStringLiteral("runtime_closed"), 64);
    try {
        if (m_phase == Phase::New) {
            if (QCoreApplication::instance() != nullptr || Sanguosha != nullptr)
                return fail(path, QStringLiteral("runtime_already_owned"), 3);
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv);
            QCoreApplication::setApplicationName(QStringLiteral("qsanguosha_client"));
            qputenv("QSAN_ASSET_ROOT", m_assets.toUtf8());
            qputenv("QSAN_USER_DATA_ROOT", m_userData.toUtf8());
            if (!QDir().mkpath(m_work) || !QDir().mkpath(m_userData))
                throw std::runtime_error("runtime_directories_unavailable");
            QString error;
            if (!QSanRuntimePaths::resolve(m_application->arguments(), &error))
                throw std::runtime_error(error.toStdString());
            m_ownsEngine = true;
            if (!EngineBootstrap::initialize(false, &error) || !EngineBootstrap::hasLuaState())
                throw std::runtime_error((QStringLiteral("engine_initialization_failed:") + error).toStdString());
            m_phase = Phase::Ready;
        }
        if (!writeJson(path, m_session.registry()))
            throw std::runtime_error("cannot_write_registry");
        return 0;
    } catch (const std::exception &error) {
        releaseEngine();
        m_phase = Phase::Closed;
        return fail(path, QString::fromUtf8(error.what()), 3);
    } catch (...) {
        releaseEngine();
        m_phase = Phase::Closed;
        return fail(path, QStringLiteral("engine_initialization_failed"), 3);
    }
}

int ClientRulesHost::evaluate()
{
    const QString path = file("result.json");
    QFile::remove(path);
    if (m_phase != Phase::Ready)
        return fail(path, QStringLiteral("engine_unavailable"), 3);
    if (m_streamEnabled)
        return fail(path, QStringLiteral("stream_snapshot_api_disabled"), 2);
    try {
        QFile input(file("request.json"));
        if (!input.open(QIODevice::ReadOnly))
            return fail(path, QStringLiteral("cannot_read_request"), 2);
        constexpr qint64 limit = 4 * 1024 * 1024;
        const QByteArray bytes = input.read(limit + 1);
        QJsonParseError error;
        const QJsonDocument request = QJsonDocument::fromJson(bytes, &error);
        if (bytes.size() > limit || error.error != QJsonParseError::NoError || !request.isObject())
            return fail(path, QStringLiteral("invalid_request_json"), 2);
        ServerInfoScope scope;
        const QJsonObject result = m_session.evaluate(request.object());
        if (!writeJson(path, result))
            return fail(path, QStringLiteral("cannot_write_result"), 5);
        return 0;
    } catch (const std::exception &error) {
        return fail(path, QString::fromUtf8(error.what()), 4);
    } catch (...) {
        return fail(path, QStringLiteral("native_rules_failed"), 4);
    }
}

int ClientRulesHost::stream()
{
    const QString path = file("stream-result.json");
    QFile::remove(path);
    if (m_phase != Phase::Ready)
        return fail(path, QStringLiteral("engine_unavailable"), 3);
    try {
        QFile source(file("stream.json"));
        if (!source.open(QIODevice::ReadOnly)) {
            m_ingress.invalidate();
            return fail(path, QStringLiteral("cannot_read_stream_operation"), 2);
        }
        const QByteArray bytes = source.read(4 * 1024 * 1024 + 1);
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        if (bytes.size() > 4 * 1024 * 1024 || parseError.error != QJsonParseError::NoError
            || !document.isObject()) {
            m_ingress.invalidate();
            return fail(path, QStringLiteral("invalid_stream_json"), 2);
        }
        const QJsonObject operation = document.object();
        const auto index = [](const QJsonValue &value) {
            const double number = value.toDouble(-1);
            return value.isDouble() && std::isfinite(number) && number >= 0
                && number <= std::numeric_limits<int>::max() && number == std::floor(number);
        };
        const QString action = operation.value(QStringLiteral("action")).toString();
        const int generation = index(operation.value(QStringLiteral("generation")))
            ? operation.value(QStringLiteral("generation")).toInt() : -1;
        QSet<QString> fields{QStringLiteral("schema_version"), QStringLiteral("action"),
                             QStringLiteral("generation")};
        if (action == QLatin1String("frame"))
            fields.unite({QStringLiteral("direction"), QStringLiteral("frame")});
        else if (action == QLatin1String("query"))
            fields.unite({QStringLiteral("revision"), QStringLiteral("request_id"), QStringLiteral("selection")});
        QString reason;
        bool success = false;
        QJsonObject response;
        bool shape = operation.value(QStringLiteral("schema_version")) == QJsonValue(1)
            && generation >= 0 && operation.size() == fields.size();
        for (auto it = operation.constBegin(); it != operation.constEnd(); ++it)
            shape = shape && fields.contains(it.key());
        if (!shape) {
            // Losing a current-generation frame would make all later state suspect.
            if (action == QLatin1String("frame")
                && generation == m_ingress.status().value(QStringLiteral("generation")).toInt())
                m_ingress.invalidate();
            reason = QStringLiteral("invalid_stream_operation");
        } else if (action == QLatin1String("reset")) {
            success = m_ingress.reset(generation,
                m_session.registry().value(QStringLiteral("rules_bundle")).toObject(), &reason);
            m_streamEnabled = m_streamEnabled || success;
        } else if (action == QLatin1String("frame")) {
            const QString direction = operation.value(QStringLiteral("direction")).toString();
            if (!operation.value(QStringLiteral("frame")).isString()
                || (direction != QLatin1String("incoming") && direction != QLatin1String("outgoing"))) {
                if (generation == m_ingress.status().value(QStringLiteral("generation")).toInt())
                    m_ingress.invalidate();
                reason = QStringLiteral("invalid_stream_frame");
            } else {
                success = m_ingress.acceptFrame(generation, direction == QLatin1String("outgoing"),
                    operation.value(QStringLiteral("frame")).toString().toUtf8(), &reason);
            }
        } else if (action == QLatin1String("query")) {
            QJsonObject query;
            if (!index(operation.value(QStringLiteral("revision")))
                || !operation.value(QStringLiteral("request_id")).isString()
                || !operation.value(QStringLiteral("selection")).isObject()) {
                reason = QStringLiteral("invalid_stream_query");
            } else if (m_ingress.prepareQuery(generation,
                    operation.value(QStringLiteral("revision")).toInt(),
                    operation.value(QStringLiteral("request_id")).toString(),
                    operation.value(QStringLiteral("selection")).toObject(), &query, &reason)) {
                ServerInfoScope scope;
                response.insert(QStringLiteral("evaluation"), m_session.evaluate(query));
                success = true;
            }
        } else if (action == QLatin1String("view")) {
            if (generation != m_ingress.status().value(QStringLiteral("generation")).toInt())
                reason = QStringLiteral("stream_stale_generation");
            else {
                response.insert(QStringLiteral("state"), m_ingress.view());
                success = true;
            }
        } else {
            reason = QStringLiteral("unknown_stream_action");
        }
        response.insert(QStringLiteral("schema_version"), 1);
        response.insert(QStringLiteral("success"), success);
        response.insert(QStringLiteral("reason"), reason);
        response.insert(QStringLiteral("status"), m_ingress.status());
        if (!success) {
            response.insert(QStringLiteral("can_confirm"), false);
            response.insert(QStringLiteral("wire"), QJsonValue(QJsonValue::Null));
        }
        if (!writeJson(path, response)) {
            m_ingress.invalidate();
            return fail(path, QStringLiteral("cannot_write_stream_result"), 5);
        }
        return 0;
    } catch (const std::exception &error) {
        m_ingress.invalidate();
        return fail(path, QString::fromUtf8(error.what()), 4);
    } catch (...) {
        m_ingress.invalidate();
        return fail(path, QStringLiteral("native_stream_failed"), 4);
    }
}

int ClientRulesHost::shutdown()
{
    if (m_phase == Phase::Closed)
        return 0;
    m_phase = Phase::Closed;
    m_ingress.invalidate();
    releaseEngine();
    // No successful registry/reply remains available after graceful shutdown.
    for (const char *name : {"init.json", "result.json", "request.json", "stream.json", "stream-result.json"})
        QFile::remove(file(name));
    return 0;
}
