%{

#include "ai.h"

#include <cmath>
#include <limits>

static bool readAiResultInteger(lua_Number value, int &result)
{
	if (!std::isfinite(double(value))
		|| value < lua_Number(std::numeric_limits<int>::min())
		|| value > lua_Number(std::numeric_limits<int>::max()))
		return false;
	const int converted = static_cast<int>(value);
	if (lua_Number(converted) != value)
		return false;
	result = converted;
	return true;
}

static bool readAiResultString(lua_State *state, int index, QString &result)
{
	size_t size = 0;
	const char *data = lua_tolstring(state, index, &size);
	if (!data || size > 64u * 1024u)
		return false;
	result = QString::fromUtf8(data, qsizetype(size));
	return true;
}

AIResult LuaAI::decide(const AIRequest &request)
{
	if (request.kind == AIRequest::Activate || !request.hasSkillActionContext || callback == 0)
		return AI::decide(request);

	AIResult result;
	result.decisionId = request.decisionId;
	result.stateRevision = request.stateRevision;
	lua_State *L = room->getLuaState();
	pushCallback(L, "askForUseCard");
	lua_pushstring(L, request.pattern.toLatin1());
	lua_pushstring(L, request.prompt.toLatin1());
	lua_pushinteger(L, request.handlingMethod);
	AiLegacyRequestView *legacyRequest = new AiLegacyRequestView(request, self);
	SWIG_NewPointerObj(L, legacyRequest, SWIGTYPE_p_AiLegacyRequestView, SWIG_POINTER_OWN);
	if (lua_pcall(L, 5, 1, 0) != 0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
		return result;
	}
	if (lua_isnil(L, -1) || (lua_isboolean(L, -1) && !lua_toboolean(L, -1))) {
		lua_pop(L, 1);
		return result;
	}
	if (lua_type(L, -1) == LUA_TSTRING) {
		result.handled = true;
		if (!readAiResultString(L, -1, result.action.legacyCardString)) {
			lua_pop(L, 1);
			result.errorCode = "invalid_legacy_ai_result";
			return result;
		}
		lua_pop(L, 1);
		if (result.action.legacyCardString.isEmpty() || result.action.legacyCardString == ".")
			return result;
		result.kind = AIResult::UseCard;
		result.action.hasSkillActionContext = true;
		result.action.skillActionContext = request.skillActionContext;
		return result;
	}
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		result.errorCode = "invalid_legacy_ai_result";
		return result;
	}

	lua_getfield(L, -1, "accepted");
	if (lua_type(L, -1) != LUA_TBOOLEAN) { lua_pop(L, 2); result.errorCode = "invalid_legacy_ai_result"; return result; }
	const bool accepted = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (!accepted) {
		lua_pop(L, 1);
		result.handled = true;
		return result;
	}

	lua_getfield(L, -1, "cards");
	if (!lua_isnil(L, -1)) {
		if (!lua_istable(L, -1)) { lua_pop(L, 2); result.errorCode = "invalid_legacy_ai_result"; return result; }
		const size_t count = lua_rawlen(L, -1);
		if (count > 2048u) { lua_pop(L, 2); result.errorCode = "invalid_legacy_ai_result"; return result; }
		for (size_t i = 1; i <= count; ++i) {
			lua_rawgeti(L, -1, i);
			if (lua_type(L, -1) != LUA_TNUMBER) { lua_pop(L, 3); result.errorCode = "invalid_legacy_ai_result"; return result; }
			const lua_Number value = lua_tonumber(L, -1);
			int id = 0;
			const bool validId = readAiResultInteger(value, id);
			lua_pop(L, 1);
			if (!validId || result.action.selectedCardIds.contains(id)) {
				lua_pop(L, 2);
				result.errorCode = "invalid_legacy_ai_result";
				return result;
			}
			result.action.selectedCardIds << id;
		}
	}
	lua_pop(L, 1);

	lua_getfield(L, -1, "targets");
	if (!lua_isnil(L, -1)) {
		if (!lua_istable(L, -1)) { lua_pop(L, 2); result.errorCode = "invalid_legacy_ai_result"; return result; }
		const size_t count = lua_rawlen(L, -1);
		if (count > 64u) { lua_pop(L, 2); result.errorCode = "invalid_legacy_ai_result"; return result; }
		for (size_t i = 1; i <= count; ++i) {
			lua_rawgeti(L, -1, i);
			if (lua_type(L, -1) != LUA_TSTRING) { lua_pop(L, 3); result.errorCode = "invalid_legacy_ai_result"; return result; }
			QString targetName;
			if (!readAiResultString(L, -1, targetName)) { lua_pop(L, 3); result.errorCode = "invalid_legacy_ai_result"; return result; }
			result.action.selectedTargetNames << targetName;
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	lua_getfield(L, -1, "user_string");
	if (!lua_isnil(L, -1)) {
		if (lua_type(L, -1) != LUA_TSTRING) { lua_pop(L, 2); result.errorCode = "invalid_legacy_ai_result"; return result; }
		if (!readAiResultString(L, -1, result.action.userString)) { lua_pop(L, 2); result.errorCode = "invalid_legacy_ai_result"; return result; }
	}
	lua_pop(L, 1);
	lua_pop(L, 1);
	result.handled = true;
	result.kind = AIResult::UseCard;
	result.action.hasSkillActionContext = true;
	result.action.skillActionContext = request.skillActionContext;
	return result;
}

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
	virtual AIResult decide(const AIRequest &request);

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
	virtual AIResult decide(const AIRequest &request);

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
	lua_State *L = getLuaState();
	if (L == nullptr || !Config.EnableAI)
		return new TrustAI(player);

	lua_getglobal(L, "CloneAI");

	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (lua_pcall(L, 1, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		output(error_msg);
	} else {
		void *ai_ptr;
		int result = SWIG_ConvertPtr(L, -1, &ai_ptr, SWIGTYPE_p_AI, 0);
		lua_pop(L, 1);
		if (SWIG_IsOK(result))
			return static_cast<AI *>(ai_ptr);
	}
	return new TrustAI(player);
}

ServerPlayer *LuaAI::askForYiji(const QList<int> &cards, const QString &reason, int &card_id)
{
	if (callback == 0)
		return TrustAI::askForYiji(cards, reason, card_id);
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
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	lua_pushinteger(L, event);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);
	// data 以 heap 拷貝 + OWN 傳給 Lua：Lua 側（smart-ai filterEvent）會把
	// data 存入全域 sgs.filterData，若傳棧上引用則 filterEvent 返回後懸垂
	// （後續 sgs.filterData[event]:toCardEffect() 等讀取 → 0xC0000005）
	QVariant *dataCopy = new QVariant(data);
	SWIG_NewPointerObj(L, dataCopy, SWIGTYPE_p_QVariant, SWIG_POINTER_OWN);

	if (lua_pcall(L, 4, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);
	}
}

const Card *LuaAI::askForCard(const QString &pattern, const QString &prompt, const QVariant &data, const Card::HandlingMethod method)
{
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	lua_pushstring(L, pattern.toLatin1());
	lua_pushstring(L, prompt.toLatin1());
	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);
	lua_pushinteger(L, (int)method);

	int error = lua_pcall(L, 5, 2, 0);
	if (error != 0) {
		const QString result = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(result);
		return TrustAI::askForCard(pattern, prompt, data, method);
	}
	const QString result = lua_tostring(L, -1);
	lua_pop(L, 1);
	if (result.isEmpty())
		return TrustAI::askForCard(pattern, prompt, data, method);

	room->setTag("AiResult",result);
	const Card *card = Card::Parse(result);
	if (!card || (method != Card::MethodUse && method != Card::MethodResponse))
		return card;

	const CardUseStruct::CardUseReason reason = method == Card::MethodResponse
		? CardUseStruct::CARD_USE_REASON_RESPONSE
		: CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
	AiLegacyRequestView request = room->getAiSkillActionContext(self, card->getActivationSkillName(),
			reason, pattern, prompt, method);
	if (request.isValid()) {
		Card *mutableCard = const_cast<Card *>(card);
		mutableCard->setActivationSkill(request.getActivationSkillName(),
			request.getActivationInstanceId());
		mutableCard->setSourceSkill(request.getSourceSkillName(),
			request.getSourceInstanceID());
	}
	return card;
}

int LuaAI::askForCardChosen(ServerPlayer *who, const QString &flags, const QString &reason, Card::HandlingMethod method)
{
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
	const Card *card = Card::Parse(result);
	if (!card) return nullptr;
	// 與 askForCard 一致：字串可能只有 skillName/#id，補齊 activation/source
	// 供後續 useCard → resolveCardSkillInstance 走 V2 cost/pay。
	const QString pattern = (self == dying) ? QStringLiteral("peach+analeptic")
						   : QStringLiteral("peach");
	AiLegacyRequestView request = room->getAiSkillActionContext(
		self, card->getActivationSkillName(),
		CardUseStruct::CARD_USE_REASON_RESPONSE_USE, pattern, QString(),
		Card::MethodUse);
	if (request.isValid()) {
		Card *mutableCard = const_cast<Card *>(card);
		mutableCard->setActivationSkill(request.getActivationSkillName(),
			request.getActivationInstanceId());
		mutableCard->setSourceSkill(request.getSourceSkillName(),
			request.getSourceInstanceID());
	}
	return card;
}

const Card *LuaAI::askForPindian(ServerPlayer *requestor, const QString &reason)
{
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
