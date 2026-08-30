#include "sync-pile-message.h"

#include "json.h"
#include "protocol/protocol-message-utils.h"

QVariant SyncPileMessage::toVariant() const
{
    JsonArray serializedCards;
    foreach (int cardId, cardIds)
        serializedCards << cardId;
    return QVariantMap{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("player_name"), playerName},
                       {QStringLiteral("pile_name"), pileName},
                       {QStringLiteral("card_ids"), QVariant::fromValue(serializedCards)}};
}

bool SyncPileMessage::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantMap)
        return false;
    const QVariantMap object = value.toMap();
    int schemaVersion = 0;
    SyncPileMessage parsed;
    if (!ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("schema_version")), schemaVersion)
        || schemaVersion != 1
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("player_name")), parsed.playerName)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("pile_name")), parsed.pileName)
        || !ProtocolMessageUtils::tryParseIntList(
            object.value(QStringLiteral("card_ids")), parsed.cardIds))
        return false;

    *this = parsed;
    return true;
}
