#include "local-response-ui-controller.h"

#include "card.h"
#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "game-view.h"
#include "game-control-panel.h"
#include "local-response-ui-probe.h"
#include "local-response-ui-inspector.h"
#include "protocol/protocol-runtime.h"
#include "protocol/session/session-payloads.h"
#include "roomscene.h"
#include "server-info.h"
#include "settings.h"
#include <cstdio>
#include "structs.h"
#include "test-client-socket.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequence>
#include <QDialog>
#include <QPointer>
#include <QMainWindow>
#include <QMetaEnum>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>

using namespace QSanProtocol;

namespace {

QJsonArray *localUiQtMessages = nullptr;
QtMessageHandler previousMessageHandler = nullptr;
QMutex localUiMessageMutex;

QString qtMessageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("debug");
    case QtInfoMsg: return QStringLiteral("info");
    case QtWarningMsg: return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg: return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

void localUiMessageHandler(QtMsgType type, const QMessageLogContext &context,
    const QString &message)
{
    {
        QMutexLocker locker(&localUiMessageMutex);
        if (localUiQtMessages) {
            QJsonObject item;
            item.insert(QStringLiteral("type"), qtMessageTypeName(type));
            item.insert(QStringLiteral("message"), message);
            item.insert(QStringLiteral("category"), QString::fromUtf8(context.category));
            if (context.file)
                item.insert(QStringLiteral("file"), QString::fromUtf8(context.file));
            if (context.line > 0)
                item.insert(QStringLiteral("line"), context.line);
            localUiQtMessages->append(item);
        }
    }
    if (previousMessageHandler)
        previousMessageHandler(type, context, message);
}

QString argumentValue(const QStringList &arguments, const QString &name)
{
    const int index = arguments.indexOf(name);
    if (index >= 0 && index + 1 < arguments.size())
        return arguments.at(index + 1);
    const QString prefix = name + QLatin1Char('=');
    for (const QString &argument : arguments) {
        if (argument.startsWith(prefix))
            return argument.mid(prefix.size());
    }
    return QString();
}

QString jsonScalarText(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
        return QString::number(value.toInt());
    return value.toString();
}

QString clientStatusName(Client::Status status)
{
    const int index = Client::staticMetaObject.indexOfEnumerator("Status");
    const QMetaEnum statusEnum = Client::staticMetaObject.enumerator(index);
    const char *key = statusEnum.valueToKey(status);
    return key ? QString::fromLatin1(key) : QString::number(status);
}

bool jsonArraysEqual(const QJsonArray &left, const QJsonArray &right)
{
    return QJsonDocument(left).toJson(QJsonDocument::Compact)
        == QJsonDocument(right).toJson(QJsonDocument::Compact);
}

bool resolveAliases(const QJsonArray &aliases, const QMap<QString, int> &aliasToId,
    QList<int> *ids, QString *error)
{
    for (const QJsonValue &value : aliases) {
        const QString alias = value.toString();
        if (alias.isEmpty() || !aliasToId.contains(alias)) {
            *error = QStringLiteral("unknown card alias '%1'").arg(alias);
            return false;
        }
        ids->append(aliasToId.value(alias));
    }
    return true;
}

} // namespace

LocalResponseUiController::LocalResponseUiController(QObject *parent)
    : QObject(parent), m_timeoutMs(3000), m_screenshotOnFailure(false),
      m_mode(RunnerMode::Auto), m_stage(RunnerStage::Configuring),
      m_client(nullptr), m_socket(nullptr), m_mainWindow(nullptr), m_view(nullptr),
      m_scene(nullptr), m_probe(nullptr), m_inspector(nullptr), m_nextServerMessageId(1),
      m_nextActionIndex(0),
      m_replyProcessed(false), m_closing(false), m_closedByUser(false),
      m_inspectExitCode(Passed)
{
}

LocalResponseUiController::~LocalResponseUiController()
{
    delete m_probe;
    delete m_mainWindow;
}

int LocalResponseUiController::run(const QStringList &arguments)
{
    LocalResponseUiController *controller = new LocalResponseUiController(qApp);
    localUiQtMessages = &controller->m_qtMessages;
    previousMessageHandler = qInstallMessageHandler(localUiMessageHandler);
    QString error;
    if (!controller->configure(arguments, &error)) {
        qCritical().noquote() << error;
        if (!controller->m_reportPath.isEmpty()) {
            controller->m_report.insert(QStringLiteral("schema_version"), 1);
            controller->m_report.insert(QStringLiteral("result"), QStringLiteral("FAIL"));
            controller->m_report.insert(QStringLiteral("failed_stage"), QStringLiteral("schema"));
            controller->m_report.insert(QStringLiteral("error"), error);
            controller->m_report.insert(QStringLiteral("qt_messages"), controller->m_qtMessages);
            QString writeError;
            controller->writeReport(&writeError);
        }
        qInstallMessageHandler(previousMessageHandler);
        previousMessageHandler = nullptr;
        localUiQtMessages = nullptr;
        delete controller;
        return InvalidCase;
    }

    if (controller->m_mode == RunnerMode::Inspect)
        qApp->setQuitOnLastWindowClosed(false);

    QTimer::singleShot(0, controller, &LocalResponseUiController::execute);
    const int exitCode = qApp->exec();
    qInstallMessageHandler(previousMessageHandler);
    previousMessageHandler = nullptr;
    localUiQtMessages = nullptr;
    delete controller;
    return exitCode;
}

bool LocalResponseUiController::configure(const QStringList &arguments, QString *error)
{
    m_casePath = argumentValue(arguments, QStringLiteral("--local-response-ui-case"));
    m_reportPath = argumentValue(arguments, QStringLiteral("--local-response-ui-report"));
    m_screenshotDir = argumentValue(arguments, QStringLiteral("--screenshot-dir"));
    if (arguments.contains(QStringLiteral("--inspect-ui")))
        m_mode = RunnerMode::Inspect;
    else if (arguments.contains(QStringLiteral("--show-ui")))
        m_mode = RunnerMode::ShowAuto;
    else
        m_mode = RunnerMode::Auto;
    m_screenshotOnFailure = arguments.contains(QStringLiteral("--screenshot-on-failure"));

    const QString timeoutText = argumentValue(arguments, QStringLiteral("--case-timeout-ms"));
    if (!timeoutText.isEmpty()) {
        bool ok = false;
        const int timeout = timeoutText.toInt(&ok);
        if (!ok || timeout <= 0) {
            *error = QStringLiteral("--case-timeout-ms must be a positive integer");
            return false;
        }
        m_timeoutMs = timeout;
    }

    if (m_casePath.isEmpty()) {
        *error = QStringLiteral("--local-response-ui-case requires a path");
        return false;
    }
    if (!LocalResponseUiCase::load(m_casePath, &m_case, error))
        return false;

    if (m_reportPath.isEmpty())
        m_reportPath = QDir::current().filePath(QStringLiteral("artifacts/skill-ui/%1/report.json").arg(m_case.name()));
    if (m_screenshotDir.isEmpty())
        m_screenshotDir = QFileInfo(m_reportPath).absolutePath();
    return true;
}

