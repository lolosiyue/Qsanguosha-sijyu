#ifndef _TAKEOVER_SCENARIO_H
#define _TAKEOVER_SCENARIO_H

#include "scenario.h"
#include "game-snapshot.h"
#include "skill.h"

#include <QMap>
#include <QPointer>

class Room;
class ServerPlayer;

/*
 * A takeover is a short-lived scenario owned by the new local Room.  It is
 * deliberately not a game mode: the snapshot's original mode remains the
 * Room mode, while this scenario only supplies the GameReady restore hook.
 */
class TakeoverScenario : public Scenario
{
    Q_OBJECT

public:
    TakeoverScenario(const QString &snapshotPath, const QString &selectedSeat,
                     QObject *parent = nullptr);

    void assign(QStringList &generals, QStringList &roles) const override;
    int getPlayerCount() const override;

    bool isLoaded() const;
    QString loadError() const;
    QString selectedSeat() const;
    const GameSnapshot *snapshot() const;

    /*
     * Runtime object names are allocated by signup and must not be renamed.
     * This map is therefore the only supported bridge between snapshot seat
     * keys and the new Room's ServerPlayer objects.
     */
    void bindRuntimePlayers(Room *room);
    ServerPlayer *runtimePlayer(const QString &snapshotObjectName) const;
    QString snapshotObjectName(const ServerPlayer *runtimePlayer) const;

private:
    QPointer<GameSnapshot> m_snapshot;
    QString m_snapshotPath;
    QString m_selectedSeat;
    QString m_loadError;
    QMap<QString, QPointer<ServerPlayer> > m_runtimeBySnapshotSeat;
    QMap<const ServerPlayer *, QString> m_snapshotByRuntime;
};

/* Restores the state before the first ordinary TurnStart dispatch. */
class TakeoverRule : public ScenarioRule
{
public:
    explicit TakeoverRule(TakeoverScenario *scenario);

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                 QVariant &data) const override;

private:
    bool restore(Room *room) const;
    TakeoverScenario *takeoverScenario() const;
    mutable bool m_restored;
};

#endif
