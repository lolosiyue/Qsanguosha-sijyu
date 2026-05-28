class LuaTriggerSkill: public TriggerSkill {
public:
	LuaTriggerSkill(const char *name, Frequency frequency, const char *limit_mark, bool change_skill, bool limited_skill, bool hide_skill,
					bool shiming_skill, const char *waked_skills);
	void addEvent(TriggerEvent event);
	void setViewAsSkill(ViewAsSkill *view_as_skill);
	void setGlobal(bool global);
	void insertPriorityTable(TriggerEvent triggerEvent, int priority);
	void setGuhuoDialog(const char *type);
	void setJuguanDialog(const char *type);
	void setTiansuanDialog(const char *type);
	void setWakedSkills(const char *waked_skills);

	virtual Frequency getFrequency(const Player *target) const;

	virtual bool triggerable(ServerPlayer *target, Room *room, TriggerEvent event, ServerPlayer *owner, QVariant data) const;
	virtual bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const;
	virtual bool canWake(TriggerEvent event, ServerPlayer *player, QVariant &data, Room *room) const;

	LuaFunction on_trigger;
	LuaFunction can_trigger;
	LuaFunction dynamic_frequency;
	LuaFunction can_wake;

	int priority;
};

class SkillContext;

class LuaTriggerV2Skill: public TriggerV2Skill {
public:
	LuaTriggerV2Skill(const char *name, Frequency frequency, const char *limit_mark);
	void addEvent(TriggerEvent event);
	void setViewAsSkill(ViewAsSkill *view_as_skill);
	void setGlobal(bool global);
	void setBaseAmount(int amount);
	void setLimitScope(Skill::LimitScope scope);
	void setMaxUsageLimit(int limit);
	void setShimingSkill(bool shiming);
	void setWakedSkills(const char *waked_skills);
	void setChangeSkill(bool change);
	void setHideSkill(bool hide);

	virtual int getPriority() const;
	virtual Frequency getFrequency(const Player *target) const;
	virtual Skill::LimitScope getLimitScope() const;
	virtual int getMaxUsageLimit(const SkillContext &ctx) const;

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
	virtual void willInvoke(SkillContext &ctx) const;
	virtual void targetConfirming(SkillContext &ctx) const;
	virtual void invoking(SkillContext &ctx) const;
	virtual void effect(SkillContext &ctx) const;
	virtual void effectFinished(SkillContext &ctx) const;
	virtual bool checkCustomUsage(const SkillContext &ctx) const;
	virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
	                     QVariant &data, ServerPlayer *owner) const;
	virtual bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const;
	void onTurnBroken(const char *function_name, TriggerEvent triggerEvent, Room *room,
	                 ServerPlayer *player, SkillContext &ctx) const;
	void onShimingSuccess(Room *room, ServerPlayer *player) const;
	void onShimingFail(Room *room, ServerPlayer *player) const;

	LuaFunction on_record;
	LuaFunction can_trigger;
	LuaFunction on_cost;
	LuaFunction on_pay;
	LuaFunction on_effect;
	LuaFunction on_effect_target;
	LuaFunction on_turn_broken;
	LuaFunction check_custom_usage;
	LuaFunction on_willInvoke;
	LuaFunction on_targetConfirming;
	LuaFunction on_invoking;
	LuaFunction on_effectContext;
	LuaFunction on_effectFinished;
	LuaFunction dynamic_frequency;
	LuaFunction on_shiming_success;
	LuaFunction on_shiming_fail;

	int priority;
};

class ScenarioRule : public TriggerSkill {
public:
	ScenarioRule(Scenario *scenario);

	virtual bool triggerable(const ServerPlayer *target) const;
};

class LuaScenarioRule: public ScenarioRule {
public:
	LuaScenarioRule(Scenario *scenario);
	void addEvent(TriggerEvent event);
	void setGlobal(bool global);
	void insertPriorityTable(TriggerEvent triggerEvent, int priority);

	virtual bool triggerable(const ServerPlayer *target) const;
	virtual bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const;

	LuaFunction can_trigger;
	LuaFunction on_trigger;

	int priority;
};

class ProhibitSkill: public Skill {
public:
	ProhibitSkill(const QString &name);

	virtual bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &others = QList<const Player *>()) const = 0;
};

class LuaProhibitSkill: public ProhibitSkill {
public:
	LuaProhibitSkill(const char *name, Frequency frequency);

	virtual bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &others = QList<const Player *>()) const;

	LuaFunction is_prohibited;
};

class ProhibitPindianSkill: public Skill {
public:
	ProhibitPindianSkill(const QString &name);

	virtual bool isPindianProhibited(const Player *from, const Player *to) const = 0;
};

class LuaProhibitPindianSkill: public ProhibitPindianSkill {
public:
	LuaProhibitPindianSkill(const char *name, Frequency frequency);

	virtual bool isPindianProhibited(const Player *from, const Player *to) const;

	LuaFunction is_pindianprohibited;
};

class DistanceSkill: public Skill {
public:
	DistanceSkill(const QString &name);

	virtual int getCorrect(const Player *from, const Player *to) const;
	virtual int getFixed(const Player *from, const Player *to) const;
};

class LuaDistanceSkill: public DistanceSkill {
public:
	LuaDistanceSkill(const char *name, Frequency frequency);

	virtual int getCorrect(const Player *from, const Player *to) const;
	virtual int getFixed(const Player *from, const Player *to) const;

	LuaFunction correct_func;
	LuaFunction fixed_func;
};

class MaxCardsSkill: public Skill {
public:
	MaxCardsSkill(const QString &name);

	virtual int getExtra(const Player *target) const;
	virtual int getFixed(const Player *target) const;
};

class LuaMaxCardsSkill: public MaxCardsSkill {
public:
	LuaMaxCardsSkill(const char *name, Frequency frequency);

	virtual int getExtra(const Player *target) const;
	virtual int getFixed(const Player *target) const;

	LuaFunction extra_func;
	LuaFunction fixed_func;
};

class TargetModSkill: public Skill {
public:
	enum ModType {
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

class LuaTargetModSkill: public TargetModSkill {
public:
	LuaTargetModSkill(const char *name, const char *pattern, Frequency frequency);

	virtual int getResidueNum(const Player *from, const Card *card, const Player *to) const;
	virtual int getDistanceLimit(const Player *from, const Card *card, const Player *to) const;
	virtual int getExtraTargetNum(const Player *from, const Card *card) const;

	LuaFunction residue_func;
	LuaFunction distance_limit_func;
	LuaFunction extra_target_func;
};

class InvaliditySkill: public Skill {
public:
	InvaliditySkill(const QString &name);

	virtual bool isSkillValid(const Player *player, const Skill *skill) const = 0;
};

class LuaInvaliditySkill: public InvaliditySkill {
public:
	LuaInvaliditySkill(const char *name, Frequency frequency);

	virtual bool isSkillValid(const Player *player, const Skill *skill) const;

	LuaFunction skill_valid;
};

class AttackRangeSkill: public Skill {
public:
	AttackRangeSkill(const QString &name);

	virtual int getExtra(const Player *target, bool include_weapon) const;
	virtual int getFixed(const Player *target, bool include_weapon) const;
};

class LuaAttackRangeSkill: public AttackRangeSkill {
public:
	LuaAttackRangeSkill(const char *name, Frequency frequency);

	virtual int getExtra(const Player *target, bool include_weapon) const;
	virtual int getFixed(const Player *target, bool include_weapon) const;

	LuaFunction extra_func;
	LuaFunction fixed_func;
};

class ViewAsEquipSkill : public Skill {
public:
	ViewAsEquipSkill(const QString &name);