bool LocalResponseUiController::bootstrap(QString *error)
{
    setStage(RunnerStage::Bootstrapping);
    const QJsonArray packages = m_case.root().value(QStringLiteral("packages")).toArray();
    for (const QJsonValue &value : packages) {
        const QString packageName = value.toString();
        if (!Sanguosha->getPackage(packageName)) {
            *error = QStringLiteral("required package '%1' is not loaded").arg(packageName);
            return false;
        }
    }

    const QJsonObject bootstrapObject = m_case.bootstrap();
    // Fixture settings are process-local: never persist them to the user's config.
    const QJsonObject settings = bootstrapObject.value(QStringLiteral("settings")).toObject();
    const QMap<QString, bool *> settingFields {
        { QStringLiteral("EnableHotKey"), &Config.EnableHotKey },
        { QStringLiteral("EnableIntellectualSelection"), &Config.EnableIntellectualSelection },
        { QStringLiteral("EnableAutoTarget"), &Config.EnableAutoTarget },
        { QStringLiteral("FreeAssignSelf"), &Config.FreeAssignSelf }
    };
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        if (!settingFields.contains(it.key()) || !it.value().isBool()) {
            *error = QStringLiteral("unsupported boolean fixture setting '%1'").arg(it.key());
            return false;
        }
        *settingFields.value(it.key()) = it.value().toBool();
    }
    const QString modeId = bootstrapObject.value(QStringLiteral("mode")).toString(QStringLiteral("02p"));
    const GameModeStruct mode = Sanguosha->getGameMode(modeId);
    if (!mode.isValid()) {
        *error = QStringLiteral("unknown bootstrap mode '%1'").arg(modeId);
        return false;
    }
    ServerInfo.GameMode = modeId;
    Config.GameMode = modeId;

    const QJsonObject selfObject = bootstrapObject.value(QStringLiteral("self")).toObject();
    const QString selfName = selfObject.value(QStringLiteral("object_name")).toString(QStringLiteral("self"));
    if (selfName.isEmpty()) {
        *error = QStringLiteral("bootstrap self object_name is empty");
        return false;
    }

    m_socket = new TestClientSocket;
    m_client = new Client(this, QString(), m_socket);

    const ProtocolCodecRouter router;
    auto injectSessionMessage = [this, &router](ProtocolMessage message) {
        message.messageId = m_nextServerMessageId++;
        QString encodeError;
        const QByteArray wire = router.encode(message, &encodeError);
        if (wire.isEmpty())
            return false;
        m_socket->injectServerPacket(QString::fromUtf8(wire));
        return true;
    };

    ServerHelloPayload helloPayload;
    helloPayload.gameVersion = Sanguosha->getVersion();
    helloPayload.modName = Sanguosha->getMODName();
    helloPayload.cardCount = Sanguosha->getCardCount();
    ProtocolMessage hello;
    hello.type = ProtocolMessageType::Notification;
    hello.source = ProtocolEndpoint::Lobby;
    hello.destination = ProtocolEndpoint::Client;
    hello.command = S_COMMAND_CHECK_VERSION;
    hello.hasPayload = true;
    hello.payload = helloPayload.toVariant();
    if (!injectSessionMessage(hello)) {
        *error = QStringLiteral("cannot inject Protocol V2 server hello");
        return false;
    }
    m_client->signup();

    SignupReplyPayload signupPayload;
    signupPayload.accepted = true;
    signupPayload.playerId = selfName;
    signupPayload.roomId = 0;
    ProtocolMessage signupReply;
    signupReply.type = ProtocolMessageType::Reply;
    signupReply.source = ProtocolEndpoint::Lobby;
    signupReply.destination = ProtocolEndpoint::Client;
    signupReply.command = S_COMMAND_SIGNUP;
    signupReply.replyTo = 1;
    signupReply.hasPayload = true;
    signupReply.payload = signupPayload.toVariant();
    if (!injectSessionMessage(signupReply)) {
        *error = QStringLiteral("cannot inject Protocol V2 signup reply");
        return false;
    }

    SetupPayload setupPayload;
    setupPayload.serverName = QStringLiteral("local-response-ui");
    setupPayload.gameMode = modeId;
    setupPayload.gameRuleMode = QStringLiteral("normal");
    // Inspect waits for a human; the production countdown must not auto-reply
    // while the user reads the prompt or operates the keyboard panel.
    setupPayload.operationTimeout = m_mode == RunnerMode::Inspect ? 0 : 15;
    setupPayload.nullificationCountdown = 8;
    setupPayload.playerCount = mode.player_count;
    ProtocolMessage setup;
    setup.type = ProtocolMessageType::Notification;
    setup.source = ProtocolEndpoint::Lobby;
    setup.destination = ProtocolEndpoint::Client;
    setup.command = S_COMMAND_SETUP;
    setup.hasPayload = true;
    setup.payload = setupPayload.toVariant();
    if (!injectSessionMessage(setup)) {
        *error = QStringLiteral("cannot inject Protocol V2 setup");
        return false;
    }
    m_socket->clearSentPackets();

    if (!Self) {
        *error = QStringLiteral("bootstrap session did not create the local player");
        return false;
    }
    Self->setObjectName(selfName);
    Self->setScreenName(selfObject.value(QStringLiteral("screen_name")).toString(QStringLiteral("Tester")));

    m_mainWindow = new QMainWindow;
    if (m_mode == RunnerMode::Auto)
        m_mainWindow->setAttribute(Qt::WA_DontShowOnScreen);
    // Flush phase boundaries so a parent-process timeout retains useful evidence.
    std::fprintf(stderr, "LOCAL_UI bootstrap: create RoomScene\n");
    std::fflush(stderr);
    m_scene = new RoomScene(m_mainWindow);
    std::fprintf(stderr, "LOCAL_UI bootstrap: RoomScene ready\n");
    std::fflush(stderr);
    m_view = new FitView(nullptr, m_mainWindow);
    m_view->setFrameStyle(QFrame::NoFrame);
    m_mainWindow->setCentralWidget(m_view);
    m_mainWindow->resize(1280, 720);
    m_mainWindow->show();
    flushEvents();
    m_view->setScene(m_scene);
    m_view->refit();
    flushEvents();

    const QJsonArray players = bootstrapObject.value(QStringLiteral("players")).toArray();
    for (const QJsonValue &value : players) {
        const QJsonObject player = value.toObject();
        const QString name = player.value(QStringLiteral("object_name")).toString();
        const QString screenName = player.value(QStringLiteral("screen_name")).toString(name);
        const QString avatar = player.value(QStringLiteral("general")).toString(QStringLiteral("caocao"));
        if (name.isEmpty()) {
            *error = QStringLiteral("bootstrap player object_name is empty");
            return false;
        }
        if (m_client->findChild<ClientPlayer *>(name)) {
            *error = QStringLiteral("duplicate bootstrap player '%1'").arg(name);
            return false;
        }
        // The current wire contract uses named fields and plain screen-name text.
        const QVariantMap body {
            { QStringLiteral("schema_version"), 1 },
            { QStringLiteral("player_name"), name },
            { QStringLiteral("screen_name"), screenName },
            { QStringLiteral("avatar"), avatar }
        };
        if (!injectNotification(S_COMMAND_ADD_PLAYER, body, error))
            return false;
    }

    auto setProperty = [this, error](const QString &name, const QString &property,
                                    const QString &value) {
        const QVariantMap body {
            { QStringLiteral("schema_version"), 1 },
            { QStringLiteral("action"), QStringLiteral("property") },
            { QStringLiteral("player_name"), name },
            { QStringLiteral("property_name"), property },
            { QStringLiteral("string_value"), value }
        };
        return injectNotification(S_COMMAND_SET_PROPERTY, body, error);
    };
    auto setProperties = [this, error, &setProperty](const QString &name, const QJsonObject &player) {
        static const QMap<QString, QString> properties {
            { QStringLiteral("general"), QStringLiteral("general") },
            { QStringLiteral("general2"), QStringLiteral("general2") },
            { QStringLiteral("hp"), QStringLiteral("hp") },
            { QStringLiteral("max_hp"), QStringLiteral("maxhp") },
            { QStringLiteral("maxhp"), QStringLiteral("maxhp") },
            { QStringLiteral("phase"), QStringLiteral("phase") },
            { QStringLiteral("role"), QStringLiteral("role") },
            { QStringLiteral("alive"), QStringLiteral("alive") },
            { QStringLiteral("state"), QStringLiteral("state") }
        };
        for (auto property = properties.cbegin(); property != properties.cend(); ++property) {
            if (!player.contains(property.key()))
                continue;
            if (!setProperty(name, property.value(), jsonScalarText(player.value(property.key()))))
                return false;
        }
        const QJsonObject marks = player.value(QStringLiteral("marks")).toObject();
        for (auto mark = marks.constBegin(); mark != marks.constEnd(); ++mark) {
            if (!injectNotification(S_COMMAND_SET_MARK,
                    JsonArray() << name << mark.key() << mark.value().toInt(), error))
                return false;
        }
        for (const QJsonValue &flag : player.value(QStringLiteral("flags")).toArray()) {
            if (!setProperty(name, QStringLiteral("flags"), flag.toString()))
                return false;
        }
        return true;
    };
    JsonArray seats;
    seats << selfName;
    for (const QJsonValue &value : players)
        seats << value.toObject().value(QStringLiteral("object_name")).toString();
    // Client::arrangeSeats requires all names to resolve before it sets seats.
    // A rejected fixture must fail here instead of reaching that native callback.
    for (const QVariant &seat : seats) {
        if (!m_client->findChild<ClientPlayer *>(seat.toString())) {
            *error = QStringLiteral("bootstrap seat player '%1' was not created").arg(seat.toString());
            return false;
        }
    }
    if (!injectNotification(S_COMMAND_ARRANGE_SEATS, seats, error))
        return false;

    // The production client registers its EngineRuntimeContext only when the
    // room sends GAME_START. All request UI paths read the current RoomState.
    if (!injectNotification(S_COMMAND_GAME_START, JsonArray(), error))
        return false;

    if (!setProperties(selfName, selfObject))
        return false;
    for (const QJsonValue &value : players) {
        const QJsonObject player = value.toObject();
        if (!setProperties(player.value(QStringLiteral("object_name")).toString(), player))
            return false;
    }

    const QJsonArray skills = selfObject.value(QStringLiteral("skills")).toArray();
    for (const QJsonValue &value : skills) {
        const QString skillName = value.toString();
        if (!Sanguosha->getSkill(skillName)) {
            *error = QStringLiteral("bootstrap skill '%1' is not loaded").arg(skillName);
            return false;
        }
        if (!injectNotification(S_COMMAND_ATTACH_SKILL, JsonArray() << selfName << skillName, error))
            return false;
    }

    if (!resolveCards(error))
        return false;
    // Count-only opponent hands reproduce the recipient-redacted wire path.
    for (const QJsonValue &value : players) {
        const QJsonObject player = value.toObject();
        const int count = player.value(QStringLiteral("hidden_hand_count")).toInt();
        if (count <= 0)
            continue;
        CardsMoveStruct move;
        for (int i = 0; i < count; ++i) move.card_ids << -1;
        move.from_place = Player::PlaceTable;
        move.to_place = Player::PlaceHand;
        move.to_player_name = player.value(QStringLiteral("object_name")).toString();
        move.open = false;
        const JsonArray body = JsonArray() << 2 << move.toVariant();
        if (!injectNotification(S_COMMAND_LOSE_CARD, body, error)
            || !injectNotification(S_COMMAND_GET_CARD, body, error))
            return false;
    }
    m_probe = new LocalResponseUiProbe(m_client, m_scene, m_aliasToId);
    connect(m_socket, &TestClientSocket::packetSent, this, [this]() {
        if (m_mode == RunnerMode::Inspect)
            QTimer::singleShot(0, this, &LocalResponseUiController::processInspectReply);
    });
    flushEvents();
    return true;
}

