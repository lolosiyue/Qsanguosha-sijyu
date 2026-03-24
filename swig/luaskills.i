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
};

class LuaCardLimitSkill : public CardLimitSkill {
public:
	LuaCardLimitSkill(const char *name, Frequency frequency);

	virtual QString limitList(const Player *target, const Card *card) const;
	virtual QString limitPattern(const Player *target, const Card *card) const;

	LuaFunction limit_list;
	LuaFunction limit_pattern;
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
#include <QMutexLocker>

// RAII tryLock wrapper for UI-thread Lua queries.
// Attempts to acquire the mutex with a timeout; if it fails,
// isLocked() returns false so the caller can return a safe default
// instead of blocking the UI thread indefinitely.
class LuaTryLocker {
	SafeLuaMutex *m_mutex;
	bool m_locked;
public:
	LuaTryLocker(SafeLuaMutex *mutex, int timeout = 50)
		: m_mutex(mutex), m_locked(mutex->tryLock(timeout)) {}
	~LuaTryLocker() { if (m_locked) m_mutex->unlock(); }
	bool isLocked() const { return m_locked; }
	LuaTryLocker(const LuaTryLocker &) = delete;
	LuaTryLocker &operator=(const LuaTryLocker &) = delete;
};

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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

bool LuaScenarioRule::triggerable(const ServerPlayer *target) const
{
	if (can_trigger == 0)
		return ScenarioRule::triggerable(target);

	LuaLocker locker;
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

	LuaLocker locker;
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

static void Error(lua_State *L)
{
	const QString &error_string = lua_tostring(L, -1);
	lua_pop(L, 1);
	QMessageBox::warning(nullptr, "Lua script error!", error_string);
}

Skill::Frequency LuaTriggerSkill::getFrequency(const Player *target) const
{
	if (dynamic_frequency == 0)
		return Skill::getFrequency(target);

	LuaLocker locker;
	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, dynamic_frequency);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaTriggerSkill, 0);
	SWIG_NewPointerObj(L, target, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return Skill::getFrequency(target);
	}

	int result = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return (Skill::Frequency)result;
}

bool LuaProhibitSkill::isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &others) const
{
	if (is_prohibited == 0)
		return false;

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return 0;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, view_filter);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaFilterSkill, 0);
	SWIG_NewPointerObj(L, to_select, SWIGTYPE_p_Card, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

const Card *LuaFilterSkill::viewAs(const Card *originalCard) const
{
	if (view_as == 0)
		return nullptr;

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return nullptr;
	lua_State*L = Sanguosha->getLuaState();

	lua_rawgeti(L, LUA_REGISTRYINDEX, view_as);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaFilterSkill, 0);
	SWIG_NewPointerObj(L, originalCard, SWIGTYPE_p_Card, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return nullptr;
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return nullptr;
}

QString LuaViewAsEquipSkill::viewAsEquip(const Player *target) const
{
	if (view_as_equip == 0)
		return QString();

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return QString();
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return QString();
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

// ----------------------

bool LuaViewAsSkill::viewFilter(const QList<const Card *> &selected, const Card *to_select) const
{
	if (view_filter == 0)
		return false;

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

const Card *LuaViewAsSkill::viewAs(const QList<const Card *> &cards) const
{
	if (view_as == 0)
		return nullptr;

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return nullptr;
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
		return nullptr;
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return nullptr;
}

bool LuaViewAsSkill::shouldBeVisible(const Player *player) const
{
	if (should_be_visible == 0)
		return ViewAsSkill::shouldBeVisible(player);

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, should_be_visible);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsSkill, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaViewAsSkill::isEnabledAtPlay(const Player *player) const
{
	if (enabled_at_play == 0)
		return ViewAsSkill::isEnabledAtPlay(player);

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, enabled_at_play);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsSkill, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaViewAsSkill::isEnabledAtResponse(const Player *player, const QString &pattern) const
{
	if (enabled_at_response == 0)
		return ViewAsSkill::isEnabledAtResponse(player, pattern);

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, enabled_at_response);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsSkill, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_Player, 0);

	lua_pushstring(L, pattern.toLatin1());

	if (lua_pcall(L, 3, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

bool LuaViewAsSkill::isEnabledAtNullification(const ServerPlayer *player) const
{
	if (enabled_at_nullification == 0)
		return ViewAsSkill::isEnabledAtNullification(player);

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
	lua_State*L = Sanguosha->getLuaState();

	// the callback
	lua_rawgeti(L, LUA_REGISTRYINDEX, enabled_at_nullification);
	SWIG_NewPointerObj(L, this, SWIGTYPE_p_LuaViewAsSkill, 0);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 2, 1, 0)!=0) {
		Error(L);
		return false;
	}

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return result;
}

// ---------------------

bool LuaSkillCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *self,
	int &maxVotes) const
{
	if (filter == 0)
		return SkillCard::targetFilter(targets, to_select, self, maxVotes);

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return 0;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return 0;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaLocker locker;
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

	LuaLocker locker;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return 0;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaTryLocker locker(&Sanguosha->getLuaMutex());
	if (!locker.isLocked()) return false;
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

	LuaLocker locker;
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

	LuaLocker locker;
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