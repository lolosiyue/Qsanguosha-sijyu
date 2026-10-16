#ifndef DAMAGE_RESOLUTION_SERVICE_H
#define DAMAGE_RESOLUTION_SERVICE_H

#include "card-lifetime-manager.h"
#include "room.h"
#include "structs.h"

#include <QList>
#include <QPointer>
#include <QString>
#include <QVariant>

#include <memory>

class ServerPlayer;

struct Room::DyingCursor {
    enum Stage { Start, Enter, Observers, Savers, PeachesDone, Quit, Finished } stage = Start;
    QPointer<ServerPlayer> player;
    DamageStruct damage;
    HpLostStruct hpLost;
    bool hasDamage = false, hasHpLost = false;
    CardLifetimeLease causeLease;
    QVariant data;
    QList<QPointer<ServerPlayer>> observers, savers;
    int observerIndex = 0, saverIndex = 0;
    quint64 owner = 0, completedOwner = 0;
    qint64 history = 0;
    bool observersReady = false, resolved = false, retained = false;
    bool decisionIncomplete = false;
    bool nativeDecisionStarted = false;
    QVariant saverAtDeferral;
};

class DamageResolutionService
{
public:
    explicit DamageResolutionService(Room &room);

    QVariantMap queryActualDamage(const QVariantMap &filter) const;
    QVariantMap queryCardUseDamage(qint64 useEventId) const;
    void recordAppliedDamage(const DamageStruct &damage, int absorbed, int hpLoss);
    void applyDamageHp(ServerPlayer *player, const DamageStruct &damage, int absorbed, int hpLoss);
    void recordDamageComponent(const DamageStruct &damage, const QString &component, int amount);

    void enterDying(ServerPlayer *player, DamageStruct *reason, HpLostStruct *hpLost);
    void continueDying(const std::shared_ptr<Room::DyingCursor> &cursor);
    void adoptCancelledDying(quint64 from, quint64 owner);
    ServerPlayer *getCurrentDyingPlayer() const;

    void loseHp(ServerPlayer *victim, int lose, bool ignore_hujia, ServerPlayer *from, const QString &reason);
    void loseHp(const HpLostStruct &lost_data);
    void changePlayerMaxHp(ServerPlayer *player, int change, const QString &reason);
    void loseMaxHp(ServerPlayer *victim, int lose, const QString &reason);
    void gainMaxHp(ServerPlayer *player, int gain, const QString &reason);
    bool changeMaxHpForAwakenSkill(ServerPlayer *player, int magnitude, const QString &reason);
    void recover(ServerPlayer *player, const RecoverStruct &recover, bool set_emotion);
    void recover(ServerPlayer *player, const RecoverStruct &recover, bool set_emotion, qint64 *historyEventId);
    ServerPlayer *getSaver(ServerPlayer *player) const;
    void resolveDamage(DamageStruct damage);
    bool hasWelfare(const ServerPlayer *player) const;

private:
    Room &m_room;
};

#endif
