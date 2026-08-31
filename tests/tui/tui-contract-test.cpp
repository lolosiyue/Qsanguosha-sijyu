#include "client-game-state.h"
#include "client-game-state-reducer.h"
#include "client-core.h"
#include "interaction-command-registry.h"
#include "protocol-interaction-request-builder.h"
#include "protocol/protocol-payload-registry.h"
#include "protocol/session/session-payloads.h"
#include "tui-command.h"
#include "tui-interaction-view.h"
#include "tui-renderer.h"
#include "tui-script-runner.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <limits>

using namespace QSanProtocol;

namespace {

int failures = 0;

void check(bool condition, const char *name)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++failures;
}

QString messageTypeName(ProtocolMessageType type)
{
    switch (type) {
    case ProtocolMessageType::Request:
        return QStringLiteral("request");
    case ProtocolMessageType::Reply:
        return QStringLiteral("reply");
    case ProtocolMessageType::Notification:
        return QStringLiteral("notification");
    default:
        return QStringLiteral("unknown");
    }
}

QString dispositionName(ClientFlowDisposition disposition)
{
    switch (disposition) {
    case ClientFlowDisposition::StateMutation:
        return QStringLiteral("state mutation");
    case ClientFlowDisposition::PresentationEvent:
        return QStringLiteral("presentation event");
    case ClientFlowDisposition::SessionControl:
        return QStringLiteral("session control");
    case ClientFlowDisposition::ExplicitTextIrrelevant:
        return QStringLiteral("explicitly irrelevant to text mode");
    case ClientFlowDisposition::Unclassified:
        return QStringLiteral("unclassified");
    }
    return QStringLiteral("unclassified");
}

QString affectedStateFor(int command, ClientFlowDisposition disposition)
{
    if (disposition == ClientFlowDisposition::PresentationEvent)
        return QStringLiteral("presentation event log");
    if (disposition == ClientFlowDisposition::ExplicitTextIrrelevant)
        return QStringLiteral("none; typed flow history only");
    if (disposition == ClientFlowDisposition::SessionControl)
        return QStringLiteral("connection, setup, ready, latency, or resync generation");

    switch (command) {
    case S_COMMAND_ADD_PLAYER:
    case S_COMMAND_ADD_PLAYER_DYNAMIC:
    case S_COMMAND_REMOVE_PLAYER:
    case S_COMMAND_ARRANGE_SEATS:
        return QStringLiteral("room roster and player identity");
    case S_COMMAND_START_IN_X_SECONDS:
    case S_COMMAND_GAME_START:
    case S_COMMAND_GAME_OVER:
    case S_COMMAND_ADD_ROUND:
    case S_COMMAND_UPDATE_STATE_ITEM:
    case S_COMMAND_UPDATE_BOSS_LEVEL:
        return QStringLiteral("room and game lifecycle");
    case S_COMMAND_CHANGE_HP:
    case S_COMMAND_CHANGE_MAXHP:
    case S_COMMAND_KILL_PLAYER:
    case S_COMMAND_REVIVE_PLAYER:
    case S_COMMAND_SET_PROPERTY:
    case S_COMMAND_SET_MARK:
    case S_COMMAND_PRESHOW:
        return QStringLiteral("player public and private presentation state");
    case S_COMMAND_ATTACH_SKILL:
    case S_COMMAND_SKILL_INSTANCE:
    case S_COMMAND_UPDATE_SKILL:
    case S_COMMAND_SKILL_DESCRIPTION_SWAP:
    case S_COMMAND_ANYTIME_SKILL_DONE:
        return QStringLiteral("player skills and skill instances");
    case S_COMMAND_SHOW_CARD:
    case S_COMMAND_SHOW_VIRTUAL_CARD:
    case S_COMMAND_CARD_PROVENANCE:
    case S_COMMAND_UPDATE_CARD:
    case S_COMMAND_GET_CARD:
    case S_COMMAND_LOSE_CARD:
    case S_COMMAND_RESET_PILE:
    case S_COMMAND_UPDATE_PILE:
    case S_COMMAND_SYNCHRONIZE_DISCARD_PILE:
    case S_COMMAND_SYNC_PILE:
    case S_COMMAND_CARD_MARK:
    case S_COMMAND_CARD_FLAG:
    case S_COMMAND_SET_SHOWN_HANDCARD:
    case S_COMMAND_SET_BROKEN_EQUIP:
    case S_COMMAND_UPDATE_CARD_DESC:
        return QStringLiteral("cards, ownership, zones, piles, and visibility");
    case S_COMMAND_SHOW_ALL_CARDS:
    case S_COMMAND_SKILL_GONGXIN:
    case S_COMMAND_EXCHANGE_KNOWN_CARDS:
    case S_COMMAND_SET_KNOWN_CARDS:
    case S_COMMAND_AVAILABLE_CARDS:
        return QStringLiteral("server-authorized known and available cards");
    case S_COMMAND_MOVE_FOCUS:
    case S_COMMAND_SWITCH_CONTEXT:
    case S_COMMAND_NULLIFICATION_ASKED:
    case S_COMMAND_ENABLE_SURRENDER:
        return QStringLiteral("current actor, prompt context, and timers");
    case S_COMMAND_FIXED_DISTANCE:
    case S_COMMAND_ATTACK_RANGE:
    case S_COMMAND_CARD_LIMITATION:
    case S_COMMAND_WEAPON_RANGE:
        return QStringLiteral("distance, range, and card availability presentation");
    case S_COMMAND_FILL_AMAZING_GRACE:
    case S_COMMAND_TAKE_AMAZING_GRACE:
    case S_COMMAND_CLEAR_AMAZING_GRACE:
    case S_COMMAND_MIRROR_GUANXING_STEP:
        return QStringLiteral("temporary card-selection state");
    case S_COMMAND_FILL_GENERAL:
    case S_COMMAND_TAKE_GENERAL:
    case S_COMMAND_RECOVER_GENERAL:
    case S_COMMAND_REVEAL_GENERAL:
    case S_COMMAND_VIEW_GENERALS:
        return QStringLiteral("general pools and authorized general visibility");
    case S_COMMAND_ADD_HISTORY:
        return QStringLiteral("player action history");
    case S_COMMAND_ADD_EQUIP_AREA:
    case S_COMMAND_SET_EQUIP_AREA_COUNT:
        return QStringLiteral("player equipment areas");
    case S_COMMAND_UPDATE_PLAYER_UI_STATE:
        return QStringLiteral("frontend-neutral player UI state");
    default:
        return QStringLiteral("typed flow history and deterministic snapshot");
    }
}

