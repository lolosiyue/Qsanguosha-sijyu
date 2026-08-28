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
    { InteractionType::ChooseGeneral, "choose_general" },
    { InteractionType::Choice, "choice" },
    { InteractionType::ChoosePlayer, "choose_player" },
    { InteractionType::SkillInvoke, "skill_invoke" },
    { InteractionType::ResponseCard, "response_card" },
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

}  // namespace

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
    switch (type) {
    case InteractionType::ChoosePlayer:
        return players.minSelection;
    case InteractionType::ResponseCard:
        return cards.minSelection;
    case InteractionType::ChooseGeneral:
    case InteractionType::Choice:
    case InteractionType::SkillInvoke:
        return cancelable ? 0 : 1;
    case InteractionType::None:
        break;
    }
    return 0;
}

int InteractionRequest::maxSelection() const
{
    switch (type) {
    case InteractionType::ChoosePlayer:
        return players.maxSelection;
    case InteractionType::ResponseCard:
        return cards.maxSelection;
    case InteractionType::ChooseGeneral:
    case InteractionType::Choice:
    case InteractionType::SkillInvoke:
        return 1;
    case InteractionType::None:
        break;
    }
    return 0;
}

bool InteractionRequest::hasOption(const QString &value) const
{
    return option(value) != nullptr;
}

const InteractionOption *InteractionRequest::option(const QString &value) const
{
    for (QList<InteractionOption>::const_iterator it = options.constBegin();
         it != options.constEnd(); ++it) {
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
    return object;
}

QByteArray InteractionRequest::toSnapshot() const
{
    return compactJson(toJson());
}

QString interactionResponseKindName(InteractionResponseKind kind)
{
    switch (kind) {
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
    return response;
}

InteractionResponse InteractionResponse::makePlayers(quint64 requestId, const QStringList &names)
{
    InteractionResponse response;
    response.requestId = requestId;
    response.kind = InteractionResponseKind::Players;
    response.players = names;
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
    return response;
}

QJsonObject InteractionResponse::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("kind"), interactionResponseKindName(kind));
    object.insert(QStringLiteral("request_id"), static_cast<qint64>(requestId));
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
    return object;
}

QByteArray InteractionResponse::toSnapshot() const
{
    return compactJson(toJson());
}

QString interactionRejectionName(InteractionRejection rejection)
{
    switch (rejection) {
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
