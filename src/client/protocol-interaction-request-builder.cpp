#include "protocol-interaction-request-builder.h"

#include "core/client-game-state.h"
#include "core/custom-interaction-registry.h"
#include "interaction-command-registry.h"
#include "interaction-request-factory.h"
#include "protocol/gameplay/simple-choice-payloads.h"

#include <QJsonObject>

#include <utility>

using namespace QSanProtocol;

namespace {

bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

QStringList strings(const QVariant &value)
{
    if (value.canConvert<QStringList>()) {
        const QStringList direct = value.toStringList();
        if (!direct.isEmpty())
            return direct;
    }
    QStringList result;
    for (const QVariant &entry : value.toList())
        result.append(entry.toString());
    return result;
}

QList<int> integers(const QVariant &value)
{
    QList<int> result;
    for (const QVariant &entry : value.toList())
        result.append(entry.toInt());
    return result;
}

QList<InteractionOption> options(const QStringList &values,
                                 const QStringList &disabled = QStringList())
{
    QList<InteractionOption> result;
    for (const QString &value : values)
        result.append(InteractionOption(value, value, !disabled.contains(value)));
    return result;
}

OptionInteractionPayload optionPayload(const QStringList &values,
                                        const QStringList &disabled = QStringList())
{
    OptionInteractionPayload payload;
    payload.options = options(values, disabled);
    return payload;
}

CardInteractionPayload cardPayload(const QList<int> &cards, int minimum, int maximum,
                                   bool enumerated = true)
{
    CardInteractionPayload payload;
    payload.selection.enumerated = enumerated;
    payload.selection.selectableCards = cards;
    payload.selection.minSelection = minimum;
    payload.selection.maxSelection = maximum;
    return payload;
}

QList<int> knownSelfCards(const ClientGameState &state, bool includeEquip)
{
    QList<int> result = state.cardsForPlayer(state.selfName(), 0);
    if (includeEquip)
        result.append(state.cardsForPlayer(state.selfName(), 1));
    return result;
}

QList<int> knownZoneCards(const ClientGameState &state, const QString &player,
                          const QString &zones, bool handCardsVisible)
{
    QList<int> result;
    if (zones.contains(QLatin1Char('h'))
        && (player == state.selfName() || handCardsVisible)) {
        result.append(state.cardsForPlayer(player, 0));
    }
    if (zones.contains(QLatin1Char('e')))
        result.append(state.cardsForPlayer(player, 1));
    if (zones.contains(QLatin1Char('j')))
        result.append(state.cardsForPlayer(player, 2));
    return result;
}

int hiddenHandCountFor(const ClientGameState &state, const QString &player,
                       const QString &zones, bool handCardsVisible)
{
    if (!zones.contains(QLatin1Char('h')))
        return 0;
    if (player == state.selfName() || handCardsVisible)
        return 0;
    const int counted = state.playerValue(player, QStringLiteral("hand_count")).toInt();
    if (counted > 0)
        return counted;
    return state.cardsForPlayer(player, 0).size();
}

const CustomInteractionRegistry &tuiCustomInteractionRegistry()
{
    static const CustomInteractionRegistry registry = []() {
        CustomInteractionRegistry value;
        value.registerType(QStringLiteral("qsanguosha.qml"), 1,
                           QStringLiteral("tui.json"));
        return value;
    }();
    return registry;
}

QString promptOf(const QVariantMap &object)
{
    return object.value(QStringLiteral("prompt")).toString();
}

} // namespace