	virtual QString viewAsEquip(const Player *target) const;
};

class LuaViewAsEquipSkill : public ViewAsEquipSkill {
public:
	LuaViewAsEquipSkill(const char *name, Frequency frequency);

	virtual QString viewAsEquip(const Player *target) const;

	LuaFunction view_as_equip;
};

class CardLimitSkill : public Skill {
public:
	CardLimitSkill(const QString &name);

	virtual QString limitList(const Player *target, const Card *card) const;
	virtual QString limitPattern(const Player *target, const Card *card) const;
	virtual QString limitReason(const Player *target, const Card *card) const;
};

class LuaCardLimitSkill : public CardLimitSkill {
public:
	LuaCardLimitSkill(const char *name, Frequency frequency);

	virtual QString limitList(const Player *target, const Card *card) const;
	virtual QString limitPattern(const Player *target, const Card *card) const;
	virtual QString limitReason(const Player *target, const Card *card) const;

	LuaFunction limit_list;
	LuaFunction limit_pattern;
	LuaFunction limit_reason;
};

class PreSelectionMetaSkill : public Skill {
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

class LuaPreSelectionMetaSkill : public PreSelectionMetaSkill {
public:
	LuaPreSelectionMetaSkill(const char *name, const char *active_skills);

	virtual QStringList onGeneralChoosing(Room *room, ServerPlayer *player,
								 QStringList generals, const QString &reason) const;
	virtual void onGeneralNotChosen(Room *room, ServerPlayer *player,
								  const QStringList &generals, const QString &chosen,
								  const QString &reason) const;

	LuaFunction on_general_choosing;
	LuaFunction on_general_not_chosen;
};

class AnytimeSkill : public Skill {
public:
	AnytimeSkill(const QString &name);

	virtual bool canTrigger(ServerPlayer *player) const;
	virtual bool onTrigger(Room *room, ServerPlayer *player) const;
};

class LuaAnytimeSkill : public AnytimeSkill {
public:
	LuaAnytimeSkill(const char *name, Frequency frequency);

	virtual bool canTrigger(ServerPlayer *player) const;
	virtual bool onTrigger(Room *room, ServerPlayer *player) const;

LuaFunction can_trigger;
    LuaFunction on_trigger;
};

class BattleArraySkill : public TriggerSkill {
public:
    BattleArraySkill(const QString &name, const QString &arrayType);

    virtual void summonFriends(ServerPlayer *player) const;

    QString getArrayType() const;
};

class LuaBattleArraySkill : public BattleArraySkill {
public:
    LuaBattleArraySkill(const char *name, const char *arrayType, Frequency frequency);

    virtual void summonFriends(ServerPlayer *player) const;

    LuaFunction on_summon;
};

class ViewAsSkill: public Skill {
public:
	ViewAsSkill(const QString &name);

	virtual bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const = 0;
	virtual const Card *viewAs(const QList<const Card *> &cards) const = 0;

	virtual bool isEnabledAtPlay(const Player *player) const;
	virtual bool isEnabledAtResponse(const Player *player, const QString &pattern) const;
	virtual bool isEnabledAtNullification(const ServerPlayer *player) const;

	bool isResponseOrUse() const;
	QString getExpandPile() const;
};

class LuaViewAsSkill: public ViewAsSkill {
public:
	LuaViewAsSkill(const char *name, const char *response_pattern, bool response_or_use, const char *expand_pile,
					Frequency frequency, const char *limit_mark);

	virtual bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const;
	virtual const Card *viewAs(const QList<const Card *> &cards) const;

	bool isEnabledAtPlay(const Player *player) const;
	bool isEnabledAtResponse(const Player *player, const QString &pattern) const;
	bool isEnabledAtNullification(const ServerPlayer *player) const;

	virtual bool shouldBeVisible(const Player *player) const;

	LuaFunction should_be_visible;

	LuaFunction view_filter;
	LuaFunction view_as;

	LuaFunction enabled_at_play;
	LuaFunction enabled_at_response;
	LuaFunction enabled_at_nullification;

	void setGuhuoDialog(const char *type);
	void setJuguanDialog(const char *type);
	void setTiansuanDialog(const char *type);
	void setWakedSkills(const char *waked_skills);
};

class OneCardViewAsSkill: public ViewAsSkill {
public:
	OneCardViewAsSkill(const QString &name);

	virtual bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const;
	virtual const Card *viewAs(const QList<const Card *> &cards) const;

	virtual bool viewFilter(const Card *to_select) const;
	virtual const Card *viewAs(const Card *originalCard) const = 0;

protected:
	QString filter_pattern;
};

class FilterSkill: public OneCardViewAsSkill {
public:
	FilterSkill(const QString &name);
};

class LuaFilterSkill: public FilterSkill {
public:
	LuaFilterSkill(const char *name, Frequency frequency);

	bool viewFilter(const Card *to_select) const;
	const Card *viewAs(const Card *originalCard) const;

	LuaFunction view_filter;
	LuaFunction view_as;
};

class LuaSkillCard: public SkillCard {
public:
	LuaSkillCard(const char *name, const char *skillName);
	void setTargetFixed(bool target_fixed);
	void setWillThrow(bool will_throw);
	void setCanRecast(bool can_recast);
	void setHandlingMethod(Card::HandlingMethod handling_method);
	void setMute(bool mute);
	LuaSkillCard *clone() const;

	LuaFunction filter;
	LuaFunction feasible;
	LuaFunction about_to_use;
	LuaFunction on_use;
	LuaFunction on_effect;
	LuaFunction on_validate;
	LuaFunction on_validate_in_response;
};

class LuaBasicCard: public BasicCard {
public:
	LuaBasicCard(Card::Suit suit, int number, const char *obj_name, const char *class_name, const char *subtype);
	LuaBasicCard *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;
	void setTargetFixed(bool target_fixed);
	void setCanRecast(bool can_recast);
	void setGift(bool flag);
	void setDamageCard(bool flag);

	virtual void onUse(Room *room, CardUseStruct &card_use) const;
	virtual void onEffect(CardEffectStruct &effect) const;
	virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;

	virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
	virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
	virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self, int &maxVotes) const;
	virtual bool isAvailable(const Player *player) const;

	virtual QString getClassName() const;
	virtual QString getSubtype() const;
	virtual bool isKindOf(const char *cardType) const;

	// the lua callbacks
	LuaFunction filter;
	LuaFunction feasible;
	LuaFunction available;
	LuaFunction about_to_use;
	LuaFunction on_use;
	LuaFunction on_effect;
	LuaFunction on_validate;
	LuaFunction on_validate_in_response;
};

class LuaTrickCard: public TrickCard {
public:
	enum SubClass { TypeNormal, TypeSingleTargetTrick, TypeDelayedTrick, TypeAOE, TypeGlobalEffect };

	LuaTrickCard(Card::Suit suit, int number, const char *obj_name, const char *class_name, const char *subtype);
	LuaTrickCard *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;
	void setTargetFixed(bool target_fixed);
	void setCanRecast(bool can_recast);
	void setGift(bool flag);
	void setDamageCard(bool flag);

	virtual void onUse(Room *room, CardUseStruct &card_use) const;
	virtual void onEffect(CardEffectStruct &effect) const;
	virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
	virtual void onNullified(ServerPlayer *target) const;
	virtual bool isCancelable(const CardEffectStruct &effect) const;

	virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
	virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
	virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self, int &maxVotes) const;
	virtual bool isAvailable(const Player *player) const;

