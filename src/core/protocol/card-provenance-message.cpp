#include "card-provenance-message.h"

#include "json.h"
#include "protocol/protocol-message-utils.h"

QVariant CardProvenanceMessage::toVariant() const
{
    JsonArray result;
    if (version == 1) {
        result << 1 << kind << initiator << card
               << sourceSkill << sourceInstanceId
               << activationSkill << activationInstanceId;
    } else {
        result << CurrentVersion << kind << initiator << card
               << sourceOwner << sourceSkill << sourceInstanceId
               << activationOwner << activationSkill << activationInstanceId;
    }
    return result;
}

bool CardProvenanceMessage::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantList)
        return false;

    const JsonArray args = value.toList();
    int parsedVersion = 0;
    if (args.isEmpty() || !ProtocolMessageUtils::tryParseInt(args[0], parsedVersion))
        return false;
    const bool legacy = parsedVersion == 1 && args.size() == 8;
    const bool current = parsedVersion == CurrentVersion && args.size() == 10;
    if (!legacy && !current)
        return false;

    CardProvenanceMessage parsed;
    parsed.version = parsedVersion;
    if (!ProtocolMessageUtils::tryParseString(args[1], parsed.kind)
        || !ProtocolMessageUtils::tryParseString(args[2], parsed.initiator)
        || !ProtocolMessageUtils::tryParseString(args[3], parsed.card))
        return false;

    if (legacy) {
        parsed.sourceOwner = parsed.initiator;
        parsed.activationOwner = parsed.initiator;
        if (!ProtocolMessageUtils::tryParseString(args[4], parsed.sourceSkill)
            || !ProtocolMessageUtils::tryParseInt(args[5], parsed.sourceInstanceId)
            || !ProtocolMessageUtils::tryParseString(args[6], parsed.activationSkill)
            || !ProtocolMessageUtils::tryParseInt(args[7], parsed.activationInstanceId))
            return false;
    } else if (!ProtocolMessageUtils::tryParseString(args[4], parsed.sourceOwner)
               || !ProtocolMessageUtils::tryParseString(args[5], parsed.sourceSkill)
               || !ProtocolMessageUtils::tryParseInt(args[6], parsed.sourceInstanceId)
               || !ProtocolMessageUtils::tryParseString(args[7], parsed.activationOwner)
               || !ProtocolMessageUtils::tryParseString(args[8], parsed.activationSkill)
               || !ProtocolMessageUtils::tryParseInt(args[9], parsed.activationInstanceId)) {
        return false;
    }

    *this = parsed;
    return true;
}
