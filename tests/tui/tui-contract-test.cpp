#include "client-game-state.h"
#include "engine-bootstrap.h"
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

#include <QDebug>

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

    ProtocolMessage playCard;
    playCard.type = ProtocolMessageType::Request;
    playCard.source = ProtocolEndpoint::Room;
    playCard.destination = ProtocolEndpoint::Client;
    playCard.command = S_COMMAND_PLAY_CARD;
    playCard.messageId = ++messageId;
    playCard.hasPayload = true;
    playCard.payload = samplePayload(S_COMMAND_PLAY_CARD);
    state.addPlayer(QStringLiteral("p1"));
    InteractionRequest playRequest;
    QString playError;
    check(ProtocolInteractionRequestBuilder::build(playCard, state, &playRequest, &playError)
              && playRequest.payloadAs<CardInteractionPayload>() != nullptr
              && playRequest.payloadAs<CardInteractionPayload>()->optionalTargets.contains(
                  QStringLiteral("p1"))
              && playRequest.payloadAs<CardInteractionPayload>()->optionalTargets.contains(
                  QStringLiteral("p2")),
          "play-card advertises numbered players as optional targets");

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

void chooseCardHiddenHandContract()
{
    ClientGameState state;
    state.setSelfName(QStringLiteral("p1"));
    state.addPlayer(QStringLiteral("p1"));
    state.addPlayer(QStringLiteral("p2"));
    state.setCardIdSpace(32);
    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("hand_count"), 3);
    state.setCardValue(20, QStringLiteral("owner"), QStringLiteral("p2"));
    state.setCardValue(20, QStringLiteral("place"), 1);
    state.setCardValue(20, QStringLiteral("card_name"), QStringLiteral("eight_diagram"));

    ProtocolMessage message;
    message.type = ProtocolMessageType::Request;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.command = S_COMMAND_CHOOSE_CARD;
    message.messageId = 0x2001;
    message.hasPayload = true;
    QVariantMap payload = samplePayload(S_COMMAND_CHOOSE_CARD);
    payload.insert(QStringLiteral("player"), QStringLiteral("p2"));
    payload.insert(QStringLiteral("zone_flags"), QStringLiteral("he"));
    payload.insert(QStringLiteral("hand_cards_visible"), false);
    message.payload = payload;

    InteractionRequest request;
    QString error;
    check(ProtocolInteractionRequestBuilder::build(message, state, &request, &error),
          "choose-card with hidden hands builds");
    const auto *cards = request.payloadAs<CardInteractionPayload>();
    check(cards != nullptr && cards->hiddenHandCount == 3
              && cards->selection.selectableCards == QList<int>{20}
              && !cards->handCardsVisible,
          "choose-card advertises hidden hand count without leaking hand ids");

    TuiRenderer renderer(false);
    const QString prompt = renderer.renderInteraction(request);
    check(prompt.contains(QStringLiteral("手牌（3 张，牌面未知）"))
              && prompt.contains(QStringLiteral("[1] 未知手牌"))
              && prompt.contains(QStringLiteral("[3] 未知手牌"))
              && !prompt.contains(QStringLiteral("ID=-1")),
          "choose-card prompt lists unknown hands by count");

    TuiInteractionView view(&renderer, [](const QString &) {});
    InteractionResponse hidden;
    check(view.parseAnswer(request, QStringLiteral("1"), &hidden, &error),
          "choose-card hidden-hand index parses");
    const auto *hiddenAnswer = hidden.payloadAs<InteractionResponse::CardSelectionData>();
    check(hiddenAnswer != nullptr && hiddenAnswer->cardIds == QList<int>{-1},
          "choose-card hidden-hand index replies with unknown sentinel");

    InteractionResponse equip;
    check(view.parseAnswer(request, QStringLiteral("4"), &equip, &error),
          "choose-card visible equip index parses");
    const auto *equipAnswer = equip.payloadAs<InteractionResponse::CardSelectionData>();
    check(equipAnswer != nullptr && equipAnswer->cardIds == QList<int>{20},
          "choose-card visible cards keep original ids");

    ClientCore core;
    *core.state() = state;
    core.beginRequest(request);
    check(core.submitResponse(hidden).accepted(),
          "choose-card unknown-hand sentinel is accepted");
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

    // Player::getPhaseString() puts a name on the wire; projecting it as an
    // integer turned every phase into 0.
    ClientGameStateReducer::applyNotification(&semantic, S_COMMAND_SET_PROPERTY,
        QVariantMap{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("action"), QStringLiteral("property")},
                    {QStringLiteral("player_name"), QStringLiteral("p1")},
                    {QStringLiteral("property_name"), QStringLiteral("phase")},
                    {QStringLiteral("string_value"), QStringLiteral("play")}});
    check(semantic.gameValue(QStringLiteral("current_phase")).toString()
              == QLatin1String("play"),
          "a phase property keeps the name the server sent");

    // The server only marshals the discard pile on a state sync, so the count
    // has to follow the moves.
    ClientGameState piles;
    const auto moveTo = [&piles](int command, const QVariantList &cardIds, int place) {
        ClientGameStateReducer::applyNotification(&piles, command,
            QVariantMap{{QStringLiteral("schema_version"), 1},
                        {QStringLiteral("moves"), QVariantList{QVariantMap{
                {QStringLiteral("card_ids"), cardIds},
                {QStringLiteral("to_player"), QString()},
                {QStringLiteral("to_place"), place},
                {QStringLiteral("to_pile"), QString()}}}}});
    };
    moveTo(S_COMMAND_LOSE_CARD, QVariantList{4, 5}, 5);
    moveTo(S_COMMAND_LOSE_CARD, QVariantList{5, 6}, 5);
    check(piles.gameValue(QStringLiteral("discard_pile")).toList().size() == 3,
          "discarded cards reach the discard pile exactly once");
    moveTo(S_COMMAND_GET_CARD, QVariantList{5}, 0);
    check(piles.gameValue(QStringLiteral("discard_pile")).toList().size() == 2,
          "a card recovered from the discard pile leaves it");

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

    state.setPlayerValue(QStringLiteral("p2"), QStringLiteral("general"),
                         QStringLiteral("zhangfei"));

    TuiRenderer plain(false);
    const auto translatePrompt = [](const QString &key) {
        if (key == QLatin1String("shoot-jink"))
            return QStringLiteral("%src 使用了【%dest】，请打出一张【闪】");
        if (key == QLatin1String("slash-jink"))
            return QStringLiteral("%src 对你使用【杀】，你需使用【闪】抵消之");
        if (key == QLatin1String("pierce_shoot"))
            return QStringLiteral("pierce_shoot");
        return key;
    };
    const auto playerPrompt = [](const QString &name) {
        return name == QLatin1String("sgs2") ? QStringLiteral("曹孟德") : name;
    };
    const QString shootJink = TuiRenderer::formatPrompt(
        QStringLiteral("shoot-jink:sgs2:pierce_shoot"), translatePrompt, playerPrompt);
    check(shootJink.contains(QStringLiteral("曹孟德"))
              && shootJink.contains(QStringLiteral("pierce_shoot"))
              && shootJink.contains(QStringLiteral("闪"))
              && !shootJink.contains(QStringLiteral("shoot-jink"))
              && !shootJink.contains(QStringLiteral("%src")),
          "askForCard prompt list fills %src/%dest instead of showing the wire key");
    const QString slashJink = TuiRenderer::formatPrompt(
        QStringLiteral("slash-jink:sgs1"), translatePrompt,
        [](const QString &name) {
            return name == QLatin1String("sgs1") ? QStringLiteral("刘玄德") : name;
        });
    check(slashJink.contains(QStringLiteral("刘玄德"))
              && slashJink.contains(QStringLiteral("杀"))
              && !slashJink.contains(QStringLiteral("slash-jink")),
          "slash-jink prompt list uses the same colon substitutions");
    InteractionRequest jinkPrompt;
    jinkPrompt.prompt = QStringLiteral("shoot-jink:sgs2:pierce_shoot");
    // The %src/%dest slots hold object names, so they go to the player
    // resolver; only the key and the arguments are lang lookups.
    const TuiRenderer promptNamed(false, TuiRenderer::Resolvers{
        {}, translatePrompt,
        [](const QString &objectName) {
            return objectName == QLatin1String("sgs2") ? QStringLiteral("曹孟德") : QString();
        }, {}});
    const QString jinkText = promptNamed.renderInteraction(jinkPrompt);
    check(jinkText.contains(QStringLiteral("曹孟德"))
              && jinkText.contains(QStringLiteral("闪"))
              && !jinkText.contains(QStringLiteral("shoot-jink")),
          "interaction renderer expands askForCard prompts");
    const QString stateSnapshot = plain.renderState(state);
    const QString playersSnapshot = plain.renderPlayers(state);
    // Stands in for the engine: lang lookups, screen names, and the kingdom a
    // general belongs to.
    const TuiRenderer engineBacked(false, TuiRenderer::Resolvers{
        {},
        [](const QString &key) {
            static const QHash<QString, QString> lang{
                {QStringLiteral("wei"), QStringLiteral("魏")},
                {QStringLiteral("shu"), QStringLiteral("蜀")},
                {QStringLiteral("caocao"), QStringLiteral("曹操")},
                {QStringLiteral("zhangfei"), QStringLiteral("张飞")},
                {QStringLiteral("lord"), QStringLiteral("主公")},
                {QStringLiteral("savage-assault-slash"),
                 QStringLiteral("%src 用了南蛮入侵，请打出一张杀")}};
            return lang.value(key, key);
        },
        [](const QString &objectName) {
            // "sgs1" is the shape a wire prompt slots a player in as.
            return objectName == QLatin1String("p1") || objectName == QLatin1String("sgs1")
                ? QStringLiteral("Alice") : QString();
        },
        [](const QString &general) {
            return general == QLatin1String("zhangfei") ? QStringLiteral("shu") : QString();
        }});
    const QString namedStateSnapshot = engineBacked.renderState(state);
    const QString namedPlayersSnapshot = engineBacked.renderPlayers(state);
    check(namedStateSnapshot.contains(QStringLiteral("Alice（p1）")),
          "the status line names the current player instead of showing an id alone");
    check(namedStateSnapshot.contains(QStringLiteral("状态=进行中")),
          "the room status reads as a game state, not as a socket state");
    // The server never broadcasts the kingdom property for a general it already
    // named, so an empty one is read off the general.
    check(namedPlayersSnapshot.contains(QStringLiteral("武将=曹操 势力=魏"))
              && namedPlayersSnapshot.contains(QStringLiteral("武将=张飞 势力=蜀")),
          "a player with no kingdom property borrows the general's kingdom");
    check(!namedPlayersSnapshot.contains(QStringLiteral("曹操/")),
          "a player with no deputy general shows no dangling separator");
    check(stateSnapshot.contains(QStringLiteral("延迟=12ms"))
              && stateSnapshot.contains(QStringLiteral("阶段：出牌阶段"))
              && !stateSnapshot.contains(QChar(0x1b)),
          "plain renderer snapshot covers connection room turn and phase");
    check(playersSnapshot.contains(QStringLiteral("身份=隐藏"))
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
    check(rejectionLines.size() >= 2 && rejectionLines.last().contains(QStringLiteral("打出牌")),
          "a rejected answer re-shows the prompt so the player can retry");

    QStringList lifecycleLines;
    TuiInteractionView lifecycleView(&plain,
        [&lifecycleLines](const QString &line) { lifecycleLines << line; });
    const InteractionRequest lifecycle = cardRequest();
    const QString wireId = QString::number(lifecycle.requestId);
    lifecycleView.finishRequest(lifecycle, InteractionResponse());
    lifecycleView.cancelRequest(lifecycle, InteractionCancelReason::Expired);
    check(lifecycleLines.size() == 2, "an accepted and a cancelled request each say so once");
    const QString lifecycleText = lifecycleLines.join(QLatin1Char('\n'));
    check(!lifecycleText.contains(wireId),
          "request lifecycle lines never show the wire request id");
    check(lifecycleLines.at(0).contains(QStringLiteral("打出牌")),
          "an accepted answer names the prompt it answered");
    check(lifecycleLines.at(1).contains(QStringLiteral("打出牌"))
              && lifecycleLines.at(1).contains(
                  TuiInteractionView::cancelReasonText(InteractionCancelReason::Expired)),
          "a cancelled request names the prompt and why it went away");

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

    InteractionRequest playPrompt;
    playPrompt.requestId = 0x200000010ULL;
    playPrompt.command = S_COMMAND_PLAY_CARD;
    playPrompt.type = InteractionType::PlayCard;
    playPrompt.responseSchema = InteractionResponseShape::Cards;
    playPrompt.cancelable = true;
    CardInteractionPayload playPayload;
    playPayload.selection.selectableCards = {7, 12};
    playPayload.selection.minSelection = 0;
    playPayload.selection.maxSelection = 1;
    playPayload.optionalTargets = {QStringLiteral("sgs1"), QStringLiteral("sgs2")};
    playPayload.skillCandidates = {SkillActivationCandidate{QStringLiteral("zhiheng"), 2}};
    playPrompt.payload = playPayload;
    const QString playText = plain.renderInteraction(playPrompt);
    // A prompt carries object names only; without a resolver the renderer has
    // nothing better to show, but it must never invent one either.
    check(playText.contains(QStringLiteral("sgs1")),
          "targets fall back to the object name when no screen name is known");
    TuiRenderer named(false, TuiRenderer::Resolvers{
        {}, {}, [](const QString &objectName) {
            return objectName == QLatin1String("sgs1") ? QStringLiteral("时语") : QString();
        }, {}});
    const QString namedPlayText = named.renderInteraction(playPrompt);
    check(namedPlayText.contains(QStringLiteral("时语（sgs1）"))
              && namedPlayText.contains(QStringLiteral("sgs2")),
          "targets show the screen name and keep the object name beside it");

    // Engine advice is rendered beside a candidate, never instead of it: a
    // wrong hint must not be able to hide a legal answer.
    TuiRenderer advised(false, TuiRenderer::Resolvers{
        {}, {}, {}, {},
        [](int cardId) { return cardId == 7 ? QStringLiteral("（不可用）") : QString(); },
        [](const QString &objectName) {
            return objectName == QLatin1String("sgs2") ? QStringLiteral(" 距离2") : QString();
        }});
    const QString advisedText = advised.renderInteraction(playPrompt);
    check(advisedText.contains(QStringLiteral("（不可用）")),
          "a card the engine rules out is marked");
    check(advisedText.contains(QStringLiteral("ID=7")) && advisedText.contains(QStringLiteral("ID=12")),
          "an advised card is still listed and still selectable");
    check(advisedText.contains(QStringLiteral("距离2")),
          "a target carries the distance the engine worked out");
    InteractionRequest disabledPrompt = playPrompt;
    std::get_if<CardInteractionPayload>(&disabledPrompt.payload)->selection.disabledCards = {7};
    const QString disabledText = advised.renderInteraction(disabledPrompt);
    check(disabledText.contains(QStringLiteral("（禁用）"))
              && !disabledText.contains(QStringLiteral("（不可用）")),
          "the server's own disabled marker wins over engine advice");
    // Where a card may be aimed is the other half of the advice: the menu
    // numbers the engine's answer against the target list printed below it.
    TuiRenderer aimed(false, TuiRenderer::Resolvers{
        {}, {}, {}, {}, {}, {},
        [](int cardId) {
            TuiRenderer::CardTargets advice;
            advice.known = true;
            if (cardId == 7)
                advice.targetFixed = true;
            else
                advice.targets = {QStringLiteral("sgs2")};
            return advice;
        }});
    const QString aimedText = aimed.renderInteraction(playPrompt);
    check(aimedText.contains(QStringLiteral("可选目标：[2]")),
          "a card's legal targets are numbered the way the target list is");
    check(aimedText.contains(QStringLiteral("无需选目标")),
          "a card that picks its own targets says so instead");
    TuiRenderer stuck(false, TuiRenderer::Resolvers{
        {}, {}, {}, {}, {}, {},
        [](int) {
            TuiRenderer::CardTargets advice;
            advice.known = true;
            return advice;
        }});
    check(stuck.renderInteraction(playPrompt).contains(QStringLiteral("无可选目标")),
          "a card with nowhere to go is called out -- isAvailable() misses this");
    check(stuck.renderInteraction(playPrompt).contains(QStringLiteral("ID=7")),
          "a card with no legal target is still listed and still selectable");
    // Advice is only worth printing for a card that could be played at all.
    TuiRenderer both(false, TuiRenderer::Resolvers{
        {}, {}, {}, {},
        [](int cardId) { return cardId == 7 ? QStringLiteral("（不可用）") : QString(); },
        {},
        [](int) {
            TuiRenderer::CardTargets advice;
            advice.known = true;
            advice.targets = {QStringLiteral("sgs2")};
            return advice;
        }});
    const QString bothText = both.renderInteraction(playPrompt);
    check(bothText.contains(QStringLiteral("ID=7）（不可用）"))
              && bothText.count(QStringLiteral("可选目标")) == 1,
          "an unplayable card is not also given a target list");

    // The instance id is not the number to type, so it only appears when one
    // skill is offered more than once.
    check(!aimedText.contains(QStringLiteral("技能 #")),
          "a skill offered once is numbered by the menu alone");
    InteractionRequest twinPrompt = playPrompt;
    std::get_if<CardInteractionPayload>(&twinPrompt.payload)->skillCandidates = {
        SkillActivationCandidate{QStringLiteral("zhiheng"), 1},
        SkillActivationCandidate{QStringLiteral("zhiheng"), 2}};
    const QString twinText = plain.renderInteraction(twinPrompt);
    check(twinText.contains(QStringLiteral("技能 #1")) && twinText.contains(QStringLiteral("技能 #2")),
          "two instances of one skill are told apart by their instance id");

    // Answering a slash with Wusheng is a skill activation, so a response
    // prompt has to number its skills after the cards the way play does.
    InteractionRequest respondingPrompt = cardRequest();
    std::get_if<CardInteractionPayload>(&respondingPrompt.payload)->skillCandidates = {
        SkillActivationCandidate{QStringLiteral("wusheng"), 0}};
    const QString respondingText = plain.renderInteraction(respondingPrompt);
    check(respondingText.contains(QStringLiteral("[4] wusheng（技能）")),
          "a response prompt lists its skills, numbered after the cards");
    TuiRenderer marked(false, TuiRenderer::Resolvers{
        {}, {}, {}, {}, {}, {}, {}, {},
        [](const QString &skillName, int) {
            return skillName == QLatin1String("wusheng") ? QStringLiteral("不可用") : QString();
        }});
    check(marked.renderInteraction(respondingPrompt)
              .contains(QStringLiteral("wusheng（技能，不可用）")),
          "a skill that cannot answer this prompt is marked, not hidden");

    InteractionRequest keyedPrompt = cardRequest();
    keyedPrompt.prompt = QStringLiteral("savage-assault-slash:sgs1");
    const QString keyedPromptText = engineBacked.renderInteraction(keyedPrompt);
    check(!keyedPromptText.contains(QStringLiteral("savage-assault-slash"))
              && keyedPromptText.contains(QStringLiteral("Alice"))
              && !keyedPromptText.contains(QStringLiteral("%src")),
          "a prompt key is rendered as its sentence with the players filled in");
    InteractionRequest markupPrompt = cardRequest();
    markupPrompt.prompt = QStringLiteral("choose<br/> <b>Source</b>: zhiheng");
    check(!plain.renderInteraction(markupPrompt).contains(QLatin1Char('<')),
          "a prompt written for the desktop log box renders as plain text");
    check(playText.contains(QStringLiteral("可选"))
              && playText.contains(QStringLiteral("目标玩家"))
              && playText.contains(QStringLiteral("结束出牌"))
              && playText.contains(QStringLiteral("1 -> 2"))
              && playText.contains(QStringLiteral("技能"))
              && !playText.contains(QStringLiteral("须选")),
          "play-card prompt lists cards, skills, numbered targets, and how to pass");

    TuiRenderer ansi(true);
    check(ansi.renderState(state).contains(QChar(0x1b)),
          "ANSI renderer is opt-in and deterministic");

    ClientGameState waiting;
    check(plain.renderPlayers(waiting).contains(QStringLiteral("等待玩家")),
          "waiting-room snapshot is explicit");
    state.setGameValue(QStringLiteral("game_over"), true);
    state.setGameValue(QStringLiteral("status"), QStringLiteral("game_over"));
    state.setGameValue(QStringLiteral("result"), QVariantMap{
        {QStringLiteral("winner_tokens"), QVariantList{QStringLiteral("lord")}},
        {QStringLiteral("standoff"), false}});
    check(plain.renderState(state).contains(QStringLiteral("游戏结束：胜方=lord")),
          "game-over snapshot includes the authorized result");
    check(engineBacked.renderState(state).contains(QStringLiteral("胜方=主公"))
              && engineBacked.renderState(state).contains(QStringLiteral("状态=已结束")),
          "the result and the room status are named, not left as wire tokens");
    check(TuiInteractionView::cancelReasonText(InteractionCancelReason::Expired)
              != interactionCancelReasonName(InteractionCancelReason::Expired),
          "a cancelled request says why in the player's language");

    // ClientLiveSession::connectionChanged carries these wire tokens straight
    // to the transcript, so every one of them needs a label.
    bool connectionStatesNamed = true;
    for (const QString &token : {QStringLiteral("connecting"), QStringLiteral("reconnecting"),
             QStringLiteral("handshake"), QStringLiteral("active"),
             QStringLiteral("disconnected"), QStringLiteral("failed")}) {
        if (plain.nameText(token) == token)
            connectionStatesNamed = false;
    }
    check(connectionStatesNamed, "every connection state reads as words, not as a wire token");
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

    const TuiCompletion addRobot = completeTuiLine(QStringLiteral("/ad"), {});
    check(addRobot.matches == QStringList{QStringLiteral("/addrobot")}
              && addRobot.line == QStringLiteral("/addrobot "),
          "tab completes a unique slash command and appends a space");
    const TuiCompletion helpPrefix = completeTuiLine(QStringLiteral("/h"), {});
    check(helpPrefix.matches.contains(QStringLiteral("/hand"))
              && helpPrefix.matches.contains(QStringLiteral("/help"))
              && helpPrefix.line == QStringLiteral("/h"),
          "tab lists ambiguous slash commands and keeps the shared prefix");
    const TuiCompletion robots = completeTuiLine(QStringLiteral("/addrobot a"), {});
    check(robots.matches == QStringList{QStringLiteral("all")}
              && robots.line == QStringLiteral("/addrobot all"),
          "tab completes /addrobot all");
    const TuiCompletion pass = completeTuiLine(
        QStringLiteral("p"), {QStringLiteral("pass"), QStringLiteral("过")});
    check(pass.matches == QStringList{QStringLiteral("pass")}
              && pass.line == QStringLiteral("pass"),
          "tab completes interaction tokens such as pass");
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
    TuiInteractionView view(&renderer, [](const QString &) {},
        TuiInteractionView::CardTextResolver(),
        [](const QString &skill, int instanceId, const QList<int> &subcards, QString *) {
            QStringList ids;
            for (int id : subcards)
                ids.append(QString::number(id));
            return QStringLiteral("@%1Card[no_suit:0]=%2#%3")
                .arg(skill, ids.join(QLatin1Char('+')), QString::number(instanceId));
        });
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

    InteractionRequest play = cards;
    play.command = S_COMMAND_PLAY_CARD;
    play.type = InteractionType::PlayCard;
    auto *playPayload = std::get_if<CardInteractionPayload>(&play.payload);
    playPayload->optionalTargets = {QStringLiteral("sgs1"), QStringLiteral("sgs2")};
    check(view.parseAnswer(play, QStringLiteral("1 -> 2"), &response, &error)
              && response.payloadAs<InteractionResponse::CardSelectionData>() != nullptr
              && response.payloadAs<InteractionResponse::CardSelectionData>()->cardIds
                    == QList<int>({7})
              && response.payloadAs<InteractionResponse::CardSelectionData>()->targets
                    == QStringList({QStringLiteral("sgs2")}),
          "play-card 1 -> 2 maps the second numbered player");
    check(view.parseAnswer(play, QStringLiteral("pass"), &response, &error)
              && response.payloadAs<InteractionResponse::CancelData>() != nullptr,
          "play-card accepts pass as end-of-phase cancel");
    check(view.parseAnswer(play, QStringLiteral("过"), &response, &error)
              && response.payloadAs<InteractionResponse::CancelData>() != nullptr,
          "play-card accepts 过 as end-of-phase cancel");

    playPayload->skillCandidates = {SkillActivationCandidate{QStringLiteral("zhiheng"), 2}};
    check(view.parseAnswer(play, QStringLiteral("4"), &response, &error)
              && response.payloadAs<InteractionResponse::CardSelectionData>() != nullptr
              && response.payloadAs<InteractionResponse::CardSelectionData>()->cardIds.isEmpty()
              && response.payloadAs<InteractionResponse::CardSelectionData>()->cardText
                    .contains(QStringLiteral("zhiheng"))
              && response.payloadAs<InteractionResponse::CardSelectionData>()
                    ->activationSkillName == QLatin1String("zhiheng")
              && response.payloadAs<InteractionResponse::CardSelectionData>()
                    ->activationSkillInstanceId == 2
              && response.payloadAs<InteractionResponse::CardSelectionData>()->subcardIds.isEmpty(),
          "play-card skill index uses the numbered SkillCard candidate");
    check(view.parseAnswer(play, QStringLiteral("4 1"), &response, &error)
              && response.payloadAs<InteractionResponse::CardSelectionData>() != nullptr
              && response.payloadAs<InteractionResponse::CardSelectionData>()->subcardIds
                    == QList<int>({7})
              && response.payloadAs<InteractionResponse::CardSelectionData>()
                    ->activationSkillName == QLatin1String("zhiheng"),
          "play-card skill index can take hand cards as subcards");
    check(view.parseAnswer(play, QStringLiteral("4 1 -> 2"), &response, &error)
              && response.payloadAs<InteractionResponse::CardSelectionData>() != nullptr
              && response.payloadAs<InteractionResponse::CardSelectionData>()->cardIds.isEmpty()
              && response.payloadAs<InteractionResponse::CardSelectionData>()->cardText
                    .contains(QStringLiteral("zhiheng"))
              && response.payloadAs<InteractionResponse::CardSelectionData>()->subcardIds
                    == QList<int>({7})
              && response.payloadAs<InteractionResponse::CardSelectionData>()->targets
                    == QStringList({QStringLiteral("sgs2")})
              && response.payloadAs<InteractionResponse::CardSelectionData>()
                    ->activationSkillName == QLatin1String("zhiheng")
              && response.payloadAs<InteractionResponse::CardSelectionData>()
                    ->activationSkillInstanceId == 2,
          "play-card skill index plus subcards plus numbered target stays a SkillCard");

    // A Guhuo-shaped skill needs one more word than its menu number: the card
    // it declares, written on the skill's own index.
    QString declaredSkill;
    QString declaredOption;
    int declarationCalls = 0;
    TuiInteractionView declaring(&renderer, [](const QString &) {},
        TuiInteractionView::CardTextResolver(),
        [](const QString &skill, int instanceId, const QList<int> &subcards, QString *) {
            QStringList ids;
            for (int id : subcards)
                ids.append(QString::number(id));
            return QStringLiteral("@%1Card[no_suit:0]=%2#%3")
                .arg(skill, ids.join(QLatin1Char('+')), QString::number(instanceId));
        },
        [&](const QString &skill, const QString &option, QString *declarationError) {
            ++declarationCalls;
            declaredSkill = skill;
            declaredOption = option;
            if (option.isEmpty() || option == QLatin1String("nope")) {
                if (declarationError != nullptr)
                    *declarationError = QStringLiteral("可声明：slash、jink");
                return false;
            }
            return true;
        });
    check(declaring.parseAnswer(play, QStringLiteral("4=slash"), &response, &error)
              && declaredSkill == QLatin1String("zhiheng")
              && declaredOption == QLatin1String("slash")
              && response.payloadAs<InteractionResponse::CardSelectionData>() != nullptr
              && response.payloadAs<InteractionResponse::CardSelectionData>()->cardText
                    .contains(QStringLiteral("zhiheng")),
          "a declaration on the skill index reaches the skill and still builds its card");
    check(declaring.parseAnswer(play, QStringLiteral("1 4=slash"), &response, &error)
              && response.payloadAs<InteractionResponse::CardSelectionData>()->subcardIds
                    == QList<int>({7}),
          "a declared activation still takes its subcards");
    declaredOption = QStringLiteral("stale");
    check(!declaring.parseAnswer(play, QStringLiteral("4"), &response, &error)
              && declaredOption.isEmpty() && !error.isEmpty(),
          "an activation with nothing declared asks the skill, and reports what it said");
    check(!declaring.parseAnswer(play, QStringLiteral("4=nope"), &response, &error)
              && !error.isEmpty(),
          "a declaration the skill refuses stops the answer");
    const int callsBefore = declarationCalls;
    check(!declaring.parseAnswer(play, QStringLiteral("1=slash 4"), &response, &error)
              && error.contains(QStringLiteral("技能编号"))
              && declarationCalls == callsBefore,
          "a declaration written on a card index is refused before the skill is asked");
    check(!declaring.parseAnswer(play, QStringLiteral("1=slash"), &response, &error)
              && error.contains(QStringLiteral("技能编号")),
          "a declaration with no activation at all is refused");
    check(!declaring.parseAnswer(play, QStringLiteral("1=slash 2=jink 4"), &response, &error)
              && error.contains(QStringLiteral("一次只能声明")),
          "two declarations in one answer are refused");
    check(declaring.parseAnswer(play, QStringLiteral("1 -> 2"), &response, &error)
              && response.payloadAs<InteractionResponse::CardSelectionData>()->cardIds
                    == QList<int>({7}),
          "an answer that activates nothing never asks about declarations");

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
    // Every string the renderer and the answer parser produce is fetched by
    // key out of lang/<language>/TUICommon.lua, which the engine loads. The
    // assertions below read that text, so the engine has to be up first.
    QString engineError;
    if (!EngineBootstrap::initialize(false, &engineError)) {
        qCritical() << "engine initialization failed:" << engineError;
        return 1;
    }
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
    chooseCardHiddenHandContract();
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
    EngineBootstrap::shutdown();
    return failures == 0 ? 0 : 1;
}
