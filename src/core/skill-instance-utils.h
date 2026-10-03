#ifndef SKILL_INSTANCE_UTILS_H
#define SKILL_INSTANCE_UTILS_H

#include <QHash>
#include <QList>
#include <QString>
#include "json.h"
#include "skill-instance-types.h"

// Centralize formatting and parsing of instance names.
// Naming:
//   innate skill   -> "skillName"
//   acquired #N    -> "skillName#N"
//   hidden innate  -> "#hiddenSkill"
//   hidden #N      -> "#hiddenSkill#N"
//
// # separator rule:
//   For a leading hidden-skill #, the next # is the instance separator.
//   Otherwise, the first # is the instance separator.

namespace SkillInstanceUtils {

    // Shared by server activation and client/server passive evaluation. The
    // lookup supplies the appropriate room/view without exposing hidden data.
    template <typename Lookup>
    SkillInstanceRef resolveRootRef(const SkillInstanceRef &ref, Lookup lookup,
                                   bool followFrozenSource = true)
    {
        SkillInstanceRef current = ref;
        QList<SkillInstanceRef> visited;
        while (current.isValid() && !visited.contains(current)) {
            visited << current;
            const SkillInstance *instance = lookup(current);
            if (!instance) return SkillInstanceRef();
            if (followFrozenSource && instance->frozenSourceRef.isValid())
                return instance->frozenSourceRef;
            if (instance->parentRef.isValid()) {
                current = instance->parentRef;
            } else if (instance->source == SourceHelper && instance->parent.isValid()) {
                current = SkillInstanceRef(current.ownerObjectName, instance->parent);
            } else {
                // Callers decide whether an unlinked source type is usable.
                // Preserve activation's existing terminal-reference semantics.
                return current;
            }
        }
        return SkillInstanceRef();
    }

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

    // Format the complete instance name.
    // instanceID=0 omits the #N suffix.
    QString formatName(const QString &skillName, int instanceID);

    // Parse the full name into base name and instanceID.
    // instanceID=0 means unspecified; every live instance ID is positive.
    int parseName(const QString &fullName, QString &skillName);

    // Return only the instanceID (0 means unspecified).
    inline int parseInstanceId(const QString &fullName) {
        QString unused;
        return parseName(fullName, unused);
    }

    // Return only the base name (without the #N suffix).
    inline QString baseName(const QString &fullName) {
        QString name;
        parseName(fullName, name);
        return name;
    }

    // Check for an instance suffix.
    bool hasInstanceId(const QString &fullName);

    // Check whether the name is a hidden skill (starts with #).
    inline bool isHiddenSkill(const QString &name) {
        return name.startsWith('#');
    }

    // Return the hidden skill's plain name (without the leading #).
    inline QString hiddenSkillBase(const QString &name) {
        return name.startsWith('#') ? name.mid(1) : name;
    }
}

#endif // SKILL_INSTANCE_UTILS_H
