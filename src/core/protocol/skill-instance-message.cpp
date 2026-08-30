#include "skill-instance-message.h"

#include "json.h"
#include "protocol/protocol-message-utils.h"

namespace
{
bool tryParseIdentity(const QVariantMap &object, QString &ownerName,
                      QString &skillName, int &instanceId)
{
    return ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("owner_name")), ownerName)
        && ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("skill_name")), skillName)
        && ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("instance_id")), instanceId)
        && !ownerName.isEmpty() && !skillName.isEmpty() && instanceId > 0;
}

bool isStateOperation(const QString &operation, bool privateState)
{
    return operation == "set" || operation == "remove" || operation == "clear"
        || (privateState && operation == "replace");
}
}

QVariant SkillInstanceEntryMessage::toVariant() const
{
    const SkillInstanceRef parentRef = instance.parentRef.isValid()
        ? instance.parentRef : SkillInstanceRef(ownerName, instance.parent);
    QVariantMap result{
        {QStringLiteral("owner_name"), ownerName},
        {QStringLiteral("skill_name"), instance.skillName},
        {QStringLiteral("instance_id"), instance.instanceID},
        {QStringLiteral("source"), static_cast<int>(instance.source)},
        {QStringLiteral("parent_owner"), parentRef.ownerObjectName},
        {QStringLiteral("parent_skill"), parentRef.key.skillName},
        {QStringLiteral("parent_instance_id"), parentRef.key.instanceID},
        {QStringLiteral("visible"), instance.visible},
        {QStringLiteral("bind_head"), instance.bindHead},
        {QStringLiteral("has_amount_override"), instance.hasAmountOverride}
    };
    if (instance.hasAmountOverride) {
        result.insert(QStringLiteral("amount"), instance.amountOverride);
    }
    if (!instance.correctState.isEmpty())
        result.insert(QStringLiteral("correct_state"), instance.correctState);
    if (!privateState.isEmpty())
        result.insert(QStringLiteral("state"), privateState);
    return result;
}

bool SkillInstanceEntryMessage::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantMap)
        return false;
    const QVariantMap object = value.toMap();

    SkillInstanceEntryMessage parsed;
    int source = 0;
    if (!ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("owner_name")), parsed.ownerName)
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("skill_name")), parsed.instance.skillName)
        || !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("instance_id")), parsed.instance.instanceID)
        || !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("source")), source)
        || parsed.ownerName.isEmpty() || parsed.instance.skillName.isEmpty()
        || parsed.instance.instanceID <= 0
        || source < static_cast<int>(SourceInnate)
        || source > static_cast<int>(SourceAttached))
        return false;
    parsed.instance.source = static_cast<SkillInstanceSource>(source);

    QString parentOwner;
    QString parentSkill;
    int parentId = 0;
    if (!ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("parent_owner")), parentOwner)
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("parent_skill")), parentSkill)
        || !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("parent_instance_id")), parentId)
        || !ProtocolMessageUtils::tryParseBool(object.value(QStringLiteral("visible")), parsed.instance.visible)
        || !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("bind_head")), parsed.instance.bindHead)
        || !ProtocolMessageUtils::tryParseBool(object.value(QStringLiteral("has_amount_override")), parsed.instance.hasAmountOverride))
        return false;
    parsed.instance.parentRef = SkillInstanceRef(parentOwner, SkillInstanceKey(parentSkill, parentId));
    parsed.instance.parent = parsed.instance.parentRef.key;
    if (parsed.instance.hasAmountOverride
        && !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("amount")), parsed.instance.amountOverride))
        return false;
    if (object.contains(QStringLiteral("correct_state"))) {
        if (object.value(QStringLiteral("correct_state")).userType() != QMetaType::QVariantMap)
            return false;
        parsed.instance.correctState = object.value(QStringLiteral("correct_state")).toMap();
    }
    if (object.contains(QStringLiteral("state"))) {
        if (object.value(QStringLiteral("state")).userType() != QMetaType::QVariantMap)
            return false;
        parsed.privateState = object.value(QStringLiteral("state")).toMap();
    }

    *this = parsed;
    return true;
}

SkillInstanceMessage SkillInstanceMessage::makeSnapshot(
    const QList<SkillInstanceEntryMessage> &entries)
{
    SkillInstanceMessage message;
    message.action = Snapshot;
    message.entries = entries;
    return message;
}

SkillInstanceMessage SkillInstanceMessage::makeUpsert(const SkillInstanceEntryMessage &entry)
{
    SkillInstanceMessage message;
    message.action = Upsert;
    message.entry = entry;
    return message;
}

SkillInstanceMessage SkillInstanceMessage::makeRemove(const QString &ownerName,
                                                       const QString &skillName, int instanceId)
{
    SkillInstanceMessage message;
    message.action = Remove;
    message.ownerName = ownerName;
    message.skillName = skillName;
    message.instanceId = instanceId;
    return message;
}

SkillInstanceMessage SkillInstanceMessage::makeAmount(const QString &ownerName,
                                                       const QString &skillName, int instanceId,
                                                       bool hasOverride, int amount)
{
    SkillInstanceMessage message = makeRemove(ownerName, skillName, instanceId);
    message.action = Amount;
    message.hasAmountOverride = hasOverride;
    message.amount = amount;
    return message;
}

