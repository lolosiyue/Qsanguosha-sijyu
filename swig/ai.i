%{

#include "ai.h"

%}

class AI: public QObject {
public:
	AI(ServerPlayer *player);

	enum Relation { Friend, Enemy, Neutrality };
	static Relation GetRelation3v3(const ServerPlayer *a, const ServerPlayer *b);
	static Relation GetRelationHegemony(const ServerPlayer *a, const ServerPlayer *b);
	static Relation GetRelation(const ServerPlayer *a, const ServerPlayer *b);
	Relation relationTo(const ServerPlayer *other) const;
	bool isFriend(const ServerPlayer *other) const;
	bool isEnemy(const ServerPlayer *other) const;

	QList<ServerPlayer *> getEnemies() const;
	QList<ServerPlayer *> getFriends() const;

	virtual void activate(CardUseStruct &card_use) = 0;
	virtual Card::Suit askForSuit(const QString&) = 0;
	virtual QString askForKingdom(QStringList kingdoms) = 0;
	virtual bool askForSkillInvoke(const char *skill_name, const QVariant &data) = 0;
	virtual QString askForChoice(const char *skill_name, const char *choices, const QVariant &data) = 0;
	virtual QList<int> askForDiscard(const char *reason, int discard_num, int min_num, bool optional, bool include_equip, const char *pattern = ".") = 0;
	virtual const Card *askForNullification(const Card *trick, ServerPlayer *from, ServerPlayer *to, bool positive) = 0;
	virtual int askForCardChosen(ServerPlayer *who, const char *flags, const char *reason, Card::HandlingMethod method) = 0;
	virtual const Card *askForCard(const char *pattern, const char *prompt, const QVariant &data, const Card::HandlingMethod method) = 0;
	virtual QString askForUseCard(const char *pattern, const char *prompt, const Card::HandlingMethod method) = 0;
	virtual int askForAG(const QList<int> &card_ids, bool refusable, const char *reason) = 0;
	virtual const Card *askForCardShow(ServerPlayer *requestor, const char *reason) = 0;
	virtual const Card *askForPindian(ServerPlayer *requestor, const char *reason) = 0;
	virtual ServerPlayer *askForPlayerChosen(const QList<ServerPlayer *> &targets, const char *reason) = 0;
	virtual QList<ServerPlayer *> askForPlayersChosen(const QList<ServerPlayer *> &targets, const char *reason, int max_num, int min_num) = 0;
	virtual const Card *askForSinglePeach(ServerPlayer *dying) = 0;
	virtual ServerPlayer *askForYiji(const QList<int> &cards, const char *reason, int &card_id) = 0;
	virtual void askForGuanxing(const QList<int> &cards, QList<int> &up, QList<int> &bottom, int guanxing_type) = 0;
	virtual QString askForGeneral(const QStringList &generals, const char *default_choice = "", const char *reason = "") = 0;
	virtual void filterEvent(TriggerEvent triggerEvent, ServerPlayer *player, const QVariant &data);
};

class TrustAI: public AI {
public:
	TrustAI(ServerPlayer *player);

	virtual void activate(CardUseStruct &card_use);
	virtual Card::Suit askForSuit(const QString&);
	virtual QString askForKingdom(QStringList kingdoms);
	virtual bool askForSkillInvoke(const char *skill_name, const QVariant &data);
	virtual QString askForChoice(const char *skill_name, const char *choices, const QVariant &data);
	virtual QList<int> askForDiscard(const char *reason, int discard_num, int min_num, bool optional, bool include_equip, const char *pattern = ".");
	virtual const Card *askForNullification(const Card *trick, ServerPlayer *from, ServerPlayer *to, bool positive);
	virtual int askForCardChosen(ServerPlayer *who, const char *flags, const char *reason, Card::HandlingMethod method);
	virtual const Card *askForCard(const char *pattern, const char *prompt, const QVariant &data, const Card::HandlingMethod method);
	virtual QString askForUseCard(const char *pattern, const char *prompt, const Card::HandlingMethod method);
	virtual int askForAG(const QList<int> &card_ids, bool refusable, const char *reason);
	virtual const Card *askForCardShow(ServerPlayer *requestor, const char *reason);
	virtual const Card *askForPindian(ServerPlayer *requestor, const char *reason);
	virtual ServerPlayer *askForPlayerChosen(const QList<ServerPlayer *> &targets, const char *reason);
	virtual QList<ServerPlayer *> askForPlayersChosen(const QList<ServerPlayer *> &targets, const char *reason, int max_num, int min_num);
	virtual const Card *askForSinglePeach(ServerPlayer *dying);
	virtual ServerPlayer *askForYiji(const QList<int> &cards, const char *reason, int &card_id);
	virtual void askForGuanxing(const QList<int> &cards, QList<int> &up, QList<int> &bottom, int guanxing_type);
	virtual QString askForGeneral(const QStringList &generals, const char *default_choice = "", const char *reason = "");