bool ProtocolInteractionRequestBuilder::build(const ProtocolMessage &message,
    const ClientGameState &state, InteractionRequest *request, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (request == nullptr)
        return fail(error, QStringLiteral("interaction request output is null"));
    if (message.type != ProtocolMessageType::Request
        || message.source != ProtocolEndpoint::Room
        || message.destination != ProtocolEndpoint::Client
        || message.messageId == 0) {
        return fail(error, QStringLiteral("interaction builder requires a correlated room request"));
    }
    const InteractionCommandDescriptor *descriptor = InteractionCommandRegistry::find(
        static_cast<CommandType>(message.command));
    if (descriptor == nullptr)
        return fail(error, QStringLiteral("unsupported production interaction command %1").arg(message.command));

    const QVariantMap object = message.payload.toMap();
    InteractionPayload payload;
    bool cancelable = object.value(QStringLiteral("optional"), false).toBool()
        || object.value(QStringLiteral("cancelable"), false).toBool()
        || object.value(QStringLiteral("can_cancel"), false).toBool();

    switch (descriptor->command) {
    case S_COMMAND_CHOOSE_ROLE: {
        RoleAssignmentInteractionPayload value;
        value.scheme = object.value(QStringLiteral("scheme")).toString();
        value.playerNames = strings(object.value(QStringLiteral("players")));
        if (value.playerNames.isEmpty())
            value.playerNames = state.playerNames();
        value.roles = strings(object.value(QStringLiteral("roles")));
        payload = value;
        cancelable = false;
        break;
    }
    case S_COMMAND_CHOOSE_GENERAL: {
        QStringList values = strings(object.value(QStringLiteral("candidates")));
        if (values.isEmpty())
            values = strings(message.payload);
        OptionInteractionPayload value = optionPayload(values);
        // PlayerDecisionService::askForGeneral() takes any general name under
        // these three, so the candidate list is a menu rather than the legal
        // set; enumerating it would make ClientCore reject the reply first.
        const QVariantMap setup = state.setup();
        const QString gameMode = setup.value(QStringLiteral("game_mode")).toString();
        value.enumerated = !setup.value(QStringLiteral("free_choose")).toBool()
            && !gameMode.startsWith(QStringLiteral("_mini_"))
            && gameMode != QStringLiteral("custom_scenario");
        payload = value;
        cancelable = false;
        break;
    }
    case S_COMMAND_CHOOSE_DIRECTION:
        payload = optionPayload({QStringLiteral("cw"), QStringLiteral("ccw")});
        cancelable = false;
        break;
    case S_COMMAND_EXCHANGE_CARD:
    case S_COMMAND_DISCARD_CARD: {
        const int minimum = object.value(QStringLiteral("min_cards")).toInt();
        const int maximum = object.value(QStringLiteral("max_cards")).toInt();
        const bool includeEquip = object.value(QStringLiteral("include_equip")).toBool();
        CardInteractionPayload value = cardPayload(
            knownSelfCards(state, includeEquip), minimum, maximum, false);
        value.selection.pattern = object.value(QStringLiteral("pattern")).toString();
        value.selection.handlingMethod = object.value(QStringLiteral("handling_method"), -1).toInt();
        value.includeEquip = includeEquip;
        payload = value;
        break;
    }
    case S_COMMAND_ASK_PEACH: {
        CardInteractionPayload value = cardPayload(knownSelfCards(state, false), 0, 1, false);
        value.selection.pattern = QStringLiteral("peach+analeptic");
        value.cardTextAllowed = true;
        value.virtualCardAllowed = true;
        value.fixedTargets << object.value(QStringLiteral("dying_player")).toString();
        payload = value;
        cancelable = true;
        break;
    }
    case S_COMMAND_SKILL_GUANXING: {
        RearrangeCardsInteractionPayload value;
        value.cardIds = integers(object.value(QStringLiteral("card_ids")));
        const QString mode = object.value(QStringLiteral("mode")).toString();
        value.mode = mode == QLatin1String("up_only") ? RearrangementMode::UpOnly
            : mode == QLatin1String("down_only") ? RearrangementMode::DownOnly
            : RearrangementMode::BothSides;
        value.minTop = value.mode == RearrangementMode::DownOnly ? 0 : 0;
        value.maxTop = value.mode == RearrangementMode::DownOnly ? 0 : value.cardIds.size();
        value.minBottom = value.mode == RearrangementMode::UpOnly ? 0 : 0;
        value.maxBottom = value.mode == RearrangementMode::UpOnly ? 0 : value.cardIds.size();
        payload = value;
        break;
    }
    case S_COMMAND_SKILL_GONGXIN: {
        GongxinInteractionPayload value;
        value.targetPlayer = object.value(QStringLiteral("player")).toString();
        value.visibleCards = integers(object.value(QStringLiteral("card_ids")));
        value.selectableCards = integers(object.value(QStringLiteral("enabled_card_ids")));
        value.allowHeartOperation = object.value(QStringLiteral("enable_heart")).toBool();
        payload = value;
        cancelable = true;
        break;
    }
    case S_COMMAND_SKILL_YIJI: {
        YijiInteractionPayload value;
        value.cardIds = integers(object.value(QStringLiteral("card_ids")));
        value.targetPlayers = strings(object.value(QStringLiteral("players")));
        value.minCards = object.value(QStringLiteral("optional")).toBool() ? 0 : 1;
        value.maxCards = object.value(QStringLiteral("max_cards"), value.cardIds.size()).toInt();
        value.remainingCount = value.cardIds.size();
        payload = value;
        break;
    }
    case S_COMMAND_PLAY_CARD: {
        CardInteractionPayload value = cardPayload(knownSelfCards(state, false), 0, 1, false);
        value.cardTextAllowed = true;
        value.virtualCardAllowed = true;
        // Numbered 1 -> 2 grammar maps through optionalTargets; leave this
        // empty and "2" is sent as the object name "2", not sgs2.
        value.optionalTargets = state.playerNames();
        payload = value;
        cancelable = true;
        break;
    }
    case S_COMMAND_RESPONSE_CARD: {
        CardInteractionPayload value = cardPayload(knownSelfCards(state, false), 0, 1, false);
        value.selection.pattern = object.value(QStringLiteral("pattern")).toString();
        value.selection.handlingMethod = object.value(QStringLiteral("handling_method"), -1).toInt();
        value.cardTextAllowed = true;
        value.virtualCardAllowed = true;
        // A response that is a use can carry targets -- a skill's card aimed at
        // two people, a slash the prompt asks to be used on someone -- and the
        // numbered "1 -> 2" grammar resolves through this list. Leave it empty
        // and "2" travels as the object name "2" rather than sgs2. Whether the
        // prompt shows a target list at all follows the handling method.
        value.optionalTargets = state.playerNames();
        payload = value;
        cancelable = !value.selection.pattern.endsWith(QLatin1Char('!'));
        break;
    }
    case S_COMMAND_MULTIPLE_CHOICE: {
        const QStringList disabled = strings(object.value(QStringLiteral("disabled_options")));
        OptionInteractionPayload value = optionPayload(strings(object.value(QStringLiteral("options"))), disabled);
        value.tip = object.value(QStringLiteral("tip")).toString();
        payload = value;
        break;
    }
    case S_COMMAND_CHOOSE_SUIT:
        payload = optionPayload({QStringLiteral("spade"), QStringLiteral("club"),
            QStringLiteral("heart"), QStringLiteral("diamond")});
        break;
    case S_COMMAND_CHOOSE_KINGDOM:
        payload = optionPayload(strings(object.value(QStringLiteral("kingdoms"))));
        break;
    case S_COMMAND_CHOOSE_PLAYER: {
        PlayerInteractionPayload value;
        value.selection.selectablePlayers = strings(object.value(QStringLiteral("players")));
        value.selection.minSelection = object.value(QStringLiteral("min_players"), 1).toInt();
        value.selection.maxSelection = object.value(QStringLiteral("max_players"), 1).toInt();
        payload = value;
        break;
    }
    case S_COMMAND_INVOKE_SKILL:
    case S_COMMAND_SURRENDER:
    case S_COMMAND_LUCK_CARD:
        payload = optionPayload({QStringLiteral("yes"), QStringLiteral("no")});
        cancelable = false;
        break;
    case S_COMMAND_TRIGGER_ORDER: {
        TriggerOrderInteractionPayload value;
        for (const QVariant &entry : object.value(QStringLiteral("options")).toList()) {
            const QVariantMap item = entry.toMap();
            TriggerOrderOption option;
            option.skillName = item.value(QStringLiteral("skill")).toString();
            option.instanceId = item.value(QStringLiteral("instanceID")).toInt();
            option.invoker = item.value(QStringLiteral("invoker")).toString();
            option.owner = item.value(QStringLiteral("owner"), option.invoker).toString();
            option.preferredTarget = item.value(QStringLiteral("preferredtarget")).toString();
            option.preferredTargetSeat = item.value(QStringLiteral("preferredtargetseat")).toInt();
            option.responseValue = item.value(QStringLiteral("response_value")).toString();
            if (option.responseValue.isEmpty()) {
                QString skillToken = option.skillName;
                if (option.instanceId > 0)
                    skillToken += QStringLiteral("#") + QString::number(option.instanceId);
                QStringList responseParts{skillToken, option.owner, option.invoker};
                if (!option.preferredTarget.isEmpty()) {
                    responseParts << option.preferredTarget
                                  << QString::number(option.preferredTargetSeat);
                }
                option.responseValue = responseParts.join(QLatin1Char(':'));
            }
            value.options.append(option);
        }
        payload = value;
        break;
    }
    case S_COMMAND_NULLIFICATION: {
        CardInteractionPayload value = cardPayload(knownSelfCards(state, false), 0, 1, false);
        value.selection.pattern = QStringLiteral("nullification");
        value.cardTextAllowed = true;
        value.virtualCardAllowed = true;
        value.sourcePlayer = object.value(QStringLiteral("source_player")).toString();
        value.fixedTargets << object.value(QStringLiteral("target_player")).toString();
        payload = value;
        cancelable = true;
        break;
    }
    case S_COMMAND_SHOW_CARD:
    case S_COMMAND_PINDIAN: {
        CardInteractionPayload value = cardPayload(knownSelfCards(state, false), 1, 1, false);
        value.selection.pattern = QStringLiteral(".");
        value.cardTextAllowed = true;
        payload = value;
        cancelable = false;
        break;
    }
    case S_COMMAND_AMAZING_GRACE: {
        AmazingGraceInteractionPayload value;
        const QVariantMap grace = state.gameValue(QStringLiteral("amazing_grace")).toMap();
        value.selection.enumerated = true;
        value.selection.selectableCards = integers(grace.value(QStringLiteral("card_ids")));
        value.selection.disabledCards = integers(grace.value(QStringLiteral("disabled_card_ids")));
        value.selection.minSelection = 1;
        value.selection.maxSelection = 1;
        payload = value;
        cancelable = object.value(QStringLiteral("refusable")).toBool();
        break;
    }
    case S_COMMAND_CHOOSE_CARD: {
        const QString sourcePlayer = object.value(QStringLiteral("player")).toString();
        const QString zoneFlags = object.value(QStringLiteral("zone_flags")).toString();
        const bool handCardsVisible = object.value(QStringLiteral("hand_cards_visible")).toBool();
        CardInteractionPayload value = cardPayload(
            knownZoneCards(state, sourcePlayer, zoneFlags, handCardsVisible), 0, 1, false);
        value.sourcePlayer = sourcePlayer;
        value.zoneFlags = zoneFlags;
        value.handCardsVisible = handCardsVisible;
        value.hiddenHandCount = hiddenHandCountFor(
            state, sourcePlayer, zoneFlags, handCardsVisible);
        value.selection.handlingMethod = object.value(QStringLiteral("handling_method"), -1).toInt();
        value.selection.disabledCards = integers(object.value(QStringLiteral("disabled_card_ids")));
        payload = value;
        break;
    }
    case S_COMMAND_CHOOSE_ORDER: {
        ChooseOrderRequestPayload parsed;
        QString parseError;
        if (!ChooseOrderRequestPayload::parseV2(message.payload, &parsed, &parseError))
            return fail(error, parseError);
        ChooseOrderInteractionPayload value;
        value.reason = parsed.reason;
        value.options = options({QStringLiteral("0"), QStringLiteral("1")});
        payload = value;
        break;
    }
    case S_COMMAND_CHOOSE_ROLE_3V3: {
        OptionInteractionPayload value = optionPayload(strings(object.value(QStringLiteral("roles"))));
        value.scheme = object.value(QStringLiteral("scheme")).toString();
        payload = value;
        break;
    }
    case S_COMMAND_ASK_GENERAL: {
        QStringList values = strings(object.value(QStringLiteral("generals")));
        if (values.isEmpty())
            values = strings(state.gameValue(QStringLiteral("general_pool")));
        payload = optionPayload(values);
        break;
    }
    case S_COMMAND_ARRANGE_GENERAL: {
        ArrangeGeneralsInteractionPayload value;
        value.generalNames = strings(object.value(QStringLiteral("generals")));
        if (value.generalNames.isEmpty())
            value.generalNames = strings(state.gameValue(QStringLiteral("general_pool")));
        value.arrangement = object.value(QStringLiteral("arrangement")).toString();
        value.slotCount = object.value(QStringLiteral("slot_count"), value.generalNames.size()).toInt();
        payload = value;
        break;
    }
    case S_COMMAND_QML_INTERACT: {
        QVariantMap interaction = object.value(QStringLiteral("interaction")).toMap();
        if (interaction.isEmpty())
            interaction = object;
        const int schemaVersion = interaction.value(QStringLiteral("schema_version"), 1).toInt();
        const QString typeName = interaction.value(QStringLiteral("type")).toString();
        if (!tuiCustomInteractionRegistry().supports(typeName, schemaVersion)
            || interaction.value(QStringLiteral("response_schema")).toMap().isEmpty()) {
            return fail(error, QStringLiteral("custom interaction is not declarative or has no response schema"));
        }
        CustomInteractionPayload value;
        value.schemaVersion = schemaVersion;
        value.typeName = typeName;
        value.title = interaction.value(QStringLiteral("title")).toString();
        value.payload = QJsonObject::fromVariantMap(interaction.value(QStringLiteral("payload")).toMap());
        value.responseSchema = QJsonObject::fromVariantMap(
            interaction.value(QStringLiteral("response_schema")).toMap());
        payload = value;
        break;
    }
    default:
        return fail(error, QStringLiteral("interaction command has no canonical request builder"));
    }

    InteractionRequest built = InteractionRequestFactory::create(descriptor->type,
        descriptor->command, descriptor->responseShape, std::move(payload), cancelable);
    built.requestId = message.messageId;
    built.prompt = promptOf(object);
    built.skillName = object.value(QStringLiteral("skill_name")).toString();
    *request = std::move(built);
    return request->isValid() || fail(error,
        QStringLiteral("canonical interaction request is incomplete for command %1").arg(message.command));
}