bool LocalResponseUiController::resolveCards(QString *error)
{
    const QJsonArray cards = m_case.bootstrap().value(QStringLiteral("cards")).toArray();
    QSet<int> usedIds;
    QMap<Player::Place, QList<int>> moves;
    QJsonArray resolved;

    for (const QJsonValue &value : cards) {
        const QJsonObject cardObject = value.toObject();
        const QString alias = cardObject.value(QStringLiteral("alias")).toString();
        const QString name = cardObject.value(QStringLiteral("name")).toString();
        const QString suit = cardObject.value(QStringLiteral("suit")).toString().toLower();
        const int number = cardObject.value(QStringLiteral("number")).toInt(-1);
        int resolvedId = -1;
        const Card *resolvedCard = nullptr;
        for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
            const Card *candidate = Sanguosha->getEngineCard(id);
            if (!candidate || usedIds.contains(id))
                continue;
            if (candidate->objectName() == name
                && candidate->getSuitString().toLower() == suit
                && candidate->getNumber() == number) {
                resolvedId = id;
                resolvedCard = candidate;
                break;
            }
        }
        const QString owner = cardObject.value(QStringLiteral("owner")).toString(Self->objectName());
        if (owner != Self->objectName()) {
            *error = QStringLiteral("card '%1' owner '%2' is unsupported; MVP cards must belong to self")
                .arg(alias, owner);
            return false;
        }
        if (alias.isEmpty() || m_aliasToId.contains(alias) || resolvedId < 0) {
            *error = QStringLiteral("cannot resolve exact physical card '%1' (%2 %3 %4)")
                .arg(alias, name, suit).arg(number);
            return false;
        }
        usedIds.insert(resolvedId);
        m_aliasToId.insert(alias, resolvedId);
        const QString placeName = cardObject.value(QStringLiteral("place")).toString(QStringLiteral("hand"));
        if (placeName != QStringLiteral("hand") && placeName != QStringLiteral("equip")) {
            *error = QStringLiteral("card '%1' place '%2' is unsupported; MVP cards must be in hand or equip")
                .arg(alias, placeName);
            return false;
        }
        const Player::Place place = placeName == QStringLiteral("equip")
            ? Player::PlaceEquip : Player::PlaceHand;
        moves[place] << resolvedId;

        QJsonObject item;
        item.insert(QStringLiteral("alias"), alias);
        item.insert(QStringLiteral("card_id"), resolvedId);
        item.insert(QStringLiteral("object_name"), resolvedCard->objectName());
        item.insert(QStringLiteral("suit"), resolvedCard->getSuitString());
        item.insert(QStringLiteral("number"), resolvedCard->getNumber());
        resolved.append(item);
    }

    JsonArray movePayload;
    movePayload << 1;
    for (auto it = moves.cbegin(); it != moves.cend(); ++it) {
        CardsMoveStruct move;
        move.card_ids = it.value();
        move.from_place = Player::PlaceTable;
        move.to_place = it.key();
        move.to_player_name = Self->objectName();
        move.open = true;
        movePayload << move.toVariant();
    }
    if (movePayload.size() > 1) {
        if (!injectNotification(S_COMMAND_LOSE_CARD, movePayload, error)
            || !injectNotification(S_COMMAND_GET_CARD, movePayload, error))
            return false;
    }

    QJsonObject bootstrapReport;
    bootstrapReport.insert(QStringLiteral("resolved_cards"), resolved);
    m_report.insert(QStringLiteral("bootstrap"), bootstrapReport);
    return true;
}

bool LocalResponseUiController::injectNotification(CommandType command, const QVariant &body,
    QString *error)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = m_nextServerMessageId++;
    message.command = command;
    message.hasPayload = !body.isNull();
    message.payload = body;
    QString encodeError;
    const QByteArray wire = ProtocolCodecRouter().encode(message, &encodeError);
    if (wire.isEmpty()) {
        // Propagate the first failure to execute()'s existing stage/error report.
        *error = QStringLiteral("cannot encode notification %1: %2")
            .arg(static_cast<int>(command)).arg(encodeError);
        return false;
    }
    m_socket->injectServerPacket(QString::fromUtf8(wire));
    return true;
}

bool LocalResponseUiController::prepareRequest(QString *error)
{
    for (const QJsonValue &value : m_case.bootstrap().value(QStringLiteral("notifications")).toArray()) {
        const QJsonObject notification = value.toObject();
        CommandType command;
        if (!LocalResponseUiCase::commandFromName(notification.value(QStringLiteral("command")).toString(), &command)) {
            *error = QStringLiteral("unsupported bootstrap notification command");
            return false;
        }
        if (!injectNotification(command, notification.value(QStringLiteral("body")).toVariant(), error))
            return false;
    }
    flushEvents();
    const QJsonObject request = m_case.request();
    if (request.value(QStringLiteral("api")).toString() != QStringLiteral("askForAG"))
        return true;

    const QJsonObject args = request.value(QStringLiteral("args")).toObject();
    QList<int> cardIds;
    QList<int> disabledIds;
    if (!resolveAliases(args.value(QStringLiteral("cards")).toArray(), m_aliasToId,
            &cardIds, error)
        || !resolveAliases(args.value(QStringLiteral("disabled_cards")).toArray(), m_aliasToId,
            &disabledIds, error)) {
        return false;
    }
    if (!injectNotification(S_COMMAND_FILL_AMAZING_GRACE,
            JsonArray() << JsonUtils::toJsonArray(cardIds) << JsonUtils::toJsonArray(disabledIds), error))
        return false;
    flushEvents();
    return true;
}

void LocalResponseUiController::execute()
{
    m_report.insert(QStringLiteral("schema_version"), 1);
    m_report.insert(QStringLiteral("case"), m_case.name());
    m_report.insert(QStringLiteral("result"), QStringLiteral("FAIL"));
    m_report.insert(QStringLiteral("mode"),
        m_mode == RunnerMode::Inspect ? QStringLiteral("inspect") : QStringLiteral("auto"));
    if (m_mode == RunnerMode::Inspect) {
        m_report.insert(QStringLiteral("presentation_result"), QStringLiteral("PENDING"));
        m_report.insert(QStringLiteral("reply_received"), false);
        m_report.insert(QStringLiteral("reply_result"), QStringLiteral("NOT_RECEIVED"));
        m_report.insert(QStringLiteral("closed_by_user"), false);
    }
    m_report.insert(QStringLiteral("qt_messages"), m_qtMessages);
    m_report.insert(QStringLiteral("artifacts"), QJsonObject());

    QString error;
    if (!bootstrap(&error)) {
        finish(SetupFailed, QStringLiteral("bootstrap"), error);
        return;
    }

    ProtocolMessage logicalRequest;
    QString commandName;
    QVariant requestBody;
    if (!m_case.makeRequestMessage(&logicalRequest, &commandName, &requestBody,
            m_aliasToId, &error)) {
        finish(InvalidCase, QStringLiteral("request"), error);
        return;
    }
    if (!prepareRequest(&error)) {
        finish(InvalidCase, QStringLiteral("request_setup"), error);
        return;
    }

    const int wireVersionValue = m_case.request()
        .value(QStringLiteral("wire_version")).toInt(2);
    if (wireVersionValue != 2) {
        finish(InvalidCase, QStringLiteral("request"),
            QStringLiteral("request.wire_version must be 2"));
        return;
    }
    if (logicalRequest.messageId < m_nextServerMessageId)
        logicalRequest.messageId = m_nextServerMessageId;
    m_nextServerMessageId = logicalRequest.messageId + 1;
    const ProtocolCodecRouter router;
    const QByteArray wireBytes = router.encode(logicalRequest, &error);
    if (wireBytes.isEmpty()) {
        finish(InvalidCase, QStringLiteral("request"),
            QStringLiteral("cannot encode request wire: %1").arg(error));
        return;
    }

    ProtocolMessage normalizedRequest;
    const ProtocolDecodeResult decoded = router.decode(wireBytes, &normalizedRequest);
    if (!decoded.success) {
        finish(InvalidCase, QStringLiteral("request"),
            QStringLiteral("cannot decode request wire: %1").arg(decoded.detail));
        return;
    }
    QJsonObject requestReport;
    requestReport.insert(QStringLiteral("command"), commandName);
    requestReport.insert(QStringLiteral("global_serial"),
        QString::number(normalizedRequest.messageId));
    requestReport.insert(QStringLiteral("body"), QJsonValue::fromVariant(requestBody));
    requestReport.insert(QStringLiteral("wire_version"), wireVersionValue);
    requestReport.insert(QStringLiteral("wire_json"), QString::fromUtf8(wireBytes));
    m_report.insert(QStringLiteral("request"), requestReport);
    m_requestPacketJson = QString::fromUtf8(wireBytes);

    if (m_mode == RunnerMode::Inspect)
        createInspector(commandName, static_cast<int>(normalizedRequest.messageId));

    QTimer::singleShot(0, this, &LocalResponseUiController::injectRequest);
}

void LocalResponseUiController::injectRequest()
{
    setStage(RunnerStage::InjectingRequest);

    m_snapshots.insert(QStringLiteral("before_request"), m_probe->snapshot());
    m_socket->clearSentPackets();
    // A request may enter QDialog::exec() synchronously. Queue the observer first
    // so its keyboard actions can run inside that nested event loop as well.
    QTimer::singleShot(0, this, &LocalResponseUiController::presentRequest);
    m_socket->injectServerPacket(m_requestPacketJson);
}