	virtual QString getClassName() const;
	void setSubClass(SubClass subclass);
	SubClass getSubClass() const;
	virtual QString getSubtype() const;
	virtual bool isKindOf(const char *cardType) const;

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
};

class LuaWeapon: public Weapon {
public:
	LuaWeapon(Card::Suit suit, int number, int range, const char *obj_name, const char *class_name);
	LuaWeapon *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

	void setGift(bool flag);
	void setTargetFixed(bool target_fixed);

	virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
	virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;

	virtual void onInstall(ServerPlayer *player) const;
	virtual void onUninstall(ServerPlayer *player) const;
	int getRange() const;

	virtual QString getClassName();
	virtual bool isKindOf(const char *cardType);

	// the lua callbacks
	LuaFunction filter;
	LuaFunction feasible;
	LuaFunction available;
	LuaFunction on_install;
	LuaFunction on_uninstall;
};

class LuaArmor: public Armor {
public:
	LuaArmor(Card::Suit suit, int number, const char *obj_name, const char *class_name);
	LuaArmor *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

	void setGift(bool flag);
	void setTargetFixed(bool target_fixed);

	virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
	virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;

	virtual void onInstall(ServerPlayer *player) const;
	virtual void onUninstall(ServerPlayer *player) const;

	virtual QString getClassName();
	virtual bool isKindOf(const char *cardType);

	// the lua callbacks
	LuaFunction filter;
	LuaFunction feasible;
	LuaFunction available;
	LuaFunction on_install;
	LuaFunction on_uninstall;
};

class LuaHorse: public Horse {
public:
	LuaHorse(Card::Suit suit, int number, int range, const char *obj_name, const char *class_name);
	LuaHorse *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

	void setGift(bool flag);
	void setTargetFixed(bool target_fixed);

	virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
	virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;

	virtual void onInstall(ServerPlayer *player) const;
	virtual void onUninstall(ServerPlayer *player) const;
	virtual int getCorrect(const Player *player) const;

	virtual QString getClassName();
	virtual bool isKindOf(const char *cardType);

	// the lua callbacks
	LuaFunction filter;
	LuaFunction feasible;
	LuaFunction available;
	LuaFunction on_install;
	LuaFunction on_uninstall;
    LuaFunction correct_func;
};

class LuaOffensiveHorse: public OffensiveHorse {
public:
	LuaOffensiveHorse(Card::Suit suit, int number, int correct, const char *obj_name, const char *class_name);
	LuaOffensiveHorse *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

	void setGift(bool flag);
	void setTargetFixed(bool target_fixed);

	virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
	virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;

	virtual void onInstall(ServerPlayer *player) const;
	virtual void onUninstall(ServerPlayer *player) const;
	virtual int getCorrect(const Player *player) const;

	virtual QString getClassName();
	virtual bool isKindOf(const char *cardType);

	// the lua callbacks
	LuaFunction filter;
	LuaFunction feasible;
	LuaFunction available;
	LuaFunction on_install;
	LuaFunction on_uninstall;
    LuaFunction correct_func;
};

class LuaDefensiveHorse: public DefensiveHorse {
public:
	LuaDefensiveHorse(Card::Suit suit, int number, int correct, const char *obj_name, const char *class_name);
	LuaDefensiveHorse *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

	void setGift(bool flag);
	void setTargetFixed(bool target_fixed);

	virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
	virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;

	virtual void onInstall(ServerPlayer *player) const;
	virtual void onUninstall(ServerPlayer *player) const;
	virtual int getCorrect(const Player *player) const;

	virtual QString getClassName();
	virtual bool isKindOf(const char *cardType);

	// the lua callbacks
	LuaFunction filter;
	LuaFunction feasible;
	LuaFunction available;
	LuaFunction on_install;
	LuaFunction on_uninstall;
    LuaFunction correct_func;
};

class LuaTreasure: public Treasure {
public:
	LuaTreasure(Card::Suit suit, int number, const char *obj_name, const char *class_name);
	LuaTreasure *clone(Card::Suit suit = Card::SuitToBeDecided, int number = -1) const;

	void setGift(bool flag);
	void setTargetFixed(bool target_fixed);

	virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
	virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;

	virtual void onInstall(ServerPlayer *player) const;
	virtual void onUninstall(ServerPlayer *player) const;

	virtual QString getClassName();
	virtual bool isKindOf(const char *cardType);

