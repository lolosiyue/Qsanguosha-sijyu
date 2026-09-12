#include "excel-bridge.h"
#include "excel-view.h"

#include "engine.h"
#include "protocol/session/session-payloads.h"
#include "server-config.h"
#include "settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>
#include <cmath>
#include <limits>

using namespace QSanProtocol;

namespace {
bool reject(QString *error, const QString &code)
{
    if (error) *error = code;
    return false;
}

bool decimal(const QJsonValue &input, quint64 *output, bool zero = true)
{
    if (!input.isString()) return false;
    const QString text = input.toString();
    if (text.isEmpty() || text.size() > 20 || (text.size() > 1 && text.startsWith(QLatin1Char('0'))))
        return false;
    quint64 value = 0;
    for (QChar c : text) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
        const uint digit = c.unicode() - ushort('0');
        if (value > (std::numeric_limits<quint64>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (!zero && value == 0) return false;
    *output = value;
    return true;
}

bool number(const QJsonValue &input, int minimum, int maximum, int *output)
{
    if (!input.isDouble()) return false;
    const double value = input.toDouble();
    if (!std::isfinite(value) || value < minimum || value > maximum || std::floor(value) != value)
        return false;
    *output = static_cast<int>(value);
    return true;
}

QString safeText(const QString &input, int limit = 2000)
{
    QString result;
    result.reserve(qMin(input.size(), limit));
    for (QChar c : input) {
        if (result.size() >= limit) break;
        if (c == QLatin1Char('\n') || c == QLatin1Char('\t') || c.unicode() >= 0x20)
            if (c.unicode() != 0x7f) result.append(c);
    }
    return result;
}

QJsonObject acknowledgement()
{
    return {{QStringLiteral("queued"), true}};
}

bool connecting(const ClientCore &core)
{
    const QString state = core.state()->connectionValue(QStringLiteral("state")).toString();
    return state == QLatin1String("connecting") || state == QLatin1String("reconnecting")
        || state == QLatin1String("handshake") || state == QLatin1String("synchronizing");
}
}

ExcelBridge::ExcelBridge(const ExcelBridgeOptions &options, QObject *parent)
    : QObject(parent), m_options(options), m_core(this), m_session(&m_core, this),
      m_interactions(&m_core), m_http(this), m_host(this)
{
    m_http.setHandler([this](const QString &method, const QString &path,
        const QUrlQuery &query, const QJsonObject &body) { return route(method, path, query, body); });
    connect(&m_http, &ExcelIpcServer::drained, this, [this]() {
        m_httpDrained = true;
        tryFinishStop();
    });
    connect(&m_session, &ClientLiveSession::stateChanged, this, &ExcelBridge::changed);
    connect(&m_session, &ClientLiveSession::interactionRequested, this,
        [this](const ProtocolMessage &message) {
            QString error;
            if (!m_interactions.beginRequest(message, &error)) {
                log(QStringLiteral("互動無法呈現：") + error);
                event(QStringLiteral("error"), {{QStringLiteral("code"), error}});
                // Leaving an unknown question pending is not a supported UI.
                m_session.disconnectGracefully();
            }
        });
    connect(&m_core, &ClientCore::requestStarted, this, [this](quint64) { changed(); });
    connect(&m_core, &ClientCore::responseAccepted, this, [this](quint64) { changed(); });
    connect(&m_core, &ClientCore::requestCancelled, this, [this](quint64, int) { changed(); });
    connect(&m_session, &ClientLiveSession::connectionChanged, this,
        [this](const QString &state) { event(QStringLiteral("connection"), {{QStringLiteral("state"), state}}); });
    connect(&m_session, &ClientLiveSession::presentationEvent, this,
        [this](int command, const QString &text, const QVariant &payload) {
            const QString line = ExcelView::presentationText(m_core, command, text, payload);
            if (!line.isEmpty()) log(line);
            ExcelView::playPresentationAudio(command, payload, m_options.assetRoot);
        });
    connect(&m_session, &ClientLiveSession::commandResult, this,
        [this](int command, bool success, const QString &message) {
            event(QStringLiteral("game_command_result"), {{QStringLiteral("command"), command},
                {QStringLiteral("ok"), success}, {QStringLiteral("message"), safeText(message)}});
        });
    connect(&m_session, &ClientLiveSession::fatalError, this,
        [this](int, const QString &code, const QString &message) {
            log(code + QStringLiteral(": ") + message);
            event(QStringLiteral("error"), {{QStringLiteral("code"), code},
                {QStringLiteral("message"), safeText(message)}});
            // A failed private startup must not leave a helper consuming a room.
            if (m_host.active()) m_host.stop();
        });
    connect(&m_session, &ClientLiveSession::sessionActive, this, [this](bool reconnected) {
        if (m_robotCount >= 0 && !reconnected) {
            AddRobotPayload robots;
            robots.fillRemaining = m_robotCount == 0;
            robots.count = m_robotCount;
            m_robotCount = -1;
            QString error;
            if (!m_session.sendControl(S_COMMAND_ADD_ROBOT, robots.toVariant(), &error)) log(error);
            if (m_privateGame && error.isEmpty()) {
                ReadyPayload ready;
                ready.ready = true;
                if (!m_session.sendControl(S_COMMAND_READY, ready.toVariant(), &error)) log(error);
            }
        }
        if (reconnected && m_trusted) {
            TrustPayload trust;
            trust.trusted = true;
            QString error;
            if (!m_session.sendControl(S_COMMAND_TRUST, trust.toVariant(), &error)) log(error);
        }
    });
    connect(&m_host, &LocalServerController::ready, this, [this]() {
        if (m_stopping) { m_host.stop(); return; }
        const QString endpoint = m_host.endpoint();
        const int separator = endpoint.lastIndexOf(QLatin1Char(':'));
        bool ok = false;
        const uint port = endpoint.mid(separator + 1).toUInt(&ok);
        if (separator < 0 || !ok || port == 0 || port > 65535) {
            log(QStringLiteral("helper_endpoint_invalid"));
            m_host.stop();
            return;
        }
        // Local hosting never connects through the public bind address.
        m_pendingConnection.host = QStringLiteral("127.0.0.1");
        m_pendingConnection.port = static_cast<quint16>(port);
        m_session.connectToServer(m_pendingConnection);
    });
    connect(&m_host, &LocalServerController::progress, this,
        [this](const QString &phase) { event(QStringLiteral("host_progress"), {{QStringLiteral("phase"), phase}}); });
    connect(&m_host, &LocalServerController::failed, this,
        [this](const QString &error) { log(error); event(QStringLiteral("error"), {{QStringLiteral("code"), safeText(error)}}); });
    connect(&m_host, &LocalServerController::stopped, this, [this](bool graceful) {
        if (m_stopping) finishStop(graceful);
        else event(QStringLiteral("host_stopped"), {{QStringLiteral("graceful"), graceful}});
    });
}

ExcelBridge::~ExcelBridge()
{
    m_http.close();
    m_session.disconnectGracefully();
    // Normal disposal happens only after LocalServerController acknowledged its
    // stop. Its own destructor remains the emergency owned-process safeguard.
}

bool ExcelBridge::start(QString *error)
{
    if (!Sanguosha) return reject(error, QStringLiteral("engine_unavailable"));
    if (m_options.session.isEmpty() || m_options.token.size() < 32)
        return reject(error, QStringLiteral("session_credentials_missing"));
    return m_http.listen(m_options.session, m_options.token, error);
}

quint16 ExcelBridge::port() const { return m_http.port(); }

void ExcelBridge::changed()
{
    if (m_revision == std::numeric_limits<quint64>::max()) {
        QTimer::singleShot(0, this, [this]() { stop(); }); return;
    }
    ++m_revision;
    m_selection = QJsonObject();
    event(QStringLiteral("state"));
}

void ExcelBridge::event(const QString &kind, const QJsonObject &data)
{
    if (m_sequence == std::numeric_limits<quint64>::max()) {
        QTimer::singleShot(0, this, [this]() { stop(); }); return;
    }
    m_events.append(QJsonObject{{QStringLiteral("sequence"), QString::number(++m_sequence)},
        {QStringLiteral("kind"), kind}, {QStringLiteral("data"), data}});
    while (m_events.size() > 256) m_events.removeAt(0);
}

void ExcelBridge::log(const QString &text)
{
    const QString line = safeText(text);
    m_logs.append(line);
    while (m_logs.size() > 200) m_logs.removeFirst();
    event(QStringLiteral("log"), {{QStringLiteral("text"), line}});
}

QJsonObject ExcelBridge::snapshot() const
{
    QJsonObject state = m_core.state()->toJson();
    // Presentation history can contain raw protocol payloads; only formatted,
    // authorized lines cross the workbook boundary.
    state.remove(QStringLiteral("presentation_events"));
    state.remove(QStringLiteral("flow_counts"));
    QJsonObject interaction;
    if (m_core.hasActiveRequest()) {
        interaction = m_interactions.requestJson();
        interaction.insert(QStringLiteral("generation"), QString::number(m_session.generation()));
        interaction.insert(QStringLiteral("revision"), QString::number(m_revision));
        interaction.insert(QStringLiteral("ui"), ExcelView::interactionUi(m_core, m_options.assetRoot, m_selection));
        const InteractionRequest &request = m_core.activeRequest();
        const qint64 remaining = request.deadlineMs > 0
            ? qMax<qint64>(0, request.deadlineMs - m_core.now()) : -1;
        interaction.insert(QStringLiteral("remaining_ms"), static_cast<double>(remaining));
    }
    return {{QStringLiteral("generation"), QString::number(m_session.generation())},
        {QStringLiteral("revision"), QString::number(m_revision)},
        {QStringLiteral("connection"), m_core.state()->connectionValue(QStringLiteral("state")).toString()},
        {QStringLiteral("request_id"), QString::number(m_core.activeRequestId())},
        {QStringLiteral("state"), state}, {QStringLiteral("interaction"), interaction},
        {QStringLiteral("view"), ExcelView::snapshotView(m_core, m_options.assetRoot, m_logs)},
        {QStringLiteral("selection"), m_selection}};
}

ExcelIpcServer::Reply ExcelBridge::route(const QString &method, const QString &path,
    const QUrlQuery &query, const QJsonObject &body)
{
    ExcelIpcServer::Reply reply;
    reply.body = {{QStringLiteral("api_version"), 1}, {QStringLiteral("session"), m_options.session}};
    if (path == QLatin1String("/v1/shutdown") && method == QLatin1String("POST")) {
        reply.body.insert(QStringLiteral("ok"), true);
        // Send the HTTP acknowledgement before closing sockets or the helper.
        QTimer::singleShot(0, this, [this]() { stop(); });
    } else if (path == QLatin1String("/v1/commands") && method == QLatin1String("POST")) {
        reply.body = command(body);
    } else if (path == QLatin1String("/v1/updates") && method == QLatin1String("GET")) {
        quint64 after = 0;
        if (!decimal(query.queryItemValue(QStringLiteral("after")), &after) || after > m_sequence) {
            reply.status = 400;
            reply.body.insert(QStringLiteral("error"), QStringLiteral("invalid_sequence"));
            return reply;
        }
        quint64 first = m_sequence + (m_sequence != std::numeric_limits<quint64>::max());
        if (!m_events.isEmpty()) decimal(m_events.first().toObject().value(QStringLiteral("sequence")), &first);
        const bool resync = after == 0 || (first > 0 && after < first - 1);
        QJsonArray events;
        for (const QJsonValue &value : m_events) {
            const QJsonObject item = value.toObject();
            quint64 sequence = 0;
            decimal(item.value(QStringLiteral("sequence")), &sequence);
            if (sequence > after && (!resync || item.value(QStringLiteral("kind")) != QLatin1String("audio")))
                events.append(item);
        }
        reply.body.insert(QStringLiteral("sequence"), QString::number(m_sequence));
        reply.body.insert(QStringLiteral("resync"), resync);
        reply.body.insert(QStringLiteral("events"), events);
        reply.body.insert(QStringLiteral("snapshot"), snapshot());
    } else {
        reply.status = 404;
        reply.body.insert(QStringLiteral("error"), QStringLiteral("unknown_endpoint"));
    }
    return reply;
}

bool ExcelBridge::checkInteractionEnvelope(const QJsonObject &body, QString *error) const
{
    if (!m_core.hasActiveRequest()) return reject(error, QStringLiteral("no_active_request"));
    quint64 generation = 0, revision = 0, request = 0;
    if (!decimal(body.value(QStringLiteral("generation")), &generation)
        || !decimal(body.value(QStringLiteral("revision")), &revision)
        || !decimal(body.value(QStringLiteral("args")).toObject().value(QStringLiteral("request_id")), &request, false))
        return reject(error, QStringLiteral("invalid_interaction_identity"));
    if (generation != m_session.generation() || revision != m_revision || request != m_core.activeRequestId())
        return reject(error, QStringLiteral("stale_interaction"));
    return true;
}

QJsonObject ExcelBridge::command(const QJsonObject &body)
{
    const QString id = body.value(QStringLiteral("id")).toString();
    QJsonObject reply{{QStringLiteral("api_version"), 1}, {QStringLiteral("session"), m_options.session},
        {QStringLiteral("id"), id}, {QStringLiteral("ok"), false}};
    quint64 sequence = 0;
    if (body.value(QStringLiteral("api_version")) != QJsonValue(1)
        || body.value(QStringLiteral("session")) != m_options.session
        || !decimal(body.value(QStringLiteral("id")), &sequence, false)
        || !body.value(QStringLiteral("name")).isString() || !body.value(QStringLiteral("args")).isObject()) {
        reply.insert(QStringLiteral("error"), QStringLiteral("invalid_command_envelope"));
        return reply;
    }
    const QByteArray fingerprint = QCryptographicHash::hash(
        QJsonDocument(body).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
    const auto previous = m_commands.constFind(id);
    if (previous != m_commands.constEnd()) {
        if (previous->fingerprint == fingerprint) return previous->response;
        reply.insert(QStringLiteral("error"), QStringLiteral("command_id_conflict"));
        return reply;
    }
    if (sequence <= m_largestCommand) {
        reply.insert(QStringLiteral("error"), QStringLiteral("command_id_expired"));
        return reply;
    }
    m_largestCommand = sequence;
    const QString name = body.value(QStringLiteral("name")).toString();
    QString error;
    if (m_stopping) error = QStringLiteral("session_stopping");
    if (error.isEmpty() && (name == QLatin1String("ready") || name == QLatin1String("chat")
        || name == QLatin1String("trust") || name == QLatin1String("add_robot")
        || name == QLatin1String("surrender") || name == QLatin1String("reconnect"))) {
        quint64 generation = 0;
        if (!decimal(body.value(QStringLiteral("generation")), &generation)
            || generation != m_session.generation()) error = QStringLiteral("stale_session");
    }
    if (error.isEmpty() && (name == QLatin1String("select") || name == QLatin1String("submit") || name == QLatin1String("cancel")))
        checkInteractionEnvelope(body, &error);
    QJsonObject result;
    if (error.isEmpty()) {
        try {
            result = execute(name, body.value(QStringLiteral("args")).toObject(), &error);
        } catch (...) {
            // Cache failure too: retries must never repeat an uncertain action.
            error = QStringLiteral("native_command_failed");
        }
    }
    reply.insert(QStringLiteral("ok"), error.isEmpty());
    reply.insert(QStringLiteral("result"), result);
    if (!error.isEmpty()) reply.insert(QStringLiteral("error"), safeText(error));
    m_commands.insert(id, CachedCommand{fingerprint, reply});
    m_commandOrder.append(id);
    while (m_commandOrder.size() > 128) m_commands.remove(m_commandOrder.takeFirst());
    return reply;
}

bool ExcelBridge::connectionOptions(const QJsonObject &args, ClientLiveSessionOptions *options,
    QString *error) const
{
    options->host = args.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1")).trimmed();
    int port = 9527;
    if (args.contains(QStringLiteral("port")) && !number(args.value(QStringLiteral("port")), 1, 65535, &port))
        return reject(error, QStringLiteral("invalid_port"));
    options->port = static_cast<quint16>(port);
    options->screenName = args.value(QStringLiteral("name")).toString(QStringLiteral("Excel")).trimmed();
    options->avatar = args.value(QStringLiteral("avatar")).toString(QStringLiteral("caocao"));
    if (options->host.isEmpty() || options->host.size() > 255 || options->screenName.isEmpty()
        || options->screenName.size() > 64 || options->avatar.size() > 128)
        return reject(error, QStringLiteral("invalid_connection_options"));
    options->maxPlayerCount = m_options.legacy ? 10 : 0;
    options->expectedCardCount = Sanguosha->getCardCount();
    options->expectedGameVersion = Sanguosha->getVersionNumber();
    options->expectedModName = Sanguosha->getMODName();
    options->expectedRulesBundle = Sanguosha->rulesBundleIdentity();
    options->fallbackToFreshSignup = false;
    return true;
}

bool ExcelBridge::startHost(const QJsonObject &args, QString *error)
{
    if (m_host.active() || m_session.isActive() || connecting(m_core))
        return reject(error, QStringLiteral("session_already_active"));
    if (!args.value(QStringLiteral("settings")).isObject()) return reject(error, QStringLiteral("settings_required"));
    if (!connectionOptions(args, &m_pendingConnection, error)) return false;
    const bool privateGame = args.value(QStringLiteral("private")).toBool(true);
    QVariantMap settings = args.value(QStringLiteral("settings")).toObject().toVariantMap();
    const QString mode = settings.value(QStringLiteral("GameMode"), QStringLiteral("05p")).toString();
    if (!Sanguosha->getAvailableModes().contains(mode)) return reject(error, QStringLiteral("unknown_game_mode"));
    const int seats = Sanguosha->getPlayerCount(mode);
    if (seats < 2 || (m_options.legacy && seats > 10)) return reject(error, QStringLiteral("frontend_player_limit"));
    settings.insert(QStringLiteral("GameMode"), mode);
    settings.insert(QStringLiteral("EnableAI"), true);
    settings.insert(QStringLiteral("WebSocketPort"), 0);
    settings.insert(QStringLiteral("BindAddress"), privateGame ? QStringLiteral("127.0.0.1") : QStringLiteral("any-ipv4"));
    if (privateGame) settings.insert(QStringLiteral("ServerPort"), 0);
    if (!settings.contains(QStringLiteral("ServerPort"))) settings.insert(QStringLiteral("ServerPort"), 9527);
    const QStringList invalid = validateServerConfigValues(settings);
    if (!invalid.isEmpty()) return reject(error, invalid.join(QStringLiteral("; ")));
    int robots = privateGame ? 0 : -1;
    if (args.contains(QStringLiteral("robots")) && !number(args.value(QStringLiteral("robots")), 0, qMax(0, seats - 1), &robots))
        return reject(error, QStringLiteral("invalid_robot_count"));
    LocalServerController::LaunchOptions launch;
    launch.helperPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        m_options.legacy ? QStringLiteral("QSanguoshaXPServer.exe") : QStringLiteral("QSanguoshaExcelServer.exe"));
    launch.assetRoot = m_options.assetRoot;
    launch.dataRoot = QDir(m_options.userDataRoot).filePath(QStringLiteral("server"));
    launch.settings = settings;
    m_host.configure(launch);
    m_privateGame = privateGame;
    m_robotCount = robots;
    if (!m_host.start(privateGame ? LocalServerController::Ownership::OwnedPrivate
                                 : LocalServerController::Ownership::OwnedHost, false, GameSessionConfig())) {
        m_robotCount = -1;
        return reject(error, QStringLiteral("host_start_failed"));
    }
    return true;
}

QJsonObject ExcelBridge::execute(const QString &name, const QJsonObject &args, QString *error)
{
    if (name == QLatin1String("catalog")) return ExcelView::catalog(m_core, m_options.assetRoot, m_options.legacy);
    if (name == QLatin1String("details")) return ExcelView::details(m_core, m_options.assetRoot,
        args.value(QStringLiteral("kind")).toString(), args.value(QStringLiteral("key")).toString(), error);
    if (name == QLatin1String("connect")) {
        if (m_host.active() || m_session.isActive() || connecting(m_core)) { reject(error, QStringLiteral("session_already_active")); return {}; }
        ClientLiveSessionOptions options;
        if (!connectionOptions(args, &options, error)) return {};
        m_session.connectToServer(options);
        return acknowledgement();
    }
    if (name == QLatin1String("host")) return startHost(args, error) ? acknowledgement() : QJsonObject();
    if (name == QLatin1String("disconnect")) {
        QTimer::singleShot(0, this, [this]() { stop(); });
        return acknowledgement();
    }
    if (name == QLatin1String("select") || name == QLatin1String("submit")) {
        if (!args.value(QStringLiteral("draft")).isObject()) { reject(error, QStringLiteral("draft_required")); return {}; }
        const QJsonObject draft = args.value(QStringLiteral("draft")).toObject();
        QJsonObject evaluated = m_interactions.evaluateDraft(draft, error);
        if (error && !error->isEmpty()) return {};
        evaluated.insert(QStringLiteral("draft"), draft);
        evaluated.insert(QStringLiteral("request_id"), QString::number(m_core.activeRequestId()));
        evaluated.insert(QStringLiteral("generation"), QString::number(m_session.generation()));
        evaluated.insert(QStringLiteral("revision"), QString::number(m_revision));
        evaluated.insert(QStringLiteral("ui"), ExcelView::interactionUi(m_core, m_options.assetRoot, evaluated));
        m_selection = evaluated;
        if (name == QLatin1String("select")) return {{QStringLiteral("selection"), evaluated}};
        if (!evaluated.value(QStringLiteral("can_confirm")).toBool()) {
            reject(error, evaluated.value(QStringLiteral("reason")).toString(QStringLiteral("selection_incomplete")));
            return {{QStringLiteral("selection"), evaluated}};
        }
        InteractionResponse response;
        if (!m_interactions.makeResponse(draft, &response, error)) return {};
        if (!m_session.submitInteractionResponse(std::move(response), error)) {
            if (error && error->isEmpty()) *error = QStringLiteral("response_rejected");
            return {};
        }
        return acknowledgement();
    }
    if (name == QLatin1String("cancel")) {
        InteractionResponse response = InteractionResponse::makeCancel(m_core.activeRequestId());
        response.command = m_core.activeRequest().command;
        if (!m_session.submitInteractionResponse(std::move(response), error) && error && error->isEmpty())
            *error = QStringLiteral("response_not_cancelable");
        return acknowledgement();
    }
    if (name == QLatin1String("reconnect")) {
        if (connecting(m_core)) { reject(error, QStringLiteral("connection_in_progress")); return {}; }
        if (m_session.generation() == 0) { reject(error, QStringLiteral("no_previous_session")); return {}; }
        m_session.reconnect();
        return acknowledgement();
    }
    if (!m_session.isActive()) { reject(error, QStringLiteral("not_connected")); return {}; }
    if (name == QLatin1String("ready")) {
        ReadyPayload payload;
        if (!args.value(QStringLiteral("ready")).isBool()) { reject(error, QStringLiteral("ready_boolean_required")); return {}; }
        payload.ready = args.value(QStringLiteral("ready")).toBool();
        m_session.sendControl(S_COMMAND_READY, payload.toVariant(), error);
    } else if (name == QLatin1String("chat")) {
        if (!args.value(QStringLiteral("text")).isString() || args.value(QStringLiteral("text")).toString().size() > 1000) {
            reject(error, QStringLiteral("invalid_chat")); return {};
        }
        ChatPayload payload;
        payload.text = safeText(args.value(QStringLiteral("text")).toString(), 1000);
        m_session.sendControl(S_COMMAND_SPEAK, payload.toVariant(), error);
    } else if (name == QLatin1String("trust")) {
        if (!args.value(QStringLiteral("enabled")).isBool()) { reject(error, QStringLiteral("trust_boolean_required")); return {}; }
        TrustPayload payload;
        payload.trusted = args.value(QStringLiteral("enabled")).toBool();
        if (m_session.sendControl(S_COMMAND_TRUST, payload.toVariant(), error)) m_trusted = payload.trusted;
    } else if (name == QLatin1String("add_robot")) {
        AddRobotPayload payload;
        if (!number(args.value(QStringLiteral("count")), 0, m_options.legacy ? 9 : 999, &payload.count)) {
            reject(error, QStringLiteral("invalid_robot_count")); return {};
        }
        payload.fillRemaining = payload.count == 0;
        m_session.sendControl(S_COMMAND_ADD_ROBOT, payload.toVariant(), error);
    } else if (name == QLatin1String("surrender")) {
        SurrenderRequestPayload payload;
        m_session.sendRequest(S_COMMAND_SURRENDER, payload.toVariant(), error);
    } else reject(error, QStringLiteral("unknown_command"));
    return acknowledgement();
}

void ExcelBridge::stop()
{
    if (m_stopping) return;
    m_stopping = true;
    m_http.drain();
    m_session.disconnectGracefully();
    if (m_host.active()) m_host.stop();
    else finishStop(true);
}

void ExcelBridge::finishStop(bool graceful)
{
    m_hostStopped = true;
    m_stopGraceful = graceful;
    tryFinishStop();
}

void ExcelBridge::tryFinishStop()
{
    if (!m_stopping || !m_hostStopped || !m_httpDrained) return;
    if (m_stopped) return;
    m_stopped = true;
    emit stopped(m_stopGraceful);
}