QJsonObject tuiFlowCoverageJson()
{
    const QSet<int> lifecycleCommands{S_COMMAND_CHECK_VERSION, S_COMMAND_SIGNUP,
        S_COMMAND_SETUP, S_COMMAND_READY};
    QJsonArray flows;
    int stateBearing = 0;
    int stateBearingWithReducer = 0;
    int interactions = 0;
    int unsupportedInteractions = 0;
    int unclassified = 0;
    int explicitTextNoOps = 0;

    for (const ProtocolFlowDescriptor &descriptor : ProtocolPayloadRegistry::descriptors()) {
        const ProtocolFlowKey &key = descriptor.key;
        if (key.source != ProtocolEndpoint::Room
            || key.destination != ProtocolEndpoint::Client) {
            continue;
        }

        ClientFlowDisposition disposition = ClientFlowDisposition::Unclassified;
        QString parser = descriptor.parser;
        QString dto = descriptor.targetSchema;
        QString reducer;
        QString renderer;
        QString reconnect;
        QString focusedTest;

        if (key.messageType == ProtocolMessageType::Request) {
            const InteractionCommandDescriptor *interaction =
                InteractionCommandRegistry::find(static_cast<CommandType>(key.command));
            ++interactions;
            if (interaction == nullptr) {
                ++unsupportedInteractions;
            } else {
                disposition = ClientFlowDisposition::SessionControl;
                parser = QStringLiteral("ProtocolInteractionRequestBuilder::build");
                dto = QStringLiteral("InteractionRequest/%1")
                    .arg(QString::fromLatin1(interaction->commandName));
                reducer = QStringLiteral("ClientCore::beginRequest");
                renderer = QStringLiteral("TuiInteractionView active prompt");
                reconnect = QStringLiteral(
                    "cancel exactly once on generation change; never replay stale input");
                focusedTest = QStringLiteral("qsanguosha_tui_contract/all-29-round-trip");
            }
        } else if (lifecycleCommands.contains(key.command)
                   || key.messageType == ProtocolMessageType::Reply) {
            disposition = ClientFlowDisposition::SessionControl;
            reducer = QStringLiteral("ClientLiveSession::dispatchMessage");
            renderer = QStringLiteral("TuiRenderer connection/status view");
            reconnect = QStringLiteral(
                "new socket and generation; re-signup; atomic state replacement");
            focusedTest = QStringLiteral("qsanguosha_tui_live_tcp");
        } else if (key.messageType == ProtocolMessageType::Notification) {
            disposition = ClientGameStateReducer::classifyNotification(key.command);
            reducer = QStringLiteral("ClientGameStateReducer::applyNotification");
            if (disposition == ClientFlowDisposition::PresentationEvent)
                renderer = QStringLiteral("TuiRenderer event log");
            else if (disposition == ClientFlowDisposition::ExplicitTextIrrelevant)
                renderer = QStringLiteral("none; documented text-mode no-op");
            else
                renderer = QStringLiteral("TuiRenderer snapshots and command views");
            reconnect = QStringLiteral(
                "staged during STATE_SYNC; committed atomically at sync end");
            focusedTest = QStringLiteral("qsanguosha_tui_contract/reducer-coverage");
        }

        if (disposition == ClientFlowDisposition::StateMutation) {
            ++stateBearing;
            if (!reducer.isEmpty())
                ++stateBearingWithReducer;
        }
        if (disposition == ClientFlowDisposition::ExplicitTextIrrelevant)
            ++explicitTextNoOps;
        if (disposition == ClientFlowDisposition::Unclassified)
            ++unclassified;

        const bool interactionRequest = key.messageType == ProtocolMessageType::Request
            && InteractionCommandRegistry::find(static_cast<CommandType>(key.command)) != nullptr;
        QJsonObject flow{{QStringLiteral("command"), descriptor.commandName},
            {QStringLiteral("command_id"), key.command},
            {QStringLiteral("message_type"), messageTypeName(key.messageType)},
            {QStringLiteral("classification"),
                interactionRequest ? QStringLiteral("interaction request")
                                   : dispositionName(disposition)},
            {QStringLiteral("parser"), parser}, {QStringLiteral("dto"), dto},
            {QStringLiteral("reducer"), reducer},
            {QStringLiteral("affected_state"),
                interactionRequest ? QStringLiteral("active canonical interaction")
                                   : affectedStateFor(key.command, disposition)},
            {QStringLiteral("renderer_visibility"), renderer},
            {QStringLiteral("reconnect_behavior"), reconnect},
            {QStringLiteral("focused_test"), focusedTest}};
        flows.append(flow);
    }

    const int silentDrops = unclassified;
    return QJsonObject{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("scope"), QStringLiteral("all production Room-to-Client V2 flows")},
        {QStringLiteral("flows"), flows},
        {QStringLiteral("summary"), QJsonObject{
            {QStringLiteral("room_to_client_flow_count"), flows.size()},
            {QStringLiteral("state_bearing_total"), stateBearing},
            {QStringLiteral("state_bearing_with_reducer"), stateBearingWithReducer},
            {QStringLiteral("interaction_request_total"), interactions},
            {QStringLiteral("unsupported_interactions"), unsupportedInteractions},
            {QStringLiteral("explicit_text_mode_no_ops"), explicitTextNoOps},
            {QStringLiteral("unclassified_flows"), unclassified},
            {QStringLiteral("silent_drops"), silentDrops}}}};
}

