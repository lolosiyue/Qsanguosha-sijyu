#include "switch-context-message.h"

#include "protocol/protocol-message-utils.h"

QVariant SwitchContextMessage::toVariant() const
{
    return playerName;
}

bool SwitchContextMessage::tryParse(const QVariant &value)
{
    SwitchContextMessage parsed;
    if (!ProtocolMessageUtils::tryParseString(value, parsed.playerName))
        return false;
    *this = parsed;
    return true;
}