void LocalResponseUiController::presentRequest()
{
    const QJsonObject expectedPresented = m_case.presentedExpectation();
    QString expectedStatus;
    if (expectedPresented.value(QStringLiteral("client")).isObject())
        expectedStatus = expectedPresented.value(QStringLiteral("client")).toObject()
            .value(QStringLiteral("status")).toString();
    if (expectedStatus.isEmpty())
        expectedStatus = expectedPresented.value(QStringLiteral("client_status")).toString();
    const bool requiresSettledSurfaceCards = expectedPresented
        .value(QStringLiteral("surface_cards")).isObject();
    const bool presented = waitForCondition([this, expectedStatus, requiresSettledSurfaceCards]() {
        if (!expectedStatus.isEmpty()
            && clientStatusName(m_client->getStatus()) != expectedStatus) {
            return false;
        }
        if (requiresSettledSurfaceCards && !m_probe->surfaceCardsSettled())
            return false;
        const QJsonObject dialogExpectation = m_case.presentedExpectation()
            .value(QStringLiteral("dialog")).toObject();
        if (dialogExpectation.value(QStringLiteral("open")).toBool(false))
            return m_probe->snapshot().value(QStringLiteral("dialog")).toObject()
                .value(QStringLiteral("open")).toBool();
        return true;
    }, m_timeoutMs);
    if (!presented) {
        finish(PresentationTimeout, QStringLiteral("expect_presented"),
            QStringLiteral("request presentation timed out"));
        return;
    }

    flushEvents();
    const QJsonObject presentedSnapshot = m_probe->snapshot();
    m_snapshots.insert(QStringLiteral("presented"), presentedSnapshot);
    setStage(RunnerStage::Presented);
    if (m_mode != RunnerMode::Inspect
        && (m_screenshotOnFailure || m_mode != RunnerMode::Auto)) {
        captureScreenshot(QStringLiteral("presented"));
    }
    if (!validateSnapshot(expectedPresented, presentedSnapshot,
        QStringLiteral("expect_presented"))) {
        if (m_mode == RunnerMode::Inspect) {
            m_report.insert(QStringLiteral("presentation_result"), QStringLiteral("FAIL"));
            if (m_inspector)
                m_inspector->setPresentationResult(QStringLiteral("FAIL"));
            captureScreenshot(QStringLiteral("presented"));
            setInspectFailure(AssertionFailed, QStringLiteral("expect_presented"),
                QStringLiteral("presentation assertions failed"));
            return;
        }
        finish(AssertionFailed, QStringLiteral("expect_presented"),
            QStringLiteral("presentation assertions failed"));
        return;
    }

    if (m_mode == RunnerMode::Inspect) {
        m_report.insert(QStringLiteral("presentation_result"), QStringLiteral("PASS"));
        setStage(RunnerStage::AwaitingManualInput);
        updateInspectorSnapshot();
        if (m_inspector) {
            m_inspector->setPresentationResult(QStringLiteral("PASS"));
            m_inspector->setFinalResult(QStringLiteral("Awaiting manual input"));
        }
        // Inspect the actual request surface, not the inspector's action panel.
        // Native choices such as ChooseGeneralDialog own their keyboard focus.
        QWidget *requestWindow = QApplication::activeModalWidget();
        if (!requestWindow || requestWindow == m_inspector) {
            requestWindow = m_mainWindow;
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (qobject_cast<QDialog *>(widget) && widget->isVisible()
                    && widget != m_inspector) {
                    requestWindow = widget;
                    break;
                }
            }
        }
        if (requestWindow) {
            requestWindow->raise();
            requestWindow->activateWindow();
            QWidget *focus = requestWindow == m_mainWindow ? m_view
                : requestWindow->focusWidget();
            if (focus)
                focus->setFocus(Qt::OtherFocusReason);
        }
        captureScreenshot(QStringLiteral("presented"));
        if (!persistReport(QStringLiteral("INSPECTING")))
            finish(InternalError, QStringLiteral("report"),
                QStringLiteral("could not persist inspect report"));
        return;
    }

    QString error;
    if (!runActions(&error)) {
        finish(AssertionFailed, QStringLiteral("actions"), error);
        return;
    }

    if (!waitForCondition([this]() { return !m_socket->sentPackets().isEmpty(); }, m_timeoutMs)) {
        finish(ReplyTimeout, QStringLiteral("reply"), QStringLiteral("reply timed out"));
        return;
    }
    if (!validateReply(&error)) {
        finish(AssertionFailed, QStringLiteral("expect_reply"), error);
        return;
    }

    flushEvents();
    const QJsonObject finalSnapshot = m_probe->snapshot();
    m_snapshots.insert(QStringLiteral("final"), finalSnapshot);
    if (!validateSnapshot(m_case.finalExpectation(), finalSnapshot,
        QStringLiteral("expect_final"))) {
        finish(AssertionFailed, QStringLiteral("expect_final"),
            QStringLiteral("final-state assertions failed"));
        return;
    }
    finish(Passed);
}

bool LocalResponseUiController::runActions(QString *error)
{
    const QJsonArray actions = m_case.actions();
    for (int index = 0; index < actions.size(); ++index) {
        if (!runAction(index, error))
            return false;
    }
    return true;
}

bool LocalResponseUiController::pressKey(const QJsonObject &action, QString *error)
{
    static const QMap<QString, int> keys {
        { QStringLiteral("Tab"), Qt::Key_Tab }, { QStringLiteral("Backtab"), Qt::Key_Backtab },
        { QStringLiteral("Left"), Qt::Key_Left }, { QStringLiteral("Right"), Qt::Key_Right },
        { QStringLiteral("Up"), Qt::Key_Up }, { QStringLiteral("Down"), Qt::Key_Down },
        { QStringLiteral("Space"), Qt::Key_Space }, { QStringLiteral("Return"), Qt::Key_Return },
        { QStringLiteral("Enter"), Qt::Key_Enter }, { QStringLiteral("Escape"), Qt::Key_Escape },
        { QStringLiteral("F2"), Qt::Key_F2 }, { QStringLiteral("+"), Qt::Key_Plus },
        { QStringLiteral("-"), Qt::Key_Minus },
        { QStringLiteral("Home"), Qt::Key_Home }, { QStringLiteral("End"), Qt::Key_End }
    };
    const QString name = action.value(QStringLiteral("key")).toString();
    if (!keys.contains(name)) {
        *error = QStringLiteral("unsupported key '%1'").arg(name);
        return false;
    }
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    for (const QJsonValue &value : action.value(QStringLiteral("modifiers")).toArray()) {
        if (value.toString() == QStringLiteral("Shift")) modifiers |= Qt::ShiftModifier;
        else if (value.toString() == QStringLiteral("Alt")) modifiers |= Qt::AltModifier;
        else if (value.toString() == QStringLiteral("Ctrl")) modifiers |= Qt::ControlModifier;
        else {
            *error = QStringLiteral("unsupported key modifier '%1'").arg(value.toString());
            return false;
        }
    }
    if (name == QStringLiteral("Backtab"))
        modifiers |= Qt::ShiftModifier;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (qobject_cast<GameControlPanel *>(widget) && widget->isVisible()) {
            *error = QStringLiteral("native keyboard fixture requires the game control panel closed");
            return false;
        }
    }

    // Deliver real events through FitView or the request dialog's current focus.
    // Never call scene keyboard handlers or selection/confirmation helpers here.
    QPointer<QWidget> receiver = m_view;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (qobject_cast<QDialog *>(widget) && widget->isVisible()
            && widget != m_inspector) {
            receiver = widget->focusWidget() ? widget->focusWidget() : widget;
            break;
        }
    }
    if (!receiver) {
        *error = QStringLiteral("native keyboard receiver is unavailable");
        return false;
    }
    QKeyEvent press(QEvent::KeyPress, keys.value(name), modifiers);
    QApplication::sendEvent(receiver, &press);
    // Repeated physical key-down events must not create a second response.
    const int repeats = action.value(QStringLiteral("auto_repeat")).toInt();
    if (repeats < 0 || repeats > 8) {
        *error = QStringLiteral("auto_repeat must be between 0 and 8");
        return false;
    }
    for (int i = 0; i < repeats && receiver; ++i) {
        QKeyEvent repeat(QEvent::KeyPress, keys.value(name), modifiers, QString(), true);
        QApplication::sendEvent(receiver, &repeat);
    }
    if (receiver) {
        QKeyEvent release(QEvent::KeyRelease, keys.value(name), modifiers);
        QApplication::sendEvent(receiver, &release);
    }
    return true;
}

