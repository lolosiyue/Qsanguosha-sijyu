#include "interaction-model.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace {

struct TypeName
{
    InteractionType type;
    const char *name;
};

const TypeName kTypeNames[] = {
    { InteractionType::None, "none" },
    { InteractionType::ChooseRole, "choose_role" },
    { InteractionType::ChooseGeneral, "choose_general" },
    { InteractionType::ChooseDirection, "choose_direction" },
    { InteractionType::ExchangeCard, "exchange_card" },
    { InteractionType::AskPeach, "ask_peach" },
    { InteractionType::SkillGuanxing, "skill_guanxing" },
    { InteractionType::SkillGongxin, "skill_gongxin" },
    { InteractionType::SkillYiji, "skill_yiji" },
    { InteractionType::PlayCard, "play_card" },
    { InteractionType::ResponseCard, "response_card" },
    { InteractionType::DiscardCard, "discard_card" },
    { InteractionType::Choice, "choice" },
    { InteractionType::ChooseSuit, "choose_suit" },
    { InteractionType::ChooseKingdom, "choose_kingdom" },
    { InteractionType::ChoosePlayer, "choose_player" },
    { InteractionType::SkillInvoke, "skill_invoke" },
    { InteractionType::TriggerOrder, "trigger_order" },
    { InteractionType::Nullification, "nullification" },
    { InteractionType::ShowCard, "show_card" },
    { InteractionType::AmazingGrace, "amazing_grace" },
    { InteractionType::Pindian, "pindian" },
    { InteractionType::ChooseCard, "choose_card" },
    { InteractionType::ChooseOrder, "choose_order" },
    { InteractionType::ChooseRole3v3, "choose_role_3v3" },
    { InteractionType::Surrender, "surrender" },
    { InteractionType::LuckCard, "luck_card" },
    { InteractionType::AskGeneral, "ask_general" },
    { InteractionType::ArrangeGeneral, "arrange_general" },
    { InteractionType::QmlInteract, "qml_interact" },
};

QJsonArray toJsonArray(const QList<int> &values)
{
    QJsonArray array;
    foreach (int value, values)
        array.append(value);
    return array;
}

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    foreach (const QString &value, values)
        array.append(value);
    return array;
}

// QVariantMap 入面可能有 view 塞落去嘅任意值。snapshot 要穩定,所以只收
// JSON 表達得到嘅嘢,其餘轉字串。
QJsonObject toJsonObject(const QVariantMap &map)
{
    QJsonObject object;
    for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it) {
        const QJsonValue value = QJsonValue::fromVariant(it.value());
        object.insert(it.key(), value.isUndefined() ? QJsonValue(it.value().toString()) : value);
    }
    return object;
}