void coverageContract()
{
    const QJsonObject coverage = tuiFlowCoverageJson();
    const QJsonObject summary = coverage.value(QStringLiteral("summary")).toObject();
    check(summary.value(QStringLiteral("state_bearing_total")).toInt() > 50,
          "state-bearing production flow inventory is non-trivial");
    check(summary.value(QStringLiteral("state_bearing_total")).toInt()
              == summary.value(QStringLiteral("state_bearing_with_reducer")).toInt(),
          "every state-bearing production flow has a reducer");
    check(summary.value(QStringLiteral("interaction_request_total")).toInt() == 29
              && summary.value(QStringLiteral("unsupported_interactions")).toInt() == 0,
          "all production interaction requests have a TUI presenter");
    check(summary.value(QStringLiteral("unclassified_flows")).toInt() == 0
              && summary.value(QStringLiteral("silent_drops")).toInt() == 0,
          "Room-to-Client coverage has no unclassified or silent drops");

    QFile artifact(QStringLiteral("artifacts/tui-flow-coverage.json"));
    const QByteArray generated = QJsonDocument(coverage).toJson(QJsonDocument::Indented);
    // Classification changes have to update the checked-in artifact. Rewriting
    // 100+ entries by hand invites mistakes, so allow an explicit refresh.
    if (qEnvironmentVariableIsSet("QSAN_TUI_COVERAGE_WRITE")) {
        check(artifact.open(QIODevice::WriteOnly)
                  && artifact.write(generated) == generated.size(),
              "TUI flow coverage artifact rewritten on request");
        return;
    }
    const bool opened = artifact.open(QIODevice::ReadOnly);
    check(opened && artifact.readAll() == generated,
          "checked TUI flow coverage artifact matches production registries");
}

InteractionRequest optionRequest()
{
    InteractionRequest request;
    request.requestId = 0x1ffffffffULL;
    request.command = S_COMMAND_MULTIPLE_CHOICE;
    request.type = InteractionType::Choice;
    request.responseSchema = InteractionResponseShape::Option;
    OptionInteractionPayload payload;
    payload.options << InteractionOption(QStringLiteral("alpha"))
                    << InteractionOption(QStringLiteral("beta"));
    request.payload = payload;
    return request;
}

InteractionRequest cardRequest()
{
    InteractionRequest request;
    request.requestId = 0x200000001ULL;
    request.command = S_COMMAND_RESPONSE_CARD;
    request.type = InteractionType::ResponseCard;
    request.responseSchema = InteractionResponseShape::Cards;
    CardInteractionPayload payload;
    payload.selection.enumerated = true;
    payload.selection.selectableCards = {7, 12, 18};
    payload.selection.minSelection = 1;
    payload.selection.maxSelection = 2;
    payload.optionalTargets = {QStringLiteral("p2"), QStringLiteral("p3")};
    payload.cardTextAllowed = true;
    request.payload = payload;
    return request;
}

QVariantMap samplePayload(CommandType command)
{
    QVariantMap payload{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("candidates"), QVariantList{QStringLiteral("g1"), QStringLiteral("g2")}},
        {QStringLiteral("players"), QVariantList{QStringLiteral("p1"), QStringLiteral("p2")}},
        {QStringLiteral("roles"), QVariantList{QStringLiteral("lord"), QStringLiteral("guard")}},
        {QStringLiteral("kingdoms"), QVariantList{QStringLiteral("wei"), QStringLiteral("shu")}},
        {QStringLiteral("generals"), QVariantList{QStringLiteral("g1"), QStringLiteral("g2")}},
        {QStringLiteral("card_ids"), QVariantList{1, 2}},
        {QStringLiteral("enabled_card_ids"), QVariantList{2}},
        {QStringLiteral("disabled_card_ids"), QVariantList()},
        {QStringLiteral("player"), QStringLiteral("p1")},
        {QStringLiteral("source_player"), QStringLiteral("p1")},
        {QStringLiteral("target_player"), QStringLiteral("p2")},
        {QStringLiteral("dying_player"), QStringLiteral("p1")},
        {QStringLiteral("zone_flags"), QStringLiteral("he")},
        {QStringLiteral("hand_cards_visible"), true},
        {QStringLiteral("mode"), QStringLiteral("up_only")},
        {QStringLiteral("scheme"), QStringLiteral("test")},
        {QStringLiteral("pattern"), QStringLiteral(".")},
        {QStringLiteral("min_cards"), 0}, {QStringLiteral("max_cards"), 2},
        {QStringLiteral("min_players"), 1}, {QStringLiteral("max_players"), 1},
        {QStringLiteral("include_equip"), true}, {QStringLiteral("optional"), true},
        {QStringLiteral("can_cancel"), true}, {QStringLiteral("refusable"), true}};
    if (command == S_COMMAND_MULTIPLE_CHOICE) {
        payload.insert(QStringLiteral("options"),
                       QVariantList{QStringLiteral("yes"), QStringLiteral("no")});
    } else if (command == S_COMMAND_TRIGGER_ORDER) {
        payload.insert(QStringLiteral("options"), QVariantList{QVariantMap{
            {QStringLiteral("skill"), QStringLiteral("jizhi")},
            {QStringLiteral("instanceID"), 2},
            {QStringLiteral("invoker"), QStringLiteral("p1")},
            {QStringLiteral("owner"), QStringLiteral("p1")},
            {QStringLiteral("preferredtarget"), QStringLiteral("p2")},
            {QStringLiteral("preferredtargetseat"), 3}}});
    } else if (command == S_COMMAND_CHOOSE_ORDER) {
        payload.insert(QStringLiteral("reason"), QStringLiteral("select"));
    } else if (command == S_COMMAND_QML_INTERACT) {
        payload.insert(QStringLiteral("interaction"), QVariantMap{
            {QStringLiteral("schema_version"), 1},
            {QStringLiteral("type"), QStringLiteral("qsanguosha.qml")},
            {QStringLiteral("title"), QStringLiteral("Choose")},
            {QStringLiteral("payload"), QVariantMap{
                {QStringLiteral("qml_path"), QStringLiteral("qml/Choose.qml")},
                {QStringLiteral("parameters"), QVariantMap{{QStringLiteral("x"), 1}}}}},
            {QStringLiteral("response_schema"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("json")}}}});
    }
    return payload;
}

