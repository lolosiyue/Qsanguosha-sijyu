#ifndef _LUA_WRAPPER_H
#define _LUA_WRAPPER_H

//#include "skill.h"
#include "standard.h"

struct lua_State;
typedef int LuaFunction;

class LuaTriggerSkill : public TriggerSkill
{
    Q_OBJECT

public:
    LuaTriggerSkill(const QString &name, Frequency frequency, const QString &limit_mark, bool change_skill, bool limited_skill, bool hide_skill,
                    bool shiming_skill, const QString &waked_skills);
    inline void addEvent(TriggerEvent triggerEvent)
    {
        events << triggerEvent;
    }
    inline void setViewAsSkill(ViewAsSkill *view_as_skill)
    {
        this->view_as_skill = view_as_skill;
    }
    inline void setGlobal(bool global)
    {
        this->global = global;
    }
    inline void insertPriorityTable(TriggerEvent triggerEvent, int priority)
    {
        priority_table[triggerEvent] = priority;
    }
    inline void setGuhuoDialog(const QString &type)
    {
        this->guhuo_type = type;
    }
    inline void setJuguanDialog(const QString &type)
    {
        this->juguan_type = type;
    }
    inline void setTiansuanDialog(const QString &type)
    {
        this->tiansuan_type = type;
    }
    inline void setWakedSkills(const QString &waked_skills)
    {
        this->waked_skills = waked_skills;
    }

    QDialog *getDialog() const;

    Frequency getFrequency(const Player *target) const;
    int getPriority(TriggerEvent triggerEvent) const;

    bool triggerable(ServerPlayer *target, Room *room, TriggerEvent event, ServerPlayer *owner, QVariant data) const;
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const;
    bool canWake(TriggerEvent triggerEvent, ServerPlayer *player, QVariant &data, Room *room) const;

    LuaFunction on_trigger;
    LuaFunction can_trigger;
    LuaFunction dynamic_frequency;
    LuaFunction can_wake;

    int priority;

protected:
    QMap<TriggerEvent, int> priority_table;
    QString guhuo_type;
    QString juguan_type;
    QString tiansuan_type;
};

class LuaScenarioRule : public ScenarioRule
{
    Q_OBJECT

public:
    static const QString &S_EXTRA_OPTION_RANDOM_ROLES;
    static const QString &S_EXTRA_OPTION_REST_IN_DISCARD_PILE;

    LuaScenarioRule(Scenario *scenario);

    inline void addEvent(TriggerEvent triggerEvent)
    {
        events << triggerEvent;
    }
    inline void setGlobal(bool global)
    {
        this->global = global;
    }
    inline void insertPriorityTable(TriggerEvent triggerEvent, int priority)
    {
        priority_table[triggerEvent] = priority;
    }

    bool triggerable(const ServerPlayer *target) const;
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const;

    LuaFunction can_trigger;
    LuaFunction on_trigger;

    int priority;
protected:
    QMap<TriggerEvent, int> priority_table;
};

class LuaProhibitSkill : public ProhibitSkill
{
    Q_OBJECT

public:
    LuaProhibitSkill(const QString &name, Frequency frequency);

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &others = QList<const Player *>()) const;

    LuaFunction is_prohibited;
};

class LuaProhibitPindianSkill : public ProhibitPindianSkill
{
    Q_OBJECT

public:
    LuaProhibitPindianSkill(const QString &name, Frequency frequency);

    bool isPindianProhibited(const Player *from, const Player *to) const;

    LuaFunction is_pindianprohibited;
};

class LuaViewAsSkill : public ViewAsSkill
{
    Q_OBJECT

public:
    LuaViewAsSkill(const QString &name, const QString &response_pattern, bool response_or_use, const QString &expand_pile,
                   Frequency frequency, const QString &limit_mark);

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const;
    const Card *viewAs(const QList<const Card *> &cards) const;

    bool shouldBeVisible(const Player *player) const;