	// the lua callbacks
	LuaFunction filter;
	LuaFunction feasible;
	LuaFunction available;
	LuaFunction on_install;
	LuaFunction on_uninstall;
};

%{

#include "lua-wrapper.h"
#include "clientplayer.h"

static QList<lua_State*>lua_list;
static lua_State*luaState()
{
	if(lua_list.isEmpty()){
		for (int i = 0; i < 99; i++)
			lua_list << lua_newthread(Sanguosha->getLuaState());
	}
	lua_State*L = lua_list.takeFirst();
	lua_list << L;
	return L;
}

bool LuaTriggerSkill::triggerable(ServerPlayer *target, Room *room, TriggerEvent event, ServerPlayer *owner, QVariant data) const
{
	if (can_trigger == 0)
		return TriggerSkill::triggerable(target, room, event, owner, data);

	lua_State*L = room->getLuaState();

	// the callback function
	lua_rawgeti(L, LUA_REGISTRYINDEX, can_trigger);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTriggerSkill, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_ServerPlayer, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
	lua_pushinteger(L, event);
	SWIG_NewPointerObj(L, owner, SWIGTYPE_p_ServerPlayer, 0);
	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);

	if (lua_pcall(L, 6, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaTriggerSkill::canWake(TriggerEvent event, ServerPlayer *player, QVariant &data, Room *room) const
{
	if (can_wake == 0)
		return TriggerSkill::canWake(event, player, data, room);

	lua_State*L = room->getLuaState();

	// the callback function
	lua_rawgeti(L, LUA_REGISTRYINDEX, can_wake);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTriggerSkill, 0);
	lua_pushinteger(L, event);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);
	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

	if (lua_pcall(L, 5, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaTriggerSkill::trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
{
	if (on_trigger == 0)
		return false;

	lua_State*L = room->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_trigger);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTriggerSkill, 0);
	// the first argument: event
	lua_pushinteger(L, event);
	// the second argument: player
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);
	// the last event: data
	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);
	// append Room as an argument
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

	if (lua_pcall(L, 5, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

LuaTriggerV2Skill::LuaTriggerV2Skill(const char *name, Frequency frequency, const char *limit_mark)
	: TriggerV2Skill(name), on_record(0), can_trigger(0), on_cost(0), on_pay(0), on_effect(0), on_effect_target(0), on_turn_broken(0), check_custom_usage(0),
	  on_willInvoke(0), on_targetConfirming(0), on_invoking(0), on_effectContext(0), on_effectFinished(0),
	  m_limitScope(Limit_None), m_maxUsageLimit(1)
{
	this->frequency = frequency;
	this->limit_mark = limit_mark;
	this->priority = 2;
}

int LuaTriggerV2Skill::getPriority() const
{
	return priority;
}

Skill::LimitScope LuaTriggerV2Skill::getLimitScope() const
{
	return m_limitScope;
}

int LuaTriggerV2Skill::getMaxUsageLimit(const SkillContext &) const
{
	return m_maxUsageLimit;
}

bool LuaTriggerV2Skill::cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const
{
	if (on_cost == 0)
		return TriggerV2Skill::cost(triggerEvent, room, player, ctx);
	try {
		lua_State *L = room->getLuaState();

		int e = static_cast<int>(triggerEvent);

		lua_rawgeti(L, LUA_REGISTRYINDEX, on_cost);

		LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
		SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

		lua_pushinteger(L, e);

		SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

		SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

		SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

		int error = lua_pcall(L, 5, 1, 0);
		if (error) {
			const char *error_msg = lua_tostring(L, -1);
			lua_pop(L, 1);
			room->output(error_msg);
			return true;
		} else {
			bool result = lua_toboolean(L, -1);
			lua_pop(L, 1);
			return result;
		}
	}
	catch (TriggerEvent e) {
		if (e == TurnBroken || e == StageChange)
			onTurnBroken("on_cost", triggerEvent, room, player, ctx);
		throw e;
	}
}

bool LuaTriggerV2Skill::pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const
{
	if (on_pay == 0)
		return TriggerV2Skill::pay(triggerEvent, room, player, ctx);
	try {
		lua_State *L = room->getLuaState();

		int e = static_cast<int>(triggerEvent);

		lua_rawgeti(L, LUA_REGISTRYINDEX, on_pay);

		LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
		SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

		lua_pushinteger(L, e);

		SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

		SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

		SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

		int error = lua_pcall(L, 5, 1, 0);
		if (error) {
			const char *error_msg = lua_tostring(L, -1);
			lua_pop(L, 1);
			room->output(error_msg);
			return false;
		} else {
			bool result = lua_toboolean(L, -1);
			lua_pop(L, 1);
			return result;
		}
	}
	catch (TriggerEvent e) {
		if (e == TurnBroken || e == StageChange)
			onTurnBroken("on_pay", triggerEvent, room, player, ctx);
		throw e;
	}
}

bool LuaTriggerV2Skill::effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const
{
	if (on_effect == 0)
		return TriggerV2Skill::effect(triggerEvent, room, player, ctx);

	try {
		lua_State *L = room->getLuaState();

		int e = static_cast<int>(triggerEvent);

		lua_rawgeti(L, LUA_REGISTRYINDEX, on_effect);

		LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
		SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

		lua_pushinteger(L, e);

		SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

		SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

		SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

		int error = lua_pcall(L, 5, 1, 0);
		if (error) {
			const char *error_msg = lua_tostring(L, -1);
			lua_pop(L, 1);
			room->output(error_msg);
			return false;
		} else {
			bool result = lua_toboolean(L, -1);
			lua_pop(L, 1);
			return result;
		}
	}
	catch (TriggerEvent e) {
		if (e == TurnBroken || e == StageChange)
			onTurnBroken("on_effect", triggerEvent, room, player, ctx);
		throw e;
	}
}

bool LuaTriggerV2Skill::effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const
{
	if (on_effect_target == 0)
		return TriggerV2Skill::effectTarget(triggerEvent, room, player, ctx, target);

	try {
		lua_State *L = room->getLuaState();

		int e = static_cast<int>(triggerEvent);

		lua_rawgeti(L, LUA_REGISTRYINDEX, on_effect_target);

		LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
		SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

		lua_pushinteger(L, e);

		SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

		SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

		SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

		SWIG_NewPointerObj(L, target, SWIGTYPE_p_ServerPlayer, 0);

		int error = lua_pcall(L, 6, 1, 0);
		if (error) {
			const char *error_msg = lua_tostring(L, -1);
			lua_pop(L, 1);
			room->output(error_msg);
			return false;
		} else {
			bool result = lua_toboolean(L, -1);
			lua_pop(L, 1);
			return result;
		}
	}
	catch (TriggerEvent e) {
		if (e == TurnBroken || e == StageChange)
			onTurnBroken("on_effect_target", triggerEvent, room, player, ctx);
		throw e;
	}
}

void LuaTriggerV2Skill::onTurnBroken(const char *function_name, TriggerEvent triggerEvent, Room *room,
                                    ServerPlayer *player, SkillContext &ctx) const
{
	if (on_turn_broken == 0) return;

	lua_State *L = room->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_turn_broken);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	lua_pushstring(L, function_name);

	lua_pushinteger(L, static_cast<int>(triggerEvent));

	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

	lua_pcall(L, 6, 0, 0);
}

void LuaTriggerV2Skill::record(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const
{
	if (on_record == 0) return;

	lua_State *L = room->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_record);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	lua_pushinteger(L, static_cast<int>(triggerEvent));

	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (ctx.original_data) {
		SWIG_NewPointerObj(L, ctx.original_data, SWIGTYPE_p_QVariant, 0);
	} else {
		lua_pushnil(L);
	}

	if (ctx.owner) {
		SWIG_NewPointerObj(L, ctx.owner, SWIGTYPE_p_ServerPlayer, 0);
	} else {
		lua_pushnil(L);
	}

	lua_pcall(L, 6, 0, 0);
}

TriggerList LuaTriggerV2Skill::triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
{
	TriggerList result;
	if (!can_trigger || !player) return result;

	lua_State *L = room->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, can_trigger);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	lua_pushinteger(L, triggerEvent);

	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);

	if (lua_pcall(L, 5, 2, 0) != 0) {
		lua_pop(L, 1);
		return result;
	}

	// 檢查第一個返回值（L[-2]）是否是 false
	if (lua_isboolean(L, -2) && !lua_toboolean(L, -2)) {
		lua_pop(L, 2);
		return result;
	}

	// 檢查第二個返回值（L[-1]）是否是 ask_who (ServerPlayer*)
	ServerPlayer *ask_who = NULL;
	if (lua_isuserdata(L, -1)) {
		SWIG_ConvertPtr(L, -1, (void**)&ask_who, SWIGTYPE_p_ServerPlayer, 0);
	}

	// 檢查第一個返回值（L[-2]）是否是 skillName (string)
	if (lua_isstring(L, -2)) {
		QString names = QString::fromUtf8(lua_tostring(L, -2));
		lua_pop(L, 2);

		if (!names.isEmpty()) {
			QStringList nameList = names.split("+");
			foreach (const QString &name, nameList) {
				result[ask_who ? ask_who : player] << name.trimmed();
			}
		}
	} else {
		lua_pop(L, 2);
	}

	return result;
}

void LuaTriggerV2Skill::willInvoke(SkillContext &ctx) const
{
	if (!on_willInvoke) return;

	lua_State *L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_willInvoke);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

	lua_pcall(L, 2, 0, 0);
}

void LuaTriggerV2Skill::targetConfirming(SkillContext &ctx) const
{
	if (!on_targetConfirming) return;

	lua_State *L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_targetConfirming);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

	lua_pcall(L, 2, 0, 0);
}

void LuaTriggerV2Skill::invoking(SkillContext &ctx) const
{
	if (!on_invoking) return;

	lua_State *L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_invoking);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

	lua_pcall(L, 2, 0, 0);
}

void LuaTriggerV2Skill::effect(SkillContext &ctx) const
{
	if (!on_effectContext) return;

	lua_State *L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_effectContext);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

	lua_pcall(L, 2, 0, 0);
}

void LuaTriggerV2Skill::effectFinished(SkillContext &ctx) const
{
	if (!on_effectFinished) return;

	lua_State *L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_effectFinished);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

	lua_pcall(L, 2, 0, 0);
}

bool LuaTriggerV2Skill::checkCustomUsage(const SkillContext &ctx) const
{
	if (!check_custom_usage)
		return TriggerV2Skill::checkCustomUsage(ctx);

	lua_State *L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, check_custom_usage);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	SWIG_NewPointerObj(L, &ctx, SWIGTYPE_p_SkillContext, 0);

	if (lua_pcall(L, 2, 1, 0) != 0) {
		lua_pop(L, 1);
		return TriggerV2Skill::checkCustomUsage(ctx);
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaTriggerV2Skill::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
{
	return TriggerV2Skill::trigger(triggerEvent, room, player, data, NULL);
}

bool LuaTriggerV2Skill::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data, ServerPlayer *owner) const
{
	return TriggerV2Skill::trigger(triggerEvent, room, player, data, owner);
}

