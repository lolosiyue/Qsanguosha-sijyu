#ifndef V2_RECORD_OWNER_INDEX_H
#define V2_RECORD_OWNER_INDEX_H

#include "room.h"
#include "serverplayer.h"
#include "skill-set-generation.h"
#include <QPointer>
#include <QHash>

// Ownership and ordered instance IDs only, never validity or callback results.
// getSkillNames() enumerates each owner's QMap in the same ID order as
// getSkillInstanceIds(). Callers retain a value
// snapshot across nested dispatch and stop filtering if generation changes.
class V2RecordOwnerIndex
{
public:
    QHash<ServerPlayer *, QList<int>> candidates(Room *room, const QList<ServerPlayer *> &players,
                                    const QString &name, quint64 &generation)
    {
        generation = SkillSet::generation();
        if (m_room != room || m_generation != generation || m_players != players) {
            m_owners.clear();
            for (ServerPlayer *player : players) {
                for (const QString &skill : player->getSkillNames()) {
                    QString baseName;
                    const int id = SkillInstanceUtils::parseName(skill, baseName);
                    m_owners[baseName][player].append(id);
                }
            }
            m_room = room;
            m_generation = generation;
            m_players = players;
        }
        return m_owners.value(name);
    }

private:
    QPointer<Room> m_room;
    quint64 m_generation = 0;
    QList<ServerPlayer *> m_players;
    QHash<QString, QHash<ServerPlayer *, QList<int>>> m_owners;
};

#endif
