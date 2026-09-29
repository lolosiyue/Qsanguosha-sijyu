#ifndef V2_RECORD_OWNER_INDEX_H
#define V2_RECORD_OWNER_INDEX_H

#include "room.h"
#include "serverplayer.h"
#include "skill-set-generation.h"
#include <QPointer>
#include <QSet>

// Ownership only, never validity or callback results. Callers retain a value
// snapshot across nested dispatch and stop filtering if generation changes.
class V2RecordOwnerIndex
{
public:
    QSet<ServerPlayer *> candidates(Room *room, const QList<ServerPlayer *> &players,
                                    const QString &name, quint64 &generation)
    {
        generation = SkillSet::generation();
        if (m_room != room || m_generation != generation || m_players != players) {
            m_owners.clear();
            for (ServerPlayer *player : players)
                for (const QString &skill : player->getSkillNames())
                    m_owners[SkillInstanceUtils::baseName(skill)].insert(player);
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
    QHash<QString, QSet<ServerPlayer *>> m_owners;
};

#endif
