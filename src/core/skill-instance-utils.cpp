#include "skill-instance-utils.h"

namespace SkillInstanceUtils {

    SkillInstanceRef resolveUsageRef(UsageRefKind kind,
                                     const SkillInstanceRef &activationRef,
                                     const SkillInstanceRef &sourceRef,
                                     const QString &legacyOwnerObjectName,
                                     const QString &legacySkillName,
                                     int legacyInstanceID)
    {
        switch (kind) {
        case UsageRef_ActivationInstance:
            if (activationRef.isValid()) return activationRef;
            if (!legacyOwnerObjectName.isEmpty() && !legacySkillName.isEmpty()
                && legacyInstanceID > 0)
                return SkillInstanceRef(legacyOwnerObjectName,
                                        SkillInstanceKey(legacySkillName, legacyInstanceID));
            break;
        case UsageRef_SourceInstance:
            if (sourceRef.isValid()) return sourceRef;
            break;
        default:
            break;
        }
        return SkillInstanceRef();
    }

    QString formatUsageMarkKey(const QString &skillName, int instanceID,
                               const QString &scopeSuffix)
    {
        if (skillName.isEmpty() || instanceID < 0 || scopeSuffix.isEmpty())
            return QString();
        return QString("Usage_%1_%2%3").arg(skillName).arg(instanceID).arg(scopeSuffix);
    }

    QString formatUsageReservationKey(const QString &holderObjectName,
                                      const QString &usageMarkKey)
    {
        if (holderObjectName.isEmpty() || usageMarkKey.isEmpty())
            return QString();
        return holderObjectName + ":" + usageMarkKey;
    }

    bool UsageReservationLedger::reserve(const QString &key, int committedUsage, int maxUsage)
    {
        if (key.isEmpty() || committedUsage < 0 || maxUsage <= 0)
            return false;
        const int reservedUsage = m_counts.value(key, 0);
        if (committedUsage + reservedUsage >= maxUsage)
            return false;
        m_counts.insert(key, reservedUsage + 1);
        return true;
    }

    bool UsageReservationLedger::release(const QString &key)
    {
        const int reservedUsage = m_counts.value(key, 0);
        if (reservedUsage <= 0)
            return false;
        if (reservedUsage == 1)
            m_counts.remove(key);
        else
            m_counts.insert(key, reservedUsage - 1);
        return true;
    }

    int UsageReservationLedger::count(const QString &key) const
    {
        return m_counts.value(key, 0);
    }

    bool decodeActivationRequest(const JsonArray &usage, const QString &cardSkillName,
                                 SkillActivationRequest &request)
    {
        request = SkillActivationRequest();
        if (usage.size() == 2) return true;
        if (usage.size() == 3 && JsonUtils::isNumber(usage[2])) {
            request.supplied = true;
            request.skillName = cardSkillName;
            request.instanceID = usage[2].toInt();
        } else if (usage.size() == 4 && JsonUtils::isString(usage[2]) && JsonUtils::isNumber(usage[3])) {
            request.skillName = usage[2].toString();
            request.instanceID = usage[3].toInt();
            request.supplied = !request.skillName.isEmpty() || request.instanceID != 0;
        } else return false;
        return request.instanceID >= 0 && (request.instanceID == 0 || !request.skillName.isEmpty());
    }

    QString formatName(const QString &skillName, int instanceID)
    {
        if (instanceID <= 0)
            return skillName;
        return QString("%1#%2").arg(skillName).arg(instanceID);
    }

    int parseName(const QString &fullName, QString &skillName)
    {
        if (fullName.isEmpty()) {
            skillName.clear();
            return 0;
        }

        // 隱藏技能以 # 開頭；從第二個字元開始找實例分隔符。
        int searchStart = fullName.startsWith('#') ? 1 : 0;
        int split = fullName.indexOf('#', searchStart);

        if (split == -1) {
            skillName = fullName;
            return 0;
        }

        skillName = fullName.left(split);
        bool ok = false;
        int id = fullName.mid(split + 1).toInt(&ok);
        return ok ? id : 0;
    }

    bool hasInstanceId(const QString &fullName)
    {
        return parseInstanceId(fullName) > 0;
    }
}
