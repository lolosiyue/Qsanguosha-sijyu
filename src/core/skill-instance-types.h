#ifndef SKILL_INSTANCE_TYPES_H
#define SKILL_INSTANCE_TYPES_H

#include <QString>
#include <QVariant>
#include <QVariantMap>

// Multi-instance skill value types; independent of ServerPlayer so structs.h and player.h can share them.

enum SkillInstanceSource {
    SourceInnate,      // Innate general skill.
    SourceAcquired,    // Acquired through Room::acquireSkill.
    SourceHelper,      // Related helper, removed with its parent instance.
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

// Native equipment admission is provenance, not a fabricated player skill instance.
// Only EquipSkillV2 can mint a receipt; interception and wire parsing cannot invent one.
class PhysicalEquipSource {
public:
    PhysicalEquipSource() = default;
    bool isValid() const { return m_cardId >= 0 && !m_holder.isEmpty() && !m_equipment.isEmpty() && !m_skill.isEmpty(); }
    const QString &holder() const { return m_holder; }
    const QString &equipment() const { return m_equipment; }
    const QString &skill() const { return m_skill; }
    int cardId() const { return m_cardId; }
    QVariantMap toVariantMap() const {
        return isValid() ? QVariantMap{{"holder", m_holder}, {"equipment", m_equipment}, {"skill", m_skill}, {"card_id", m_cardId}} : QVariantMap();
    }
    bool operator==(const PhysicalEquipSource &other) const {
        return m_holder == other.m_holder && m_equipment == other.m_equipment && m_skill == other.m_skill && m_cardId == other.m_cardId;
    }
    bool operator!=(const PhysicalEquipSource &other) const { return !(*this == other); }
private:
    friend class EquipSkillV2;
    PhysicalEquipSource(const QString &holder, const QString &equipment, const QString &skill, int cardId)
        : m_holder(holder), m_equipment(equipment), m_skill(skill), m_cardId(cardId) {}
    QString m_holder;
    QString m_equipment;
    QString m_skill;
    int m_cardId = -1;
};

struct SkillInstance {
    QString skillName;
    int instanceID;
    SkillInstanceSource source;
    SkillInstanceKey parent;
    SkillInstanceRef parentRef;
    // Applied grants outlive their provider. These are provenance, not lifecycle links.
    SkillInstanceRef frozenSourceRef;
    SkillInstanceRef grantActivationRef;
    bool visible;
    bool hasAmountOverride;
    int amountOverride;
    QVariantMap correctState;
    int bindHead; // 0=unbound, 1=head general, 2=deputy general.

    SkillInstance()
        : instanceID(0), source(SourceInnate), visible(true),
          hasAmountOverride(false), amountOverride(0), bindHead(0) {}

    SkillInstanceKey key() const { return SkillInstanceKey(skillName, instanceID); }

private:
    friend class Player;
    friend class RoomManagedState;
    // Private logical state is server-authoritative and synchronized only to its owner; see Room::notifySkillInstanceState.
    QVariantMap state;
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
