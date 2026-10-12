%{
#include "ai-strategy/ol-ai.h"


#include "ai.h"
#include "ai-strategy/official.h"
#include "ai-strategy/lua-seal.h"
#include "ai-strategy/diy.h"
#include "ai-strategy/anime.h"
#include "ai-strategy/shadows.h"
#include "ai-strategy/runtime.h"
#include "ai-strategy/inovation-ai.h"
#include "ai-strategy/tenyear-ai.h"
#include <map>
#include <mutex>
#include <memory>
#include "ai-strategy/lua-raw-inspect.h"
#include "ai-strategy/source-slots.h"

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
	if (LuaRuntime::protectedCall(L, 5, 1, 0) != 0) {
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
    if(room)enterAIShebianInquiry(*room,self,"invoke",skill_name);
	if (callback == 0)
		return TrustAI::askForSkillInvoke(skill_name, data);
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	lua_pushstring(L, skill_name.toLatin1());
	SWIG_NewPointerObj(L, &data, SWIGTYPE_p_QVariant, 0);

	if (LuaRuntime::protectedCall(L, 3, 1, 0)!=0) {
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
	int error = LuaRuntime::protectedCall(L, 4, 1, 0);
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

	if (LuaRuntime::protectedCall(L, 2, 0, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(error_msg);

		TrustAI::activate(card_use);
	}
}

bool aiInovationRoomMatches(lua_State *L,int index,const Room *room)
{
 // Do not pass arbitrary full userdata to SWIG_ConvertPtr: it dereferences
 // usr->type. The native constructor emits this exact registered SWIG type.
 if(lua_type(L,index)!=LUA_TUSERDATA||lua_rawlen(L,index)!=sizeof(swig_lua_userdata))return false;
 const auto*usr=static_cast<const swig_lua_userdata*>(lua_touserdata(L,index));
 return usr&&usr->type==SWIGTYPE_p_Room&&usr->ptr==room;
}

bool aiInovationPlayerMatches(lua_State *L,int index,const ServerPlayer *player)
{
 if(lua_type(L,index)!=LUA_TUSERDATA||lua_rawlen(L,index)!=sizeof(swig_lua_userdata))return false;
 const auto*usr=static_cast<const swig_lua_userdata*>(lua_touserdata(L,index));
 return usr&&usr->type==SWIGTYPE_p_ServerPlayer&&usr->ptr==player;
}

namespace {
bool slotBindingGameVM(lua_State*L){
 auto*vm=LuaRuntime::fromState(L);
 return vm&&vm==LuaRuntime::current()&&vm->kind()==LuaRuntime::Game&&vm->state()==L&&!vm->isClosing();
}
// Validate the complete instance dispatch surface against generated immutable
// native class descriptors, not against a possibly altered current registry.
// .static is not consulted by SWIG_Lua_class_do_get/do_set for this base-less
// class; no source list method can reach it. sgs.list/qlist code is separately
// attested by the caller that translates its strategy read into this API.
bool swigPlainEmptyTable(lua_State*L,int index){
 if(!lua_istable(L,index))return false;
 if(lua_getmetatable(L,index)){lua_pop(L,1);return false;}
 index=lua_absindex(L,index);lua_pushnil(L);const bool empty=!AILuaInspect::next(L,index);
 if(!empty)lua_pop(L,2);return empty;
}
bool swigExactCFunction(lua_State*L,int index,lua_CFunction expected){
 if(!lua_iscfunction(L,index)||lua_tocfunction(L,index)!=expected)return false;
 if(lua_getupvalue(L,index,1)){lua_pop(L,1);return false;}return true;
}
bool slotTargetsShape(lua_State*L,int index,lua_CFunction gc){
 const int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 if(!lua_istable(L,index))return false;index=lua_absindex(L,index);
 if(lua_getmetatable(L,index))return false;
 const auto*clss=static_cast<const swig_lua_class*>(SWIGTYPE_p_QListT_ServerPlayer_p_t->clientdata);
 if(!clss||clss->bases[0]||clss->attributes[0].name||clss->metatable[0].name)return false;
 int count=0;lua_pushnil(L);
 while(AILuaInspect::next(L,index)){
  if(lua_type(L,-2)!=LUA_TSTRING)return false;const char*k=lua_tostring(L,-2);++count;
  if(!strcmp(k,"__index")){if(!swigExactCFunction(L,-1,SWIG_Lua_class_get))return false;}
  else if(!strcmp(k,"__newindex")){if(!swigExactCFunction(L,-1,SWIG_Lua_class_set))return false;}
  else if(!strcmp(k,"__gc")){if(!swigExactCFunction(L,-1,gc))return false;}
  else if(!strcmp(k,"__eq")){if(!swigExactCFunction(L,-1,SWIG_Lua_class_equal))return false;}
  else if(!strcmp(k,"__tostring")){if(!swigExactCFunction(L,-1,SWIG_Lua_class_tostring))return false;}
  else if(!strcmp(k,".type")){if(lua_type(L,-1)!=LUA_TSTRING||strcmp(lua_tostring(L,-1),clss->fqname))return false;}
  else if(!strcmp(k,".bases")||!strcmp(k,".get")||!strcmp(k,".set")){if(!swigPlainEmptyTable(L,-1))return false;}
  else if(!strcmp(k,".static")){if(!lua_istable(L,-1))return false;}
  else if(!strcmp(k,".fn")){
   if(!lua_istable(L,-1))return false;const int methods=lua_absindex(L,-1);if(lua_getmetatable(L,methods))return false;
   int seen=0,expected=1;for(int i=0;clss->methods[i].name;++i)++expected;
   lua_pushnil(L);while(AILuaInspect::next(L,methods)){
    if(lua_type(L,-2)!=LUA_TSTRING)return false;const char*name=lua_tostring(L,-2);lua_CFunction fn=nullptr;
    if(!strcmp(name,"__disown"))fn=SWIG_Lua_class_disown;
    else for(int i=0;clss->methods[i].name;++i)if(!strcmp(name,clss->methods[i].name)){fn=clss->methods[i].func;break;}
    if(!fn||!swigExactCFunction(L,-1,fn))return false;++seen;lua_pop(L,1);
   }if(seen!=expected)return false;
  }else return false; // includes injected __len/__getitem or other metamethods.
  lua_pop(L,1);
 }
 return count==11;
}
struct SlotOwnedTargets {
 LuaRuntime*runtime=nullptr;quint64 generation=0;const void*metatable=nullptr;
 std::unique_ptr<QList<ServerPlayer*>> values;
};
std::mutex slotTargetsMutex;
std::map<void*,SlotOwnedTargets> slotOwnedTargetLists;
int slotTargetsGc(lua_State*L){
 if(lua_type(L,1)!=LUA_TUSERDATA||lua_rawlen(L,1)!=sizeof(swig_lua_userdata))return 0;
 auto*u=static_cast<swig_lua_userdata*>(lua_touserdata(L,1));
 std::lock_guard<std::mutex>lock(slotTargetsMutex);auto it=slotOwnedTargetLists.find(u);
 if(it==slotOwnedTargetLists.end()||u->type!=SWIGTYPE_p_QListT_ServerPlayer_p_t||u->ptr!=it->second.values.get())return 0;
 // Generic SWIG __gc sees own=0, so even an explicitly invoked old class GC
 // cannot double-delete this native-owned list. Our exact GC is idempotent.
 u->ptr=nullptr;u->own=0;slotOwnedTargetLists.erase(it);return 0;
}
}
bool pushAISlotPlayer(lua_State*L,const ServerPlayer*p){
 if(!slotBindingGameVM(L)||!p)return false;
 SWIG_NewPointerObj(L,p,SWIGTYPE_p_ServerPlayer,0);return aiInovationPlayerMatches(L,-1,p);
}
bool matchAISlotPlayer(lua_State*L,int index,const ServerPlayer*p){
 return slotBindingGameVM(L)&&p&&aiInovationPlayerMatches(L,index,p);
}
bool pushAISlotCard(lua_State*L,const Card*c){
 if(!slotBindingGameVM(L)||!c)return false;
 // Existing qsgsCardNewPointerObj binds/pins the canonical lifetime token.
 SWIG_NewPointerObj(L,c,SWIGTYPE_p_Card,0);return matchAISlotCard(L,-1,c);
}
bool matchAISlotCard(lua_State*L,int index,const Card*c){
 if(!slotBindingGameVM(L)||!c||lua_type(L,index)!=LUA_TUSERDATA||lua_rawlen(L,index)!=sizeof(swig_lua_userdata))return false;
 const auto*u=static_cast<const swig_lua_userdata*>(lua_touserdata(L,index));
 if(!u||u->type!=SWIGTYPE_p_Card||u->ptr!=c)return false;
 const auto token=globalCardLifetimeManager().wrapperBinding(u);
 return token&&globalCardLifetimeManager().isLive(token);
}
bool matchAIRangyiTemplate(lua_State*L,int index,const Card*c){
 if(!slotBindingGameVM(L)||!c||lua_type(L,index)!=LUA_TUSERDATA||lua_rawlen(L,index)!=sizeof(swig_lua_userdata))return false;
 const auto*u=static_cast<const swig_lua_userdata*>(lua_touserdata(L,index));
 if(!u||u->type!=SWIGTYPE_p_LuaSkillCard||u->ptr!=c)return false;
 const auto token=globalCardLifetimeManager().wrapperBinding(u);
 return token&&globalCardLifetimeManager().isLive(token);
}
bool pushAISlotTargets(lua_State*L,const QList<ServerPlayer*>&targets){
 if(!slotBindingGameVM(L)||!lua_checkstack(L,128))return false;
 auto*vm=LuaRuntime::fromState(L);const int top=lua_gettop(L);
 // Allocate the userdata first with a null pointer. No native resource can be
 // orphaned by an allocation error while constructing its Lua metatable.
 auto*u=static_cast<swig_lua_userdata*>(lua_newuserdata(L,sizeof(swig_lua_userdata)));
 u->ptr=nullptr;u->type=SWIGTYPE_p_QListT_ServerPlayer_p_t;u->own=0;
 SWIG_Lua_AddMetatable(L,SWIGTYPE_p_QListT_ServerPlayer_p_t);const int object=lua_absindex(L,-1);
 if(!lua_getmetatable(L,object)){lua_settop(L,top);return false;}
 const int original=lua_absindex(L,-1);if(!slotTargetsShape(L,original,SWIG_Lua_class_destruct)){lua_settop(L,top);return false;}lua_newtable(L);const int copy=lua_absindex(L,-1);
 lua_pushnil(L);while(lua_next(L,original)!=0){lua_pushvalue(L,-2);lua_pushvalue(L,-2);lua_rawset(L,copy);lua_pop(L,1);}
 lua_pushcfunction(L,slotTargetsGc);lua_setfield(L,copy,"__gc");const void*mt=lua_topointer(L,copy);lua_pushvalue(L,copy);lua_setmetatable(L,object);lua_settop(L,object);
 SlotOwnedTargets record;record.runtime=vm;record.generation=vm->generation();record.metatable=mt;record.values=std::make_unique<QList<ServerPlayer*>>(targets);
 {
  std::lock_guard<std::mutex>lock(slotTargetsMutex);
  if(slotOwnedTargetLists.find(u)!=slotOwnedTargetLists.end()){lua_settop(L,top);return false;}
  auto result=slotOwnedTargetLists.emplace(u,std::move(record));u->ptr=result.first->second.values.get();
 }
 return true;
}
bool readAISlotTargets(lua_State*L,int index,QList<ServerPlayer*>&out){
 if(!slotBindingGameVM(L)||!lua_checkstack(L,128)||lua_type(L,index)!=LUA_TUSERDATA||lua_rawlen(L,index)!=sizeof(swig_lua_userdata))return false;
 auto*u=static_cast<const swig_lua_userdata*>(lua_touserdata(L,index));
 if(!u||u->type!=SWIGTYPE_p_QListT_ServerPlayer_p_t||u->own!=0)return false;
 const int top=lua_gettop(L);if(!lua_getmetatable(L,index))return false;const void*mt=lua_topointer(L,-1);
 const bool shape=slotTargetsShape(L,-1,slotTargetsGc);lua_settop(L,top);if(!shape)return false;
 auto*vm=LuaRuntime::fromState(L);std::lock_guard<std::mutex>lock(slotTargetsMutex);const auto it=slotOwnedTargetLists.find(const_cast<swig_lua_userdata*>(u));
 if(it==slotOwnedTargetLists.end()||it->second.runtime!=vm||it->second.generation!=vm->generation()||it->second.metatable!=mt||u->ptr!=it->second.values.get())return false;
 out=*it->second.values;return true;
}
void clearAISlotBindings(LuaRuntime&vm){
 std::lock_guard<std::mutex>lock(slotTargetsMutex);
 for(auto it=slotOwnedTargetLists.begin();it!=slotOwnedTargetLists.end();)if(it->second.runtime==&vm)it=slotOwnedTargetLists.erase(it);else ++it;
}

namespace {
bool goutongMoveShape(lua_State*L,int index,lua_CFunction gc){
 const int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 if(!lua_istable(L,index))return false;index=lua_absindex(L,index);if(lua_getmetatable(L,index))return false;
 const auto*c=static_cast<const swig_lua_class*>(SWIGTYPE_p_CardsMoveOneTimeStruct->clientdata);
 if(!c||c->bases[0]||c->metatable[0].name)return false;
 int count=0;lua_pushnil(L);while(AILuaInspect::next(L,index)){
  if(lua_type(L,-2)!=LUA_TSTRING)return false;const char*k=lua_tostring(L,-2);++count;
  lua_CFunction exact=nullptr;
  if(!strcmp(k,"__index"))exact=SWIG_Lua_class_get;
  else if(!strcmp(k,"__newindex"))exact=SWIG_Lua_class_set;
  else if(!strcmp(k,"__gc"))exact=gc;
  else if(!strcmp(k,"__eq"))exact=SWIG_Lua_class_equal;
  else if(!strcmp(k,"__tostring"))exact=SWIG_Lua_class_tostring;
  if(exact){if(!swigExactCFunction(L,-1,exact))return false;}
  else if(!strcmp(k,".type")){if(lua_type(L,-1)!=LUA_TSTRING||strcmp(lua_tostring(L,-1),c->fqname))return false;}
  else if(!strcmp(k,".bases")){if(!swigPlainEmptyTable(L,-1))return false;}
  else if(!strcmp(k,".static")){if(!lua_istable(L,-1))return false;}
  else if(!strcmp(k,".get")||!strcmp(k,".set")||!strcmp(k,".fn")){
   if(!lua_istable(L,-1))return false;const int table=lua_absindex(L,-1);if(lua_getmetatable(L,table))return false;
   const bool methods=!strcmp(k,".fn"),getters=!strcmp(k,".get");int expected=methods?1:0,seen=0;
   if(methods){for(int i=0;c->methods[i].name;++i)++expected;}
   else for(int i=0;c->attributes[i].name;++i)if(getters?c->attributes[i].getmethod:c->attributes[i].setmethod)++expected;
   lua_pushnil(L);while(AILuaInspect::next(L,table)){
    if(lua_type(L,-2)!=LUA_TSTRING)return false;const char*name=lua_tostring(L,-2);lua_CFunction fn=nullptr;
    if(methods){
     if(!strcmp(name,"__disown"))fn=SWIG_Lua_class_disown;
     else for(int i=0;c->methods[i].name;++i)if(!strcmp(name,c->methods[i].name)){fn=c->methods[i].func;break;}
    }else for(int i=0;c->attributes[i].name;++i)if(!strcmp(name,c->attributes[i].name)){fn=getters?c->attributes[i].getmethod:c->attributes[i].setmethod;break;}
    if(!fn||!swigExactCFunction(L,-1,fn))return false;++seen;lua_pop(L,1);
   }if(seen!=expected)return false;
  }else return false;
  lua_pop(L,1);
 }return count==11;
}

struct GoutongMoveWrapper {LuaRuntime*runtime=nullptr;quint64 generation=0;std::unique_ptr<CardsMoveOneTimeStruct>move;};
std::mutex goutongMoveMutex;
std::map<void*,GoutongMoveWrapper>goutongMoves;
int goutongMoveGc(lua_State*L){
 if(lua_type(L,1)!=LUA_TUSERDATA||lua_rawlen(L,1)!=sizeof(swig_lua_userdata))return 0;
 auto*u=static_cast<swig_lua_userdata*>(lua_touserdata(L,1));std::lock_guard<std::mutex>lock(goutongMoveMutex);
 auto i=goutongMoves.find(u);if(i==goutongMoves.end()||u->type!=SWIGTYPE_p_CardsMoveOneTimeStruct||u->ptr!=i->second.move.get())return 0;
 u->ptr=nullptr;u->own=0;goutongMoves.erase(i);return 0;
}
}
bool pushAIGoutongMove(lua_State*L,const CardsMoveOneTimeStruct&source){
 if(!slotBindingGameVM(L)||!lua_checkstack(L,128))return false;
 const int top=lua_gettop(L);auto*u=static_cast<swig_lua_userdata*>(lua_newuserdata(L,sizeof(swig_lua_userdata)));
 u->ptr=nullptr;u->own=0;u->type=SWIGTYPE_p_CardsMoveOneTimeStruct;SWIG_Lua_AddMetatable(L,u->type);
 const int object=lua_absindex(L,-1);if(!lua_getmetatable(L,object)){lua_settop(L,top);return false;}
 const int old=lua_absindex(L,-1);if(!goutongMoveShape(L,old,SWIG_Lua_class_destruct)){lua_settop(L,top);return false;}lua_newtable(L);const int copy=lua_absindex(L,-1);
 lua_pushnil(L);while(lua_next(L,old)){lua_pushvalue(L,-2);lua_pushvalue(L,-2);lua_rawset(L,copy);lua_pop(L,1);}
 lua_pushcfunction(L,goutongMoveGc);lua_setfield(L,copy,"__gc");lua_setmetatable(L,object);lua_settop(L,object);
 // Source QVariant::toMoveOneTime returns this exact native copy. Its private
 // members remain solely in game Lua; no field is exported to isolated here.
 GoutongMoveWrapper w;w.runtime=LuaRuntime::fromState(L);w.generation=w.runtime->generation();w.move=std::make_unique<CardsMoveOneTimeStruct>(source);
 std::lock_guard<std::mutex>lock(goutongMoveMutex);if(goutongMoves.count(u)){lua_settop(L,top);return false;}
 auto i=goutongMoves.emplace(u,std::move(w)).first;u->ptr=i->second.move.get();return true;
}
bool readAIGoutongEndpoints(lua_State*L,int index,Player*&from,Player*&to){
 if(!slotBindingGameVM(L)||!lua_checkstack(L,256)||lua_type(L,index)!=LUA_TUSERDATA||lua_rawlen(L,index)!=sizeof(swig_lua_userdata))return false;
 const auto*u=static_cast<const swig_lua_userdata*>(lua_touserdata(L,index));
 if(!u||u->type!=SWIGTYPE_p_CardsMoveOneTimeStruct||u->own)return false;
 const int top=lua_gettop(L);if(!lua_getmetatable(L,index))return false;
 const bool shape=goutongMoveShape(L,-1,goutongMoveGc);lua_settop(L,top);if(!shape)return false;
 std::lock_guard<std::mutex>lock(goutongMoveMutex);auto i=goutongMoves.find(const_cast<swig_lua_userdata*>(u));
 if(i==goutongMoves.end()||i->second.runtime!=LuaRuntime::fromState(L)||i->second.generation!=i->second.runtime->generation()||u->ptr!=i->second.move.get())return false;
 from=i->second.move->from;to=i->second.move->to;return true;
}
void clearAIGoutongBindings(LuaRuntime&runtime){
 std::lock_guard<std::mutex>lock(goutongMoveMutex);for(auto i=goutongMoves.begin();i!=goutongMoves.end();)if(i->second.runtime==&runtime)i=goutongMoves.erase(i);else ++i;
}

namespace {
bool lianzhanUseShape(lua_State*L,int index,lua_CFunction gc){
 const int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 if(!lua_istable(L,index))return false;index=lua_absindex(L,index);if(lua_getmetatable(L,index))return false;
 const auto*c=static_cast<const swig_lua_class*>(SWIGTYPE_p_CardUseStruct->clientdata);
 if(!c||c->bases[0]||c->metatable[0].name)return false;
 int count=0;lua_pushnil(L);while(AILuaInspect::next(L,index)){
  if(lua_type(L,-2)!=LUA_TSTRING)return false;const char*k=lua_tostring(L,-2);++count;
  lua_CFunction exact=nullptr;
  if(!strcmp(k,"__index"))exact=SWIG_Lua_class_get;
  else if(!strcmp(k,"__newindex"))exact=SWIG_Lua_class_set;
  else if(!strcmp(k,"__gc"))exact=gc;
  else if(!strcmp(k,"__eq"))exact=SWIG_Lua_class_equal;
  else if(!strcmp(k,"__tostring"))exact=SWIG_Lua_class_tostring;
  if(exact){if(!swigExactCFunction(L,-1,exact))return false;}
  else if(!strcmp(k,".type")){if(lua_type(L,-1)!=LUA_TSTRING||strcmp(lua_tostring(L,-1),c->fqname))return false;}
  else if(!strcmp(k,".bases")){if(!swigPlainEmptyTable(L,-1))return false;}
  else if(!strcmp(k,".static")){if(!lua_istable(L,-1))return false;}
  else if(!strcmp(k,".get")||!strcmp(k,".set")||!strcmp(k,".fn")){
   if(!lua_istable(L,-1))return false;const int table=lua_absindex(L,-1);if(lua_getmetatable(L,table))return false;
   const bool methods=!strcmp(k,".fn"),getters=!strcmp(k,".get");int expected=methods?1:0,seen=0;
   if(methods){for(int i=0;c->methods[i].name;++i)++expected;}
   else for(int i=0;c->attributes[i].name;++i)if(getters?c->attributes[i].getmethod:c->attributes[i].setmethod)++expected;
   lua_pushnil(L);while(AILuaInspect::next(L,table)){
    if(lua_type(L,-2)!=LUA_TSTRING)return false;const char*name=lua_tostring(L,-2);lua_CFunction fn=nullptr;
    if(methods){
     if(!strcmp(name,"__disown"))fn=SWIG_Lua_class_disown;
     else for(int i=0;c->methods[i].name;++i)if(!strcmp(name,c->methods[i].name)){fn=c->methods[i].func;break;}
    }else for(int i=0;c->attributes[i].name;++i)if(!strcmp(name,c->attributes[i].name)){fn=getters?c->attributes[i].getmethod:c->attributes[i].setmethod;break;}
    if(!fn||!swigExactCFunction(L,-1,fn))return false;++seen;lua_pop(L,1);
   }if(seen!=expected)return false;
  }else return false;
  lua_pop(L,1);
 }return count==11;
}

// Only CardUseStruct's canonical native GC is replaced. Preserve SWIG's
// own/non-owning destructor semantics, but tombstone before deletion so a
// manually invoked or repeated GC cannot leave a readable dangling pointer.
int lianzhanCanonicalUseGc(lua_State*L){
 if(lua_type(L,-1)!=LUA_TUSERDATA||lua_rawlen(L,-1)!=sizeof(swig_lua_userdata))return 0;
 auto*u=static_cast<swig_lua_userdata*>(lua_touserdata(L,-1));
 if(!u||u->type!=SWIGTYPE_p_CardUseStruct)return 0;
 void*ptr=u->ptr;const bool own=u->own!=0;u->ptr=nullptr;u->own=0;
 const auto*c=static_cast<const swig_lua_class*>(SWIGTYPE_p_CardUseStruct->clientdata);
 if(ptr&&own&&c&&c->destructor)c->destructor(ptr);return 0;
}
struct LianzhanUseWrapper {LuaRuntime*runtime=nullptr;quint64 generation=0;std::unique_ptr<CardUseStruct>use;};
std::mutex lianzhanUseMutex;
std::map<void*,LianzhanUseWrapper>lianzhanUses;
int lianzhanUseGc(lua_State*L){
 if(lua_type(L,1)!=LUA_TUSERDATA||lua_rawlen(L,1)!=sizeof(swig_lua_userdata))return 0;
 auto*u=static_cast<swig_lua_userdata*>(lua_touserdata(L,1));std::lock_guard<std::mutex>lock(lianzhanUseMutex);
 auto i=lianzhanUses.find(u);if(i==lianzhanUses.end()||u->type!=SWIGTYPE_p_CardUseStruct||u->ptr!=i->second.use.get())return 0;
 u->ptr=nullptr;u->own=0;lianzhanUses.erase(i);return 0;
}
}
bool pushAILianzhanUse(lua_State*L,const CardUseStruct&source){
 if(!slotBindingGameVM(L)||!lua_checkstack(L,128))return false;
 const int top=lua_gettop(L);auto*u=static_cast<swig_lua_userdata*>(lua_newuserdata(L,sizeof(swig_lua_userdata)));
 u->ptr=nullptr;u->own=0;u->type=SWIGTYPE_p_CardUseStruct;SWIG_Lua_AddMetatable(L,u->type);
 const int object=lua_absindex(L,-1);if(!lua_getmetatable(L,object)){lua_settop(L,top);return false;}
 const int old=lua_absindex(L,-1);if(!lianzhanUseShape(L,old,lianzhanCanonicalUseGc)){lua_settop(L,top);return false;}lua_newtable(L);const int copy=lua_absindex(L,-1);
 lua_pushnil(L);while(lua_next(L,old)){lua_pushvalue(L,-2);lua_pushvalue(L,-2);lua_rawset(L,copy);lua_pop(L,1);}
 lua_pushcfunction(L,lianzhanUseGc);lua_setfield(L,copy,"__gc");lua_setmetatable(L,object);lua_settop(L,object);
 // Source QVariant::toCardUse returns this exact native copy. Its private
 // members remain solely in game Lua; no field is exported to isolated here.
 LianzhanUseWrapper w;w.runtime=LuaRuntime::fromState(L);w.generation=w.runtime->generation();w.use=std::make_unique<CardUseStruct>(source);
 std::lock_guard<std::mutex>lock(lianzhanUseMutex);if(lianzhanUses.count(u)){lua_settop(L,top);return false;}
 auto i=lianzhanUses.emplace(u,std::move(w)).first;u->ptr=i->second.use.get();return true;
}
bool installAILianzhanBinding(lua_State*L){
 // Called only by the trusted post-rules bootstrap inside a protected thunk.
 if(!slotBindingGameVM(L)||!lua_checkstack(L,256))return false;
 const int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 const auto*c=static_cast<const swig_lua_class*>(SWIGTYPE_p_CardUseStruct->clientdata);if(!c||!c->destructor)return false;
 SWIG_Lua_get_class_registry(L);if(!lua_istable(L,-1))return false;
 AILuaInspect::field(L,-1,c->fqname);const int meta=lua_absindex(L,-1);
 if(lianzhanUseShape(L,meta,lianzhanCanonicalUseGc))return true;
 if(!lianzhanUseShape(L,meta,SWIG_Lua_class_destruct))return false;
 lua_pushcfunction(L,lianzhanCanonicalUseGc);lua_setfield(L,meta,"__gc");return true;
}
bool readAILianzhanUse(lua_State*L,int index,const Card*&card,ServerPlayer*&from,QList<ServerPlayer*>&to){
 if(!slotBindingGameVM(L)||!lua_checkstack(L,256)||lua_type(L,index)!=LUA_TUSERDATA||lua_rawlen(L,index)!=sizeof(swig_lua_userdata))return false;
 const auto*u=static_cast<const swig_lua_userdata*>(lua_touserdata(L,index));
 if(!u||u->type!=SWIGTYPE_p_CardUseStruct||!u->ptr)return false;
 const int top=lua_gettop(L);if(!lua_getmetatable(L,index))return false;
 const bool tracked=lianzhanUseShape(L,-1,lianzhanUseGc);
 const bool canonical=!tracked&&lianzhanUseShape(L,-1,lianzhanCanonicalUseGc);lua_settop(L,top);
 if(tracked){
  if(u->own)return false;std::lock_guard<std::mutex>lock(lianzhanUseMutex);auto i=lianzhanUses.find(const_cast<swig_lua_userdata*>(u));
  if(i==lianzhanUses.end()||i->second.runtime!=LuaRuntime::fromState(L)||i->second.generation!=i->second.runtime->generation()||u->ptr!=i->second.use.get())return false;
  card=i->second.use->card;from=i->second.use->from;to=i->second.use->to;return true;
 }
 // Genuine QVariant::toCardUse/new CardUseStruct results are owning SWIG
 // values. Exact canonical bindings + idempotent GC prove the rooted pointer
 // remains live. Non-owning foreign stack aliases have no lifetime proof.
 if(!canonical||u->own!=1)return false;
 const auto*use=static_cast<const CardUseStruct*>(u->ptr);card=use->card;from=use->from;to=use->to;return true;
}
void clearAILianzhanBindings(LuaRuntime&runtime){
 std::lock_guard<std::mutex>lock(lianzhanUseMutex);for(auto i=lianzhanUses.begin();i!=lianzhanUses.end();)if(i->second.runtime==&runtime)i=lianzhanUses.erase(i);else ++i;
}


// R47: decode only the generated inherited-metamethod proxy. No invocation,
// conversion, registry access, or authored code. The static class pointer is
// compared with the compiled type universe BEFORE dereference. Return the
// exact native DFS lookup order (the root is deliberately skipped by SWIG).
bool aiNativeMetamethodProxy(lua_State *L,int index,const QString &name,
                                  const QString &key,QStringList &search)
{
    search.clear();
    if(key!="__tostring"&&key!="__eq")return false; // complete fixed generated proxy family
    if(!AILuaInspect::capacity(L,8)
       ||lua_tocfunction(L,index)!=SWIG_Lua_resolve_metamethod)return false;
    index=lua_absindex(L,index);const int top=lua_gettop(L);
    struct Restore {lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};
    if(!lua_getupvalue(L,index,1)||lua_type(L,-1)!=LUA_TSTRING)return false;
    size_t n=0;const char*s=lua_tolstring(L,-1,&n);const auto bytes=key.toUtf8();
    if(n!=size_t(bytes.size())||std::memcmp(s,bytes.constData(),n))return false;
    lua_pop(L,1);
    if(!lua_getupvalue(L,index,2)||lua_type(L,-1)!=LUA_TLIGHTUSERDATA)return false;
    const void *ptr=lua_touserdata(L,-1);lua_pop(L,1);
    if(lua_getupvalue(L,index,3))return false;
    const swig_lua_class *actual=nullptr;
    for(size_t i=0;i<swig_module.size;++i){
        const auto*c=static_cast<const swig_lua_class*>(swig_types[i]->clientdata);
        if(c&&c==ptr&&QString::fromUtf8(c->fqname)==name){actual=c;break;}
    }
    if(!actual)return false;
    const auto walk=[&](auto&&visit,const swig_lua_class*c,int depth)->bool{
        if(!c||depth>64||search.size()>=512)return false;
        bool registered=false;for(size_t i=0;i<swig_module.size;++i)
            if(swig_types[i]->clientdata==c){registered=true;break;}
        if(!registered||!c->fqname||!c->bases)return false;
        search.append(QString::fromUtf8(c->fqname));
        for(int i=0;c->bases[i];++i)if(!visit(visit,c->bases[i],depth+1))return false;
        return true;
    };
    if(!actual->bases)return false;
    for(int i=0;actual->bases[i];++i)if(!walk(walk,actual->bases[i],1))return false;
    return !search.isEmpty();
}

// Native-only metadata proof. Never dereference an unrecognized userdata
// type or call a conversion/getter. Room/player pointer provenance is checked
// separately by the certificate's actual native caller proof.
const char *aiNativeUserdataClass(lua_State *L,int index)
{
    if (lua_type(L,index)!=LUA_TUSERDATA || lua_rawlen(L,index)!=sizeof(swig_lua_userdata))
        return nullptr;
    const auto *u=static_cast<const swig_lua_userdata *>(lua_touserdata(L,index));
    if (!u || !u->ptr || !u->type) return nullptr;
    for (int i=0;swig_types[i];++i) {
        if (u->type!=swig_types[i]) continue;
        const auto *c=static_cast<const swig_lua_class *>(swig_types[i]->clientdata);
        return c ? c->fqname : nullptr;
    }
    return nullptr;
}

// R56 metadata-only identity check; no conversion or userdata allocation.
bool matchAINativeRoom(lua_State*L,int index,const Room*room){
 if(!room||lua_type(L,index)!=LUA_TUSERDATA||lua_rawlen(L,index)!=sizeof(swig_lua_userdata))return false;
 const auto*u=static_cast<const swig_lua_userdata*>(lua_touserdata(L,index));
 return u&&u->type==SWIGTYPE_p_Room&&u->ptr==room;
}

bool matchAINativePlayer(lua_State*L,int index,const ServerPlayer*player){
 if(!player||lua_type(L,index)!=LUA_TUSERDATA||lua_rawlen(L,index)!=sizeof(swig_lua_userdata))return false;
 const auto*u=static_cast<const swig_lua_userdata*>(lua_touserdata(L,index));
 return u&&u->type==SWIGTYPE_p_ServerPlayer&&u->ptr==player;
}

AI *Room::cloneAI(ServerPlayer *player)
{
	lua_State *L = getLuaState();
	if (L == nullptr || !Config.EnableAI)
		return new TrustAI(player);

    AICloneScope cloneScope(*this, player, L);
	lua_getglobal(L, "CloneAI");
	// Isolated rooms deliberately do not load SmartAI. Keep the native AI handle
	// for coordinator entry points and emergency defaults without invoking nil.
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return new TrustAI(player);
	}

    AIOriginalTurnUseConstructor constructorWitness(*this, player, L);
    invalidateAIOverseasPriorities(*this);
    invalidateAITargetCache(*this);
    if (!cloneScope.accepted()) invalidateAITargetRevises(*this);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);

	if (LuaRuntime::protectedCall(L, 1, 1, 0)!=0) {
		const char *error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
		output(error_msg);
	} else {
		void *ai_ptr;
		int result = SWIG_ConvertPtr(L, -1, &ai_ptr, SWIGTYPE_p_AI, 0);
		lua_pop(L, 1);
		if (SWIG_IsOK(result)) {
            constructorWitness.complete(static_cast<AI *>(ai_ptr));
            captureAIInovationProviders(*this,player,static_cast<AI *>(ai_ptr));
			return static_cast<AI *>(ai_ptr);
        }
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

	if (LuaRuntime::protectedCall(L, 3, 2, 0)!=0) {
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
    enterAIJijiangFilter(*room,int(event),player,data);
	if (callback == 0)
		return;
	observeAITargetHistoryFilterEvent(*room, int(event), player, data);
	lua_State*L = room->getLuaState();
	AIPlayLoopFilterScope playLoopFilterScope(*room, this, player, int(event), data, L);

	pushCallback(L, __FUNCTION__);
	lua_pushinteger(L, event);
	SWIG_NewPointerObj(L, player, SWIGTYPE_p_ServerPlayer, 0);
	// Copy data to Lua-owned heap storage: smart-ai's filterEvent stores it globally in sgs.filterData.
	// A stack reference would dangle after filterEvent returns and can crash later reads.
	QVariant *dataCopy = new QVariant(data);
	AIFocusFrame imagineFocusFrame(*room, player, int(event), *dataCopy);
	SWIG_NewPointerObj(L, dataCopy, SWIGTYPE_p_QVariant, SWIG_POINTER_OWN);
	AINaturalFilterEntry naturalFilterEntry(*room, this, player, int(event), data, *dataCopy, L);

	if (LuaRuntime::protectedCall(L, 4, 0, 0)!=0) {
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

	int error = LuaRuntime::protectedCall(L, 5, 2, 0);
	if (error != 0) {
		const QString result = lua_tostring(L, -1);
		lua_pop(L, 1);
		room->output(result);
		return TrustAI::askForCard(pattern, prompt, data, method);
	}
	// nresults=2: first return is the card string, second is an optional
	// cardsview request. A single Lua return is padded with nil, so reading
	// the top of stack treated SmartAI's compulsive ".!" answer as empty.
	const char *cstr = lua_tostring(L, -2);
	const QString result = cstr ? QString::fromUtf8(cstr) : QString();
	lua_pop(L, 2);
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
    if(room)enterAIShebianInquiry(*room,self,"card_chosen",reason);
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	SWIG_NewPointerObj(L, who, SWIGTYPE_p_ServerPlayer, 0);
	lua_pushstring(L, flags.toLatin1());
	lua_pushstring(L, reason.toLatin1());
	lua_pushinteger(L, (int)method);

	if (LuaRuntime::protectedCall(L, 5, 1, 0)!=0) {
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
    if(room)enterAIShebianInquiry(*room,self,"player_chosen",reason);
	lua_State*L = room->getLuaState();

	pushCallback(L, __FUNCTION__);
	SWIG_NewPointerObj(L, &targets, SWIGTYPE_p_QListT_ServerPlayer_p_t, 0);
	lua_pushstring(L, reason.toLatin1());

	if (LuaRuntime::protectedCall(L, 3, 1, 0)!=0) {
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

	if (LuaRuntime::protectedCall(L, 5, 1, 0)!=0) {
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

	if (LuaRuntime::protectedCall(L, 5, 1, 0)!=0) {
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

	if (LuaRuntime::protectedCall(L, 3, 1, 0)!=0) {
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

	int error = LuaRuntime::protectedCall(L, 2, 1, 0);
	const QString &result = lua_tostring(L, -1);
	lua_pop(L, 1);
	if (error!=0||result.isEmpty()) {
		room->output(result);
		return TrustAI::askForSinglePeach(dying);
	}
	const Card *card = Card::Parse(result);
	if (!card) return nullptr;
	// Match askForCard: complete skillName/#id answers with activation and source for V2 cost/pay.
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
	if (LuaRuntime::protectedCall(L, 3, 1, 0)!=0) {
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
	if (LuaRuntime::protectedCall(L, 2, 1, 0)!=0) {
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
