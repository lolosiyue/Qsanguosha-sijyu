#ifndef _GAME_RULE_H
#define _GAME_RULE_H

#include "skill.h"

static QVariant _dummy_variant;

class Room;
class ServerPlayer;

bool tryLuaGameModeReward(Room *room, ServerPlayer *killer, ServerPlayer *victim);
bool tryLuaGameModeGetWinner(Room *room, ServerPlayer *victim, QString *winner);

class GameRule : public TriggerSkill
{
    Q_OBJECT

public:
    enum BossModeDifficulty
    {
        BMDRevive,
        BMDRecover,
        BMDDraw,
        BMDReward,
        BMDIncMaxHp,
        BMDDecMaxHp
    };

    GameRule(QObject *parent);
    virtual bool triggerable(const ServerPlayer *target) const;
    virtual int getPriority(TriggerEvent triggerEvent) const;
    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data = _dummy_variant) const;

private:
    void onPhaseProceed(ServerPlayer *player, Room *room) const;
    void rewardAndPunish(ServerPlayer *killer, ServerPlayer *victim) const;
    void changeGeneral1v1(ServerPlayer *player) const;
    void changeGeneralXMode(ServerPlayer *player) const;
    void changeGeneralBossMode(ServerPlayer *player, Room *room) const;
    void acquireBossSkills(ServerPlayer *player, int level) const;
    void doBossModeDifficultySettings(ServerPlayer *lord) const;
    QString getWinner(ServerPlayer *victim, Room *room) const;
};

class HulaoPassMode : public GameRule
{
    Q_OBJECT

public:
    HulaoPassMode(QObject *parent);
    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data = _dummy_variant) const;
};

// The original Hegemony rules use the current Room/SkillInstance runtime.
class HegemonyRule : public GameRule
{
public:
    explicit HegemonyRule(QObject *parent);
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player,
                 QVariant &data = _dummy_variant) const override;
    static QString winner(Room *room);
    static QString getMappedRole(const QString &role);

private:
    void rewardAndPunish(ServerPlayer *killer, ServerPlayer *victim) const;
    void rewardReveal(Room *room, ServerPlayer *player) const;
};

#endif
