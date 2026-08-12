#include "skill-instance-message.h"

#include "json.h"
#include "protocol/protocol-message-utils.h"

namespace
{
bool tryParseIdentity(const JsonArray &args, QString &ownerName, QString &skillName, int &instanceId)
{
    return ProtocolMessageUtils::tryParseString(args[1], ownerName)
        && ProtocolMessageUtils::tryParseString(args[2], skillName)
        && ProtocolMessageUtils::tryParseInt(args[3], instanceId)
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
    JsonArray result;
    const SkillInstanceRef parentRef = instance.parentRef.isValid()
        ? instance.parentRef : SkillInstanceRef(ownerName, instance.parent);
    result << ownerName << instance.skillName << instance.instanceID
           << static_cast<int>(instance.source) << parentRef.ownerObjectName
           << parentRef.key.skillName << parentRef.key.instanceID
           << instance.visible << instance.bindHead;

    QVariantMap metadata;
    if (instance.hasAmountOverride) {
        metadata.insert("has_amount", true);
        metadata.insert("amount", instance.amountOverride);
    }
    if (!instance.correctState.isEmpty())
        metadata.insert("correct_state", instance.correctState);
    if (!privateState.isEmpty())
        metadata.insert("state", privateState);
    if (!metadata.isEmpty())
        result << metadata;
    return result;
}

bool SkillInstanceEntryMessage::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantList)
        return false;

    const JsonArray args = value.toList();
    if (args.size() < 8 || args.size() > 10)
        return false;

    SkillInstanceEntryMessage parsed;
    int source = 0;
    if (!ProtocolMessageUtils::tryParseString(args[0], parsed.ownerName)
        || !ProtocolMessageUtils::tryParseString(args[1], parsed.instance.skillName)
        || !ProtocolMessageUtils::tryParseInt(args[2], parsed.instance.instanceID)
        || !ProtocolMessageUtils::tryParseInt(args[3], source)
        || parsed.ownerName.isEmpty() || parsed.instance.skillName.isEmpty()
        || parsed.instance.instanceID <= 0
        || source < static_cast<int>(SourceInnate)
        || source > static_cast<int>(SourceAttached))
        return false;
    parsed.instance.source = static_cast<SkillInstanceSource>(source);

    if (args.size() == 8) {
        QString parentSkill;
        int parentId = 0;
        if (!ProtocolMessageUtils::tryParseString(args[4], parentSkill)
            || !ProtocolMessageUtils::tryParseInt(args[5], parentId)
            || !ProtocolMessageUtils::tryParseBool(args[6], parsed.instance.visible)
            || !ProtocolMessageUtils::tryParseInt(args[7], parsed.instance.bindHead))
            return false;
        parsed.instance.parent = SkillInstanceKey(parentSkill, parentId);
        parsed.instance.parentRef = SkillInstanceRef(parsed.ownerName, parsed.instance.parent);
    } else {
        QString parentOwner;
        QString parentSkill;
        int parentId = 0;
        if (!ProtocolMessageUtils::tryParseString(args[4], parentOwner)
            || !ProtocolMessageUtils::tryParseString(args[5], parentSkill)
            || !ProtocolMessageUtils::tryParseInt(args[6], parentId)
            || !ProtocolMessageUtils::tryParseBool(args[7], parsed.instance.visible)
            || !ProtocolMessageUtils::tryParseInt(args[8], parsed.instance.bindHead))
            return false;
        parsed.instance.parentRef = SkillInstanceRef(parentOwner,
                                                     SkillInstanceKey(parentSkill, parentId));
        parsed.instance.parent = parsed.instance.parentRef.key;
    }

    if (args.size() == 10) {
        if (args[9].userType() != QMetaType::QVariantMap)
            return false;
        const QVariantMap metadata = args[9].toMap();
        if (metadata.contains("has_amount")
            && !ProtocolMessageUtils::tryParseBool(metadata.value("has_amount"),
                                                   parsed.instance.hasAmountOverride))
            return false;
        if (parsed.instance.hasAmountOverride
            && !ProtocolMessageUtils::tryParseInt(metadata.value("amount"),
                                                  parsed.instance.amountOverride))
            return false;
        if (metadata.contains("correct_state")) {
            if (metadata.value("correct_state").userType() != QMetaType::QVariantMap)
                return false;
            parsed.instance.correctState = metadata.value("correct_state").toMap();
        }
        if (metadata.contains("state")) {
            if (metadata.value("state").userType() != QMetaType::QVariantMap)
                return false;
            parsed.privateState = metadata.value("state").toMap();
        }
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
    JsonArray result;
    switch (action) {
    case Snapshot: {
        JsonArray serializedEntries;
        foreach (const SkillInstanceEntryMessage &entry, entries)
            serializedEntries << entry.toVariant();
        result << "snapshot" << QVariant::fromValue(serializedEntries);
        break;
    }
    case Upsert:
        result << "upsert" << entry.toVariant();
        break;
    case Remove:
        result << "remove" << ownerName << skillName << instanceId;
        break;
    case Amount:
        result << "amount" << ownerName << skillName << instanceId
               << hasAmountOverride << amount;
        break;
    case CorrectState:
        result << "correct_state" << ownerName << skillName << instanceId
               << operation << key << value;
        break;
    case State:
        result << "state" << ownerName << skillName << instanceId
               << operation << key << value;
        break;
    default:
        return QVariant();
    }
    return result;
}

bool SkillInstanceMessage::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantList)
        return false;

    const JsonArray args = value.toList();
    QString actionName;
    if (args.isEmpty() || !ProtocolMessageUtils::tryParseString(args[0], actionName))
        return false;

    SkillInstanceMessage parsed;
    if (actionName == "snapshot" && args.size() == 2
        && args[1].userType() == QMetaType::QVariantList) {
        parsed.action = Snapshot;
        foreach (const QVariant &value, args[1].toList()) {
            SkillInstanceEntryMessage entry;
            if (!entry.tryParse(value))
                return false;
            parsed.entries << entry;
        }
    } else if (actionName == "upsert" && args.size() == 2) {
        parsed.action = Upsert;
        if (!parsed.entry.tryParse(args[1]))
            return false;
    } else if (actionName == "remove" && args.size() == 4) {
        parsed.action = Remove;
        if (!tryParseIdentity(args, parsed.ownerName, parsed.skillName, parsed.instanceId))
            return false;
    } else if (actionName == "amount" && args.size() == 6) {
        parsed.action = Amount;
        if (!tryParseIdentity(args, parsed.ownerName, parsed.skillName, parsed.instanceId)
            || !ProtocolMessageUtils::tryParseBool(args[4], parsed.hasAmountOverride)
            || !ProtocolMessageUtils::tryParseInt(args[5], parsed.amount))
            return false;
    } else if ((actionName == "correct_state" || actionName == "state")
               && args.size() == 7) {
        const bool privateState = actionName == "state";
        parsed.action = privateState ? State : CorrectState;
        if (!tryParseIdentity(args, parsed.ownerName, parsed.skillName, parsed.instanceId)
            || !ProtocolMessageUtils::tryParseString(args[4], parsed.operation)
            || !ProtocolMessageUtils::tryParseString(args[5], parsed.key)
            || !isStateOperation(parsed.operation, privateState)
            || (privateState && parsed.operation == "replace"
                && args[6].userType() != QMetaType::QVariantMap))
            return false;
        parsed.value = args[6];
    } else {
        return false;
    }

    *this = parsed;
    return true;
}