	virtual bool useCard(const Card *card);
};

class LuaAI: public TrustAI {
public:
	LuaAI(ServerPlayer *player);

	virtual const Card *askForCardShow(ServerPlayer *requestor, const char *reason);
	virtual bool askForSkillInvoke(const char *skill_name, const QVariant &data);
	virtual void activate(CardUseStruct &card_use);
	virtual QList<int> askForDiscard(const char *reason, int discard_num, int min_num, bool optional, bool include_equip, const char *pattern = ".");
	virtual QString askForChoice(const char *skill_name, const char *choices, const QVariant &data);
	virtual int askForCardChosen(ServerPlayer *who, const char *flags, const char *reason, Card::HandlingMethod method);
	virtual ServerPlayer *askForPlayerChosen(const QList<ServerPlayer *> &targets, const char *reason);
	virtual QList<ServerPlayer *> askForPlayersChosen(const QList<ServerPlayer *> &targets, const char *reason, int max_num, int min_num);
	virtual const Card *askForCard(const char *pattern, const char *prompt, const QVariant &data, const Card::HandlingMethod method);
	virtual int askForAG(const QList<int> &card_ids, bool refusable, const char *reason);
	virtual const Card *askForSinglePeach(ServerPlayer *dying);
	virtual const Card *askForPindian(ServerPlayer *requestor, const char *reanson);
	virtual Card::Suit askForSuit(const QString&);
	virtual ServerPlayer *askForYiji(const QList<int> &cards, const char *reason, int &card_id);
	virtual void askForGuanxing(const QList<int> &cards, QList<int> &up, QList<int> &bottom, int guanxing_type);
	virtual QString askForGeneral(const QStringList &generals, const char *default_choice = "", const char *reason = "");
	virtual void filterEvent(TriggerEvent triggerEvent, ServerPlayer *player, const QVariant &data);

	LuaFunction callback;
};

// for some AI use
/*class Shit:public BasicCard{
public:
	Shit(Card::Suit suit, int number);
	virtual QString getSubtype() const;
	virtual void onMove(const CardMoveStruct &move) const;

	static bool HasShit(const Card *card);
};*/

%{

bool LuaAI::askForSkillInvoke(const QString &skill_name, const QVariant &data)
{
	if (callback == 0)
		return TrustAI::askForSkillInvoke(skill_name, data);

	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	lua_pushstring(L, skill_name.toLatin1());
	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);

	if (lua_pcall(L, 3, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
		return TrustAI::askForSkillInvoke(skill_name, data);
	}
	bool invoke = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return invoke;
}

QString LuaAI::askForChoice(const QString &skill_name, const QString &choices, const QVariant &data)
{
	if (callback == 0)
		return TrustAI::askForChoice(skill_name, choices, data);

	LuaLocker locker;
	lua_State*L = room->getLuaState();
	pushCallback(L, __FUNCTION__);
	lua_pushstring(L, skill_name.toLatin1());
	lua_pushstring(L, choices.toLatin1());
	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);
	int error = lua_pcall(L, 4, 1, 0);
	const char *result = lua_tostring(L, -1);
	lua_pop(L, 1);
	if (error!=0) {
		room->output(result);
		return TrustAI::askForChoice(skill_name, choices, data);
	}
	return result;
}

void LuaAI::activate(CardUseStruct &card_use)
{
	Q_ASSERT(callback);

	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	SWIG_NewPointerObj(L, &card_use, SWIGTYPE_p_CardUseStruct, 0);

	if (lua_pcall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);

		TrustAI::activate(card_use);
	}
}