bool LocalResponseUiController::runAction(int index, QString *error)
{
    const QJsonObject action = m_case.actions().at(index).toObject();
    const QString type = action.value(QStringLiteral("type")).toString();
    QString actionError;
    bool ok = false;

    if (type == QStringLiteral("key_press"))
        ok = pressKey(action, &actionError);
    else if (type == QStringLiteral("select_card"))
        ok = m_probe->selectCard(action.value(QStringLiteral("card")).toString(), true, &actionError);
    else if (type == QStringLiteral("unselect_card"))
        ok = m_probe->selectCard(action.value(QStringLiteral("card")).toString(), false, &actionError);
    else if (type == QStringLiteral("activate_skill"))
        ok = m_probe->activateSkill(action.value(QStringLiteral("skill")).toString(), true, &actionError);
    else if (type == QStringLiteral("deactivate_skill"))
        ok = m_probe->activateSkill(action.value(QStringLiteral("skill")).toString(), false, &actionError);
    else if (type == QStringLiteral("select_player"))
        ok = m_probe->selectPlayer(action.value(QStringLiteral("player")).toString(), true, &actionError);
    else if (type == QStringLiteral("unselect_player"))
        ok = m_probe->selectPlayer(action.value(QStringLiteral("player")).toString(), false, &actionError);
    else if (type == QStringLiteral("click_ok") || type == QStringLiteral("invoke_skill_yes"))
        ok = m_probe->clickButton(QStringLiteral("ok"), &actionError);
    else if (type == QStringLiteral("click_cancel") || type == QStringLiteral("invoke_skill_no"))
        ok = m_probe->clickButton(QStringLiteral("cancel"), &actionError);
    else if (type == QStringLiteral("click_discard"))
        ok = m_probe->clickButton(QStringLiteral("discard"), &actionError);
    else if (type == QStringLiteral("choose_option"))
        ok = m_probe->chooseOption(action.value(QStringLiteral("option")).toString(), &actionError);
    else if (type == QStringLiteral("choose_general")) {
        const QString general = action.value(QStringLiteral("general")).toString();
        if (general.isEmpty())
            actionError = QStringLiteral("choose_general requires a non-empty general");
        else {
            // Exercise the same public slot used by the production general chooser.
            m_client->onPlayerChooseGeneral(general);
            ok = true;
        }
    }
    else if (type == QStringLiteral("choose_surface_card"))
        ok = m_probe->chooseSurfaceCard(action.value(QStringLiteral("card")).toString(), &actionError);
    else if (type == QStringLiteral("toggle_guanxing_card"))
        ok = m_probe->toggleGuanxingCard(action.value(QStringLiteral("card")).toString(), &actionError);
    else if (type == QStringLiteral("assert")) {
        ok = validateSnapshot(action.value(QStringLiteral("expect")).toObject(),
            m_probe->snapshot(), QStringLiteral("actions[%1].assert").arg(index));
        if (!ok)
            actionError = QStringLiteral("action assertion failed");
    } else if (type == QStringLiteral("take_screenshot")) {
        ok = !captureScreenshot(action.value(QStringLiteral("name"))
            .toString(QStringLiteral("action_%1").arg(index))).isEmpty();
        if (!ok)
            actionError = QStringLiteral("screenshot could not be saved");
    } else {
        actionError = QStringLiteral("unsupported action '%1'").arg(type);
    }

    QJsonObject result;
    result.insert(QStringLiteral("type"), type);
    result.insert(QStringLiteral("status"), ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    if (!actionError.isEmpty())
        result.insert(QStringLiteral("error"), actionError);
    m_actionResults.append(result);
    flushEvents();
    m_snapshots.insert(QStringLiteral("after_action_%1").arg(index + 1), m_probe->snapshot());
    updateInspectorSnapshot();
    if (!ok)
        *error = QStringLiteral("action %1 (%2) failed: %3").arg(index).arg(type, actionError);
    return ok;
}

bool LocalResponseUiController::eventFilter(QObject *watched, QEvent *event)
{
    if (m_mode == RunnerMode::Inspect && !m_closing && m_probe
        && event->type() == QEvent::KeyRelease) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        // Only trace navigation keys in this inspector process, never typed text.
        if (key == Qt::Key_F2 || key == Qt::Key_F6 || key == Qt::Key_Tab
            || key == Qt::Key_Backtab || key == Qt::Key_Space
            || key == Qt::Key_Left || key == Qt::Key_Right
            || key == Qt::Key_Up || key == Qt::Key_Down
            || key == Qt::Key_Return || key == Qt::Key_Enter) {
            QJsonObject entry;
            entry.insert(QStringLiteral("key"), QKeySequence(key).toString());
            entry.insert(QStringLiteral("receiver"), QString::fromLatin1(watched->metaObject()->className()));
            if (auto *focus = QApplication::focusWidget()) {
                entry.insert(QStringLiteral("focus_class"), QString::fromLatin1(focus->metaObject()->className()));
                entry.insert(QStringLiteral("focus_name"), focus->objectName());
            }
            QTimer::singleShot(0, this, [this, entry]() mutable {
                if (m_closing) return;
                const auto snapshot = m_probe->snapshot();
                entry.insert(QStringLiteral("cards"), snapshot.value(QStringLiteral("cards")));
                entry.insert(QStringLiteral("skills"), snapshot.value(QStringLiteral("skills")));
                QJsonArray trace = m_report.value(QStringLiteral("keyboard_trace")).toArray();
                if (trace.size() >= 64) trace.removeFirst();
                trace.append(entry);
                m_report.insert(QStringLiteral("keyboard_trace"), trace);
                persistReport(m_replyProcessed ? QStringLiteral("PASS") : QStringLiteral("INSPECTING"));
            });
        }
    }
    if (m_mode == RunnerMode::Inspect && !m_closing
        && event->type() == QEvent::Close
        && (watched == m_mainWindow || watched == m_inspector)) {
        static_cast<QCloseEvent *>(event)->ignore();
        QTimer::singleShot(0, this, &LocalResponseUiController::closeInspection);
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void LocalResponseUiController::createInspector(const QString &command, int serial)
{
    m_inspector = new LocalResponseUiInspector(m_mainWindow);
    m_inspector->setCaseName(m_case.name());
    m_inspector->setMode(QStringLiteral("Inspect"));
    m_inspector->setRequest(command, serial);
    qApp->installEventFilter(this);
    connect(m_inspector->nextActionButton(), &QPushButton::clicked,
        this, &LocalResponseUiController::runNextInspectAction);
    connect(m_inspector->remainingActionsButton(), &QPushButton::clicked,
        this, &LocalResponseUiController::runRemainingInspectActions);
    connect(m_inspector->snapshotButton(), &QPushButton::clicked,
        this, &LocalResponseUiController::saveManualSnapshot);
    connect(m_inspector->screenshotButton(), &QPushButton::clicked,
        this, &LocalResponseUiController::saveManualScreenshot);
    // Inspect the production draft and reply route; these buttons never run
    // fixture actions or substitute a synthetic GameActionModel.
    connect(m_inspector->gameControlsButton(), &QPushButton::clicked,
        m_scene, &RoomScene::showGameControlPanel);
    connect(m_inspector->gameTextButton(), &QPushButton::clicked,
        m_scene, &RoomScene::showGameStateSnapshot);
    connect(m_inspector->closeButton(), &QPushButton::clicked,
        this, &LocalResponseUiController::closeInspection);
    m_inspector->show();
    m_inspector->move(m_mainWindow->frameGeometry().topRight() + QPoint(12, 0));
    flushEvents();
}

void LocalResponseUiController::updateInspectorSnapshot()
{
    if (!m_inspector || !m_probe)
        return;
    const QJsonObject client = m_probe->snapshot().value(QStringLiteral("client")).toObject();
    m_inspector->setClientState(client.value(QStringLiteral("status")).toString(),
        client.value(QStringLiteral("pattern")).toString());
}

void LocalResponseUiController::setStage(RunnerStage stage)
{
    m_stage = stage;
    QString name;
    switch (stage) {
    case RunnerStage::Configuring: name = QStringLiteral("Configuring"); break;
    case RunnerStage::Bootstrapping: name = QStringLiteral("Bootstrapping"); break;
    case RunnerStage::InjectingRequest: name = QStringLiteral("InjectingRequest"); break;
    case RunnerStage::Presented: name = QStringLiteral("Presented"); break;
    case RunnerStage::AwaitingManualInput: name = QStringLiteral("AwaitingManualInput"); break;
    case RunnerStage::ReplyReceived: name = QStringLiteral("ReplyReceived"); break;
    case RunnerStage::Completed: name = QStringLiteral("Completed"); break;
    case RunnerStage::Closing: name = QStringLiteral("Closing"); break;
    }
    m_report.insert(QStringLiteral("runner_stage"), name);
    std::fprintf(stderr, "LOCAL_UI stage: %s\n", name.toUtf8().constData());
    std::fflush(stderr);
}

void LocalResponseUiController::setInspectFailure(ExitCode code, const QString &stage,
    const QString &message)
{
    m_inspectExitCode = code;
    m_inspectFailureStage = stage;
    m_inspectFailureMessage = message;
    setStage(RunnerStage::Completed);
    updateInspectorSnapshot();
    if (m_inspector)
        m_inspector->setFinalResult(QStringLiteral("FAIL: %1").arg(message));
    if (!persistReport(QStringLiteral("FAIL"), stage, message))
        finish(InternalError, QStringLiteral("report"),
            QStringLiteral("could not persist inspect failure report"));
}

void LocalResponseUiController::runNextInspectAction()
{
    if (m_mode != RunnerMode::Inspect || m_replyProcessed
        || m_nextActionIndex >= m_case.actions().size()) {
        return;
    }
    QString error;
    const int index = m_nextActionIndex++;
    if (!runAction(index, &error)) {
        setInspectFailure(AssertionFailed, QStringLiteral("actions"), error);
        return;
    }
    if (!m_replyProcessed)
        persistReport(QStringLiteral("INSPECTING"));
}

void LocalResponseUiController::runRemainingInspectActions()
{
    while (!m_replyProcessed && m_nextActionIndex < m_case.actions().size()) {
        const int before = m_nextActionIndex;
        runNextInspectAction();
        if (m_inspectExitCode != Passed || m_nextActionIndex == before)
            break;
    }
}

void LocalResponseUiController::saveManualSnapshot()
{
    if (!m_probe)
        return;
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    QDir().mkpath(m_screenshotDir);
    const QString path = QDir(m_screenshotDir).filePath(
        QStringLiteral("manual-snapshot-%1.json").arg(timestamp));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setInspectFailure(InternalError, QStringLiteral("snapshot"), file.errorString());
        return;
    }
    file.write(QJsonDocument(m_probe->snapshot()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setInspectFailure(InternalError, QStringLiteral("snapshot"), file.errorString());
        return;
    }
    QJsonObject artifacts = m_report.value(QStringLiteral("artifacts")).toObject();
    artifacts.insert(QStringLiteral("manual_snapshot_%1").arg(timestamp),
        QFileInfo(path).absoluteFilePath());
    m_report.insert(QStringLiteral("artifacts"), artifacts);
    persistReport(m_replyProcessed ? QStringLiteral("PASS") : QStringLiteral("INSPECTING"));
}

void LocalResponseUiController::saveManualScreenshot()
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    if (captureScreenshot(QStringLiteral("manual-screenshot-%1").arg(timestamp)).isEmpty()) {
        setInspectFailure(InternalError, QStringLiteral("screenshot"),
            QStringLiteral("manual screenshot could not be saved"));
        return;
    }
    persistReport(m_replyProcessed ? QStringLiteral("PASS") : QStringLiteral("INSPECTING"));
}

void LocalResponseUiController::processInspectReply()
{
    if (m_mode != RunnerMode::Inspect || m_closing)
        return;
    // Guanxing edits emit mirror notifications before the terminal reply.
    // Count reply frames, not packets, so editing neither completes the case
    // nor becomes a false duplicate submission.
    int replyCount = 0;
    for (const QString &raw : m_socket->sentPackets()) {
        ProtocolMessage outgoing;
        if (!ProtocolCodecRouter().decode(raw.toUtf8(), &outgoing).success) {
            setInspectFailure(AssertionFailed, QStringLiteral("expect_reply"),
                QStringLiteral("captured outbound frame is not valid Protocol V2"));
            return;
        }
        if (outgoing.type == ProtocolMessageType::Reply)
            ++replyCount;
    }
    if (replyCount == 0)
        return;
    if (m_replyProcessed) {
        if (replyCount > 1)
            setInspectFailure(AssertionFailed, QStringLiteral("expect_reply"),
                QStringLiteral("multiple terminal replies were captured"));
        return;
    }

    m_replyProcessed = true;
    setStage(RunnerStage::ReplyReceived);
    m_report.insert(QStringLiteral("reply_received"), true);
    QString error;
    const bool replyPassed = validateReply(&error);
    flushEvents();
    const QJsonObject finalSnapshot = m_probe->snapshot();
    m_snapshots.insert(QStringLiteral("final"), finalSnapshot);
    const bool finalPassed = validateSnapshot(m_case.finalExpectation(), finalSnapshot,
        QStringLiteral("expect_final"));
    const bool passed = replyPassed && finalPassed;
    m_report.insert(QStringLiteral("reply_result"),
        passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"));

    const QJsonObject captured = m_capturedPackets.isEmpty()
        ? QJsonObject() : m_capturedPackets.last().toObject();
    QByteArray encodedBody = QJsonDocument(QJsonArray { captured.value(QStringLiteral("body")) })
        .toJson(QJsonDocument::Compact);
    if (encodedBody.size() >= 2) {
        encodedBody.remove(0, 1);
        encodedBody.chop(1);
    }
    if (m_inspector) {
        m_inspector->setReply(true, captured.value(QStringLiteral("command")).toString(),
            QString::fromUtf8(encodedBody));
    }
    updateInspectorSnapshot();
    setStage(RunnerStage::Completed);

    if (!passed) {
        const QString message = replyPassed
            ? QStringLiteral("final-state assertions failed") : error;
        setInspectFailure(AssertionFailed,
            replyPassed ? QStringLiteral("expect_final") : QStringLiteral("expect_reply"), message);
        return;
    }

    m_inspectExitCode = Passed;
    if (m_inspector)
        m_inspector->setFinalResult(QStringLiteral("PASS - close when finished inspecting"));
    persistReport(QStringLiteral("PASS"));
}

void LocalResponseUiController::closeInspection()
{
    if (m_mode != RunnerMode::Inspect || m_closing)
        return;
    m_closing = true;
    m_closedByUser = true;
    setStage(RunnerStage::Closing);
    m_report.insert(QStringLiteral("closed_by_user"), true);

    const bool failed = m_inspectExitCode != Passed;
    const QString result = failed ? QStringLiteral("FAIL")
        : (m_replyProcessed ? QStringLiteral("PASS") : QStringLiteral("INSPECTED"));
    if (m_inspector)
        m_inspector->setFinalResult(result);
    if (!persistReport(result, m_inspectFailureStage, m_inspectFailureMessage)) {
        qApp->exit(InternalError);
        return;
    }
    qInfo().noquote() << QStringLiteral("LOCAL RESPONSE UI %1: %2").arg(result, m_case.name());
    if (m_inspector)
        m_inspector->close();
    if (m_mainWindow)
        m_mainWindow->close();
    qApp->exit(m_inspectExitCode);
}

bool LocalResponseUiController::validateSnapshot(const QJsonObject &expected,
    const QJsonObject &actual, const QString &path)
{
    bool passed = true;
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
        const QString childPath = path.isEmpty() ? it.key() : path + QLatin1Char('.') + it.key();
        if (it.key() == QStringLiteral("prompt")) {
            passed = validatePrompt(it.value().toObject(), actual, childPath) && passed;
        } else if (it.key() == QStringLiteral("cards")) {
            passed = validateNamedStates(it.value().toObject(),
                actual.value(QStringLiteral("cards")).toArray(), QStringLiteral("alias"), childPath) && passed;
        } else if (it.key() == QStringLiteral("surface_cards")) {
            passed = validateNamedStates(it.value().toObject(),
                actual.value(QStringLiteral("surface_cards")).toArray(), QStringLiteral("alias"), childPath) && passed;
        } else if (it.key() == QStringLiteral("players")) {
            passed = validateNamedStates(it.value().toObject(),
                actual.value(QStringLiteral("players")).toArray(), QStringLiteral("object_name"), childPath) && passed;
        } else if (it.key() == QStringLiteral("skills")) {
            passed = validateNamedStates(it.value().toObject(),
                actual.value(QStringLiteral("skills")).toArray(), QStringLiteral("object_name"), childPath) && passed;
        } else if (it.key() == QStringLiteral("client_status")) {
            passed = validateJsonValue(it.value(), actual.value(QStringLiteral("client")).toObject()
                .value(QStringLiteral("status")), childPath) && passed;
        } else if (it.key() == QStringLiteral("current_pattern")) {
            passed = validateJsonValue(it.value(), actual.value(QStringLiteral("client")).toObject()
                .value(QStringLiteral("pattern")), childPath) && passed;
        } else if (it.key() == QStringLiteral("use_reason")) {
            passed = validateJsonValue(it.value(), actual.value(QStringLiteral("client")).toObject()
                .value(QStringLiteral("use_reason")), childPath) && passed;
        } else {
            passed = validateJsonValue(it.value(), actual.value(it.key()), childPath) && passed;
        }
    }
    return passed;
}

bool LocalResponseUiController::validateJsonValue(const QJsonValue &expected,
    const QJsonValue &actual, const QString &path)
{
    if (expected.isObject()) {
        if (!actual.isObject()) {
            recordAssertion(path, expected, actual, false);
            return false;
        }
        return validateSnapshot(expected.toObject(), actual.toObject(), path);
    }
    if (expected.isArray()) {
        const QJsonArray expectedArray = expected.toArray();
        const QJsonArray actualArray = actual.toArray();
        bool containsAll = true;
        for (const QJsonValue &value : expectedArray)
            containsAll = actualArray.contains(value) && containsAll;
        recordAssertion(path, expected, actual, containsAll);
        return containsAll;
    }
    const bool passed = expected == actual;
    recordAssertion(path, expected, actual, passed);
    return passed;
}

bool LocalResponseUiController::validatePrompt(const QJsonObject &expected,
    const QJsonObject &snapshot, const QString &path)
{
    const QJsonObject client = snapshot.value(QStringLiteral("client")).toObject();
    const QString plain = client.value(QStringLiteral("prompt_plain_text")).toString();
    bool passed = true;
    if (expected.contains(QStringLiteral("equals"))) {
        const QString value = expected.value(QStringLiteral("equals")).toString();
        const bool itemPassed = plain == value;
        recordAssertion(path + QStringLiteral(".equals"), value, plain, itemPassed);
        passed = itemPassed && passed;
    }
    for (const QJsonValue &value : expected.value(QStringLiteral("contains")).toArray()) {
        const bool itemPassed = plain.contains(value.toString());
        recordAssertion(path + QStringLiteral(".contains"), value, plain, itemPassed);
        passed = itemPassed && passed;
    }
    for (const QJsonValue &value : expected.value(QStringLiteral("not_contains")).toArray()) {
        const bool itemPassed = !plain.contains(value.toString());
        recordAssertion(path + QStringLiteral(".not_contains"), value, plain, itemPassed);
        passed = itemPassed && passed;
    }
    if (expected.value(QStringLiteral("translation_required")).toBool()) {
        const bool itemPassed = !plain.trimmed().isEmpty() && !plain.trimmed().startsWith(QLatin1Char('@'))
            && !plain.contains(QStringLiteral("%src")) && !plain.contains(QStringLiteral("%dest"))
            && !plain.contains(QStringLiteral("%arg")) && !plain.contains(QStringLiteral("%arg2"));
        recordAssertion(path + QStringLiteral(".translation_required"), true, plain, itemPassed);
        passed = itemPassed && passed;
    }
    return passed;
}

bool LocalResponseUiController::validateNamedStates(const QJsonObject &expected,
    const QJsonArray &actual, const QString &nameKey, const QString &path)
{
    static const QMap<QString, QPair<QString, bool>> states {
        { QStringLiteral("enabled"), { QStringLiteral("enabled"), true } },
        { QStringLiteral("disabled"), { QStringLiteral("enabled"), false } },
        { QStringLiteral("selected"), { QStringLiteral("selected"), true } },
        { QStringLiteral("unselected"), { QStringLiteral("selected"), false } },
        { QStringLiteral("visible"), { QStringLiteral("visible"), true } }
    };
    bool passed = true;
    for (auto state = states.cbegin(); state != states.cend(); ++state) {
        for (const QJsonValue &nameValue : expected.value(state.key()).toArray()) {
            const QString name = nameValue.toString();
            QJsonObject found;
            for (const QJsonValue &item : actual) {
                if (item.toObject().value(nameKey).toString() == name) {
                    found = item.toObject();
                    break;
                }
            }
            const bool itemPassed = !found.isEmpty()
                && found.value(state.value().first).toBool() == state.value().second;
            recordAssertion(path + QLatin1Char('.') + state.key() + QLatin1Char('.') + name,
                state.value().second, found.value(state.value().first), itemPassed);
            passed = itemPassed && passed;
        }
    }
    return passed;
}

bool LocalResponseUiController::validateReply(QString *error)
{
    const QList<QString> sent = m_socket->sentPackets();
    QList<QString> replies;
    for (const QString &raw : sent) {
        ProtocolMessage outgoing;
        if (!ProtocolCodecRouter().decode(raw.toUtf8(), &outgoing).success) {
            *error = QStringLiteral("captured outbound frame is not valid Protocol V2");
            return false;
        }
        if (outgoing.type == ProtocolMessageType::Reply) {
            replies.append(raw);
            continue;
        }
        QJsonObject intermediate;
        intermediate.insert(QStringLiteral("packet_type"), static_cast<int>(outgoing.type));
        intermediate.insert(QStringLiteral("source"), static_cast<int>(outgoing.source));
        intermediate.insert(QStringLiteral("destination"), static_cast<int>(outgoing.destination));
        intermediate.insert(QStringLiteral("command"),
            LocalResponseUiCase::commandName(static_cast<CommandType>(outgoing.command)));
        intermediate.insert(QStringLiteral("global_serial"), QString::number(outgoing.messageId));
        intermediate.insert(QStringLiteral("local_serial"), QString::number(outgoing.replyTo));
        intermediate.insert(QStringLiteral("body"), QJsonValue::fromVariant(outgoing.payload));
        m_capturedPackets.append(intermediate);
    }
    if (replies.size() != 1) {
        *error = QStringLiteral("expected one terminal reply, captured %1 replies in %2 packets")
            .arg(replies.size()).arg(sent.size());
        return false;
    }
    ProtocolMessage packet;
    if (!ProtocolCodecRouter().decode(replies.first().toUtf8(), &packet).success) {
        *error = QStringLiteral("captured reply is not valid Protocol V2");
        return false;
    }

    QJsonObject captured;
    captured.insert(QStringLiteral("packet_type"), static_cast<int>(packet.type));
    captured.insert(QStringLiteral("source"), static_cast<int>(packet.source));
    captured.insert(QStringLiteral("destination"), static_cast<int>(packet.destination));
    captured.insert(QStringLiteral("command"),
        LocalResponseUiCase::commandName(static_cast<CommandType>(packet.command)));
    captured.insert(QStringLiteral("global_serial"), QString::number(packet.messageId));
    captured.insert(QStringLiteral("local_serial"), QString::number(packet.replyTo));
    captured.insert(QStringLiteral("body"), QJsonValue::fromVariant(packet.payload));
    const QVariantMap replyBody = packet.payload.toMap();

    QJsonObject decodedCard;
    if (packet.command == S_COMMAND_RESPONSE_CARD
        && !replyBody.value(QStringLiteral("cancelled")).toBool()) {
        const QString cardText = replyBody.value(QStringLiteral("card_text")).toString();
        if (!cardText.isEmpty()) {
            const Card *card = Card::Parse(cardText);
            if (card) {
                decodedCard.insert(QStringLiteral("card_name"), card->objectName());
                decodedCard.insert(QStringLiteral("card_class"), card->getClassName());
                decodedCard.insert(QStringLiteral("skill_name"), card->getSkillName());
                decodedCard.insert(QStringLiteral("activation_skill_name"),
                    replyBody.value(QStringLiteral("activation_skill_name")).toString());
                decodedCard.insert(QStringLiteral("activation_instance_id"),
                    replyBody.value(QStringLiteral("activation_skill_instance_id")).toInt());
                const int effectiveId = card->getEffectiveId();
                decodedCard.insert(QStringLiteral("card_id"), effectiveId);
                decodedCard.insert(QStringLiteral("card_alias"), m_aliasToId.key(effectiveId));
                QJsonArray subcards;
                for (int id : card->getSubcards())
                    subcards.append(m_aliasToId.key(id, QString::number(id)));
                decodedCard.insert(QStringLiteral("subcards"), subcards);
                QJsonArray targets;
                for (const QVariant &target
                     : replyBody.value(QStringLiteral("targets")).toList())
                    targets.append(target.toString());
                decodedCard.insert(QStringLiteral("targets"), targets);
            }
        }
    }
    if (!decodedCard.isEmpty())
        captured.insert(QStringLiteral("decoded_card"), decodedCard);
    m_capturedPackets.append(captured);

    const QJsonObject expected = m_case.replyExpectation();
    // Check exact typed fields for arrangements, roles and concealed-card replies.
    if (expected.contains(QStringLiteral("payload"))) {
        const QJsonObject fields = expected.value(QStringLiteral("payload")).toObject();
        const QJsonObject actual = QJsonObject::fromVariantMap(replyBody);
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            const bool matches = actual.value(it.key()) == it.value();
            recordAssertion(QStringLiteral("expect_reply.payload.") + it.key(),
                it.value(), actual.value(it.key()), matches);
            if (!matches) {
                *error = QStringLiteral("reply payload assertion failed");
                return false;
            }
        }
    }
    bool passed = true;
    const QString expectedCommand = expected.value(QStringLiteral("command")).toString();
    if (!expectedCommand.isEmpty()) {
        const QString actualCommand = LocalResponseUiCase::commandName(
            static_cast<CommandType>(packet.command));
        const bool itemPassed = actualCommand == expectedCommand;
        recordAssertion(QStringLiteral("expect_reply.command"), expectedCommand, actualCommand, itemPassed);
        passed = itemPassed && passed;
    }
    if (expected.contains(QStringLiteral("local_serial"))) {
        const int expectedSerial = expected.value(QStringLiteral("local_serial")).toInt();
        const bool itemPassed = static_cast<int>(packet.replyTo) == expectedSerial;
        recordAssertion(QStringLiteral("expect_reply.local_serial"), expectedSerial,
            static_cast<int>(packet.replyTo), itemPassed);
        passed = itemPassed && passed;
    }
    const bool packetShape = packet.type == ProtocolMessageType::Reply
        && packet.source == ProtocolEndpoint::Client
        && packet.destination == ProtocolEndpoint::Room;
    recordAssertion(QStringLiteral("expect_reply.packet_shape"), true, packetShape, packetShape);
    passed = packetShape && passed;

    if (expected.contains(QStringLiteral("invoke"))) {
        const bool expectedInvoke = expected.value(QStringLiteral("invoke")).toBool();
        const QString field = packet.command == S_COMMAND_SURRENDER ? QStringLiteral("surrender")
            : packet.command == S_COMMAND_LUCK_CARD ? QStringLiteral("use_luck_card")
            : QStringLiteral("invoke");
        const bool actualInvoke = replyBody.value(field).toBool();
        // Missing fields must never accidentally pass a false/no expectation.
        const bool itemPassed = replyBody.contains(field) && expectedInvoke == actualInvoke;
        recordAssertion(QStringLiteral("expect_reply.invoke"), expectedInvoke, actualInvoke, itemPassed);
        passed = itemPassed && passed;
    }
    if (expected.contains(QStringLiteral("choice"))) {
        const QString expectedChoice = expected.value(QStringLiteral("choice")).toString();
        QString actualChoice;
        static const QStringList choiceFields{
            QStringLiteral("choice"), QStringLiteral("direction"),
            QStringLiteral("suit"), QStringLiteral("kingdom"),
            QStringLiteral("trigger"), QStringLiteral("role"),
            QStringLiteral("general")
        };
        for (const QString &field : choiceFields) {
            if (replyBody.contains(field)) {
                actualChoice = replyBody.value(field).toString();
                break;
            }
        }
        const bool itemPassed = expectedChoice == actualChoice;
        recordAssertion(QStringLiteral("expect_reply.choice"), expectedChoice, actualChoice, itemPassed);
        passed = itemPassed && passed;
    }
    if (expected.contains(QStringLiteral("card_aliases"))) {
        QJsonArray actualAliases;
        const QVariantList idValues = replyBody.value(QStringLiteral("card_ids")).toList();
        for (const QVariant &idValue : idValues)
            actualAliases.append(m_aliasToId.key(idValue.toInt(), QString::number(idValue.toInt())));
        const QJsonArray expectedAliases = expected.value(QStringLiteral("card_aliases")).toArray();
        const bool itemPassed = jsonArraysEqual(expectedAliases, actualAliases);
        recordAssertion(QStringLiteral("expect_reply.card_aliases"), expectedAliases,
            actualAliases, itemPassed);
        passed = itemPassed && passed;
    }
    if (expected.contains(QStringLiteral("card_alias"))) {
        const QString expectedAlias = expected.value(QStringLiteral("card_alias")).toString();
        const QString actualAlias = decodedCard.contains(QStringLiteral("card_alias"))
            ? decodedCard.value(QStringLiteral("card_alias")).toString()
            : m_aliasToId.key(replyBody.value(QStringLiteral("card_id")).toInt(),
                QString::number(replyBody.value(QStringLiteral("card_id")).toInt()));
        const bool itemPassed = expectedAlias == actualAlias;
        recordAssertion(QStringLiteral("expect_reply.card_alias"), expectedAlias,
            actualAlias, itemPassed);
        passed = itemPassed && passed;
    }
    if (expected.contains(QStringLiteral("player"))) {
        const QString actualPlayer = replyBody
            .value(QStringLiteral("target_player")).toString();
        const QString expectedPlayer = expected.value(QStringLiteral("player")).toString();
        const bool itemPassed = expectedPlayer == actualPlayer;
        recordAssertion(QStringLiteral("expect_reply.player"), expectedPlayer,
            actualPlayer, itemPassed);
        passed = itemPassed && passed;
    }
    auto validateDeck = [this, &replyBody, &expected, &passed](
                            const QString &key, const QString &field) {
        if (!expected.contains(key))
            return;
        const QVariantList ids = replyBody.value(field).toList();
        QJsonArray actualAliases;
        for (const QVariant &id : ids)
            actualAliases.append(m_aliasToId.key(id.toInt(), QString::number(id.toInt())));
        const QJsonArray expectedAliases = expected.value(key).toArray();
        const bool itemPassed = jsonArraysEqual(expectedAliases, actualAliases);
        recordAssertion(QStringLiteral("expect_reply.") + key, expectedAliases,
            actualAliases, itemPassed);
        passed = itemPassed && passed;
    };
    validateDeck(QStringLiteral("up_cards"), QStringLiteral("top_card_ids"));
    validateDeck(QStringLiteral("down_cards"), QStringLiteral("bottom_card_ids"));
    if (expected.contains(QStringLiteral("players"))) {
        QJsonArray actualPlayers;
        for (const QVariant &name : replyBody.value(QStringLiteral("players")).toList())
            actualPlayers.append(name.toString());
        const QJsonArray expectedPlayers = expected.value(QStringLiteral("players")).toArray();
        const bool itemPassed = jsonArraysEqual(expectedPlayers, actualPlayers);
        recordAssertion(QStringLiteral("expect_reply.players"), expectedPlayers,
            actualPlayers, itemPassed);
        passed = itemPassed && passed;
    }
    if (expected.value(QStringLiteral("cancelled")).toBool()) {
        const bool cancelled = replyBody.value(QStringLiteral("cancelled")).toBool()
            || (replyBody.contains(QStringLiteral("has_value"))
                && !replyBody.value(QStringLiteral("has_value")).toBool());
        recordAssertion(QStringLiteral("expect_reply.cancelled"), true, cancelled, cancelled);
        passed = cancelled && passed;
    }

    static const QStringList decodedKeys {
        QStringLiteral("card_name"), QStringLiteral("card_class"),
        QStringLiteral("skill_name"), QStringLiteral("activation_skill_name"),
        QStringLiteral("activation_instance_id"), QStringLiteral("subcards"),
        QStringLiteral("targets")
    };
    for (const QString &key : decodedKeys) {
        if (!expected.contains(key))
            continue;
        passed = validateJsonValue(expected.value(key), decodedCard.value(key),
            QStringLiteral("expect_reply.") + key) && passed;
    }

    if (!passed)
        *error = QStringLiteral("reply assertions failed");
    return passed;
}