void registryContract()
{
    const auto &descriptors = InteractionCommandRegistry::descriptors();
    check(descriptors.size() == 29, "all 29 production interactions registered");
    QSet<int> commands;
    QSet<int> types;
    bool complete = true;
    for (const InteractionCommandDescriptor &descriptor : descriptors) {
        commands.insert(descriptor.command);
        types.insert(static_cast<int>(descriptor.type));
        complete = complete && descriptor.replyEncoder != nullptr
            && descriptor.responseShape != InteractionResponseShape::None
            && descriptor.commandName != nullptr && descriptor.testName != nullptr;
    }
    check(commands.size() == 29, "interaction command keys are unique");
    check(types.size() == 29, "interaction types are unique");
    check(complete, "every interaction has shape encoder and diagnostics");
}

void builderContract()
{
    ClientGameState state;
    state.setSelfName(QStringLiteral("p1"));
    state.addPlayer(QStringLiteral("p2"));
    state.setCardIdSpace(32);
    state.setCardValue(7, QStringLiteral("owner"), QStringLiteral("p1"));
    state.setCardValue(7, QStringLiteral("place"), 0);
    bool complete = true;
    quint64 messageId = 0x100000000ULL;
    for (const InteractionCommandDescriptor &descriptor
         : InteractionCommandRegistry::descriptors()) {
        ProtocolMessage message;
        message.type = ProtocolMessageType::Request;
        message.source = ProtocolEndpoint::Room;
        message.destination = ProtocolEndpoint::Client;
        message.command = descriptor.command;
        message.messageId = ++messageId;
        message.hasPayload = true;
        message.payload = samplePayload(descriptor.command);
        InteractionRequest request;
        QString error;
        complete = complete && ProtocolInteractionRequestBuilder::build(
            message, state, &request, &error)
            && request.requestId == message.messageId
            && request.command == descriptor.command
            && request.type == descriptor.type;
    }
    check(complete, "all 29 V2 requests build canonical TUI interactions");

    ProtocolMessage trigger;
    trigger.type = ProtocolMessageType::Request;
    trigger.source = ProtocolEndpoint::Room;
    trigger.destination = ProtocolEndpoint::Client;
    trigger.command = S_COMMAND_TRIGGER_ORDER;
    trigger.messageId = ++messageId;
    trigger.hasPayload = true;
    trigger.payload = samplePayload(S_COMMAND_TRIGGER_ORDER);
    InteractionRequest request;
    QString error;
    const bool triggerBuilt = ProtocolInteractionRequestBuilder::build(
        trigger, state, &request, &error);
    const auto *triggerPayload = request.payloadAs<TriggerOrderInteractionPayload>();
    check(triggerBuilt && triggerPayload != nullptr && triggerPayload->options.size() == 1
              && triggerPayload->options.first().responseValue
                    == QLatin1String("jizhi#2:p1:p1:p2:3"),
          "trigger-order response preserves production option fields");

    ProtocolMessage order = trigger;
    order.command = S_COMMAND_CHOOSE_ORDER;
    order.messageId = ++messageId;
    order.payload = samplePayload(S_COMMAND_CHOOSE_ORDER);
    check(ProtocolInteractionRequestBuilder::build(order, state, &request, &error)
              && request.payloadAs<ChooseOrderInteractionPayload>() != nullptr
              && request.payloadAs<ChooseOrderInteractionPayload>()->reason
                    == S_REASON_CHOOSE_ORDER_SELECT,
          "choose-order maps the V2 reason string to the domain enum");

    ProtocolMessage custom = trigger;
    custom.command = S_COMMAND_QML_INTERACT;
    custom.messageId = ++messageId;
    custom.payload = samplePayload(S_COMMAND_QML_INTERACT);
    TuiRenderer renderer(false);
    const bool customBuilt = ProtocolInteractionRequestBuilder::build(
        custom, state, &request, &error);
    check(customBuilt && !renderer.renderInteraction(request).contains(
              QStringLiteral("qml/Choose.qml")),
          "registered custom interaction renders structurally without exposing a QML path");
    QVariantMap unknown = custom.payload.toMap();
    QVariantMap interaction = unknown.value(QStringLiteral("interaction")).toMap();
    interaction.insert(QStringLiteral("type"), QStringLiteral("unknown.custom"));
    unknown.insert(QStringLiteral("interaction"), interaction);
    custom.payload = unknown;
    check(!ProtocolInteractionRequestBuilder::build(custom, state, &request, &error),
          "unknown custom interaction type fails closed");
}

QString validAnswerFor(const InteractionRequest &request)
{
    switch (request.responseSchema) {
    case InteractionResponseShape::Option:
    case InteractionResponseShape::Players:
    case InteractionResponseShape::Cards:
        return QStringLiteral("1");
    case InteractionResponseShape::Assignment:
        return QStringLiteral("p1=lord p2=guard");
    case InteractionResponseShape::Rearrangement:
        return QStringLiteral("1 2 |");
    case InteractionResponseShape::Distribution:
        return QStringLiteral("cards 1 -> 1");
    case InteractionResponseShape::GeneralArrangement:
        return QStringLiteral("1 2");
    case InteractionResponseShape::Custom:
        return QStringLiteral("{}");
    case InteractionResponseShape::None:
        return QString();
    }
    return QString();
}