AI *Room::cloneAI(ServerPlayer *player)
{
	if (m_lua == nullptr || !Config.EnableAI)
		return new TrustAI(player);

	LuaLocker locker;
	lua_getglobal(m_lua, "CloneAI");

	SWIG_NewPointerObj(m_lua, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(m_lua, 1, 1, 0)!=0) {
		const char *error_msg = lua_tostring(m_lua, -1);
		lua_pop(m_lua, 1);
		output(error_msg);
	} else {
		void *ai_ptr;
		int result = SWIG_ConvertPtr(m_lua, -1, &ai_ptr, SWIGTYPE_p_AI, 0);
		lua_pop(m_lua, 1);
		if (SWIG_IsOK(result))
			return static_cast<AI *>(ai_ptr);
	}
	return new TrustAI(player);
}

ServerPlayer *LuaAI::askForYiji(const QList<int> &cards, const QString &reason, int &card_id)
{
	if (callback == 0)
		return TrustAI::askForYiji(cards, reason, card_id);

	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	lua_createtable(L, cards.length(), 0);
	lua_pushstring(L, reason.toLatin1());

	for (int i = 0; i < cards.length(); i++) {
		int elem = cards.at(i);
		lua_pushnumber(L, elem);
		lua_rawseti(L, -3, i + 1);
	}

	if (lua_pcall(L, 3, 2, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
		return nullptr;
	}

	void *player_ptr;
	int result = SWIG_ConvertPtr(L, -2, &player_ptr, SWIGTYPE_p_ServerPlayer, 0);
	int number = lua_tonumber(L, -1);
	lua_pop(L, 2);

	if (SWIG_IsOK(result)) {
		card_id = number;
		return static_cast<ServerPlayer *>(player_ptr);
	}

	return nullptr;
}

void LuaAI::filterEvent(TriggerEvent event, ServerPlayer *player, const QVariant &data)
{
	if (callback == 0)
		return;

	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	lua_pushinteger(L, event);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);
	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);

	if (lua_pcall(L, 4, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
	}
}

const Card *LuaAI::askForCard(const QString &pattern, const QString &prompt, const QVariant &data, const Card::HandlingMethod method)
{
	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	lua_pushstring(L, pattern.toLatin1());
	lua_pushstring(L, prompt.toLatin1());
	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);
	lua_pushinteger(L, (int)method);

	int error = lua_pcall(L, 5, 1, 0);
	const QString &result = lua_tostring(L, -1);
	lua_pop(L, 1);
	if (error!=0||result.isEmpty()) {
		room->output(result);
		return TrustAI::askForCard(pattern, prompt, data, method);
	}
	room->setTag("AiResult",result);
	return Card::Parse(result);
}

int LuaAI::askForCardChosen(ServerPlayer *who, const QString &flags, const QString &reason, Card::HandlingMethod method)
{
	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	SWIG_NewPointerObj(L, who, SWIGTYPE_p_ServerPlayer, 0);
	lua_pushstring(L, flags.toLatin1());
	lua_pushstring(L, reason.toLatin1());
	lua_pushinteger(L, (int)method);

	if (lua_pcall(L, 5, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);

		return TrustAI::askForCardChosen(who, flags, reason, method);
	}
	if (lua_isnumber(L, -1)) {
		int result = lua_tointeger(L, -1);
		lua_pop(L, 1);
		return result;
	}
	room->output(QString("The result of function %1 should be an integer!").arg(__FUNCTION__));
	return TrustAI::askForCardChosen(who, flags, reason, method);
}