void LuaTriggerV2Skill::onShimingSuccess(Room *room, ServerPlayer *player) const
{
	if (on_shiming_success == 0)
		return;

	lua_State *L = room->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_shiming_success);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	int error = lua_pcall(L, 3, 0, 0);
	if (error) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
	}
}

void LuaTriggerV2Skill::onShimingFail(Room *room, ServerPlayer *player) const
{
	if (on_shiming_fail == 0)
		return;

	lua_State *L = room->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_shiming_fail);

	LuaTriggerV2Skill *self = const_cast<LuaTriggerV2Skill *>(this);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_LuaTriggerV2Skill, 0);

	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);

	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	int error = lua_pcall(L, 3, 0, 0);
	if (error) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
	}
}

bool LuaScenarioRule::triggerable(const ServerPlayer *target) const
{
	if (can_trigger == 0)
		return ScenarioRule::triggerable(target);

	lua_State*L = Sanguosha->getLuaState();

	// the callback function
	lua_rawgeti(L, LUA_REGISTRYINDEX, can_trigger);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaScenarioRule, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		target->getRoom()->output(error_msg);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaScenarioRule::trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
{
	if (on_trigger == 0)
		return false;

	lua_State*L = room->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_trigger);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaScenarioRule, 0);
	lua_pushinteger(L, event);// the first argument: event
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);// the second argument: player
	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);// the last event: data
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);// append Room as an argument

	if (lua_pcall(L, 5, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

//#include <QMessageBox>
#include <QThread>
#include <QCoreApplication>

static void Error(lua_State *L)
{
	const QString error_string = lua_tostring(L, -1);
	lua_pop(L, 1);
	qWarning("Lua script error: %s", error_string.toUtf8().constData());
	if (QThread::currentThread() == qApp->thread()) {
		QMessageBox::warning(nullptr, "Lua script error!", error_string);
	}
}

Skill::Frequency LuaTriggerSkill::getFrequency(const Player *target) const
{
	return Skill::getFrequency(target);
}

Skill::Frequency LuaTriggerV2Skill::getFrequency(const Player *target) const
{
	return Skill::getFrequency(target);
}

bool LuaProhibitSkill::isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &others) const
{
	if (is_prohibited == 0)
		return false;

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, is_prohibited);

	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaProhibitSkill, 0);
	SWIG_NewPointerObj(L, from, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, to, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, card, SWIGTYPE_p_Card, 0);
	lua_createtable(L, others.length(), 0);
	for (int i = 0; i < others.length(); i++) {
		SWIG_NewPointerObj(L, others[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}

	if (lua_pcall(L, 5, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaProhibitPindianSkill::isPindianProhibited(const Player *from, const Player *to) const
{
	if (is_pindianprohibited == 0)
		return false;

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, is_pindianprohibited);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaProhibitPindianSkill, 0);
	SWIG_NewPointerObj(L, from, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, to, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

int LuaDistanceSkill::getCorrect(const Player *from, const Player *to) const
{
	if (correct_func == 0)
		return DistanceSkill::getCorrect(from,to);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, correct_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaDistanceSkill, 0);
	SWIG_NewPointerObj(L, from, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, to, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return DistanceSkill::getCorrect(from,to);
	}

	int correct = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return correct;
}

int LuaDistanceSkill::getFixed(const Player *from, const Player *to) const
{
	if (fixed_func == 0)
		return DistanceSkill::getFixed(from,to);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, fixed_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaDistanceSkill, 0);
	SWIG_NewPointerObj(L, from, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, to, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return DistanceSkill::getFixed(from,to);
	}

	int fixed = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return fixed;
}

int LuaMaxCardsSkill::getExtra(const Player *target) const
{
	if (extra_func == 0)
		return MaxCardsSkill::getExtra(target);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, extra_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaMaxCardsSkill, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return MaxCardsSkill::getExtra(target);
	}

	int extra = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return extra;
}

int LuaMaxCardsSkill::getFixed(const Player *target) const
{
	if (fixed_func == 0)
		return MaxCardsSkill::getFixed(target);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, fixed_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaMaxCardsSkill, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return MaxCardsSkill::getFixed(target);
	}

	int fixed = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return fixed;
}

int LuaTargetModSkill::getResidueNum(const Player *from, const Card *card, const Player *to) const
{
	if (residue_func == 0)
		return TargetModSkill::getResidueNum(from,card,to);
	if (!from || !card)
		return TargetModSkill::getResidueNum(from,card,to);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, residue_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTargetModSkill, 0);
	SWIG_NewPointerObj(L, from, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, card, SWIGTYPE_p_Card, 0);
	SWIG_NewPointerObj(L, to, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 1, 0)!=0) {
		Error(L);
		return TargetModSkill::getResidueNum(from,card,to);
	}

	int residue = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return residue;
}

int LuaTargetModSkill::getDistanceLimit(const Player *from, const Card *card, const Player *to) const
{
	if (distance_limit_func == 0)
		return TargetModSkill::getDistanceLimit(from,card,to);
	if (!from || !card)
		return TargetModSkill::getDistanceLimit(from,card,to);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, distance_limit_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTargetModSkill, 0);
	SWIG_NewPointerObj(L, from, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, card, SWIGTYPE_p_Card, 0);
	SWIG_NewPointerObj(L, to, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 1, 0)!=0) {
		Error(L);
		return TargetModSkill::getDistanceLimit(from,card,to);
	}

	int distance_limit = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return distance_limit;
}

int LuaTargetModSkill::getExtraTargetNum(const Player *from, const Card *card) const
{
	if (extra_target_func == 0)
		return TargetModSkill::getExtraTargetNum(from,card);
	if (!from || !card)
		return TargetModSkill::getExtraTargetNum(from,card);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, extra_target_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTargetModSkill, 0);
	SWIG_NewPointerObj(L, from, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, card, SWIGTYPE_p_Card, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return TargetModSkill::getExtraTargetNum(from,card);
	}

	int extra = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return extra;
}

int LuaAttackRangeSkill::getExtra(const Player *target, bool include_weapon) const
{
	if (extra_func == 0)
		return AttackRangeSkill::getExtra(target, include_weapon);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, extra_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaAttackRangeSkill, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_Player, 0);
	lua_pushboolean(L, include_weapon);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return AttackRangeSkill::getExtra(target, include_weapon);
	}

	int extra = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return extra;
}

int LuaAttackRangeSkill::getFixed(const Player *target, bool include_weapon) const
{
	if (fixed_func == 0)
		return AttackRangeSkill::getFixed(target, include_weapon);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, fixed_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaAttackRangeSkill, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_Player, 0);
	lua_pushboolean(L, include_weapon);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return AttackRangeSkill::getFixed(target, include_weapon);
	}

	int fixed = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return fixed;
}

bool LuaInvaliditySkill::isSkillValid(const Player *player, const Skill *skill) const
{
	if (skill_valid == 0)
		return true;

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, skill_valid);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaInvaliditySkill, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, skill, SWIGTYPE_p_Skill, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return true;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaFilterSkill::viewFilter(const Card *to_select) const
{
	if (view_filter == 0)
		return false;

	// 【核心修復：Try-Lock 避讓機制】
	bool locked = Sanguosha->getLuaMutex().tryLock();
	if (!locked) {
		// 拿不到鎖 (代表 Server 正在算 AI)，UI 絕對不能死等！
		// 直接回傳 false (牌在畫面上暫時反灰 1 毫秒)，避開死鎖與崩潰。
		return false;
	}

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, view_filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaFilterSkill, 0);
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Card, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
	return result;
}

