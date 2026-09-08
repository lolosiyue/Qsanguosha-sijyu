#include "client-rules-host.h"
#include "card.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "runtime-paths.h"
#include "server-info.h"
#include "rules-bundle-identity.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <stdexcept>

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
        QJsonObject registry = m_session.registry();
        registry.insert(QStringLiteral("rules_bundle"), QJsonObject::fromVariantMap(QSanRulesIdentity::current()));
        if (!writeJson(path, registry))
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

int ClientRulesHost::shutdown()
{
    if (m_phase == Phase::Closed)
        return 0;
    m_phase = Phase::Closed;
    releaseEngine();
    // No successful registry/reply remains available after graceful shutdown.
    for (const char *name : {"init.json", "result.json", "request.json"})
        QFile::remove(file(name));
    return 0;
}
