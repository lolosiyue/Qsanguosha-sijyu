#ifndef _SKILL_H
#define _SKILL_H

class QDialog;

#include "structs.h"
#include "scenario.h"

struct SkillContext {
    QString skill_name;
    ServerPlayer *invoker;
    ServerPlayer *owner;
    QList<ServerPlayer *> targets;
    QList<ServerPlayer *> updated_targets;
    const Card *use_card;
    QVariant *original_data;
    int instanceID;

    ServerPlayer *preferredTarget;
    int preferredTargetSeat;

    bool is_forced;
    bool is_canceled;
    bool bypass_cost;
    bool manual_effect;
    TriggerEvent current_event;

    int amount;
    int modified_amount;
    int trigger_count;
    int multiplier;

    QString choice;
    QVariant extra_data;

    SkillContext() : invoker(nullptr), owner(nullptr), use_card(nullptr),
                     original_data(nullptr), instanceID(0), preferredTarget(nullptr), preferredTargetSeat(-1),
                     is_forced(false), is_canceled(false),
                     bypass_cost(false), manual_effect(false), current_event(NonTrigger),
                     amount(1), modified_amount(0), trigger_count(0), multiplier(1) {}

    QVariant toVariant() const;
};
Q_DECLARE_METATYPE(SkillContext)

class Skill : public QObject
{
    Q_OBJECT
    Q_ENUMS(Frequency)
    Q_ENUMS(LimitScope)

public:
    enum Frequency
    {
        Frequent,
        NotFrequent,
        Compulsory,
        Limited,
        Wake,
        Club,
        NotCompulsory
    };

    enum LimitScope
    {
        Limit_None,
        Limit_Round,
        Limit_Turn,
        Limit_Phase,
        Limit_Game,
        Limit_Target,
        Limit_Custom
    };

    explicit Skill(const QString &name, Frequency frequent = NotFrequent);
    bool isLordSkill() const;
    bool isAttachedLordSkill() const;
    bool isChangeSkill() const;
    bool isLimitedSkill() const;
    bool isHideSkill() const;
    bool isShiMingSkill() const;
    virtual bool canPreshow() const;
    virtual bool shouldBeVisible(const Player *Self) const;
    QString getDescription(const Player *target = nullptr) const;
    QString getOracleText(const Player *target = nullptr) const;
    QString getNotice(int index) const;
    bool isVisible() const;

    virtual int getEffectIndex(const ServerPlayer *player, const Card *card) const;
    virtual QDialog *getDialog() const;

    void initMediaSource();
    void playAudioEffect(int index = -1, bool superpose = true) const;
    virtual Frequency getFrequency(const Player *target = nullptr) const;
    QString getLimitMark() const;
    int getInstanceId() const { return m_instanceId; }
    static int getGlobalInstanceCount() { return m_globalInstanceCount; }
    QString getClubName() const;
    QString getClubMark() const;
    QString getWakedSkills() const;
    QStringList getSources() const;
    bool setProperty(const char* name, const QVariant& value);

    virtual LimitScope getLimitScope() const;
    virtual int getMaxUsageLimit(const SkillContext &ctx) const;
    virtual bool isUsable(const SkillContext &ctx) const;
    virtual void addUsage(const SkillContext &ctx) const;
    virtual void resetUsage(ServerPlayer *owner, ServerPlayer *target = nullptr) const;
    virtual bool checkCustomUsage(const SkillContext &ctx) const;
    virtual ServerPlayer *getUsageHolder(const SkillContext &ctx) const;
    QString getUsageTagKey(const SkillContext &ctx) const;

    virtual bool isEquipSkill() const;

protected:
    Frequency frequency;
    QString limit_mark;
    QString club_name;
    bool attached_lord_skill;
    bool change_skill;
    bool limited_skill;
    bool hide_skill;
    bool shiming_skill;
    QString waked_skills;

private:
    bool lord_skill;
    QStringList sources;
    int m_instanceId;
    static int m_globalInstanceCount;
};

inline int Skill_getInstanceId(const Skill *skill)
{
    return skill ? skill->getInstanceId() : 0;
}