const Card *LuaFilterSkill::viewAs(const Card *originalCard) const
{
	if (view_as == 0)
		return nullptr;

	// 【核心修復：Try-Lock 避讓機制】
	bool locked = Sanguosha->getLuaMutex().tryLock();
	if (!locked) {
		// 拿不到鎖，當作沒有這張牌
		return nullptr;
	}

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, view_as);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaFilterSkill, 0);
	SWIG_NewPointerObj(L, originalCard, SWIGTYPE_p_Card, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
		return nullptr;
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return nullptr;
}

QString LuaViewAsEquipSkill::viewAsEquip(const Player *target) const
{
	if (view_as_equip == 0)
		return QString();

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, view_as_equip);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsEquipSkill, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return QString();
	}

	const QString &result = lua_tostring(L, -1);
	lua_pop(L, 1);
	return result;
}

QString LuaCardLimitSkill::limitList(const Player *target, const Card *card) const
{
	if (limit_list == 0)
		return QString();

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, limit_list);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaCardLimitSkill, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, card, SWIGTYPE_p_Card, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return QString();
	}

	const QString &result = lua_tostring(L, -1);
	lua_pop(L, 1);
	return result;
}

QString LuaCardLimitSkill::limitPattern(const Player *target, const Card *card) const
{
	if (limit_pattern == 0)
		return QString();

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, limit_pattern);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaCardLimitSkill, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, card, SWIGTYPE_p_Card, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return QString();
	}

	const QString &result = lua_tostring(L, -1);
	lua_pop(L, 1);
	return result;
}

QString LuaCardLimitSkill::limitReason(const Player *target, const Card *card) const
{
	if (limit_reason == 0)
		return QString();

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, limit_reason);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaCardLimitSkill, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, card, SWIGTYPE_p_Card, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return QString();
	}

	const QString &result = lua_tostring(L, -1);
	lua_pop(L, 1);
	return result;
}

QStringList LuaPreSelectionMetaSkill::onGeneralChoosing(Room *room, ServerPlayer *player,
											QStringList generals, const QString &reason) const
{
	if (on_general_choosing == 0)
		return generals;

	lua_State *L = room->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_general_choosing);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaPreSelectionMetaSkill, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);
	lua_createtable(L, generals.length(), 0);
	for (int i = 0; i < generals.length(); i++) {
		lua_pushstring(L, generals.at(i).toLatin1().data());
		lua_rawseti(L, -2, i + 1);
	}
	lua_pushstring(L, reason.toLatin1().data());

	if (lua_pcall(L, 5, 1, 0) != 0) {
		Error(L);
		return generals;
	}

	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return generals;
	}

	QStringList result;
	size_t len = lua_rawlen(L, -1);
	for (size_t i = 1; i <= len; i++) {
		lua_rawgeti(L, -1, i);
		result << QString::fromUtf8(lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return result;
}

void LuaPreSelectionMetaSkill::onGeneralNotChosen(Room *room, ServerPlayer *player,
										 const QStringList &generals, const QString &chosen,
										 const QString &reason) const
{
	if (on_general_not_chosen == 0)
		return;

	lua_State *L = room->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_general_not_chosen);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaPreSelectionMetaSkill, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);
	lua_createtable(L, generals.length(), 0);
	for (int i = 0; i < generals.length(); i++) {
		lua_pushstring(L, generals.at(i).toLatin1().data());
		lua_rawseti(L, -2, i + 1);
	}
	lua_pushstring(L, chosen.toLatin1().data());
	lua_pushstring(L, reason.toLatin1().data());

	if (lua_pcall(L, 6, 0, 0) != 0) {
		Error(L);
	}
}

bool LuaAnytimeSkill::canTrigger(ServerPlayer *player) const
{
	if (can_trigger == 0)
		return AnytimeSkill::canTrigger(player);

	lua_State *L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, can_trigger);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaAnytimeSkill, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 1, 0) != 0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaAnytimeSkill::onTrigger(Room *room, ServerPlayer *player) const
{
	if (on_trigger == 0)
		return AnytimeSkill::onTrigger(room, player);

	lua_State *L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, on_trigger);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaAnytimeSkill, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 3, 1, 0) != 0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

// ----------------------

bool LuaViewAsSkill::viewFilter(const QList<const Card *> &selected, const Card *to_select) const
{
	if (view_filter == 0)
		return false;

	// 【核心修復：Try-Lock 避讓機制】
	bool locked = Sanguosha->getLuaMutex().tryLock();
	if (!locked) {
		// 拿不到鎖 (代表 Server 正在算 AI)，UI 絕對不能死等！
		// 直接回傳 false (牌在畫面上暫時反灰 1 毫秒)，避開死鎖與崩潰。
		return false;
	}

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, view_filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsSkill, 0);
	lua_createtable(L, selected.length(), 0);
	for (int i = 0; i < selected.length(); i++) {
		SWIG_NewPointerObj(L, selected[i], SWIGTYPE_p_Card, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Card, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
	return result;
}

const Card *LuaViewAsSkill::viewAs(const QList<const Card *> &cards) const
{
	if (view_as == 0)
		return nullptr;

	// 【核心修復：Try-Lock 避讓機制】
	bool locked = Sanguosha->getLuaMutex().tryLock();
	if (!locked) {
		// 拿不到鎖，當作沒有這張牌
		return nullptr;
	}

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, view_as);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsSkill, 0);
	lua_createtable(L, cards.length(), 0);
	for (int i = 0; i < cards.length(); i++) {
		SWIG_NewPointerObj(L, cards[i], SWIGTYPE_p_Card, 0);
		lua_rawseti(L, -2, i + 1);
	}

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
		return nullptr;
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return nullptr;
}

bool LuaViewAsSkill::shouldBeVisible(const Player *player) const
{
	if (should_be_visible == 0)
		return ViewAsSkill::shouldBeVisible(player);

	// 【核心修復：Try-Lock 避讓機制】
	bool locked = Sanguosha->getLuaMutex().tryLock();
	if (!locked) {
		// 拿不到鎖，返回 false (暫時不顯示)
		return false;
	}

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, should_be_visible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsSkill, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
	return result;
}

bool LuaViewAsSkill::isEnabledAtPlay(const Player *player) const
{
	if (enabled_at_play == 0)
		return ViewAsSkill::isEnabledAtPlay(player);

	// 【核心修復：Try-Lock 避讓機制】
	bool locked = Sanguosha->getLuaMutex().tryLock();
	if (!locked) {
		// 拿不到鎖，返回 false (暫時禁用)
		return false;
	}

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, enabled_at_play);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsSkill, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
	return result;
}

bool LuaViewAsSkill::isEnabledAtResponse(const Player *player, const QString &pattern) const
{
	if (enabled_at_response == 0)
		return ViewAsSkill::isEnabledAtResponse(player, pattern);

	// 【核心修復：Try-Lock 避讓機制】
	bool locked = Sanguosha->getLuaMutex().tryLock();
	if (!locked) {
		// 拿不到鎖，返回 false (暫時禁用)
		return false;
	}

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, enabled_at_response);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsSkill, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	lua_pushstring(L, pattern.toLatin1());

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
	return result;
}