    inline void setGuhuoDialog(const QString &type)
    {
        this->guhuo_type = type;
    }
    inline void setJuguanDialog(const QString &type)
    {
        this->juguan_type = type;
    }
    inline void setTiansuanDialog(const QString &type)
    {
        this->tiansuan_type = type;
    }
    inline void setWakedSkills(const QString &waked_skills)
    {
        this->waked_skills = waked_skills;
    }

    LuaFunction view_filter;
    LuaFunction view_as;

    LuaFunction should_be_visible;

    LuaFunction enabled_at_play;
    LuaFunction enabled_at_response;
    LuaFunction enabled_at_nullification;

    bool isEnabledAtPlay(const Player *player) const;
    bool isEnabledAtResponse(const Player *player, const QString &pattern) const;
    bool isEnabledAtNullification(const ServerPlayer *player) const;
    QDialog *getDialog() const;
private:
    QString guhuo_type;
    QString juguan_type;
    QString tiansuan_type;
};

class LuaFilterSkill : public FilterSkill
{
    Q_OBJECT

public:
    LuaFilterSkill(const QString &name, Frequency frequency);

    bool viewFilter(const Card *to_select) const;
    const Card *viewAs(const Card *originalCard) const;

    LuaFunction view_filter;
    LuaFunction view_as;
};

class LuaDistanceSkill : public DistanceSkill
{
    Q_OBJECT

public:
    LuaDistanceSkill(const QString &name, Frequency frequency);

    int getCorrect(const Player *from, const Player *to) const;
    int getFixed(const Player *from, const Player *to) const;

    LuaFunction correct_func;
    LuaFunction fixed_func;
};

class LuaMaxCardsSkill : public MaxCardsSkill
{
    Q_OBJECT

public:
    LuaMaxCardsSkill(const QString &name, Frequency frequency);

    int getExtra(const Player *target) const;
    int getFixed(const Player *target) const;

    LuaFunction extra_func;
    LuaFunction fixed_func;
};

class LuaTargetModSkill : public TargetModSkill
{
    Q_OBJECT

public:
    LuaTargetModSkill(const QString &name, const QString &pattern, Frequency frequency);

    int getResidueNum(const Player *from, const Card *card, const Player *to) const;
    int getDistanceLimit(const Player *from, const Card *card, const Player *to) const;
    int getExtraTargetNum(const Player *from, const Card *card) const;

    LuaFunction residue_func;
    LuaFunction distance_limit_func;
    LuaFunction extra_target_func;
};

class LuaInvaliditySkill : public InvaliditySkill
{
    Q_OBJECT

public:
    LuaInvaliditySkill(const QString &name, Frequency frequency);

    bool isSkillValid(const Player *player, const Skill *skill) const;

    LuaFunction skill_valid;
};

class LuaAttackRangeSkill : public AttackRangeSkill
{
    Q_OBJECT

public:
    LuaAttackRangeSkill(const QString &name, Frequency frequency);

    int getExtra(const Player *target, bool include_weapon) const;
    int getFixed(const Player *target, bool include_weapon) const;

    LuaFunction extra_func;
    LuaFunction fixed_func;
};

class LuaViewAsEquipSkill : public ViewAsEquipSkill
{
    Q_OBJECT

public:
    LuaViewAsEquipSkill(const QString &name, Frequency frequency);

    QString viewAsEquip(const Player *target) const;

    LuaFunction view_as_equip;
};

class LuaCardLimitSkill : public CardLimitSkill
{
    Q_OBJECT

public:
    LuaCardLimitSkill(const QString &name, Frequency frequency);

    QString limitList(const Player *target, const Card *card) const;
    QString limitPattern(const Player *target, const Card *card) const;

    LuaFunction limit_list;
    LuaFunction limit_pattern;
};