QByteArray compactJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject requestPayloadToJson(const InteractionPayload &payload)
{
    QJsonObject object;
    if (const OptionInteractionPayload *value = std::get_if<OptionInteractionPayload>(&payload)) {
        QJsonArray options;
        for (const InteractionOption &option : value->options)
            options.append(option.toJson());
        object.insert(QStringLiteral("options"), options);
        object.insert(QStringLiteral("enumerated"), value->enumerated);
    } else if (const PlayerInteractionPayload *value = std::get_if<PlayerInteractionPayload>(&payload)) {
        object = value->selection.toJson();
        object.insert(QStringLiteral("min"), value->selection.minSelection);
        object.insert(QStringLiteral("max"), value->selection.maxSelection);
    } else if (const CardInteractionPayload *value = std::get_if<CardInteractionPayload>(&payload)) {
        object = value->selection.toJson();
        object.insert(QStringLiteral("min"), value->selection.minSelection);
        object.insert(QStringLiteral("max"), value->selection.maxSelection);
        if (!value->sourcePlayer.isEmpty())
            object.insert(QStringLiteral("source_player"), value->sourcePlayer);
        if (!value->fixedTargets.isEmpty())
            object.insert(QStringLiteral("fixed_targets"), toJsonArray(value->fixedTargets));
        if (!value->zoneFlags.isEmpty())
            object.insert(QStringLiteral("zone_flags"), value->zoneFlags);
        if (value->handCardsVisible)
            object.insert(QStringLiteral("hand_cards_visible"), true);
        if (value->includeEquip)
            object.insert(QStringLiteral("include_equip"), true);
        if (!value->suggestedCards.isEmpty())
            object.insert(QStringLiteral("suggested_cards"), toJsonArray(value->suggestedCards));
    } else if (const RoleAssignmentInteractionPayload *value = std::get_if<RoleAssignmentInteractionPayload>(&payload)) {
        object.insert(QStringLiteral("scheme"), value->scheme);
        object.insert(QStringLiteral("players"), toJsonArray(value->playerNames));
        object.insert(QStringLiteral("roles"), toJsonArray(value->roles));
    } else if (const RearrangeCardsInteractionPayload *value = std::get_if<RearrangeCardsInteractionPayload>(&payload)) {
        object.insert(QStringLiteral("cards"), toJsonArray(value->cardIds));
        object.insert(QStringLiteral("min_top"), value->minTop);
        object.insert(QStringLiteral("max_top"), value->maxTop);
        object.insert(QStringLiteral("min_bottom"), value->minBottom);
        object.insert(QStringLiteral("max_bottom"), value->maxBottom);
        object.insert(QStringLiteral("mirrored"), value->mirrored);
    } else if (const GongxinInteractionPayload *value = std::get_if<GongxinInteractionPayload>(&payload)) {
        object.insert(QStringLiteral("target_player"), value->targetPlayer);
        object.insert(QStringLiteral("visible_cards"), toJsonArray(value->visibleCards));
        object.insert(QStringLiteral("selectable_cards"), toJsonArray(value->selectableCards));
        object.insert(QStringLiteral("enable_heart"), value->enableHeart);
    } else if (const YijiInteractionPayload *value = std::get_if<YijiInteractionPayload>(&payload)) {
        object.insert(QStringLiteral("cards"), toJsonArray(value->cardIds));
        object.insert(QStringLiteral("target_players"), toJsonArray(value->targetPlayers));
        object.insert(QStringLiteral("min_cards"), value->minCards);
        object.insert(QStringLiteral("max_cards"), value->maxCards);
    } else if (const PindianInteractionPayload *value = std::get_if<PindianInteractionPayload>(&payload)) {
        object = value->selection.toJson();
        object.insert(QStringLiteral("opponent"), value->opponent);
        object.insert(QStringLiteral("reveal_immediately"), value->revealImmediately);
    } else if (const AmazingGraceInteractionPayload *value = std::get_if<AmazingGraceInteractionPayload>(&payload)) {
        object.insert(QStringLiteral("cards"), toJsonArray(value->cardIds));
        object.insert(QStringLiteral("disabled_cards"), toJsonArray(value->disabledCards));
        object.insert(QStringLiteral("taken_cards"), toJsonArray(value->takenCards));
    } else if (const ArrangeGeneralsInteractionPayload *value = std::get_if<ArrangeGeneralsInteractionPayload>(&payload)) {
        object.insert(QStringLiteral("generals"), toJsonArray(value->generalNames));
        object.insert(QStringLiteral("arrangement"), value->arrangement);
        object.insert(QStringLiteral("slot_count"), value->slotCount);
    } else if (const CustomInteractionPayload *value = std::get_if<CustomInteractionPayload>(&payload)) {
        object.insert(QStringLiteral("schema_version"), value->schemaVersion);
        object.insert(QStringLiteral("type"), value->typeName);
        object.insert(QStringLiteral("title"), value->title);
        object.insert(QStringLiteral("payload"), value->payload);
        object.insert(QStringLiteral("response_schema"), value->responseSchema);
        object.insert(QStringLiteral("legacy"), value->legacy);
        if (!value->legacyQmlPath.isEmpty())
            object.insert(QStringLiteral("legacy_qml_path"), value->legacyQmlPath);
    }
    return object;
}

}  // namespace