class ViewAsSkill : public Skill
{
    Q_OBJECT

public:
    ViewAsSkill(const QString &name);

    virtual bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const = 0;
    virtual const Card *viewAs(const QList<const Card *> &cards) const = 0;

    bool isAvailable(const Player *invoker, CardUseStruct::CardUseReason reason, const QString &pattern) const;
    virtual bool isEnabledAtPlay(const Player *player) const;
    virtual bool isEnabledAtResponse(const Player *player, const QString &pattern) const;
    virtual bool isEnabledAtNullification(const ServerPlayer *player) const;
    static const ViewAsSkill *parseViewAsSkill(const Skill *skill);

    inline virtual bool isResponseOrUse() const
    {
        return response_or_use;
    }
    inline QString getExpandPile() const
    {
        return expand_pile;
    }

protected:
    QString response_pattern;
    bool response_or_use;
    QString expand_pile;
};

class ZeroCardViewAsSkill : public ViewAsSkill
{
    Q_OBJECT

public:
    ZeroCardViewAsSkill(const QString &name);

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const;
    const Card *viewAs(const QList<const Card *> &cards) const;
    virtual const Card *viewAs() const = 0;
};

class OneCardViewAsSkill : public ViewAsSkill
{
    Q_OBJECT

public:
    OneCardViewAsSkill(const QString &name);

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const;
    const Card *viewAs(const QList<const Card *> &cards) const;

    virtual bool viewFilter(const Card *to_select) const;
    virtual const Card *viewAs(const Card *originalCard) const = 0;

protected:
    QString filter_pattern;
};

class FilterSkill : public OneCardViewAsSkill
{
    Q_OBJECT

public:
    FilterSkill(const QString &name);
};

typedef QMap<ServerPlayer*, QStringList> TriggerList;

class TriggerSkill : public Skill
{
    Q_OBJECT

public:
    TriggerSkill(const QString &name);
    const ViewAsSkill *getViewAsSkill() const;
    QList<TriggerEvent> getTriggerEvents() const;

    virtual int getPriority(TriggerEvent event) const;
    virtual bool triggerable(ServerPlayer *target, Room *room, TriggerEvent event, ServerPlayer *owner, QVariant data) const;
    virtual bool triggerable(const ServerPlayer *target, Room *room, TriggerEvent event) const;
    virtual bool triggerable(const ServerPlayer *target) const;
    virtual bool canWake(TriggerEvent event, ServerPlayer *player,QVariant data, Room *room) const;
    virtual void record(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data, ServerPlayer *owner) const;
    virtual bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, ServerPlayer *owner) const;
    virtual bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const = 0;
    virtual bool hasEvent(TriggerEvent event) const;
    virtual bool hasCan(ServerPlayer *target, ServerPlayer *owner) const;

    inline double getDynamicPriority() const
    {
        return dynamic_priority;
    }
    inline void setDynamicPriority(double value)
    {
        dynamic_priority = value;
    }

    inline bool isGlobal() const
    {
        return global;
    }

protected:
    const ViewAsSkill *view_as_skill;
    QList<TriggerEvent> events;
    bool global;

private:
    mutable double dynamic_priority;
};

class ScenarioRule : public TriggerSkill
{
    Q_OBJECT

public:
    ScenarioRule(Scenario *scenario);

    virtual int getPriority(TriggerEvent triggerEvent) const;
    virtual bool triggerable(const ServerPlayer *target) const;
};

class MasochismSkill : public TriggerSkill
{
    Q_OBJECT

public:
    MasochismSkill(const QString &name);

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const;
    virtual void onDamaged(ServerPlayer *target, const DamageStruct &damage) const = 0;
};

class PhaseChangeSkill : public TriggerSkill
{
    Q_OBJECT

public:
    PhaseChangeSkill(const QString &name);

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const;
    virtual bool onPhaseChange(ServerPlayer *player, Room *room) const = 0;
};

class DrawCardsSkill : public TriggerSkill
{
    Q_OBJECT

public:
    DrawCardsSkill(const QString &name, bool is_initial = false);