void interactionRoundTripContract()
{
    ClientGameState state;
    state.setSelfName(QStringLiteral("p1"));
    state.addPlayer(QStringLiteral("p1"));
    state.addPlayer(QStringLiteral("p2"));
    state.setCardIdSpace(32);
    state.setCardValue(1, QStringLiteral("owner"), QStringLiteral("p1"));
    state.setCardValue(1, QStringLiteral("place"), 0);
    state.setCardValue(2, QStringLiteral("owner"), QStringLiteral("p1"));
    state.setCardValue(2, QStringLiteral("place"), 0);
    state.setCardValue(7, QStringLiteral("owner"), QStringLiteral("p1"));
    state.setCardValue(7, QStringLiteral("place"), 0);
    state.setGameValue(QStringLiteral("amazing_grace"), QVariantMap{
        {QStringLiteral("card_ids"), QVariantList{1, 2}},
        {QStringLiteral("disabled_card_ids"), QVariantList()}});

    TuiRenderer renderer(false);
    TuiInteractionView view(&renderer, [](const QString &) {},
        [](int cardId) { return QStringLiteral("slash:%1").arg(cardId); });
    int built = 0;
    int parsed = 0;
    int accepted = 0;
    int encoded = 0;
    quint64 messageId = 0x100000000ULL;
    QStringList failures;
    for (const InteractionCommandDescriptor &descriptor
         : InteractionCommandRegistry::descriptors()) {
        ProtocolMessage message;
        message.type = ProtocolMessageType::Request;
        message.source = ProtocolEndpoint::Room;
        message.destination = ProtocolEndpoint::Client;
        message.command = descriptor.command;
        message.messageId = ++messageId;
        message.hasPayload = true;
        message.payload = samplePayload(descriptor.command);

        InteractionRequest request;
        QString error;
        if (!ProtocolInteractionRequestBuilder::build(
                message, state, &request, &error)) {
            failures << QStringLiteral("%1 build: %2")
                .arg(QString::fromLatin1(descriptor.commandName), error);
            continue;
        }
        ++built;
        InteractionResponse response;
        if (!view.parseAnswer(request, validAnswerFor(request),
                              &response, &error)) {
            failures << QStringLiteral("%1 parse: %2")
                .arg(QString::fromLatin1(descriptor.commandName), error);
            continue;
        }
        ++parsed;
        ClientCore core;
        *core.state() = state;
        core.beginRequest(request);
        const InteractionValidation validation = core.submitResponse(response);
        if (!validation.accepted()) {
            failures << QStringLiteral("%1 validate: %2 %3")
                .arg(QString::fromLatin1(descriptor.commandName),
                     validation.reasonName(), validation.detail);
            continue;
        }
        ++accepted;
        const InteractionWireReply wire = descriptor.replyEncoder(request, response);
        if (wire.command == S_COMMAND_UNKNOWN || wire.replyTo != request.requestId
            || wire.replyTo <= std::numeric_limits<quint32>::max()) {
            failures << QStringLiteral("%1 encode")
                .arg(QString::fromLatin1(descriptor.commandName));
            continue;
        }
        ++encoded;
    }
    if (!failures.isEmpty())
        std::printf("interaction round-trip failures: %s\n",
                    qPrintable(failures.join(QStringLiteral(" | "))));
    check(built == 29 && parsed == 29 && accepted == 29 && encoded == 29,
          "all 29 interactions parse validate encode with full-width correlation");
}

void reducerContract()
{
    const QSet<int> lifecycleCommands{
        S_COMMAND_CHECK_VERSION, S_COMMAND_SIGNUP, S_COMMAND_SETUP, S_COMMAND_READY};
    bool complete = true;
    int notificationCount = 0;
    for (const ProtocolFlowDescriptor &descriptor : ProtocolPayloadRegistry::descriptors()) {
        const ProtocolFlowKey &key = descriptor.key;
        if (key.messageType != ProtocolMessageType::Notification
            || key.source != ProtocolEndpoint::Room
            || key.destination != ProtocolEndpoint::Client
            || lifecycleCommands.contains(key.command)) {
            continue;
        }
        ++notificationCount;
        complete = complete
            && ClientGameStateReducer::classifyNotification(key.command)
                != ClientFlowDisposition::Unclassified;
    }
    check(notificationCount > 50, "production room notification inventory inspected");
    check(complete, "room notifications are explicitly classified");

    ClientGameState state;
    const ClientStateReduction unknown = ClientGameStateReducer::applyNotification(
        &state, S_COMMAND_UNKNOWN, QVariantMap());
    check(!unknown.success && unknown.disposition == ClientFlowDisposition::Unclassified,
          "unknown notification fails closed");

    ClientGameState semantic;
    semantic.setSelfName(QStringLiteral("p1"));
    ClientGameStateReducer::applyNotification(&semantic, S_COMMAND_ADD_PLAYER,
        QVariantMap{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("player_name"), QStringLiteral("p1")},
                    {QStringLiteral("screen_name"), QStringLiteral("Alice")}});
    ClientGameStateReducer::applyNotification(&semantic, S_COMMAND_ATTACH_SKILL,
        QVariantMap{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("player_name"), QStringLiteral("p1")},
                    {QStringLiteral("skill_name"), QStringLiteral("jizhi")}});
    ClientGameStateReducer::applyNotification(&semantic, S_COMMAND_GET_CARD,
        QVariantMap{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("moves"), QVariantList{QVariantMap{
            {QStringLiteral("card_ids"), QVariantList{7, 8}},
            {QStringLiteral("to_player"), QStringLiteral("p1")},
            {QStringLiteral("to_place"), 0}, {QStringLiteral("to_pile"), QString()}}}}});
    ClientGameStateReducer::applyNotification(&semantic, S_COMMAND_GET_CARD,
        QVariantMap{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("moves"), QVariantList{QVariantMap{
            {QStringLiteral("card_ids"), QVariantList{9}},
            {QStringLiteral("to_player"), QStringLiteral("p1")},
            {QStringLiteral("to_place"), 1}, {QStringLiteral("to_pile"), QString()}}}}});
    check(semantic.playerValue(QStringLiteral("p1"), QStringLiteral("skills"))
                  .toStringList().contains(QStringLiteral("jizhi"))
              && semantic.cardsForPlayer(QStringLiteral("p1"), 0) == QList<int>({7, 8})
              && semantic.cardsForPlayer(QStringLiteral("p1"), 1) == QList<int>({9}),
          "reducer projects skills hand cards and equipment semantically");
    TuiRenderer renderer(false);
    check(renderer.renderHand(semantic).contains(QStringLiteral("ID=7")),
          "hand renderer snapshot includes a known card identity");

    ClientGameState selfReference;
    const QVariantMap objectName{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("action"), QStringLiteral("property")},
        {QStringLiteral("player_name"), QString::fromLatin1(S_PLAYER_SELF_REFERENCE_ID)},
        {QStringLiteral("property_name"), QStringLiteral("objectName")},
        {QStringLiteral("string_value"), QStringLiteral("p9")}};
    const QVariantMap hp{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("action"), QStringLiteral("property")},
        {QStringLiteral("player_name"), QString::fromLatin1(S_PLAYER_SELF_REFERENCE_ID)},
        {QStringLiteral("property_name"), QStringLiteral("hp")},
        {QStringLiteral("string_value"), QStringLiteral("4")}};
    ClientGameStateReducer::applyNotification(
        &selfReference, S_COMMAND_SET_PROPERTY, objectName);
    ClientGameStateReducer::applyNotification(
        &selfReference, S_COMMAND_SET_PROPERTY, hp);
    check(selfReference.selfName() == QLatin1String("p9")
              && selfReference.playerValue(QStringLiteral("p9"),
                     QStringLiteral("hp")).toInt() == 4
              && !selfReference.hasPlayer(QString::fromLatin1(S_PLAYER_SELF_REFERENCE_ID)),
          "self-reference properties project onto the signed-up player");

    const QJsonObject beforeInvalid = selfReference.toJson();
    const ClientStateReduction invalid = ClientGameStateReducer::applyNotification(
        &selfReference, S_COMMAND_SET_MARK,
        QVariantMap{{QStringLiteral("player_name"), QStringLiteral("p9")}});
    check(!invalid.success && selfReference.toJson() == beforeInvalid,
          "invalid unversioned payload does not mutate state");
}