class LuaSkillCard : public SkillCard
{
    Q_OBJECT

public:
    LuaSkillCard(const QString &name, const QString &skillName);
    LuaSkillCard *clone() const;
    inline void setTargetFixed(bool target_fixed)
    {
        this->target_fixed = target_fixed;
    }
    inline void setWillThrow(bool will_throw)
    {
        this->will_throw = will_throw;
    }
    inline void setCanRecast(bool can_recast)
    {
        this->can_recast = can_recast;
    }
    inline void setHandlingMethod(Card::HandlingMethod handling_method)
    {
        this->handling_method = handling_method;
    }
    inline void setMute(bool mute)
    {
        this->mute = mute;
    }

    // member functions that do not expose to Lua interpreter
    static LuaSkillCard *Parse(const QString &str);

    QString toString(bool hidden = false) const;

    // these functions are defined at swig/luaskills.i
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self, int &maxVotes) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
    const Card *validate(CardUseStruct &cardUse) const;
    const Card *validateInResponse(ServerPlayer *user) const;

    // the lua callbacks
    LuaFunction filter;
    LuaFunction feasible;
    LuaFunction about_to_use;
    LuaFunction on_use;
    LuaFunction on_effect;
    LuaFunction on_validate;
    LuaFunction on_validate_in_response;
};

class LuaBasicCard : public BasicCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LuaBasicCard(Card::Suit suit, int number, const QString &obj_name, const QString &class_name, const QString &subtype);
    LuaBasicCard *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;
    inline void setTargetFixed(bool target_fixed)
    {
        this->target_fixed = target_fixed;
    }
    inline void setCanRecast(bool can_recast)
    {
        this->can_recast = can_recast;
    }

    // member functions that do not expose to Lua interpreter

    void onUse(Room *room, CardUseStruct &card_use) const;
    void onEffect(CardEffectStruct &effect) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self, int &maxVotes) const;
    bool isAvailable(const Player *player) const;

    inline QString getClassName() const
    {
		return class_names.first();
    }
    inline QString getSubtype() const
    {
        return subtype;
    }
    inline bool isKindOf(const char *cardType) const
    {
		return strcmp(cardType,"LuaCard") == 0
		|| class_names.contains(QString(cardType))
		|| BasicCard::isKindOf(cardType);
    }
    const Card *validate(CardUseStruct &cardUse) const;
    const Card *validateInResponse(ServerPlayer *user) const;

    // the lua callbacks
    LuaFunction filter;
    LuaFunction feasible;
    LuaFunction available;
    LuaFunction about_to_use;
    LuaFunction on_use;
    LuaFunction on_effect;
    LuaFunction on_validate;
    LuaFunction on_validate_in_response;

private:
    QStringList class_names;
	QString class_name, subtype;
};

class LuaTrickCard : public TrickCard
{
    Q_OBJECT

public:
    enum SubClass
    {
        TypeNormal, TypeSingleTargetTrick, TypeDelayedTrick, TypeAOE, TypeGlobalEffect
    };

    Q_INVOKABLE LuaTrickCard(Card::Suit suit, int number, const QString &obj_name, const QString &class_name, const QString &subtype);
    LuaTrickCard *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;
    inline void setTargetFixed(bool target_fixed)
    {
        this->target_fixed = target_fixed;
    }
    inline void setCanRecast(bool can_recast)
    {
        this->can_recast = can_recast;
    }

    // member functions that do not expose to Lua interpreter

