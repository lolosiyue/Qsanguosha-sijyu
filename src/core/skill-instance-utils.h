#ifndef SKILL_INSTANCE_UTILS_H
#define SKILL_INSTANCE_UTILS_H

#include <QHash>
#include <QString>
#include "json.h"
#include "skill-instance-types.h"

// 集中實作 instance 名稱格式化與解析
// 命名慣例：
//   innate 技能   → "skillName"
//   acquired #N   → "skillName#N"
//   hidden innate → "#hiddenSkill"
//   hidden #N     → "#hiddenSkill#N"
//
// # 分隔符規則：
//   若字串以 # 開頭（隱藏技能 prefix），第一個 # 後的下一個 # 才是實例分隔符；
//   否則第一個 # 即為實例分隔符。

namespace SkillInstanceUtils {

    struct SkillActivationRequest {
        bool supplied;
        QString skillName;
        int instanceID;
        SkillActivationRequest() : supplied(false), instanceID(0) {}
    };

    // Default quota reference selection. Only activation usage supports the
    // legacy owner/name/ID fallback; source-sharing skills override
    // Skill::getUsageRef() and return their immutable sourceRef directly.
    SkillInstanceRef resolveActivationUsageRef(
        const SkillInstanceRef &activationRef,
        const QString &legacyOwnerObjectName = QString(),
        const QString &legacySkillName = QString(),
        int legacyInstanceID = 0);

    QString formatUsageMarkKey(const QString &skillName, int instanceID,
                               const QString &scopeSuffix);
    QString formatUsageReservationKey(const QString &holderObjectName,
                                      const QString &usageMarkKey);

    // Counts in-flight executions separately from committed player marks.
    class UsageReservationLedger
    {
    public:
        bool reserve(const QString &key, int committedUsage, int maxUsage);
        bool release(const QString &key);
        int count(const QString &key) const;

    private:
        QHash<QString, int> m_counts;
    };

    // Decode the optional [activation name, activation ID] reply suffix only.
    bool decodeActivationRequest(const JsonArray &usage, const QString &cardSkillName,
                                 SkillActivationRequest &request);

    // 格式化完整實例名稱
    // instanceID=0 時不回綴 #N
    QString formatName(const QString &skillName, int instanceID);

    // 解析完整名稱，拆出 base name 與 instanceID
    // instanceID=0 表示未指定；所有真實實例 ID 都是正整數
    int parseName(const QString &fullName, QString &skillName);

    // 只取 instanceID（0 表示未指定）
    inline int parseInstanceId(const QString &fullName) {
        QString unused;
        return parseName(fullName, unused);
    }

    // 只取 base name（去除 #N 尾綴）
    inline QString baseName(const QString &fullName) {
        QString name;
        parseName(fullName, name);
        return name;
    }

    // 判斷是否包含實例尾綴
    bool hasInstanceId(const QString &fullName);

    // 判斷是否為隱藏技能（以 # 開頭）
    inline bool isHiddenSkill(const QString &name) {
        return name.startsWith('#');
    }

    // 從完整名稱取得隱藏技能的純名稱（去除前綴 #）
    inline QString hiddenSkillBase(const QString &name) {
        return name.startsWith('#') ? name.mid(1) : name;
    }
}

#endif // SKILL_INSTANCE_UTILS_H
