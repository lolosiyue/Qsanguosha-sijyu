#ifndef NOTIFIED_PILE_QUERY_SCOPE_H
#define NOTIFIED_PILE_QUERY_SCOPE_H

#include "skill-instance-types.h"
#include <QList>
#include <QPointer>
#include <QString>

struct ActiveSkillRequest;
class Player;
class Room;
class ServerPlayer;
class ViewAsSkillV2;

// Compatibility for six audited response-use providers whose native predicates
// query the client-notified pile. This supplies a read-only view of the actual
// notification; it neither changes server piles nor admits a rejected cost.
class NotifiedPileQueryScope final {
public:
    NotifiedPileQueryScope(const ActiveSkillRequest &, const ViewAsSkillV2 *);
    ~NotifiedPileQueryScope();
    bool isActive() const;
    static bool pile(const Player *, const QString &, QList<int> *);
    static bool pileName(const Player *, int, QString *);
    NotifiedPileQueryScope(const NotifiedPileQueryScope &) = delete;
    NotifiedPileQueryScope &operator=(const NotifiedPileQueryScope &) = delete;
private:
    void initialize(const ActiveSkillRequest &, const ViewAsSkillV2 *);
    QPointer<Room> m_room;
    QPointer<ServerPlayer> m_owner;
    QString m_pile;
    QString m_tag;
    QList<int> m_ids;
    SkillInstanceRef m_activation;
    SkillInstanceRef m_source;
    quint64 m_revision = 0;
    bool m_active = false;
    NotifiedPileQueryScope *m_previous;
    static thread_local NotifiedPileQueryScope *s_current;
};
#endif