SkillInstanceMessage SkillInstanceMessage::makeCorrectState(
    const QString &ownerName, const QString &skillName, int instanceId,
    const QString &operation, const QString &key, const QVariant &value)
{
    SkillInstanceMessage message = makeRemove(ownerName, skillName, instanceId);
    message.action = CorrectState;
    message.operation = operation;
    message.key = key;
    message.value = value;
    return message;
}

SkillInstanceMessage SkillInstanceMessage::makeState(const QString &ownerName,
                                                      const QString &skillName, int instanceId,
                                                      const QString &operation,
                                                      const QString &key, const QVariant &value)
{
    SkillInstanceMessage message = makeCorrectState(ownerName, skillName, instanceId,
                                                    operation, key, value);
    message.action = State;
    return message;
}

QVariant SkillInstanceMessage::toVariant() const
{
    QVariantMap result{{QStringLiteral("schema_version"), 1}};
    switch (action) {
    case Snapshot: {
        QVariantList serializedEntries;
        foreach (const SkillInstanceEntryMessage &entry, entries)
            serializedEntries << entry.toVariant();
        result.insert(QStringLiteral("action"), QStringLiteral("snapshot"));
        result.insert(QStringLiteral("entries"), serializedEntries);
        break;
    }
    case Upsert:
        result.insert(QStringLiteral("action"), QStringLiteral("upsert"));
        result.insert(QStringLiteral("entry"), entry.toVariant());
        break;
    case Remove:
        result.insert(QStringLiteral("action"), QStringLiteral("remove"));
        result.insert(QStringLiteral("owner_name"), ownerName);
        result.insert(QStringLiteral("skill_name"), skillName);
        result.insert(QStringLiteral("instance_id"), instanceId);
        break;
    case Amount:
        result.insert(QStringLiteral("action"), QStringLiteral("amount"));
        result.insert(QStringLiteral("owner_name"), ownerName);
        result.insert(QStringLiteral("skill_name"), skillName);
        result.insert(QStringLiteral("instance_id"), instanceId);
        result.insert(QStringLiteral("has_amount_override"), hasAmountOverride);
        result.insert(QStringLiteral("amount"), amount);
        break;
    case CorrectState:
        result.insert(QStringLiteral("action"), QStringLiteral("correct_state"));
        result.insert(QStringLiteral("owner_name"), ownerName);
        result.insert(QStringLiteral("skill_name"), skillName);
        result.insert(QStringLiteral("instance_id"), instanceId);
        result.insert(QStringLiteral("operation"), operation);
        result.insert(QStringLiteral("key"), key);
        result.insert(QStringLiteral("value"), value);
        break;
    case State:
        result.insert(QStringLiteral("action"), QStringLiteral("state"));
        result.insert(QStringLiteral("owner_name"), ownerName);
        result.insert(QStringLiteral("skill_name"), skillName);
        result.insert(QStringLiteral("instance_id"), instanceId);
        result.insert(QStringLiteral("operation"), operation);
        result.insert(QStringLiteral("key"), key);
        result.insert(QStringLiteral("value"), value);
        break;
    default:
        return QVariant();
    }
    return result;
}

bool SkillInstanceMessage::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantMap)
        return false;
    const QVariantMap object = value.toMap();
    int schemaVersion = 0;
    QString actionName;
    if (!ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("schema_version")), schemaVersion)
        || schemaVersion != 1
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("action")), actionName))
        return false;

    SkillInstanceMessage parsed;
    if (actionName == "snapshot"
        && object.value(QStringLiteral("entries")).userType() == QMetaType::QVariantList) {
        parsed.action = Snapshot;
        foreach (const QVariant &entryValue, object.value(QStringLiteral("entries")).toList()) {
            SkillInstanceEntryMessage entry;
            if (!entry.tryParse(entryValue))
                return false;
            parsed.entries << entry;
        }
    } else if (actionName == "upsert") {
        parsed.action = Upsert;
        if (!parsed.entry.tryParse(object.value(QStringLiteral("entry"))))
            return false;
    } else if (actionName == "remove") {
        parsed.action = Remove;
        if (!tryParseIdentity(object, parsed.ownerName, parsed.skillName, parsed.instanceId))
            return false;
    } else if (actionName == "amount") {
        parsed.action = Amount;
        if (!tryParseIdentity(object, parsed.ownerName, parsed.skillName, parsed.instanceId)
            || !ProtocolMessageUtils::tryParseBool(object.value(QStringLiteral("has_amount_override")), parsed.hasAmountOverride)
            || !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("amount")), parsed.amount))
            return false;
    } else if ((actionName == "correct_state" || actionName == "state")
               && object.contains(QStringLiteral("value"))) {
        const bool privateState = actionName == "state";
        parsed.action = privateState ? State : CorrectState;
        if (!tryParseIdentity(object, parsed.ownerName, parsed.skillName, parsed.instanceId)
            || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("operation")), parsed.operation)
            || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("key")), parsed.key)
            || !isStateOperation(parsed.operation, privateState)
            || (privateState && parsed.operation == "replace"
                && object.value(QStringLiteral("value")).userType() != QMetaType::QVariantMap))
            return false;
        parsed.value = object.value(QStringLiteral("value"));
    } else {
        return false;
    }

    *this = parsed;
    return true;
}