    virtual bool triggerable(ServerPlayer *target, Room *room, TriggerEvent event, ServerPlayer *owner, QVariant data) const;
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const;
    virtual int getDrawNum(ServerPlayer *player, int n) const = 0;

protected:
    bool is_initial;
};

class GameStartSkill : public TriggerSkill
{
    Q_OBJECT

public:
    GameStartSkill(const QString &name);

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const;
    virtual void onGameStart(ServerPlayer *player) const = 0;
};

class TriggerV2Skill : public TriggerSkill
{
    Q_OBJECT

public:
    TriggerV2Skill(const QString &name);

    virtual TriggerList triggerable(TriggerEvent triggerEvent, Room *room,
                                     ServerPlayer *player, QVariant &data) const;
    virtual void record(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                       SkillContext &ctx) const;
    virtual bool cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                      SkillContext &ctx) const;
    virtual bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                     SkillContext &ctx) const;
    virtual bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                        SkillContext &ctx) const;
    virtual bool effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                              SkillContext &ctx, ServerPlayer *target) const;
    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                         QVariant &data, ServerPlayer *owner) const override;

    bool skillEffect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                     SkillContext &ctx, ServerPlayer *target) const;

    virtual void willInvoke(SkillContext &ctx) const;
    virtual void targetConfirming(SkillContext &ctx) const;
    virtual void invoking(SkillContext &ctx) const;
    virtual void effect(SkillContext &ctx) const;
    virtual void effectFinished(SkillContext &ctx) const;

    virtual int getBaseAmount() const;
    int getEffectiveAmount(const SkillContext &ctx) const;

    static QString parseSkillName(const QString &fullName, QString *source = NULL,
                                   QString *target = NULL, int *multiplier = NULL,
                                   int *instanceId = NULL);

protected:
    int m_baseAmount;
};

class RetrialSkill : public TriggerSkill
{
    Q_OBJECT
        
public:
    RetrialSkill(const QString &name, bool exchange = false);

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const;
    virtual const Card *onRetrial(ServerPlayer *player, JudgeStruct *judge) const = 0;

private:
    bool exchange;
};

class SPConvertSkill : public GameStartSkill
{
    Q_OBJECT

public:
    SPConvertSkill(const QString &from, const QString &to);

    bool triggerable(const ServerPlayer *target) const;
    void onGameStart(ServerPlayer *player) const;

private:
    QString from, to;
    QStringList to_list;
};

class ProhibitSkill : public Skill
{
    Q_OBJECT

public:
    ProhibitSkill(const QString &name);

    virtual bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &others = QList<const Player *>()) const = 0;
};

class ProhibitPindianSkill : public Skill
{
    Q_OBJECT

public:
    ProhibitPindianSkill(const QString &name);

    virtual bool isPindianProhibited(const Player *from, const Player *to) const = 0;
};

class DistanceSkill : public Skill
{
    Q_OBJECT

public:
    DistanceSkill(const QString &name);

    virtual int getCorrect(const Player *from, const Player *to) const;
    virtual int getFixed(const Player *from, const Player *to) const;
};

class MaxCardsSkill : public Skill
{
    Q_OBJECT

public:
    MaxCardsSkill(const QString &name);

    virtual int getExtra(const Player *target) const;
    virtual int getFixed(const Player *target) const;
};

class TargetModSkill : public Skill
{
    Q_OBJECT
        Q_ENUMS(ModType)

public:
    enum ModType
    {
        Residue,
        DistanceLimit,
        ExtraTarget
    };

    TargetModSkill(const QString &name);
    virtual QString getPattern() const;

    virtual int getResidueNum(const Player *from, const Card *card, const Player *to) const;
    virtual int getDistanceLimit(const Player *from, const Card *card, const Player *to) const;
    virtual int getExtraTargetNum(const Player *from, const Card *card) const;

protected:
    QString pattern;
};

class SlashNoDistanceLimitSkill : public TargetModSkill
{
    Q_OBJECT

public:
    SlashNoDistanceLimitSkill(const QString &skill_name);