QString interactionResponseShapeName(InteractionResponseShape shape)
{
    switch (shape) {
    case InteractionResponseShape::Option: return QStringLiteral("option");
    case InteractionResponseShape::Players: return QStringLiteral("players");
    case InteractionResponseShape::Cards: return QStringLiteral("cards");
    case InteractionResponseShape::Assignment: return QStringLiteral("assignment");
    case InteractionResponseShape::Rearrangement: return QStringLiteral("rearrangement");
    case InteractionResponseShape::Distribution: return QStringLiteral("distribution");
    case InteractionResponseShape::Custom: return QStringLiteral("custom");
    case InteractionResponseShape::None: break;
    }
    return QStringLiteral("none");
}

QString interactionTypeName(InteractionType type)
{
    for (const TypeName &entry : kTypeNames) {
        if (entry.type == type)
            return QString::fromLatin1(entry.name);
    }
    return QStringLiteral("none");
}

InteractionType interactionTypeFromName(const QString &name)
{
    for (const TypeName &entry : kTypeNames) {
        if (name == QLatin1String(entry.name))
            return entry.type;
    }
    return InteractionType::None;
}

InteractionOption::InteractionOption(const QString &value, const QString &label, bool enabled)
    : value(value)
    , label(label)
    , enabled(enabled)
{
}

QJsonObject InteractionOption::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("value"), value);
    if (!label.isEmpty() && label != value)
        object.insert(QStringLiteral("label"), label);
    if (!enabled)
        object.insert(QStringLiteral("enabled"), false);
    if (!metadata.isEmpty())
        object.insert(QStringLiteral("metadata"), toJsonObject(metadata));
    return object;
}

bool CardSelectionState::isActive() const
{
    return enumerated || maxSelection > 0 || !selectableCards.isEmpty() || !pattern.isEmpty();
}

QJsonObject CardSelectionState::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("selectable_cards"), toJsonArray(selectableCards));
    if (!disabledCards.isEmpty())
        object.insert(QStringLiteral("disabled_cards"), toJsonArray(disabledCards));
    if (!enumerated)
        object.insert(QStringLiteral("enumerated_cards"), false);
    if (!pattern.isEmpty())
        object.insert(QStringLiteral("pattern"), pattern);
    if (handlingMethod >= 0)
        object.insert(QStringLiteral("handling_method"), handlingMethod);
    return object;
}

bool PlayerSelectionState::isActive() const
{
    return !selectablePlayers.isEmpty() || maxSelection > 0;
}

QJsonObject PlayerSelectionState::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("selectable_players"), toJsonArray(selectablePlayers));
    return object;
}

bool InteractionRequest::isValid() const
{
    return type != InteractionType::None;
}

int InteractionRequest::minSelection() const
{
    if (const PlayerInteractionPayload *value = payloadAs<PlayerInteractionPayload>())
        return value->selection.minSelection;
    if (const CardInteractionPayload *value = payloadAs<CardInteractionPayload>())
        return value->selection.minSelection;
    if (const YijiInteractionPayload *value = payloadAs<YijiInteractionPayload>())
        return value->minCards;
    if (const PindianInteractionPayload *value = payloadAs<PindianInteractionPayload>())
        return value->selection.minSelection;
    if (const ArrangeGeneralsInteractionPayload *value = payloadAs<ArrangeGeneralsInteractionPayload>())
        return value->slotCount;
    if (const RearrangeCardsInteractionPayload *value = payloadAs<RearrangeCardsInteractionPayload>())
        return value->cardIds.isEmpty() ? 0 : 1;
    if (responseSchema == InteractionResponseShape::Cards)
        return cards.minSelection;
    if (responseSchema == InteractionResponseShape::Option)
        return cancelable ? 0 : 1;
    if (type == InteractionType::ChooseGeneral || type == InteractionType::Choice
        || type == InteractionType::SkillInvoke)
        return cancelable ? 0 : 1;
    if (type == InteractionType::ChoosePlayer)
        return players.minSelection;
    if (type == InteractionType::ResponseCard)
        return cards.minSelection;
    return 0;
}