void rendererContract()
{
    ClientGameState state;
    state.setConnectionValue(QStringLiteral("state"), QStringLiteral("active"));
    state.setConnectionValue(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    state.setConnectionValue(QStringLiteral("port"), 9527);
    state.setConnectionValue(QStringLiteral("game_version"), QStringLiteral("2.0"));
    state.setConnectionValue(QStringLiteral("mod_name"), QStringLiteral("QSanguosha"));
    state.setConnectionValue(QStringLiteral("reconnected"), true);
    state.setConnectionValue(QStringLiteral("latency_ms"), 12);
    state.setSetup(QVariantMap{{QStringLiteral("server_name"), QStringLiteral("test")},
        {QStringLiteral("game_mode"), QStringLiteral("03_1v2")},
        {QStringLiteral("player_count"), 3}});
    state.setSelfName(QStringLiteral("p1"));
    state.addPlayer(QStringLiteral("p2"));
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("screen_name"),
                         QStringLiteral("Alice"));
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("general"),
                         QStringLiteral("caocao"));
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("kingdom"),
                         QStringLiteral("wei"));
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("hp"), 3);
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("max_hp"), 4);
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("state"),
                         QStringLiteral("trust"));
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("marks"),
                         QVariantMap{{QStringLiteral("@fubi"), 1},
                                     {QStringLiteral("Global_TurnCount"), 3},
                                     {QStringLiteral("mtyanyi_phase-Clear"), 4}});
    state.setPlayerValue(QStringLiteral("p1"), QStringLiteral("flags"),
                         QStringList{QStringLiteral("CurrentPlayer")});
    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("screen_name"),
                         QStringLiteral("Bob"));
    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("alive"), false);
    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("state"),
                         QStringLiteral("offline"));
    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("hand_count"), 1);
    state.setCardValue(7, QStringLiteral("owner"), QStringLiteral("p1"));
    state.setCardValue(7, QStringLiteral("place"), 0);
    state.setCardValue(7, QStringLiteral("card_name"), QStringLiteral("slash"));
    state.setCardValue(8, QStringLiteral("owner"), QStringLiteral("p1"));
    state.setCardValue(8, QStringLiteral("place"), 1);
    state.setCardValue(8, QStringLiteral("card_name"), QStringLiteral("crossbow"));
    state.setCardValue(9, QStringLiteral("owner"), QStringLiteral("p2"));
    state.setCardValue(9, QStringLiteral("place"), 2);
    state.setCardValue(9, QStringLiteral("card_name"), QStringLiteral("indulgence"));
    state.setCardValue(99, QStringLiteral("owner"), QStringLiteral("p2"));
    state.setCardValue(99, QStringLiteral("place"), 0);
    state.setCardValue(99, QStringLiteral("card_name"), QStringLiteral("secret-card"));
    state.setGameValue(QStringLiteral("status"), QStringLiteral("active"));
    state.setGameValue(QStringLiteral("current_player"), QStringLiteral("p1"));
    state.setGameValue(QStringLiteral("current_phase"), QStringLiteral("play"));
    state.setGameValue(QStringLiteral("draw_pile_count"), 42);
    state.setGameValue(QStringLiteral("discard_pile"), QVariantList{1, 2});

    TuiRenderer plain(false);
    const QString stateSnapshot = plain.renderState(state);
    const QString playersSnapshot = plain.renderPlayers(state);
    check(stateSnapshot.contains(QStringLiteral("延遲=12ms"))
              && stateSnapshot.contains(QStringLiteral("階段：出牌階段"))
              && !stateSnapshot.contains(QChar(0x1b)),
          "plain renderer snapshot covers connection room turn and phase");
    check(playersSnapshot.contains(QStringLiteral("身分=隱藏"))
              && playersSnapshot.contains(QStringLiteral("生存=死亡"))
              && playersSnapshot.contains(QStringLiteral("crossbow"))
              && playersSnapshot.contains(QStringLiteral("indulgence"))
              && !playersSnapshot.contains(QStringLiteral("secret-card")),
          "player snapshot shows public zones without leaking an opponent hand");
    check(plain.renderHand(state).contains(QStringLiteral("slash")),
          "self hand snapshot renders authorized card identity");
    check(playersSnapshot.contains(QStringLiteral("@fubi"))
              && !playersSnapshot.contains(QStringLiteral("Global_TurnCount"))
              && !playersSnapshot.contains(QStringLiteral("mtyanyi_phase-Clear")),
          "player snapshot shows game marks and hides engine bookkeeping");
    check(!playersSnapshot.contains(QStringLiteral("CurrentPlayer")),
          "player snapshot does not leak engine flags");

    check(TuiRenderer::commandResultText(S_COMMAND_NETWORK_DELAY_TEST, true, QString()).isEmpty(),
          "a successful internal command stays out of the transcript");
    const QString trustResult = TuiRenderer::commandResultText(S_COMMAND_TRUST, true, QString());
    check(!trustResult.isEmpty() && !trustResult.contains(QString::number(S_COMMAND_TRUST)),
          "a command the player typed is confirmed by name, not by wire id");
    check(TuiRenderer::commandResultText(S_COMMAND_NETWORK_DELAY_TEST, false,
              QStringLiteral("boom")).contains(QStringLiteral("boom")),
          "a failing command is always reported with its detail");

    QStringList rejectionLines;
    TuiInteractionView rejectView(&plain,
        [&rejectionLines](const QString &line) { rejectionLines << line; },
        [](int cardId) { return QString::number(cardId); });
    const InteractionRequest rejected = cardRequest();
    rejectView.rejectResponse(rejected, InteractionResponse(),
        InteractionValidation::fail(InteractionRejection::SelectionCountOutOfRange,
            QStringLiteral("request wants 2..2 cards but the reply has 1")));
    const QString rejectionText = rejectionLines.join(QLatin1Char('\n'));
    check(!rejectionText.contains(QStringLiteral("selection_count_out_of_range")),
          "a rejected answer explains itself without an internal error code");
    check(rejectionText.contains(QStringLiteral("2")),
          "a rejected answer keeps the detail that says what was wrong");
    check(rejectionLines.size() >= 2 && rejectionLines.last().contains(QStringLiteral("互動")),
          "a rejected answer re-shows the prompt so the player can retry");

    InteractionRequest anyCard = cardRequest();
    std::get_if<CardInteractionPayload>(&anyCard.payload)->selection.pattern
        = QStringLiteral(".");
    check(!plain.renderInteraction(anyCard).contains(QStringLiteral("模式")),
          "a wildcard card pattern is not shown as a raw dot");
    InteractionRequest slashOnly = cardRequest();
    std::get_if<CardInteractionPayload>(&slashOnly.payload)->selection.pattern
        = QStringLiteral("slash");
    check(plain.renderInteraction(slashOnly).contains(QStringLiteral("模式")),
          "a real card pattern is still shown");

    TuiRenderer ansi(true);
    check(ansi.renderState(state).contains(QChar(0x1b)),
          "ANSI renderer is opt-in and deterministic");

    ClientGameState waiting;
    check(plain.renderPlayers(waiting).contains(QStringLiteral("等待玩家")),
          "waiting-room snapshot is explicit");
    state.setGameValue(QStringLiteral("game_over"), true);
    state.setGameValue(QStringLiteral("result"), QVariantMap{
        {QStringLiteral("winner_tokens"), QVariantList{QStringLiteral("lord")}},
        {QStringLiteral("standoff"), false}});
    check(plain.renderState(state).contains(QStringLiteral("遊戲結束：勝方=lord")),
          "game-over snapshot includes the authorized result");
}

