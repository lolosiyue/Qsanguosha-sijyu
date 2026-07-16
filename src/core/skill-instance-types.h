#ifndef SKILL_INSTANCE_TYPES_H
#define SKILL_INSTANCE_TYPES_H

#include <QString>
#include <QVariant>
#include <QVariantMap>

// 技能多實例基礎型別——不依賴 serverplayer，故可安全被 structs.h 與 player.h 共用

enum SkillInstanceSource {
    SourceInnate,      // 原生技能（武將天生）
    SourceAcquired,    // 後天獲得（Room::acquireSkill）
    SourceHelper,      // related helper 技能（隨父實例級聯移除）
    SourceAttached
};

struct SkillInstanceKey {
    QString skillName;
    int instanceID;

    SkillInstanceKey() : instanceID(0) {}
    SkillInstanceKey(const QString &name, int id) : skillName(name), instanceID(id) {}

    bool isValid() const { return !skillName.isEmpty(); }
    bool operator==(const SkillInstanceKey &other) const {
        return skillName == other.skillName && instanceID == other.instanceID;
    }
    bool operator!=(const SkillInstanceKey &other) const { return !(*this == other); }
    bool operator<(const SkillInstanceKey &other) const {
        if (skillName != other.skillName) return skillName < other.skillName;
        return instanceID < other.instanceID;
    }

    QString toString() const;
};

// A key is local to a player. Attached skills must preserve their owner's identity.
struct SkillInstanceRef {
    QString ownerObjectName;
    SkillInstanceKey key;

    SkillInstanceRef() {}
    SkillInstanceRef(const QString &owner, const SkillInstanceKey &instance)
        : ownerObjectName(owner), key(instance) {}

    bool isValid() const { return !ownerObjectName.isEmpty() && key.isValid() && key.instanceID > 0; }
    bool operator==(const SkillInstanceRef &other) const { return ownerObjectName == other.ownerObjectName && key == other.key; }
    bool operator!=(const SkillInstanceRef &other) const { return !(*this == other); }
    bool operator<(const SkillInstanceRef &other) const {
        if (ownerObjectName != other.ownerObjectName) return ownerObjectName < other.ownerObjectName;
        return key < other.key;
    }
};

struct SkillInstance {
    QString skillName;
    int instanceID;
    SkillInstanceSource source;
    SkillInstanceKey parent;
    SkillInstanceRef parentRef;
    bool visible;
    QVariantMap state;
    int bindHead; // 0=未綁定, 1=主將, 2=副將

    SkillInstance() : instanceID(0), source(SourceInnate), visible(true), bindHead(0) {}

    SkillInstanceKey key() const { return SkillInstanceKey(skillName, instanceID); }
};

struct SkillChangeStruct {
    QString skillName;
    int instanceID;
    SkillInstanceSource source;
    QString parentSkillName;
    int parentInstanceID;
    bool visible;

    SkillChangeStruct()
        : instanceID(0), source(SourceInnate), parentInstanceID(0), visible(true) {}
    SkillChangeStruct(const QString &name, int id)
        : skillName(name), instanceID(id), source(SourceAcquired), parentInstanceID(0), visible(true) {}

    QString toString() const { return skillName; }
    QVariant toVariant() const;
    bool tryParse(const QVariant &arg);
};

Q_DECLARE_METATYPE(SkillInstanceSource)
Q_DECLARE_METATYPE(SkillInstanceKey)
Q_DECLARE_METATYPE(SkillInstanceRef)
Q_DECLARE_METATYPE(SkillInstance)
Q_DECLARE_METATYPE(SkillChangeStruct)
#endif // SKILL_INSTANCE_TYPES_H
