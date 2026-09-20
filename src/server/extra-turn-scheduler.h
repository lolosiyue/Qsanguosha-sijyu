#ifndef EXTRA_TURN_SCHEDULER_H
#define EXTRA_TURN_SCHEDULER_H

#include "player.h"
#include "skill-instance-types.h"

#include <QList>
#include <QString>

#include <functional>

class Room;
class ServerPlayer;

class ExtraTurnScheduler
{
public:
    struct SnapshotRequest
    {
        QString playerObjectName;
        QList<int> phases;
        QString reason;
        SkillInstanceRef sourceRef;
        // Causal event is captured when the request is scheduled; the turn
        // event itself is allocated only when the request starts executing.
        qint64 causeEventId = 0;
    };

    using PlayerResolver = std::function<ServerPlayer *(const QString &objectName)>;

    explicit ExtraTurnScheduler(Room &room);

    int schedule(ServerPlayer *player, const QString &reason,
                 QList<Player::Phase> phases, int times);
    int schedule(ServerPlayer *player, const SkillInstanceRef &sourceRef,
                 QList<Player::Phase> phases, int times);
    bool isCurrentExtraTurn() const;
    QString currentReason() const;
    SkillInstanceRef currentSourceRef() const;
    qint64 currentCauseEventId() const;

    QList<SnapshotRequest> pendingRequestsSnapshot() const;
    bool restorePendingRequests(const QList<SnapshotRequest> &requests,
                                const PlayerResolver &resolver,
                                QString *error = nullptr);

    void process();
    void execute(ServerPlayer *player, QList<Player::Phase> phases,
                 const QString &reason, const SkillInstanceRef &sourceRef,
                 qint64 causeEventId = 0);

private:
    struct Request {
        ServerPlayer *player;
        QList<Player::Phase> phases;
        QString reason;
        SkillInstanceRef sourceRef;
        qint64 causeEventId;

        Request()
            : player(nullptr), causeEventId(0) {}
    };

    struct Context {
        ServerPlayer *player;
        QString reason;
        SkillInstanceRef sourceRef;
        qint64 causeEventId;

        Context()
            : player(nullptr), causeEventId(0) {}
    };

    void restoreRequests(const QList<Request> &requests);

    Room &m_room;
    QList<Request> m_requests;
    QList<Context> m_contexts;
    bool m_processing;
};

#endif