void commandContract()
{
    TuiCommandIntent intent;
    QString error;
    check(TuiCommandParser::parse(QStringLiteral("/chat hello"), &intent, &error)
              && intent.type == TuiCommandType::Chat && intent.text == QLatin1String("hello"),
          "global command parser produces a typed chat intent");
    check(TuiCommandParser::parse(QStringLiteral("/trust off"), &intent, &error)
              && intent.type == TuiCommandType::Trust
              && intent.trustMode == TuiTrustMode::Disable,
          "global command parser produces a typed trust intent");
    check(TuiCommandParser::parse(QStringLiteral("/addrobot 2"), &intent, &error)
              && intent.type == TuiCommandType::AddRobot && intent.count == 2
              && !intent.fillRemaining,
          "global command parser validates robot count");
    check(!TuiCommandParser::parse(QStringLiteral("/status extra"), &intent, &error)
              && !error.isEmpty(),
          "read-only command rejects stray arguments");
    check(!TuiCommandParser::parse(QStringLiteral("/unknown"), &intent, &error),
          "unknown global command fails closed");
}

void syncContract()
{
    StateSyncPayload begin;
    begin.syncId = QStringLiteral("18446744073709551615");
    begin.phase = QStringLiteral("begin");
    begin.reconnect = true;
    StateSyncPayload parsed;
    QString error;
    check(StateSyncPayload::parse(begin.toVariant(), &parsed, &error)
              && parsed.syncId == begin.syncId && parsed.phase == begin.phase
              && parsed.reconnect,
          "STATE_SYNC preserves full-width id and phase");
    QVariantMap invalid = begin.toVariant();
    invalid.insert(QStringLiteral("sync_id"), QStringLiteral("-1"));
    check(!StateSyncPayload::parse(invalid, &parsed, &error),
          "STATE_SYNC rejects a non-positive decimal id");
    invalid.insert(QStringLiteral("sync_id"), QStringLiteral("01"));
    check(!StateSyncPayload::parse(invalid, &parsed, &error),
          "STATE_SYNC rejects a non-canonical decimal id");
}

