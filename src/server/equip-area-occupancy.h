#ifndef EQUIP_AREA_OCCUPANCY_H
#define EQUIP_AREA_OCCUPANCY_H

#include <QMap>
#include <QMutex>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <limits>
#include <utility>

// Owned by Room. Seat names are stable even while the live seat order changes.
class EquipAreaOccupancy
{
public:
    struct Update
    {
        quint64 generation;
        bool changed;
    };

    struct EffectiveAreas
    {
        bool ownArea;
        QStringList occupiedAreas;
    };

    struct Grant
    {
        QString occupier;
        quint64 generation;
        QString reason;
        bool requiresExpiry;
    };

    struct Snapshot
    {
        quint64 lastGeneration = 0;
        QMap<QString, Grant> grants;

        QVariantList rows() const
        {
            QVariantList result;
            for (auto it = grants.cbegin(); it != grants.cend(); ++it) {
                QVariantMap row{{"area_owner", it.key()}, {"occupier", it->occupier},
                                {"generation", QString::number(it->generation)}, {"source_skill", it->reason}};
                if (it->requiresExpiry)
                    row.insert("expiry_required", true);
                result << row;
            }
            return result;
        }

        QVariantMap wire() const
        {
            return {{"last_generation", QString::number(lastGeneration)}, {"grants", rows()}};
        }

        static bool reject(QString *error, const QString &reason)
        {
            if (error) *error = reason;
            return false;
        }

        static bool parse(const QVariant &wire, Snapshot &snapshot, QString *error)
        {
            const auto invalid = [error] {
                return reject(error, QStringLiteral("invalid equip_area_occupancy snapshot"));
            };
            const auto decimal = [](const QVariant &value, quint64 &number) {
                if (value.userType() != QMetaType::QString) return false;
                bool ok = false;
                number = value.toString().toULongLong(&ok);
                return ok && value.toString() == QString::number(number);
            };
            const QVariantMap table = wire.toMap();
            Snapshot result;
            if (wire.userType() != QMetaType::QVariantMap
                || table.value("grants").userType() != QMetaType::QVariantList
                || !decimal(table.value("last_generation"), result.lastGeneration))
                return invalid();
            QSet<quint64> generations;
            for (const QVariant &value : table.value("grants").toList()) {
                const QVariantMap row = value.toMap();
                quint64 generation = 0;
                if (value.userType() != QMetaType::QVariantMap
                    || row.value("area_owner").userType() != QMetaType::QString
                    || row.value("occupier").userType() != QMetaType::QString
                    || row.value("source_skill").userType() != QMetaType::QString
                    || (row.contains("expiry_required") && row.value("expiry_required").userType() != QMetaType::Bool)
                    || !decimal(row.value("generation"), generation)
                    || generation == 0 || generation > result.lastGeneration || generations.contains(generation))
                    return invalid();
                const QString owner = row.value("area_owner").toString();
                const QString occupier = row.value("occupier").toString();
                if (owner.isEmpty() || occupier.isEmpty() || owner == occupier || result.grants.contains(owner))
                    return invalid();
                generations.insert(generation);
                result.grants.insert(owner, {occupier, generation, row.value("source_skill").toString(),
                                             row.value("expiry_required").toBool()});
            }
            snapshot = std::move(result);
            return true;
        }

        template <typename HasExpiry>
        bool validate(const QSet<QString> &alive, HasExpiry hasExpiry, QString *error) const
        {
            for (auto it = grants.cbegin(); it != grants.cend(); ++it) {
                if (!alive.contains(it.key()) || !alive.contains(it->occupier))
                    return reject(error, QStringLiteral("equip area occupancy references an unknown or dead seat"));
                if (it->requiresExpiry && !hasExpiry(*it))
                    return reject(error, QStringLiteral("equip area occupancy grant %1 has no restorable expiry").arg(it->generation));
            }
            return true;
        }
    };

    // A timed skill persists its rule boundary under the exact grant token in
    // its own instance state. The token is a decimal string, not a Lua number.
    static bool hasPersistedExpiry(const QVariantMap &instanceState, quint64 generation)
    {
        const QVariant entry = instanceState.value("equip_area_occupancy_expiry")
            .toMap().value(QString::number(generation));
        return entry.userType() == QMetaType::QString && !entry.toString().isEmpty();
    }

    Snapshot snapshot() const
    {
        QMutexLocker locker(&m_mutex);
        return {m_generation, m_grants};
    }

    void swapSnapshot(Snapshot &snapshot) noexcept
    {
        QMutexLocker locker(&m_mutex);
        // The checkpoint counter is restored. Tokens issued on an abandoned
        // branch remain reserved within this room.
        m_highestIssued = qMax(m_highestIssued, qMax(m_generation, snapshot.lastGeneration));
        m_grants.swap(snapshot.grants);
        std::swap(m_generation, snapshot.lastGeneration);
    }

    // reason names the granting skill; it is display data and never decides a result.
    Update assign(const QString &areaOwner, const QString &occupier, bool renew = false,
                  const QString &reason = QString())
    {
        QMutexLocker locker(&m_mutex);
        if (occupier.isEmpty())
            return {0, m_grants.remove(areaOwner) != 0};

        const auto current = m_grants.constFind(areaOwner);
        if (!renew && current != m_grants.cend() && current->occupier == occupier)
            return {current->generation, false};

        // Every independently expiring grant gets its own token, including a
        // new grant to the seat already occupying the area.
        if (m_highestIssued == std::numeric_limits<quint64>::max())
            return {0, false};
        m_generation = ++m_highestIssued;
        m_grants.insert(areaOwner, {occupier, m_generation, reason, renew});
        return {m_generation, true};
    }

    // The public projection clients render: one row per occupied area, ordered by area owner.
    QVariantList publicTable() const
    {
        QMutexLocker locker(&m_mutex);
        QVariantList table;
        for (auto it = m_grants.cbegin(); it != m_grants.cend(); ++it)
            table << QVariantMap{{QStringLiteral("area_owner"), it.key()},
                                 {QStringLiteral("occupier"), it->occupier},
                                 {QStringLiteral("reason"), it->reason}};
        return table;
    }

    bool release(const QString &areaOwner, const QString &occupier, quint64 generation)
    {
        QMutexLocker locker(&m_mutex);
        const auto current = m_grants.find(areaOwner);
        if (current == m_grants.end() || current->occupier != occupier
            || (generation != 0 && current->generation != generation))
            return false;

        m_grants.erase(current);
        return true;
    }

    bool releaseForPlayer(const QString &player)
    {
        QMutexLocker locker(&m_mutex);
        bool changed = false;
        for (auto it = m_grants.begin(); it != m_grants.end();) {
            if (it.key() == player || it->occupier == player) {
                it = m_grants.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        return changed;
    }

    EffectiveAreas effectiveAreas(const QString &player) const
    {
        QMutexLocker locker(&m_mutex);
        EffectiveAreas areas{!m_grants.contains(player), {}};
        for (auto it = m_grants.cbegin(); it != m_grants.cend(); ++it)
            if (it->occupier == player) areas.occupiedAreas << it.key();
        return areas;
    }

    QString beneficiaryOf(const QString &areaOwner) const
    {
        QMutexLocker locker(&m_mutex);
        const auto grant = m_grants.constFind(areaOwner);
        return grant == m_grants.cend() ? areaOwner : grant->occupier;
    }

private:
    mutable QMutex m_mutex;
    QMap<QString, Grant> m_grants;
    quint64 m_generation = 0;
    quint64 m_highestIssued = 0;
};

#endif