void LocalResponseUiController::recordAssertion(const QString &path,
    const QJsonValue &expected, const QJsonValue &actual, bool passed)
{
    QJsonObject assertion;
    assertion.insert(QStringLiteral("path"), path);
    assertion.insert(QStringLiteral("expected"), expected);
    assertion.insert(QStringLiteral("actual"), actual);
    assertion.insert(QStringLiteral("result"), passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    m_assertions.append(assertion);
}

void LocalResponseUiController::flushEvents()
{
    QEventLoop loop;
    QTimer::singleShot(0, &loop, &QEventLoop::quit);
    loop.exec();
}

bool LocalResponseUiController::waitForCondition(const std::function<bool()> &condition,
    int timeoutMs)
{
    if (condition())
        return true;
    QEventLoop loop;
    QTimer pollTimer;
    QTimer timeoutTimer;
    bool satisfied = false;
    pollTimer.setInterval(10);
    timeoutTimer.setSingleShot(true);
    connect(&pollTimer, &QTimer::timeout, &loop, [&]() {
        if (!condition())
            return;
        satisfied = true;
        loop.quit();
    });
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    pollTimer.start();
    timeoutTimer.start(timeoutMs);
    loop.exec();
    return satisfied || condition();
}

QString LocalResponseUiController::captureScreenshot(const QString &name)
{
    if (!m_mainWindow)
        return QString();
    QDir directory;
    if (!directory.mkpath(m_screenshotDir))
        return QString();
    const QString path = QDir(m_screenshotDir).filePath(name + QStringLiteral(".png"));

    QList<QPair<QRect, QPixmap>> captures;
    QRect bounds;
    auto appendWidget = [this, &captures, &bounds](QWidget *widget) {
        if (!widget || !widget->isVisible())
            return;
        QPixmap pixmap = widget->grab();
        if (pixmap.isNull())
            return;
        if (widget == m_mainWindow && m_view && m_view->scene()) {
            QPainter scenePainter(&pixmap);
            const QPoint offset = m_view->viewport()->mapTo(widget, QPoint(0, 0));
            const QRectF target(offset, m_view->viewport()->size());
            const QRectF source = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
            m_view->scene()->render(&scenePainter, target, source, Qt::IgnoreAspectRatio);
        }
        const QRect rect(widget->mapToGlobal(QPoint(0, 0)),
            pixmap.deviceIndependentSize().toSize());
        captures.append(qMakePair(rect, pixmap));
        bounds = bounds.isNull() ? rect : bounds.united(rect);
    };
    appendWidget(m_mainWindow);
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget != m_mainWindow)
            appendWidget(widget);
    }
    if (captures.isEmpty())
        return QString();

    QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    for (const auto &capture : captures)
        painter.drawPixmap(capture.first.topLeft() - bounds.topLeft(), capture.second);
    painter.end();
    if (!image.save(path, "PNG"))
        return QString();
    QJsonObject artifacts = m_report.value(QStringLiteral("artifacts")).toObject();
    artifacts.insert(name, QFileInfo(path).absoluteFilePath());
    m_report.insert(QStringLiteral("artifacts"), artifacts);
    return path;
}