    void onUse(Room *room, CardUseStruct &card_use) const;
    void onEffect(CardEffectStruct &effect) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onNullified(ServerPlayer *target) const;
    bool isCancelable(const CardEffectStruct &effect) const;

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self, int &maxVotes) const;
    bool isAvailable(const Player *player) const;

    inline QString getClassName() const
    {
        return class_names.first();
    }
    inline void setSubtype(const QString &subtype)
    {
        this->subtype = subtype;
    }
    inline QString getSubtype() const
    {
        return subtype;
    }
    inline void setSubClass(SubClass subclass)
    {
        this->subclass = subclass;
    }
    inline SubClass getSubClass() const
    {
        return subclass;
    }
    inline bool isKindOf(const char *cardType) const
    {
		if(strcmp(cardType, "LuaCard") == 0 || class_names.contains(QString(cardType)) || TrickCard::isKindOf(cardType))
			return true;
		switch (subclass) {
		case TypeSingleTargetTrick: return strcmp(cardType, "SingleTargetTrick") == 0; break;
		case TypeDelayedTrick: return strcmp(cardType, "DelayedTrick") == 0; break;
		case TypeGlobalEffect: return strcmp(cardType, "GlobalEffect") == 0; break;
		case TypeAOE: return strcmp(cardType, "AOE") == 0; break;
		//case TypeNormal:
		default:
			break;
        }
		return false;
    }
    const Card *validate(CardUseStruct &cardUse) const;
    const Card *validateInResponse(ServerPlayer *user) const;

    // the lua callbacks
    LuaFunction filter;
    LuaFunction feasible;
    LuaFunction available;
    LuaFunction is_cancelable;
    LuaFunction about_to_use;
    LuaFunction on_use;
    LuaFunction on_effect;
    LuaFunction on_nullified;
    LuaFunction on_validate;
    LuaFunction on_validate_in_response;

private:
    SubClass subclass;
    QStringList class_names;
    QString class_name, subtype;
};

class LuaWeapon : public Weapon
{
    Q_OBJECT

public:
    Q_INVOKABLE LuaWeapon(Card::Suit suit, int number, int range, const QString &obj_name, const QString &class_name);
    LuaWeapon *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

    // member functions that do not expose to Lua interpreter

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool isAvailable(const Player *player) const;

    void onInstall(ServerPlayer *player) const;
    void onUninstall(ServerPlayer *player) const;

    inline QString getClassName() const
    {
        return class_names.first();
    }
    inline int getRange() const
    {
        return range;
    }
    inline bool isKindOf(const char *cardType) const
    {
		return strcmp(cardType, "LuaCard") == 0
			|| class_names.contains(QString(cardType))
			|| Weapon::isKindOf(cardType);
    }
    inline void setTargetFixed(bool target_fixed)
    {
        this->target_fixed = target_fixed;
    }

    // the lua callbacks
    LuaFunction filter;
    LuaFunction feasible;
    LuaFunction available;
    LuaFunction on_install;
    LuaFunction on_uninstall;

private:
    QStringList class_names;
    QString class_name;
};

class LuaArmor : public Armor
{
    Q_OBJECT

public:
    Q_INVOKABLE LuaArmor(Card::Suit suit, int number, const QString &obj_name, const QString &class_name);
    LuaArmor *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

    // member functions that do not expose to Lua interpreter

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool isAvailable(const Player *player) const;

    void onInstall(ServerPlayer *player) const;
    void onUninstall(ServerPlayer *player) const;

    inline QString getClassName() const
    {
        return class_names.first();
    }
    inline bool isKindOf(const char *cardType) const
    {
		return strcmp(cardType, "LuaCard") == 0
			|| class_names.contains(QString(cardType))
			|| Armor::isKindOf(cardType);
    }
    inline void setTargetFixed(bool target_fixed)
    {
        this->target_fixed = target_fixed;
    }

    // the lua callbacks
    LuaFunction filter;
    LuaFunction feasible;
    LuaFunction available;
    LuaFunction on_install;
    LuaFunction on_uninstall;

private:
    QStringList class_names;
    QString class_name;
};

class LuaHorse : public Horse
{
    Q_OBJECT

public:
    Q_INVOKABLE LuaHorse(Card::Suit suit, int number, int correct, const QString &obj_name, const QString &class_name);
    LuaHorse *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

    // member functions that do not expose to Lua interpreter

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool isAvailable(const Player *player) const;

    void onInstall(ServerPlayer *player) const;
    void onUninstall(ServerPlayer *player) const;
    int getCorrect(const Player *player = nullptr) const;