void terminalContract()
{
    const QString sanitized = TuiRenderer::sanitize(
        QString::fromLatin1("safe\x1b[31m\x01text"), 32);
    check(!sanitized.contains(QChar(0x1b)) && !sanitized.contains(QChar(0x01))
              && sanitized.contains(QStringLiteral("safe[31mtext")),
          "terminal output strips escape and control bytes");

    TuiRenderer renderer(false);
    TuiInteractionView view(&renderer, [](const QString &) {});
    InteractionResponse response;
    QString error;
    const InteractionRequest option = optionRequest();
    check(view.parseAnswer(option, QStringLiteral("2"), &response, &error)
              && response.requestId == option.requestId
              && response.payloadAs<InteractionResponse::OptionData>() != nullptr
              && response.payloadAs<InteractionResponse::OptionData>()->value
                    == QLatin1String("beta"),
          "option parser retains quint64 correlation");

    const InteractionRequest cards = cardRequest();
    check(view.parseAnswer(cards, QStringLiteral("1-2 -> p2"), &response, &error)
              && response.requestId == cards.requestId
              && response.payloadAs<InteractionResponse::CardSelectionData>() != nullptr
              && response.payloadAs<InteractionResponse::CardSelectionData>()->cardIds
                    == QList<int>({7, 12})
              && response.payloadAs<InteractionResponse::CardSelectionData>()->targets
                    == QStringList({QStringLiteral("p2")}),
          "card range and target parser uses advertised candidates");
    check(!view.parseAnswer(cards, QStringLiteral("4"), &response, &error),
          "out-of-range card index is rejected");

    check(view.parseAnswer(cards,
              QStringLiteral("skill longdan#4: card 1 -> 2"), &response, &error)
              && response.payloadAs<InteractionResponse::CardSelectionData>() != nullptr
              && response.payloadAs<InteractionResponse::CardSelectionData>()->cardIds
                    == QList<int>({7})
              && response.payloadAs<InteractionResponse::CardSelectionData>()->targets
                    == QStringList({QStringLiteral("p3")})
              && response.payloadAs<InteractionResponse::CardSelectionData>()
                     ->activationSkillName == QLatin1String("longdan")
              && response.payloadAs<InteractionResponse::CardSelectionData>()
                     ->activationSkillInstanceId == 4,
          "skill-card grammar preserves activation instance and target order");

    InteractionRequest fixed = cards;
    auto *fixedPayload = std::get_if<CardInteractionPayload>(&fixed.payload);
    fixedPayload->fixedTargets = {QStringLiteral("p1")};
    fixedPayload->optionalTargets = {QStringLiteral("p2")};
    check(view.parseAnswer(fixed, QStringLiteral("1 -> 1"), &response, &error)
              && response.payloadAs<InteractionResponse::CardSelectionData>()->targets
                    == QStringList({QStringLiteral("p1"), QStringLiteral("p2")}),
          "fixed and indexed optional targets are combined deterministically");

    InteractionRequest noCandidates = cards;
    auto *noCandidatePayload = std::get_if<CardInteractionPayload>(&noCandidates.payload);
    noCandidatePayload->selection.selectableCards.clear();
    noCandidatePayload->suggestedCards.clear();
    check(!view.parseAnswer(noCandidates, QStringLiteral("7"), &response, &error),
          "numeric card ids are rejected without an authorized candidate list");

    InteractionRequest disabled = option;
    auto *disabledPayload = std::get_if<OptionInteractionPayload>(&disabled.payload);
    disabledPayload->options[1].enabled = false;
    check(view.parseAnswer(disabled, QStringLiteral("2"), &response, &error),
          "disabled option syntax still maps to its stable value");
    ClientCore core;
    core.beginRequest(disabled);
    check(core.submitResponse(response).rejection == InteractionRejection::DisabledOption,
          "ClientCore rejects a disabled option before wire submission");

    InteractionRequest once = option;
    ClientCore exactlyOnce;
    exactlyOnce.beginRequest(once);
    InteractionResponse first;
    view.parseAnswer(once, QStringLiteral("1"), &first, &error);
    check(exactlyOnce.submitResponse(first).accepted()
              && exactlyOnce.submitResponse(first).rejection
                    == InteractionRejection::AlreadyCompleted,
          "duplicate terminal input cannot produce a second reply");

    qint64 now = 100;
    ClientCore timed;
    timed.setClock([&now]() { return now; });
    InteractionRequest expiring = option;
    expiring.timeoutMs = 10;
    timed.beginRequest(expiring);
    now = 111;
    view.parseAnswer(expiring, QStringLiteral("1"), &first, &error);
    check(timed.submitResponse(first).rejection == InteractionRejection::RequestExpired,
          "expired prompt input is rejected as stale");
}

void scriptContract()
{
    QTemporaryDir directory;
    check(directory.isValid(), "script contract temporary directory created");
    if (!directory.isValid())
        return;

    ClientCore core;
    core.state()->setGameValue(QStringLiteral("game_over"), true);
    core.state()->setGameValue(QStringLiteral("winner"), QStringLiteral("lord camp"));
    core.state()->appendPresentationEvent(
        S_COMMAND_LOG_EVENT, QStringLiteral("game over winner lord camp"));

    const QString successPath = directory.filePath(QStringLiteral("success.txt"));
    QFile successFile(successPath);
    check(successFile.open(QIODevice::WriteOnly | QIODevice::Text),
          "script assertion fixture opened");
    if (!successFile.isOpen())
        return;
    successFile.write("assert state game.game_over true\n"
                      "assert state game.winner lord camp\n"
                      "assert log winner lord camp\n");
    successFile.close();

    bool completed = false;
    bool failed = false;
    QEventLoop successLoop;
    TuiScriptRunner successRunner(&core, [](const QString &) {});
    QString error;
    check(successRunner.load(successPath, &error),
          "script assertion fixture loaded");
    QObject::connect(&successRunner, &TuiScriptRunner::finished,
                     &successLoop, [&]() { completed = true; successLoop.quit(); });
    QObject::connect(&successRunner, &TuiScriptRunner::scriptError,
                     &successLoop, [&](const QString &) { failed = true; successLoop.quit(); });
    QTimer::singleShot(1000, &successLoop, &QEventLoop::quit);
    successRunner.start();
    successLoop.exec();
    check(completed && !failed,
          "script can assert deterministic visible state and semantic log");

    const QString failurePath = directory.filePath(QStringLiteral("failure.txt"));
    QFile failureFile(failurePath);
    check(failureFile.open(QIODevice::WriteOnly | QIODevice::Text),
          "script rejection fixture opened");
    if (!failureFile.isOpen())
        return;
    failureFile.write("assert state game.game_over false\n");
    failureFile.close();

    bool rejected = false;
    QEventLoop failureLoop;
    TuiScriptRunner failureRunner(&core, [](const QString &) {});
    check(failureRunner.load(failurePath, &error),
          "script rejection fixture loaded");
    QObject::connect(&failureRunner, &TuiScriptRunner::scriptError,
                     &failureLoop, [&](const QString &) { rejected = true; failureLoop.quit(); });
    QTimer::singleShot(1000, &failureLoop, &QEventLoop::quit);
    failureRunner.start();
    failureLoop.exec();
    check(rejected, "script rejects mismatched visible state assertion");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const int coverageOption = arguments.indexOf(QStringLiteral("--write-coverage"));
    if (coverageOption >= 0) {
        if (coverageOption + 1 >= arguments.size()) {
            std::fprintf(stderr, "--write-coverage requires an output path\n");
            return 2;
        }
        QFile output(arguments.at(coverageOption + 1));
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "cannot write coverage artifact: %s\n",
                         qPrintable(output.errorString()));
            return 2;
        }
        output.write(QJsonDocument(tuiFlowCoverageJson()).toJson(QJsonDocument::Indented));
    }
    registryContract();
    builderContract();
    interactionRoundTripContract();
    reducerContract();
    coverageContract();
    rendererContract();
    commandContract();
    syncContract();
    terminalContract();
    scriptContract();
    std::printf("[AUTOTEST] TUI_CONTRACT_RESULT status=%s failures=%d\n",
                failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
