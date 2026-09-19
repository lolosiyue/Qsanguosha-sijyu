#include "player-ui-state.h"

#include <QMetaType>
#include <QVariantList>
#include <QVariantMap>
#include <cmath>
#include <limits>

namespace
{
QVariant toStringArray(const QStringList &strings)
{
    QVariantList result;
    foreach (const QString &string, strings)
        result << string;
    return result;
}

bool tryParseInt(const QVariant &value, int &result)
{
    switch (value.userType()) {
    case QMetaType::Int:
        result = value.toInt();
        return true;
    case QMetaType::UInt: {
        const uint number = value.toUInt();
        if (number > static_cast<uint>(std::numeric_limits<int>::max()))
            return false;
        result = static_cast<int>(number);
        return true;
    }
    case QMetaType::LongLong: {
        const qlonglong number = value.toLongLong();
        if (number < std::numeric_limits<int>::min()
            || number > std::numeric_limits<int>::max())
            return false;
        result = static_cast<int>(number);
        return true;
    }
    case QMetaType::ULongLong: {
        const qulonglong number = value.toULongLong();
        if (number > static_cast<qulonglong>(std::numeric_limits<int>::max()))
            return false;
        result = static_cast<int>(number);
        return true;
    }
    case QMetaType::Double: {
        // Qt 5 decodes every JSON number as double. Accept only values that
        // preserve the integer wire contract exactly.
        const double number = value.toDouble();
        if (number < std::numeric_limits<int>::min()
            || number > std::numeric_limits<int>::max()
            || number != std::floor(number))
            return false;
        result = static_cast<int>(number);
        return true;
    }
    default:
        return false;
    }
}

bool tryParseStringList(const QVariant &value, QStringList &result)
{
    if (value.userType() != QMetaType::QVariantList)
        return false;

    QStringList parsed;
    foreach (const QVariant &entry, value.toList()) {
        if (entry.userType() != QMetaType::QString)
            return false;
        parsed << entry.toString();
    }
    result = parsed;
    return true;
}

bool hasRequiredFields(const QVariantMap &map, const QStringList &fields)
{
    foreach (const QString &field, fields) {
        if (!map.contains(field))
            return false;
    }
    return true;
}
}

QVariant PlayerUIState::toVariant() const
{
    QVariantMap result;
    result.insert("handMax", handMax);
    result.insert("offensiveDistance", offensiveDistance);
    result.insert("defensiveDistance", defensiveDistance);
    result.insert("maxCardsSkills", toStringArray(maxCardsSkills));
    result.insert("offensiveSkills", toStringArray(offensiveSkills));
    result.insert("defensiveSkills", toStringArray(defensiveSkills));
    result.insert("viewAsEquipSkills", toStringArray(viewAsEquipSkills));
    if (!skillUsage.isEmpty()) result.insert("skillUsage", skillUsage);
    if (!skillValidity.isEmpty()) result.insert("skillValidity", skillValidity);
    if (!skillEffects.isEmpty()) result.insert("skillEffects", skillEffects);
    return result;
}

bool PlayerUIState::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantMap)
        return false;

    const QVariantMap map = value.toMap();
    const QStringList requiredFields = {
        "handMax",
        "offensiveDistance",
        "defensiveDistance",
        "maxCardsSkills",
        "offensiveSkills",
        "defensiveSkills",
        "viewAsEquipSkills"
    };
    if (!hasRequiredFields(map, requiredFields))
        return false;

    PlayerUIState parsed;
    if (!tryParseInt(map.value("handMax"), parsed.handMax)
        || !tryParseInt(map.value("offensiveDistance"), parsed.offensiveDistance)
        || !tryParseInt(map.value("defensiveDistance"), parsed.defensiveDistance)
        || !tryParseStringList(map.value("maxCardsSkills"), parsed.maxCardsSkills)
        || !tryParseStringList(map.value("offensiveSkills"), parsed.offensiveSkills)
        || !tryParseStringList(map.value("defensiveSkills"), parsed.defensiveSkills)
        || !tryParseStringList(map.value("viewAsEquipSkills"), parsed.viewAsEquipSkills))
        return false;

    // Optional for older recordings/servers; absence replaces, never retains, a cache.
    for (const QString &key : {QStringLiteral("skillUsage"), QStringLiteral("skillValidity")}) {
        if (map.contains(key) && map.value(key).userType() != QMetaType::QVariantMap)
            return false;
    }
    if (map.contains("skillEffects") && map.value("skillEffects").userType() != QMetaType::QVariantList)
        return false;
    parsed.skillUsage = map.value("skillUsage").toMap();
    parsed.skillValidity = map.value("skillValidity").toMap();
    parsed.skillEffects = map.value("skillEffects").toList();
    for (auto it = parsed.skillValidity.cbegin(); it != parsed.skillValidity.cend(); ++it) {
        if (it.value().userType() != QMetaType::Bool) return false;
    }
    for (auto it = parsed.skillUsage.cbegin(); it != parsed.skillUsage.cend(); ++it) {
        if (it.value().userType() != QMetaType::QVariantMap) return false;
        const QVariantMap usage = it.value().toMap();
        if (usage.value("scope").userType() != QMetaType::QString) return false;
        for (const QString &key : {QStringLiteral("used"), QStringLiteral("limit"), QStringLiteral("reserved")}) {
            int number = 0;
            if (usage.contains(key) && !tryParseInt(usage.value(key), number)) return false;
        }
    }
    for (const QVariant &effect : parsed.skillEffects) {
        if (effect.userType() != QMetaType::QVariantMap) return false;
    }
    *this = parsed;
    return true;
}

bool PlayerUIState::operator==(const PlayerUIState &other) const
{
    return handMax == other.handMax
        && offensiveDistance == other.offensiveDistance
        && defensiveDistance == other.defensiveDistance
        && maxCardsSkills == other.maxCardsSkills
        && offensiveSkills == other.offensiveSkills
        && defensiveSkills == other.defensiveSkills
        && viewAsEquipSkills == other.viewAsEquipSkills
        && skillUsage == other.skillUsage
        && skillValidity == other.skillValidity
        && skillEffects == other.skillEffects;
}

PlayerUIState PlayerUIState::forObserver() const
{
    PlayerUIState result = *this;
    result.skillUsage.clear();
    result.skillEffects.clear();
    for (const QVariant &effect : skillEffects) {
        if (effect.toMap().value("public").toBool()) result.skillEffects << effect;
    }
    return result;
}

QVariant PlayerUIStateMessage::toVariant() const
{
    QVariantMap result;
    result.insert("schema_version", 1);
    result.insert("player_name", playerName);
    result.insert("state", state.toVariant());
    return result;
}

bool PlayerUIStateMessage::tryParse(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantMap)
        return false;

    const QVariantMap map = value.toMap();
    if (!hasRequiredFields(map, { "schema_version", "player_name", "state" })
        || map.value("schema_version").toInt() != 1
        || map.value("player_name").userType() != QMetaType::QString)
        return false;

    PlayerUIState parsedState;
    if (!parsedState.tryParse(map.value("state")))
        return false;

    PlayerUIStateMessage parsed;
    parsed.playerName = map.value("player_name").toString();
    parsed.state = parsedState;
    *this = parsed;
    return true;
}