int InteractionRequest::maxSelection() const
{
    if (const PlayerInteractionPayload *value = payloadAs<PlayerInteractionPayload>())
        return value->selection.maxSelection;
    if (const CardInteractionPayload *value = payloadAs<CardInteractionPayload>())
        return value->selection.maxSelection;
    if (const YijiInteractionPayload *value = payloadAs<YijiInteractionPayload>())
        return value->maxCards;
    if (const PindianInteractionPayload *value = payloadAs<PindianInteractionPayload>())
        return value->selection.maxSelection;
    if (const ArrangeGeneralsInteractionPayload *value = payloadAs<ArrangeGeneralsInteractionPayload>())
        return value->slotCount;
    if (const RearrangeCardsInteractionPayload *value = payloadAs<RearrangeCardsInteractionPayload>())
        return value->cardIds.size();
    if (responseSchema == InteractionResponseShape::Cards)
        return cards.maxSelection;
    if (responseSchema == InteractionResponseShape::Option)
        return 1;
    if (type == InteractionType::ChooseGeneral || type == InteractionType::Choice
        || type == InteractionType::SkillInvoke)
        return 1;
    if (type == InteractionType::ChoosePlayer)
        return players.maxSelection;
    if (type == InteractionType::ResponseCard)
        return cards.maxSelection;
    return 0;
}

bool InteractionRequest::hasOption(const QString &value) const
{
    return option(value) != nullptr;
}

const InteractionOption *InteractionRequest::option(const QString &value) const
{
    const OptionInteractionPayload *typed = payloadAs<OptionInteractionPayload>();
    const QList<InteractionOption> &available = typed != nullptr ? typed->options : options;
    for (QList<InteractionOption>::const_iterator it = available.constBegin();
         it != available.constEnd(); ++it) {
        if (it->value == value)
            return &(*it);
    }
    return nullptr;
}

QJsonObject InteractionRequest::toJson() const
{
    // 永遠出嘅 key:type／request_id／min／max／cancelable。其餘只喺有內容
    // 嗰陣先出,令 snapshot 讀得明而唔會被一堆預設值淹沒。
    QJsonObject object;
    object.insert(QStringLiteral("type"), interactionTypeName(type));
    object.insert(QStringLiteral("request_id"), static_cast<qint64>(requestId));
    object.insert(QStringLiteral("min"), minSelection());
    object.insert(QStringLiteral("max"), maxSelection());
    object.insert(QStringLiteral("cancelable"), cancelable);
    if (responseSchema != InteractionResponseShape::None)
        object.insert(QStringLiteral("response_schema"), interactionResponseShapeName(responseSchema));

    if (command != 0)
        object.insert(QStringLiteral("command"), command);
    if (serverSerial != 0)
        object.insert(QStringLiteral("server_serial"), static_cast<qint64>(serverSerial));
    if (!skillName.isEmpty())
        object.insert(QStringLiteral("skill"), skillName);
    if (!prompt.isEmpty())
        object.insert(QStringLiteral("prompt"), prompt);
    if (timeoutMs > 0)
        object.insert(QStringLiteral("timeout_ms"), timeoutMs);
    if (!optionsEnumerated)
        object.insert(QStringLiteral("enumerated_options"), false);
    if (!options.isEmpty()) {
        QJsonArray array;
        foreach (const InteractionOption &entry, options)
            array.append(entry.toJson());
        object.insert(QStringLiteral("options"), array);
    }
    if (players.isActive()) {
        const QJsonObject playerJson = players.toJson();
        for (QJsonObject::const_iterator it = playerJson.constBegin();
             it != playerJson.constEnd(); ++it)
            object.insert(it.key(), it.value());
    }
    if (cards.isActive()) {
        const QJsonObject cardJson = cards.toJson();
        for (QJsonObject::const_iterator it = cardJson.constBegin();
             it != cardJson.constEnd(); ++it)
            object.insert(it.key(), it.value());
    }
    if (!context.isEmpty())
        object.insert(QStringLiteral("context"), toJsonObject(context));
    if (!std::holds_alternative<std::monostate>(payload))
        object.insert(QStringLiteral("payload"), requestPayloadToJson(payload));
    return object;
}

QByteArray InteractionRequest::toSnapshot() const
{
    return compactJson(toJson());
}

