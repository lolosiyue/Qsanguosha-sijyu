#include "skill-instance-utils.h"

namespace SkillInstanceUtils {

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