bool LuaViewAsSkill::isEnabledAtNullification(const ServerPlayer *player) const
{
	if (enabled_at_nullification == 0)
		return ViewAsSkill::isEnabledAtNullification(player);

	// 【核心修復：Try-Lock 避讓機制】
	bool locked = Sanguosha->getLuaMutex().tryLock();
	if (!locked) {
		// 拿不到鎖，返回 false (暫時禁用)
		return false;
	}

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, enabled_at_nullification);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsSkill, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	Sanguosha->getLuaMutex().unlock(); // 執行完務必手動解鎖
	return result;
}

// ---------------------

bool LuaSkillCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self,
	int &maxVotes) const
{
	if (filter == 0)
		return SkillCard::targetFilter(targets, to_select, self, maxVotes);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaSkillCard, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); ++i) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 2, 0)!=0) {
		Error(L);
		return false;
	}

	if (lua_isnumber(L, -1) && lua_isboolean(L, -2)) {
		maxVotes = lua_tointeger(L, -1);
		bool result = lua_toboolean(L, -2);
		lua_pop(L, 2);
		return result;
	}
	return false;
}

bool LuaSkillCard::targetsFeasible(const QList<const Player *> &targets, const Player *self) const
{
	if (feasible == 0)
		return SkillCard::targetsFeasible(targets, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, feasible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaSkillCard, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

void LuaSkillCard::onUse(Room *room, CardUseStruct &card_use) const
{
	if (about_to_use == 0)
		return SkillCard::onUse(room, card_use);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, about_to_use);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaSkillCard, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
	SWIG_NewPointerObj(L, &card_use, SWIGTYPE_p_CardUseStruct, 0);

	if (lua_pcall(L, 3, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
	}
}

void LuaSkillCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if (on_use == 0)
		return SkillCard::use(room, source, targets);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_use);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaSkillCard, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
	SWIG_NewPointerObj(L, source, SWIGTYPE_p_ServerPlayer, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_ServerPlayer, 0);
		lua_rawseti(L, -2, i + 1);
	}

	if (lua_pcall(L, 4, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
	}
}

void LuaSkillCard::onEffect(CardEffectStruct &effect) const
{
	if (on_effect == 0)
		return SkillCard::onEffect(effect);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_effect);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaSkillCard, 0);
	SWIG_NewPointerObj(L, &effect, SWIGTYPE_p_CardEffectStruct, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		effect.to->getRoom()->output(error_msg);
	}
}

const Card *LuaSkillCard::validate(CardUseStruct &cardUse) const
{
	if (on_validate == 0)
		return SkillCard::validate(cardUse);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_validate);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaSkillCard, 0);
	SWIG_NewPointerObj(L, &cardUse, SWIGTYPE_p_CardUseStruct, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return SkillCard::validate(cardUse);
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return SkillCard::validate(cardUse);
}

const Card *LuaSkillCard::validateInResponse(ServerPlayer *user) const
{
	if (on_validate_in_response == 0)
		return SkillCard::validateInResponse(user);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_validate_in_response);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaSkillCard, 0);
	SWIG_NewPointerObj(L, user, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return SkillCard::validateInResponse(user);
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return SkillCard::validateInResponse(user);
}

// ---------------------

bool LuaBasicCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self) const
{
	if (filter == 0)
		return BasicCard::targetFilter(targets, to_select, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaBasicCard, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaBasicCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self,
	int &maxVotes) const
{
	if (filter == 0)
		return BasicCard::targetFilter(targets, to_select, self, maxVotes);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaBasicCard, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); ++i) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 2, 0)!=0) {
		Error(L);
		return false;
	}

	if (lua_isnumber(L, -1) && lua_isboolean(L, -2)) {
		maxVotes = lua_tointeger(L, -1);
		bool result = lua_toboolean(L, -2);
		lua_pop(L, 2);
		return result;
	}
	return false;
}

bool LuaBasicCard::targetsFeasible(const QList<const Player *> &targets, const Player *self) const
{
	if (feasible == 0)
		return BasicCard::targetsFeasible(targets, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, feasible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaBasicCard, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

void LuaBasicCard::onUse(Room *room, CardUseStruct &card_use) const
{
	if (about_to_use == 0)
		return BasicCard::onUse(room, card_use);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, about_to_use);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaBasicCard, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
	SWIG_NewPointerObj(L, &card_use, SWIGTYPE_p_CardUseStruct, 0);

	if (lua_pcall(L, 3, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
	}
}

void LuaBasicCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if (on_use == 0)
		return BasicCard::use(room, source, targets);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_use);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaBasicCard, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
	SWIG_NewPointerObj(L, source, SWIGTYPE_p_ServerPlayer, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_ServerPlayer, 0);
		lua_rawseti(L, -2, i + 1);
	}

	if (lua_pcall(L, 4, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
	}
}

void LuaBasicCard::onEffect(CardEffectStruct &effect) const
{
	if (on_effect == 0)
		return BasicCard::onEffect(effect);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_effect);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaBasicCard, 0);
	SWIG_NewPointerObj(L, &effect, SWIGTYPE_p_CardEffectStruct, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		effect.to->getRoom()->output(error_msg);
	}
}

bool LuaBasicCard::isAvailable(const Player *player) const
{
	if (available == 0)
		return BasicCard::isAvailable(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, available);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaBasicCard, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

const Card *LuaBasicCard::validate(CardUseStruct &cardUse) const
{
	if (on_validate == 0)
		return BasicCard::validate(cardUse);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_validate);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaBasicCard, 0);
	SWIG_NewPointerObj(L, &cardUse, SWIGTYPE_p_CardUseStruct, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return BasicCard::validate(cardUse);
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return BasicCard::validate(cardUse);
}

const Card *LuaBasicCard::validateInResponse(ServerPlayer *user) const
{
	if (on_validate_in_response == 0)
		return BasicCard::validateInResponse(user);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_validate_in_response);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaBasicCard, 0);
	SWIG_NewPointerObj(L, user, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return BasicCard::validateInResponse(user);
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return BasicCard::validateInResponse(user);
}

// ---------------------

bool LuaTrickCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self) const
{
	if (filter == 0)
		return TrickCard::targetFilter(targets, to_select, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaTrickCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self,
	int &maxVotes) const
{
	if (filter == 0)
		return TrickCard::targetFilter(targets, to_select, self, maxVotes);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); ++i) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 2, 0)!=0) {
		Error(L);
		return false;
	}

	if (lua_isnumber(L, -1) && lua_isboolean(L, -2)) {
		maxVotes = lua_tointeger(L, -1);
		bool result = lua_toboolean(L, -2);
		lua_pop(L, 2);
		return result;
	}
	return false;
}

bool LuaTrickCard::targetsFeasible(const QList<const Player *> &targets, const Player *self) const
{
	if (feasible == 0)
		return TrickCard::targetsFeasible(targets, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, feasible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

void LuaTrickCard::onNullified(ServerPlayer *target) const
{
	if (on_nullified == 0)
		return TrickCard::onNullified(target);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_nullified);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		target->getRoom()->output(error_msg);
	}
}

bool LuaTrickCard::isCancelable(const CardEffectStruct &effect) const
{
	if (is_cancelable == 0)
		return TrickCard::isCancelable(effect);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, is_cancelable);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	SWIG_NewPointerObj(L, &effect, SWIGTYPE_p_CardEffectStruct, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}
	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

void LuaTrickCard::onUse(Room *room, CardUseStruct &card_use) const
{
	if (about_to_use == 0)
		return TrickCard::onUse(room, card_use);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, about_to_use);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
	SWIG_NewPointerObj(L, &card_use, SWIGTYPE_p_CardUseStruct, 0);

	if (lua_pcall(L, 3, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
	}
}

void LuaTrickCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if (on_use == 0)
		return TrickCard::use(room, source, targets);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_use);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
	SWIG_NewPointerObj(L, source, SWIGTYPE_p_ServerPlayer, 0);

	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_ServerPlayer, 0);
		lua_rawseti(L, -2, i + 1);
	}

	if (lua_pcall(L, 4, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
	}
}

void LuaTrickCard::onEffect(CardEffectStruct &effect) const
{
	if (on_effect == 0)
		return TrickCard::onEffect(effect);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_effect);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	SWIG_NewPointerObj(L, &effect, SWIGTYPE_p_CardEffectStruct, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		effect.to->getRoom()->output(error_msg);
	}
}

bool LuaTrickCard::isAvailable(const Player *player) const
{
	if (available == 0)
		return TrickCard::isAvailable(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, available);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

const Card *LuaTrickCard::validate(CardUseStruct &cardUse) const
{
	if (on_validate == 0)
		return TrickCard::validate(cardUse);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_validate);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	SWIG_NewPointerObj(L, &cardUse, SWIGTYPE_p_CardUseStruct, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return TrickCard::validate(cardUse);
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return TrickCard::validate(cardUse);
}

const Card *LuaTrickCard::validateInResponse(ServerPlayer *user) const
{
	if (on_validate_in_response == 0)
		return TrickCard::validateInResponse(user);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_validate_in_response);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTrickCard, 0);
	SWIG_NewPointerObj(L, user, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return TrickCard::validateInResponse(user);
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return TrickCard::validateInResponse(user);
}

bool LuaWeapon::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self) const
{
	if (filter == 0)
		return Weapon::targetFilter(targets, to_select, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaWeapon, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaWeapon::targetsFeasible(const QList<const Player *> &targets, const Player *self) const
{
	if (feasible == 0)
		return Weapon::targetsFeasible(targets, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, feasible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaWeapon, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaWeapon::isAvailable(const Player *player) const
{
	if (available == 0)
		return Weapon::isAvailable(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, available);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaWeapon, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

void LuaWeapon::onInstall(ServerPlayer *player) const
{
	if (on_install == 0)
		return Weapon::onInstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_install);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaWeapon, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

void LuaWeapon::onUninstall(ServerPlayer *player) const
{
	if (on_uninstall == 0)
		return Weapon::onUninstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_uninstall);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaWeapon, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

bool LuaArmor::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self) const
{
	if (filter == 0)
		return Armor::targetFilter(targets, to_select, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaArmor, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaArmor::targetsFeasible(const QList<const Player *> &targets, const Player *self) const
{
	if (feasible == 0)
		return Armor::targetsFeasible(targets, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, feasible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaArmor, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaArmor::isAvailable(const Player *player) const
{
	if (available == 0)
		return Armor::isAvailable(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, available);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaArmor, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

void LuaArmor::onInstall(ServerPlayer *player) const
{
	if (on_install == 0)
		return Armor::onInstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_install);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaArmor, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

void LuaArmor::onUninstall(ServerPlayer *player) const
{
	if (on_uninstall == 0)
		return Armor::onUninstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_uninstall);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaArmor, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

bool LuaHorse::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self) const
{
	if (filter == 0)
		return Horse::targetFilter(targets, to_select, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaHorse, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaHorse::targetsFeasible(const QList<const Player *> &targets, const Player *self) const
{
	if (feasible == 0)
		return Horse::targetsFeasible(targets, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, feasible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaHorse, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaHorse::isAvailable(const Player *player) const
{
	if (available == 0)
		return Horse::isAvailable(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, available);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

void LuaHorse::onInstall(ServerPlayer *player) const
{
	if (on_install == 0)
		return Horse::onInstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_install);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

void LuaHorse::onUninstall(ServerPlayer *player) const
{
	if (on_uninstall == 0)
		return Horse::onUninstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_uninstall);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

int LuaHorse::getCorrect(const Player *player) const
{
	if (correct_func == 0)
		return Horse::getCorrect(player);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, correct_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return Horse::getCorrect(player);
	}

	int correct = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return correct;
}

bool LuaOffensiveHorse::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self) const
{
	if (filter == 0)
		return OffensiveHorse::targetFilter(targets, to_select, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaOffensiveHorse, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaOffensiveHorse::isAvailable(const Player *player) const
{
	if (available == 0)
		return OffensiveHorse::isAvailable(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, available);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaOffensiveHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaOffensiveHorse::targetsFeasible(const QList<const Player *> &targets, const Player *self) const
{
	if (feasible == 0)
		return OffensiveHorse::targetsFeasible(targets, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, feasible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaOffensiveHorse, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

void LuaOffensiveHorse::onInstall(ServerPlayer *player) const
{
	if (on_install == 0)
		return OffensiveHorse::onInstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_install);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaOffensiveHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

void LuaOffensiveHorse::onUninstall(ServerPlayer *player) const
{
	if (on_uninstall == 0)
		return OffensiveHorse::onUninstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_uninstall);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaOffensiveHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

int LuaOffensiveHorse::getCorrect(const Player *player) const
{
	if (correct_func == 0)
		return OffensiveHorse::getCorrect(player);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, correct_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaOffensiveHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return OffensiveHorse::getCorrect(player);
	}

	int correct = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return correct;
}

bool LuaDefensiveHorse::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self) const
{
	if (filter == 0)
		return DefensiveHorse::targetFilter(targets, to_select, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);

	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaDefensiveHorse, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaDefensiveHorse::targetsFeasible(const QList<const Player *> &targets, const Player *self) const
{
	if (feasible == 0)
		return DefensiveHorse::targetsFeasible(targets, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, feasible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaDefensiveHorse, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaDefensiveHorse::isAvailable(const Player *player) const
{
	if (available == 0)
		return DefensiveHorse::isAvailable(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, available);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaDefensiveHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

void LuaDefensiveHorse::onInstall(ServerPlayer *player) const
{
	if (on_install == 0)
		return DefensiveHorse::onInstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_install);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaDefensiveHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

void LuaDefensiveHorse::onUninstall(ServerPlayer *player) const
{
	if (on_uninstall == 0)
		return DefensiveHorse::onUninstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_uninstall);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaDefensiveHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

int LuaDefensiveHorse::getCorrect(const Player *player) const
{
	if (correct_func == 0)
		return DefensiveHorse::getCorrect(player);

	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, correct_func);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaDefensiveHorse, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return DefensiveHorse::getCorrect(player);
	}

	int correct = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return correct;
}

bool LuaTreasure::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self) const
{
	if (filter == 0)
		return Treasure::targetFilter(targets, to_select, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTreasure, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Player, 0);
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 4, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaTreasure::targetsFeasible(const QList<const Player *> &targets, const Player *self) const
{
	if (feasible == 0)
		return Treasure::targetsFeasible(targets, self);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, feasible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTreasure, 0);
	lua_createtable(L, targets.length(), 0);
	for (int i = 0; i < targets.length(); i++) {
		SWIG_NewPointerObj(L, targets[i], SWIGTYPE_p_Player, 0);
		lua_rawseti(L, -2, i + 1);
	}
	SWIG_NewPointerObj(L, self, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaTreasure::isAvailable(const Player *player) const
{
	if (available == 0)
		return Treasure::isAvailable(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, available);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTreasure, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

void LuaTreasure::onInstall(ServerPlayer *player) const
{
	if (on_install == 0)
		return Treasure::onInstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_install);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTreasure, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}

void LuaTreasure::onUninstall(ServerPlayer *player) const
{
	if (on_uninstall == 0)
		return Treasure::onUninstall(player);

	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, on_uninstall);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTreasure, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		player->getRoom()->output(error_msg);
	}
}
%}