QString interactionResponseKindName(InteractionResponseKind kind)
{
    switch (kind) {
    case InteractionResponseKind::Assignment: return QStringLiteral("assignment");
    case InteractionResponseKind::Rearrangement: return QStringLiteral("rearrangement");
    case InteractionResponseKind::Distribution: return QStringLiteral("distribution");
    case InteractionResponseKind::Custom: return QStringLiteral("custom");
    case InteractionResponseKind::Cancel: return QStringLiteral("cancel");
    case InteractionResponseKind::Option: return QStringLiteral("option");
    case InteractionResponseKind::Players: return QStringLiteral("players");
    case InteractionResponseKind::Cards: return QStringLiteral("cards");
    case InteractionResponseKind::None: break;
    }
    return QStringLiteral("none");
}

InteractionResponse InteractionResponse::makeCancel(quint64 requestId)
{
    InteractionResponse response;
    response.requestId = requestId;
    response.kind = InteractionResponseKind::Cancel;
    return response;
}

InteractionResponse InteractionResponse::makeOption(quint64 requestId, const QString &value)
{
    InteractionResponse response;
    response.requestId = requestId;
    response.kind = InteractionResponseKind::Option;
    response.option = value;
    response.structuredPayload = value;
    return response;
}

InteractionResponse InteractionResponse::makePlayers(quint64 requestId, const QStringList &names)
{
    InteractionResponse response;
    response.requestId = requestId;
    response.kind = InteractionResponseKind::Players;
    response.players = names;
    response.structuredPayload = names;
    return response;
}

InteractionResponse InteractionResponse::makeCards(quint64 requestId, const QList<int> &ids,
    const QString &cardText)
{
    InteractionResponse response;
    response.requestId = requestId;
    response.kind = InteractionResponseKind::Cards;
    response.cards = ids;
    response.cardText = cardText;
    response.structuredPayload = ids;
    return response;
}

InteractionResponse InteractionResponse::makeAssignment(quint64 requestId,
    const QStringList &names, const QStringList &values)
{
    InteractionResponse response;
    response.requestId = requestId;
    response.kind = InteractionResponseKind::Assignment;
    response.structuredPayload = AssignmentData { names, values };
    return response;
}

InteractionResponse InteractionResponse::makeRearrangement(quint64 requestId,
    const QList<int> &first, const QList<int> &second)
{
    InteractionResponse response;
    response.requestId = requestId;
    response.kind = InteractionResponseKind::Rearrangement;
    response.structuredPayload = RearrangementData { first, second };
    return response;
}

InteractionResponse InteractionResponse::makeDistribution(quint64 requestId,
    const QList<int> &ids, const QString &target)
{
    InteractionResponse response;
    response.requestId = requestId;
    response.kind = InteractionResponseKind::Distribution;
    response.structuredPayload = DistributionData { ids, target };
    return response;
}

InteractionResponse InteractionResponse::makeCustom(quint64 requestId, int schemaVersion,
    const QString &typeName, const QJsonObject &value)
{
    InteractionResponse response;
    response.requestId = requestId;
    response.kind = InteractionResponseKind::Custom;
    response.structuredPayload = CustomData { schemaVersion, typeName, value };
    return response;
}