    int getDistanceLimit(const Player *from, const Card *card, const Player *to) const;

protected:
    QString name;
};

class InvaliditySkill : public Skill
{
    Q_OBJECT

public:
    InvaliditySkill(const QString &skill_name);

    virtual bool isSkillValid(const Player *player, const Skill *skill) const = 0;
};

class AttackRangeSkill : public Skill
{
    Q_OBJECT

public:
    AttackRangeSkill(const QString &name);

    virtual int getExtra(const Player *target, bool include_weapon) const;
    virtual int getFixed(const Player *target, bool include_weapon) const;
    //virtual bool inRange(const Player *from, const Player *to) const;
};

class ViewAsEquipSkill : public Skill
{
    Q_OBJECT

public:
    ViewAsEquipSkill(const QString &name);

    virtual QString viewAsEquip(const Player *target) const;
};

class CardLimitSkill : public Skill
{
    Q_OBJECT

public:
    CardLimitSkill(const QString &name);

    virtual QString limitList(const Player *target) const;
    virtual QString limitPattern(const Player *target) const;
    virtual QString limitReason(const Player *target) const;
    virtual QString limitList(const Player *target, const Card *card) const;
    virtual QString limitPattern(const Player *target, const Card *card) const;
    virtual QString limitReason(const Player *target, const Card *card) const;
};

class DetachEffectSkill : public TriggerSkill
{
    Q_OBJECT

public:
    DetachEffectSkill(const QString &skillname, const QString &pilename = "");

    bool triggerable(const ServerPlayer *target) const;
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const;
    virtual void onSkillDetached(Room *room, ServerPlayer *player) const;

private:
    QString name, pile_name;
};

class WeaponSkill : public TriggerSkill
{
    Q_OBJECT

public:
    WeaponSkill(const QString &name);

    virtual bool triggerable(const ServerPlayer *target) const;
    virtual bool isEquipSkill() const override;
};

class ArmorSkill : public TriggerSkill
{
    Q_OBJECT

public:
    ArmorSkill(const QString &name);

    virtual bool triggerable(const ServerPlayer *target) const;
    virtual bool isEquipSkill() const override;
};

class TreasureSkill : public TriggerSkill
{
    Q_OBJECT

public:
    TreasureSkill(const QString &name);

    virtual bool triggerable(const ServerPlayer *target) const;
    virtual bool isEquipSkill() const override;
};

class MarkAssignSkill : public GameStartSkill
{
    Q_OBJECT

public:
    MarkAssignSkill(const QString &mark, int n);

    void onGameStart(ServerPlayer *player) const;

private:
    QString mark_name;
    int n;
};

class PreSelectionMetaSkill : public Skill
{
    Q_OBJECT

public:
    PreSelectionMetaSkill(const QString &name);

    virtual QStringList onGeneralChoosing(Room *room, ServerPlayer *player,
                                          QStringList generals, const QString &reason) const;
    virtual void onGeneralNotChosen(Room *room, ServerPlayer *player,
                                    const QStringList &generals, const QString &chosen,
                                    const QString &reason) const;

    QString getActiveSkills() const;

protected:
    QString active_skills;
};

class AnytimeSkill : public Skill
{
    Q_OBJECT

public:
    AnytimeSkill(const QString &name);

    virtual bool canTrigger(ServerPlayer *player) const;
    virtual bool onTrigger(Room *room, ServerPlayer *player) const;

    inline bool isAnytime() const { return true; }
};

class BattleArraySkill : public TriggerSkill
{
    Q_OBJECT

public:
    BattleArraySkill(const QString &name, const QString &arrayType);

    virtual void summonFriends(ServerPlayer *player) const;

    inline QString getArrayType() const { return array_type; }

private:
    QString array_type;
};

class ArraySummonSkill : public ZeroCardViewAsSkill
{
    Q_OBJECT

public:
    explicit ArraySummonSkill(const QString &name);

    const Card *viewAs() const override;
    bool isEnabledAtPlay(const Player *player) const override;
};

#endif