    inline QString getClassName() const
    {
        return class_names.first();
    }
    inline bool isKindOf(const char *cardType) const
    {
		return strcmp(cardType, "LuaCard") == 0
			|| class_names.contains(QString(cardType))
			|| Horse::isKindOf(cardType);
    }
    inline void setTargetFixed(bool target_fixed)
    {
        this->target_fixed = target_fixed;
    }

    // the lua callbacks
    LuaFunction filter;
    LuaFunction feasible;
    LuaFunction available;
    LuaFunction on_install;
    LuaFunction on_uninstall;
    LuaFunction correct_func;

private:
    QStringList class_names;
    QString class_name;
};

class LuaOffensiveHorse : public OffensiveHorse
{
    Q_OBJECT

public:
    Q_INVOKABLE LuaOffensiveHorse(Card::Suit suit, int number, int correct, const QString &obj_name, const QString &class_name);
    LuaOffensiveHorse *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

    // member functions that do not expose to Lua interpreter

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool isAvailable(const Player *player) const;

    void onInstall(ServerPlayer *player) const;
    void onUninstall(ServerPlayer *player) const;
    int getCorrect(const Player *player = nullptr) const;

    inline QString getClassName() const
    {
        return class_names.first();
    }
    inline bool isKindOf(const char *cardType) const
    {
		return strcmp(cardType, "LuaCard") == 0
			|| class_names.contains(QString(cardType))
			|| OffensiveHorse::isKindOf(cardType);
    }
    inline void setTargetFixed(bool target_fixed)
    {
        this->target_fixed = target_fixed;
    }

    // the lua callbacks
    LuaFunction filter;
    LuaFunction feasible;
    LuaFunction available;
    LuaFunction on_install;
    LuaFunction on_uninstall;
    LuaFunction correct_func;

private:
    QStringList class_names;
    QString class_name;
};

class LuaDefensiveHorse : public DefensiveHorse
{
    Q_OBJECT

public:
    Q_INVOKABLE LuaDefensiveHorse(Card::Suit suit, int number, int correct, const QString &obj_name, const QString &class_name);
    LuaDefensiveHorse *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

    // member functions that do not expose to Lua interpreter

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool isAvailable(const Player *player) const;

    void onInstall(ServerPlayer *player) const;
    void onUninstall(ServerPlayer *player) const;
    int getCorrect(const Player *player = nullptr) const;

    inline QString getClassName() const
    {
        return class_names.first();
    }
    inline bool isKindOf(const char *cardType) const
    {
		return strcmp(cardType, "LuaCard") == 0
			|| class_names.contains(QString(cardType))
			|| DefensiveHorse::isKindOf(cardType);
    }
    inline void setTargetFixed(bool target_fixed)
    {
        this->target_fixed = target_fixed;
    }

    // the lua callbacks
    LuaFunction filter;
    LuaFunction feasible;
    LuaFunction available;
    LuaFunction on_install;
    LuaFunction on_uninstall;
    LuaFunction correct_func;

private:
    QStringList class_names;
    QString class_name;
};

class LuaTreasure : public Treasure
{
    Q_OBJECT

public:
    Q_INVOKABLE LuaTreasure(Card::Suit suit, int number, const QString &obj_name, const QString &class_name);
    LuaTreasure *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

    // member functions that do not expose to Lua interpreter

    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool isAvailable(const Player *player) const;

    void onInstall(ServerPlayer *player) const;
    void onUninstall(ServerPlayer *player) const;

    inline QString getClassName() const
    {
        return class_names.first();
    }
    inline bool isKindOf(const char *cardType) const
    {
		return strcmp(cardType, "LuaCard") == 0
			|| class_names.contains(QString(cardType))
			|| Treasure::isKindOf(cardType);
    }
    inline void setTargetFixed(bool target_fixed)
    {
        this->target_fixed = target_fixed;
    }

    // the lua callbacks
    LuaFunction filter;
    LuaFunction feasible;
    LuaFunction available;
    LuaFunction on_install;
    LuaFunction on_uninstall;

private:
    QStringList class_names;
    QString class_name;
};

#endif