QJsonObject InteractionResponse::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("kind"), interactionResponseKindName(kind));
    object.insert(QStringLiteral("request_id"), static_cast<qint64>(requestId));
    if (serverSerial != 0)
        object.insert(QStringLiteral("server_serial"), static_cast<qint64>(serverSerial));
    if (command != 0)
        object.insert(QStringLiteral("command"), command);
    if (!option.isEmpty())
        object.insert(QStringLiteral("option"), option);
    if (!players.isEmpty())
        object.insert(QStringLiteral("players"), toJsonArray(players));
    if (!cards.isEmpty())
        object.insert(QStringLiteral("cards"), toJsonArray(cards));
    if (!cardText.isEmpty())
        object.insert(QStringLiteral("card_text"), cardText);
    if (!payload.isEmpty())
        object.insert(QStringLiteral("payload"), toJsonObject(payload));
    if (const AssignmentData *value = std::get_if<AssignmentData>(&structuredPayload)) {
        QJsonObject structured;
        structured.insert(QStringLiteral("names"), toJsonArray(value->names));
        structured.insert(QStringLiteral("values"), toJsonArray(value->values));
        object.insert(QStringLiteral("structured_payload"), structured);
    } else if (const RearrangementData *value = std::get_if<RearrangementData>(&structuredPayload)) {
        QJsonObject structured;
        structured.insert(QStringLiteral("first"), toJsonArray(value->first));
        structured.insert(QStringLiteral("second"), toJsonArray(value->second));
        object.insert(QStringLiteral("structured_payload"), structured);
    } else if (const DistributionData *value = std::get_if<DistributionData>(&structuredPayload)) {
        QJsonObject structured;
        structured.insert(QStringLiteral("cards"), toJsonArray(value->cards));
        structured.insert(QStringLiteral("target"), value->target);
        object.insert(QStringLiteral("structured_payload"), structured);
    } else if (const CustomData *value = std::get_if<CustomData>(&structuredPayload)) {
        QJsonObject structured;
        structured.insert(QStringLiteral("schema_version"), value->schemaVersion);
        structured.insert(QStringLiteral("type"), value->typeName);
        structured.insert(QStringLiteral("value"), value->value);
        object.insert(QStringLiteral("structured_payload"), structured);
    }
    return object;
}

QByteArray InteractionResponse::toSnapshot() const
{
    return compactJson(toJson());
}

QString interactionRejectionName(InteractionRejection rejection)
{
    switch (rejection) {
    case InteractionRejection::ServerSerialMismatch: return QStringLiteral("server_serial_mismatch");
    case InteractionRejection::CommandMismatch: return QStringLiteral("command_mismatch");
    case InteractionRejection::MalformedResponse: return QStringLiteral("malformed_response");
    case InteractionRejection::UnsupportedInteraction: return QStringLiteral("unsupported_interaction");
    case InteractionRejection::None: return QStringLiteral("accepted");
    case InteractionRejection::NoActiveRequest: return QStringLiteral("no_active_request");
    case InteractionRejection::RequestIdMismatch: return QStringLiteral("request_id_mismatch");
    case InteractionRejection::AlreadyCompleted: return QStringLiteral("already_completed");
    case InteractionRejection::RequestCancelled: return QStringLiteral("request_cancelled");
    case InteractionRejection::RequestExpired: return QStringLiteral("request_expired");
    case InteractionRejection::KindMismatch: return QStringLiteral("kind_mismatch");
    case InteractionRejection::UnknownOption: return QStringLiteral("unknown_option");
    case InteractionRejection::DisabledOption: return QStringLiteral("disabled_option");
    case InteractionRejection::UnknownPlayer: return QStringLiteral("unknown_player");
    case InteractionRejection::DuplicatePlayer: return QStringLiteral("duplicate_player");
    case InteractionRejection::UnknownCard: return QStringLiteral("unknown_card");
    case InteractionRejection::DisabledCard: return QStringLiteral("disabled_card");
    case InteractionRejection::DuplicateCard: return QStringLiteral("duplicate_card");
    case InteractionRejection::SelectionCountOutOfRange: return QStringLiteral("selection_count_out_of_range");
    case InteractionRejection::NotCancelable: return QStringLiteral("not_cancelable");
    }
    return QStringLiteral("unknown");
}

InteractionValidation InteractionValidation::ok()
{
    return InteractionValidation();
}

InteractionValidation InteractionValidation::fail(InteractionRejection rejection, const QString &detail)
{
    InteractionValidation validation;
    validation.rejection = rejection;
    validation.detail = detail;
    return validation;
}

QString interactionCancelReasonName(InteractionCancelReason reason)
{
    switch (reason) {
    case InteractionCancelReason::Superseded: return QStringLiteral("superseded");
    case InteractionCancelReason::Expired: return QStringLiteral("expired");
    case InteractionCancelReason::Abandoned: return QStringLiteral("abandoned");
    case InteractionCancelReason::Disconnected: return QStringLiteral("disconnected");
    }
    return QStringLiteral("unknown");
}