ServerPlayer *LuaAI::askForPlayerChosen(const QList<ServerPlayer *> &targets, const QString &reason)
{
	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	SWIG_NewPointerObj(L, &targets, SWIGTYPE_p_QListT_ServerPlayer_p_t, 0);
	lua_pushstring(L, reason.toLatin1());

	if (lua_pcall(L, 3, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
		return TrustAI::askForPlayerChosen(targets, reason);
	}
	void *player_ptr;
	int result = SWIG_ConvertPtr(L, -1, &player_ptr, SWIGTYPE_p_ServerPlayer, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<ServerPlayer *>(player_ptr);
	return TrustAI::askForPlayerChosen(targets, reason);
}

QList<ServerPlayer *> LuaAI::askForPlayersChosen(const QList<ServerPlayer *> &targets, const QString &reason, int max_num, int min_num)
{
	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	SWIG_NewPointerObj(L, &targets, SWIGTYPE_p_QListT_ServerPlayer_p_t, 0);
	lua_pushstring(L, reason.toLatin1());
	lua_pushnumber(L, max_num);
	lua_pushnumber(L, min_num);

	if (lua_pcall(L, 5, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);

		return TrustAI::askForPlayersChosen(targets, reason, max_num, min_num);
	}

	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		//room->output(QString("The result of function %1 should all a table!").arg(__FUNCTION__));
		return TrustAI::askForPlayersChosen(targets, reason, max_num, min_num);
	}

	QList<ServerPlayer *> return_result;
	size_t len = lua_rawlen(L, -1);
	for (size_t i = 1; i <= len; i++) {
		lua_rawgeti(L, -1, i);
		void *player_ptr;
		int result = SWIG_ConvertPtr(L, -1, &player_ptr, SWIGTYPE_p_ServerPlayer, 0);
		lua_pop(L, 1);
		if (SWIG_IsOK(result))
			return_result << static_cast<ServerPlayer *>(player_ptr);
	}
	return return_result;
}

const Card *LuaAI::askForNullification(const Card *trick, ServerPlayer *from, ServerPlayer *to, bool positive)
{
	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	SWIG_NewPointerObj(L, trick, SWIGTYPE_p_Card, 0);
	SWIG_NewPointerObj(L, from, SWIGTYPE_p_ServerPlayer, 0);
	SWIG_NewPointerObj(L, to, SWIGTYPE_p_ServerPlayer, 0);
	lua_pushboolean(L, positive);

	if (lua_pcall(L, 5, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);

		return TrustAI::askForNullification(trick, from, to, positive);
	}

	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return TrustAI::askForNullification(trick, from, to, positive);
}

const Card *LuaAI::askForCardShow(ServerPlayer *requestor, const QString &reason)
{
	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	SWIG_NewPointerObj(L, requestor, SWIGTYPE_p_ServerPlayer, 0);
	lua_pushstring(L, reason.toLatin1());

	if (lua_pcall(L, 3, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);

		return TrustAI::askForCardShow(requestor, reason);
	}
	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return TrustAI::askForCardShow(requestor, reason);
}

const Card *LuaAI::askForSinglePeach(ServerPlayer *dying)
{
	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	SWIG_NewPointerObj(L, dying, SWIGTYPE_p_ServerPlayer, 0);

	int error = lua_pcall(L, 2, 1, 0);
	const QString &result = lua_tostring(L, -1);
	lua_pop(L, 1);
	if (error!=0||result.isEmpty()) {
		room->output(result);
		return TrustAI::askForSinglePeach(dying);
	}
	return Card::Parse(result);
}

const Card *LuaAI::askForPindian(ServerPlayer *requestor, const QString &reason)
{
	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	SWIG_NewPointerObj(L, requestor, SWIGTYPE_p_ServerPlayer, 0);
	lua_pushstring(L, reason.toLatin1());
	if (lua_pcall(L, 3, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
		return TrustAI::askForPindian(requestor, reason);
	}
	void *card_ptr;
	int result = SWIG_ConvertPtr(L, -1, &card_ptr, SWIGTYPE_p_Card, 0);
	lua_pop(L, 1);
	if (SWIG_IsOK(result))
		return static_cast<const Card *>(card_ptr);
	return TrustAI::askForPindian(requestor, reason);
}

Card::Suit LuaAI::askForSuit(const QString &reason)
{
	LuaLocker locker;
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	lua_pushstring(L, reason.toLatin1());
	if (lua_pcall(L, 2, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
		return TrustAI::askForSuit(reason);
	}

	if (lua_isnumber(L, -1)) {
		int result = lua_tointeger(L, -1);
		lua_pop(L, 1);
		return (Card::Suit)result;
	}
	return TrustAI::askForSuit(reason);
}

%}