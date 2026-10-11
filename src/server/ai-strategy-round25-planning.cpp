#include "ai-strategy-round25-planning.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include <QJsonArray>
#include <QSet>
namespace {
bool owner(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.viewerObjectName==p->objectName()&&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
}
bool aiRound25PlanningConstruction(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s,QString*error){
 auto no=[&](const char*e){if(error)*error=QString::fromLatin1(e);return false;};
 if(!owner(r,p)||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty())return no("Dmpkancolle readonly constructor needs current owner Play");
 const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return no("Dmpkancolle constructor fields differ");
 for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return no("unaudited Dmpkancolle constructor field");
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()||!s.value("suit").isDouble()||!s.value("number").isDouble()||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return no("Dmpkancolle source preview carries no payment or inferred face");
 const auto key=s.value("source_key").toString();if(!p->hasSkill(key))return no("Dmpkancolle constructor source is not owned");
 if(key=="se_hongzha")return s.value("name")=="slash"&&s.value("skill")=="se_hongzha"&&s.value("suit").toDouble()==int(Card::NoSuit)&&s.value("number").toDouble()==0;
 if(key=="nuequ"&&s.value("name")=="fire_slash"&&s.value("skill").toString().isEmpty()){
  // Fixed AI751-758 reads the first sorted owner hand material's *current*
  // face, but the clone itself has NO subcards. Only actual owner hand faces
  // may witness this readonly constructor; selected payment is a later action.
  for(const Card*c:p->getHandcards())if(c&&s.value("suit").toDouble()==int(c->getSuit())&&s.value("number").toDouble()==c->getNumber())return true;
 }
 return no("Dmpkancolle constructor differs from the two fixed source recipes");
}
void projectAIRound25PlanningContext(AIRequest&r,Room&room,const ServerPlayer*p,const QVariant&data,const std::function<QJsonObject(const Card*)>&face){
 projectAIRound25PlanningMetadata(r.worldView,room);
 if(!owner(r,p)||p->getRoom()!=&room||r.kind!=AIRequest::SkillInvoke||r.choiceOptions.reason!="se_huibi_jink"||data.metaType().id()!=QMetaType::QStringList)return;
 const auto values=data.toStringList();if(values.size()<4||values.at(2)!="use")return;
 // player-decision-service.cpp1213-1222 is the exact CardAsked list writer;
 // GameRule792 stores the publicly used card before asking its recipients.
 // No caller-controlled tag name and no history-wide enumeration is accepted.
 const QString index=values.at(3);const QVariant history=room.getTag("UseHistory"+index);
 QJsonObject row{{"known",true},{"history_index",index},{"present",false}};
 if(history.isValid()&&!history.isNull()){
  if(!history.canConvert<CardUseStruct>())return;
  const auto use=history.value<CardUseStruct>();
  if(use.card){
   if(use.card->toString()!=index||!use.to.contains(const_cast<ServerPlayer*>(p))||!face)return;
   if(use.from&&!room.getAllPlayers(true).contains(use.from))return;
   auto card=face(use.card);if(card.isEmpty())return;
   // This publicly declared rescue Slash flag is set by the native
   // NosJiefan provider (yjcm2012.cpp1899), not an arbitrary private flag.
   auto flags=card.value("known_flags").toObject();flags.insert("NosJiefanUsed",use.card->hasFlag("NosJiefanUsed"));card.insert("known_flags",flags);
   row.insert("present",true);row.insert("use",QJsonObject{{"card",card},{"from",use.from?QJsonValue(use.from->objectName()):QJsonValue(false)}});
  }
 }
 auto context=r.choiceOptions.context.value("dmpkancolle").toObject();context.insert("huibi_use",row);r.choiceOptions.context.insert("dmpkancolle",context);r.worldView.decisionContext.insert("dmpkancolle",context);
}

#include "lua-runtime.h"
#include "lua.hpp"
#include <QObject>
namespace {
class HayateGlobalSnapshot final:public QObject{
public:HayateGlobalSnapshot(Room&r,const QJsonObject&v):QObject(&r),value(v){}const QJsonObject value;
};
const HayateGlobalSnapshot*hayateCapture(Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<HayateGlobalSnapshot*>(c))return s;return nullptr;}
}
void captureAIRound25PlanningMetadata(Room&room){
 if(hayateCapture(room))return;
 auto&vm=room.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();QJsonObject row{{"known",false},{"known_nil",false}};
 if(!L){new HayateGlobalSnapshot(room,row);return;}
 const int top=lua_gettop(L);lua_pushglobaltable(L);const int globals=lua_absindex(L,-1);
 lua_pushliteral(L,"enemy");lua_rawget(L,globals);bool known=true;const bool absent=lua_isnil(L,-1);lua_pop(L,1);
 if(absent&&lua_getmetatable(L,globals)){lua_pushliteral(L,"__index");lua_rawget(L,-2);known=lua_isnil(L,-1);lua_pop(L,2);}
 // Source registration is checked without invoking strategy. Writer audit of
 // fixed engine/ext Lua corpus is recorded in planning-round25-global-audit:
 // no _ENV.enemy stores, dynamic loader/restore sites inspected separately.
 lua_pushliteral(L,"sgs");lua_rawget(L,globals);bool source=false;
 if(lua_istable(L,-1)){lua_pushliteral(L,"ai_skill_playerchosen");lua_rawget(L,-2);
  if(lua_istable(L,-1)){lua_pushliteral(L,"vector");lua_rawget(L,-2);
   if(lua_isfunction(L,-1)){lua_Debug info{};lua_pushvalue(L,-1);if(lua_getinfo(L,">S",&info)){
    const QString file=QString::fromUtf8(info.source?info.source:"");source=file.endsWith("/hayate-ai.lua")&&info.linedefined==727&&QString::fromLatin1(info.what?info.what:"")=="Lua";
    row.insert("source",file);row.insert("line",info.linedefined);
   }}
  }
 }
 row.insert("known",known&&source);row.insert("known_nil",known&&source&&absent);lua_settop(L,top);new HayateGlobalSnapshot(room,row);
}
void projectAIRound25PlanningMetadata(AIWorldView&world,Room&room){
 auto hayate=world.strategyFacts.value("hayate").toObject();const auto*c=hayateCapture(room);
 const auto row=c?c->value:QJsonObject{{"known",false},{"known_nil",false}};
 hayate.insert("legacy_enemy_global",row);hayate.insert("legacy_enemy_global_known_nil",row.value("known").toBool()&&row.value("known_nil").toBool());world.strategyFacts.insert("hayate",hayate);
}
