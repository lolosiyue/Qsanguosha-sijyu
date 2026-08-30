#include "switch-context-message.h"

#include "protocol/protocol-message-utils.h"

QVariant SwitchContextMessage::toVariant() const
{
    return QVariantMap{{QStringLiteral("schema_version"), 1},
                       {QStringLiteral("player_name"), playerName}};
}

bool SwitchContextMessage::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantMap)
        return false;
    const QVariantMap object = value.toMap();
    int schemaVersion = 0;
    SwitchContextMessage parsed;
    if (!ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("schema_version")), schemaVersion)
        || schemaVersion != 1
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("player_name")), parsed.playerName))
        return false;
    *this = parsed;
    return true;
}