void LocalResponseUiController::syncReport(const QString &result, const QString &stage,
    const QString &message)
{
    m_report.insert(QStringLiteral("result"), result);
    if (stage.isEmpty())
        m_report.remove(QStringLiteral("failed_stage"));
    else
        m_report.insert(QStringLiteral("failed_stage"), stage);
    if (message.isEmpty())
        m_report.remove(QStringLiteral("error"));
    else
        m_report.insert(QStringLiteral("error"), message);
    m_report.insert(QStringLiteral("snapshots"), m_snapshots);
    m_report.insert(QStringLiteral("actions"), m_actionResults);
    m_report.insert(QStringLiteral("captured_packets"), m_capturedPackets);
    m_report.insert(QStringLiteral("assertions"), m_assertions);
    m_report.insert(QStringLiteral("qt_messages"), m_qtMessages);
    if (m_mode == RunnerMode::Inspect) {
        m_report.insert(QStringLiteral("reply_received"), m_replyProcessed);
        m_report.insert(QStringLiteral("closed_by_user"), m_closedByUser);
    }
}

bool LocalResponseUiController::persistReport(const QString &result,
    const QString &stage, const QString &message)
{
    syncReport(result, stage, message);
    QString writeError;
    if (writeReport(&writeError))
        return true;
    qCritical().noquote() << writeError;
    return false;
}

