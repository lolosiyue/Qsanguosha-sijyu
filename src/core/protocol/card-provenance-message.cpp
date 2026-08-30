#include "card-provenance-message.h"

#include "json.h"
#include "protocol/protocol-message-utils.h"

QVariant CardProvenanceMessage::toVariant() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), CurrentVersion},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("initiator"), initiator},
        {QStringLiteral("card"), card},
        {QStringLiteral("source_owner"), sourceOwner},
        {QStringLiteral("source_skill"), sourceSkill},
        {QStringLiteral("source_instance_id"), sourceInstanceId},
        {QStringLiteral("activation_owner"), activationOwner},
        {QStringLiteral("activation_skill"), activationSkill},
        {QStringLiteral("activation_instance_id"), activationInstanceId}
    };
}

bool CardProvenanceMessage::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantMap)
        return false;
    const QVariantMap object = value.toMap();
    int parsedVersion = 0;
    if (!ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("schema_version")), parsedVersion)
        || parsedVersion != CurrentVersion)
        return false;

    CardProvenanceMessage parsed;
    parsed.version = parsedVersion;
    if (!ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("kind")), parsed.kind)
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("initiator")), parsed.initiator)
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("card")), parsed.card))
        return false;

    if (!ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("source_owner")), parsed.sourceOwner)
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("source_skill")), parsed.sourceSkill)
        || !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("source_instance_id")), parsed.sourceInstanceId)
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("activation_owner")), parsed.activationOwner)
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("activation_skill")), parsed.activationSkill)
        || !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("activation_instance_id")), parsed.activationInstanceId)) {
        return false;
    }

    *this = parsed;
    return true;
}
