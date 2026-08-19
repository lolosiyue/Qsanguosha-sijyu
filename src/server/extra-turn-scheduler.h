#ifndef EXTRA_TURN_SCHEDULER_H
#define EXTRA_TURN_SCHEDULER_H

#include "player.h"
#include "skill-instance-types.h"

#include <QList>
#include <QString>

class Room;
class ServerPlayer;

class ExtraTurnScheduler
{
public:
    explicit ExtraTurnScheduler(Room &room);

    int schedule(ServerPlayer *player, const QString &reason,
                 QList<Player::Phase> phases, int times);
    int schedule(ServerPlayer *player, const SkillInstanceRef &sourceRef,
                 QList<Player::Phase> phases, int times);
    bool isCurrentExtraTurn() const;
    QString currentReason() const;
    SkillInstanceRef currentSourceRef() const;

    void process();
    void execute(ServerPlayer *player, QList<Player::Phase> phases,
                 const QString &reason, const SkillInstanceRef &sourceRef);

private:
    struct Request {
        ServerPlayer *player;
        QList<Player::Phase> phases;
        QString reason;
        SkillInstanceRef sourceRef;

        Request()
            : player(nullptr) {}
    };

    struct Context {
        ServerPlayer *player;
        QString reason;
        SkillInstanceRef sourceRef;

        Context()
            : player(nullptr) {}
    };

    void restoreRequests(const QList<Request> &requests);

    Room &m_room;
    QList<Request> m_requests;
    QList<Context> m_contexts;
    bool m_processing;
};

#endif