void LocalResponseUiController::finish(ExitCode code, const QString &stage,
    const QString &message)
{
    if (code != Passed && m_screenshotOnFailure)
        captureScreenshot(QStringLiteral("failed"));
    setStage(RunnerStage::Completed);
    syncReport(code == Passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
        stage, message);

    QString writeError;
    if (!writeReport(&writeError)) {
        qCritical().noquote() << writeError;
        qApp->exit(InternalError);
        return;
    }
    qInfo().noquote() << QStringLiteral("LOCAL RESPONSE UI %1: %2")
        .arg(code == Passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"), m_case.name());
    if (!message.isEmpty())
        qCritical().noquote() << message;
    qApp->exit(code);
}

bool LocalResponseUiController::writeReport(QString *error)
{
    const QFileInfo reportInfo(m_reportPath);
    if (!QDir().mkpath(reportInfo.absolutePath())) {
        *error = QStringLiteral("cannot create report directory '%1'").arg(reportInfo.absolutePath());
        return false;
    }
    QSaveFile file(m_reportPath);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("cannot open report '%1': %2").arg(m_reportPath, file.errorString());
        return false;
    }
    file.write(QJsonDocument(m_report).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = QStringLiteral("cannot commit report '%1': %2").arg(m_reportPath, file.errorString());
        return false;
    }
    return true;
}
