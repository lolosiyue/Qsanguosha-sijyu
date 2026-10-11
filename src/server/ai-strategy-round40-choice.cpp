#include "ai-strategy-round40-choice.h"
#include "ai-strategy-round54-jijiang.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "general.h"
#include "engine.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QScopeGuard>
#include <QJsonArray>
#include <QSet>
#include <QHash>
namespace {
class Ledger final:public QObject {
public:explicit Ledger(Room&r):QObject(&r){}
 bool captured=false,entered=false,sourceKnown=false,targetsKnown=false,sourcePresent=false,targetsPresent=false;
 QString viewer,source,clearedEvent;QStringList targets;
};
Ledger*state(Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<Ledger*>(c))return s;return new Ledger(r);}
void forget(Ledger*s){s->sourceKnown=false;s->targetsKnown=false;s->source.clear();s->targets.clear();s->clearedEvent.clear();}
QJsonObject unknown(const QString&reason){return{{"kind","ol_jijiang_state"},{"known",false},{"reason",reason}};}
bool current(Room&r,const AIRequest&q,const ServerPlayer*p){return p&&p->getRoom()==&r&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision();}
bool tableRaw(lua_State*L,const char*key){lua_pushstring(L,key);lua_rawget(L,-2);return lua_istable(L,-1);}
}
void projectAIRound40OLGeneralContext(Room &room,ServerPlayer *viewer,const QStringList &offered,QJsonObject &ctx){
 if(!viewer||viewer->getRoom()!=&room)return;
 QJsonObject catalog=ctx.value("general_catalog").toObject();
 for(const auto&name:offered){auto row=catalog.value(name).toObject();if(const General*g=Sanguosha->getGeneral(name))row.insert("max_hp",g->getMaxHp());catalog.insert(name,row);}ctx.insert("general_catalog",catalog);
 LuaRuntime::Binding binding(room.roomRuntime()->lua(),false);lua_State*L=room.roomRuntime()->lua().state();if(!L)return;
 int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 lua_pushglobaltable(L);if(!tableRaw(L,"sgs")||!tableRaw(L,"ai_general_choice"))return;
 const int registry=lua_absindex(L,-1);QJsonObject rows;
 for(const auto&key:QStringList{"ov_lusu","ov_zhaoe","heg_qingcheng","mobilemou_caoren","mobilemou_gaoshun","mobilemou_lvmeng","mobilemou_machao","sp_machao","tw_machao","xizhicai","liubei","nos_huanggai","nos_liubei","nos_xiahoudun","cenhun"}){
  const auto k=key.toUtf8();lua_pushlstring(L,k.constData(),k.size());lua_rawget(L,registry);
  QJsonObject row{{"known",true},{"present",!lua_isnil(L,-1)},{"value_type",QString::fromLatin1(lua_typename(L,lua_type(L,-1)))}};
  if(lua_isfunction(L,-1)){lua_Debug ar{};lua_pushvalue(L,-1);if(lua_getinfo(L,">S",&ar)){row.insert("source",QString::fromUtf8(ar.source?ar.source:""));row.insert("line",ar.linedefined);row.insert("last_line",ar.lastlinedefined);}}
  rows.insert(key,row);lua_pop(L,1);
 }ctx.insert("ol_general_sources",rows);
}
void captureAIRound40OLJijiang(Room&r){
 auto*s=state(r);if(s->captured)return;s->captured=true;if(s->entered)return;
 LuaRuntime::Binding binding(r.roomRuntime()->lua(),false);lua_State*L=r.roomRuntime()->lua().state();if(!L)return;
 int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});lua_pushglobaltable(L);if(!tableRaw(L,"sgs"))return;
 // Only proven initial nil is exported. No SWIG player/global value is read or
 // decoded; startup owner/foreign populated globals remain unknown.
 lua_pushliteral(L,"oljijiangsource");lua_rawget(L,-2);s->sourceKnown=lua_isnil(L,-1);s->sourcePresent=false;lua_pop(L,1);
 lua_pushliteral(L,"oljijiangtarget");lua_rawget(L,-2);s->targetsKnown=lua_isnil(L,-1);s->targetsPresent=false;
}
void beginAIRound40OLJijiang(Room&r,const QString&viewer){auto*s=state(r);s->entered=true;if(viewer.isEmpty()||(!s->viewer.isEmpty()&&s->viewer!=viewer))forget(s);s->viewer=viewer;}
void invalidateAIRound40OLJijiang(Room&r){auto*s=state(r);s->entered=true;forget(s);}
QJsonObject aiRound40OLJijiangEvent(const AIEventView&e){
 const auto prompt=e.details.value("prompt_list").toArray();const auto kind=e.details.value("choice_kind").toString();
 if(e.kind=="card_use"&&(e.cardName=="OLJijiangCard"||e.cardClass=="OLJijiangCard"))
  return{{"writer","card_use"},{"actor",e.details.value("player")},{"sequence",QString::number(e.sequence)}};
 if(e.kind=="choice"&&kind=="skillInvoke"&&e.reason=="oljijiang"&&!prompt.isEmpty()&&prompt.last()=="yes")
  return{{"writer","invoke"},{"actor",e.from},{"sequence",QString::number(e.sequence)}};
 if(e.kind=="choice"&&kind=="cardResponded"&&prompt.size()>=3&&prompt.at(2)=="@oljijiang-slash")
  return{{"writer","respond"},{"actor",e.from},{"sequence",QString::number(e.sequence)},{"answer_nonempty",prompt.last().isString()&&!prompt.last().toString().isEmpty()}};
 return {};
}
QJsonObject queryAIRound40OLJijiang(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QString&value){
 // Actual shared namespace only. The old viewer-local shadow is not a
 // cross-viewer writer witness and cannot substitute for a current source.
 return queryAIRound54Jijiang(r,q,p,op,value);
}
