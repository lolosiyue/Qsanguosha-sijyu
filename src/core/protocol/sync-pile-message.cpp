#include "sync-pile-message.h"

#include "json.h"
#include "protocol/protocol-message-utils.h"

QVariant SyncPileMessage::toVariant() const
{
    JsonArray serializedCards;
    foreach (int cardId, cardIds)
        serializedCards << cardId;
    JsonArray result;
    result << playerName << pileName << QVariant::fromValue(serializedCards);
    return result;
}

bool SyncPileMessage::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantList)
        return false;

    const JsonArray args = value.toList();
    if (args.size() != 3)
        return false;

    SyncPileMessage parsed;
    if (!ProtocolMessageUtils::tryParseString(args[0], parsed.playerName)
        || !ProtocolMessageUtils::tryParseString(args[1], parsed.pileName)
        || !ProtocolMessageUtils::tryParseIntList(args[2], parsed.cardIds))
        return false;

    *this = parsed;
    return true;
}
