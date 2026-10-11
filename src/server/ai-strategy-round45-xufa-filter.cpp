#include "ai-strategy-round45-xufa-filter.h"
#include "ai-strategy-round45-natural-event.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-round45-native-seal.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "room.h"
#include "serverplayer.h"
#include "card.h"
#include "structs.h"
#include <QPointer>
#include <QSet>
#include <QHash>
#include <QList>
#include <QVariant>
#include <cmath>
#include <cstring>
// Lua internals define macros conflicting with Qt headers: include last.
#include "ai-strategy-round45-lua-inspect.h"
namespace {
thread_local QHash<Room*,QList<const AIRound45XufaMoveBodyScope*>> frames;
thread_local QHash<Room*,QList<const AIRound45XufaMoveEntry*>> entries;
using namespace AIRound45Inspect;
struct Stack {lua_State*L;int top;explicit Stack(lua_State*p):L(p),top(lua_gettop(p)){}~Stack(){lua_settop(L,top);}};
bool plain(lua_State*L,int n){auto*t=table(L,n);return t&&!t->metatable;}
struct Source{const char*path;const char*sha;};
const Source sources[]={
 {"lua/ai/chat-ai.lua","29e507189bc261e46ec9ab0fa327781833ccf9c3e7962521eddc471d478ba98e"},
 {"lua/ai/original-hegemony/heg-common-ai.lua","9ad128880b7876a95d022551533a8168ef924173a29d704af837cd5edb98ff7f"},
 {"extensions/addFunction.lua","32938f895048d1abc62a119f179ed356fda155d1ccc8ba40664ff81c2ecba046"},
 {"lua/utilities.lua","b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"}
};
struct Graph {
 Room&r;lua_State*L;int sgs=0;QString why;
 bool fail(const QString&s){if(why.isEmpty())why=s;return false;}
 bool env(int f,const char*unused=nullptr){f=lua_absindex(L,f);
  for(int i=1;;++i){const char*n=lua_getupvalue(L,f,i);if(!n)break;bool ok=false;
   if(!std::strcmp(n,"_ENV")){globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
   else if(unused&&!std::strcmp(n,unused))ok=true;
   lua_pop(L,1);if(!ok)return fail("Xufa reached closure binding differs");
  }return true;
 }
 bool code(int f,const char*p,int a,int b,const char*unused=nullptr){
  return (matchAIRound45PinnedFunction(r,L,f,p,a,b)&&env(f,unused))||fail(QString("Xufa source closure: ")+p+':'+QString::number(a));
 }
 bool globalCode(const char*key,const char*p,int a,int b){Stack st(L);globals(L);field(L,-1,key);return code(-1,p,a,b);}
 bool native(const char*space,const char*key){Stack st(L);globals(L);if(space){field(L,-1,space);if(!plain(L,-1))return fail("Xufa native library table is indirect");}field(L,-1,key);
  return matchAIRound45NativeBinding(r,L,-1,space?QString::fromLatin1(space)+'.'+key:QString::fromLatin1(key))||fail(QString("Xufa native binding: ")+(space?QString::fromLatin1(space)+'.':QString())+key);
 }
 bool method(const char*cls,const char*key){return matchAIRound45NativeClassMethod(r,L,cls,key)||fail(QString("Xufa method: ")+cls+'.'+key);}
 bool property(const char*cls,const char*key){return matchAIRound45NativeClassProperty(r,L,cls,key)||fail(QString("Xufa property: ")+cls+'.'+key);}
 bool constant(const char*key){Stack st(L);field(L,sgs,key);return matchAIRound45NativeConstant(r,L,-1,QStringLiteral("sgs.")+key)||fail(QString("Xufa constant: ")+key);}
 bool qlist(){Stack st(L);field(L,sgs,"qlist");const int f=lua_absindex(L,-1);
  if(!matchAIRound45PinnedFunction(r,L,f,"lua/utilities.lua",19,35))return fail("Xufa qlist source differs");
  for(int i=1;;++i){const char*n=lua_getupvalue(L,f,i);if(!n)break;bool ok=false;
   if(!std::strcmp(n,"_ENV")){globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
   else if(!std::strcmp(n,"qlist_userdata_iterator"))ok=code(-1,"lua/utilities.lua",7,11);
   else if(!std::strcmp(n,"qlist_table_iterator"))ok=code(-1,"lua/utilities.lua",13,17);
   lua_pop(L,1);if(!ok)return fail("Xufa qlist captured iterator differs");
  }
  for(const char*cls:{"SPlayerList","IntList"})for(const char*key:{"length","at"})if(!method(cls,key))return false;
  return true;
 }
 bool callbacks(int event){Stack st(L);field(L,sgs,"ai_event_callback");if(!plain(L,-1))return fail("Xufa callback root is indirect");rawGeti(L,-1,event);
  if(!lua_istable(L,-1))return true; // source type guard, not invented default
  if(!plain(L,-1))return fail("Xufa callback row is indirect");const int row=lua_absindex(L,-1);int count=0;lua_pushnil(L);
  while(next(L,row)){if(++count>4096)return fail("Xufa callback row bound");
   if(lua_isfunction(L,-1)){
    if(event!=int(CardsMoveOneTime))return fail("Xufa non-switch movement event has a callable sibling");
    bool ok=false;
    if(matchAIRound45PinnedFunction(r,L,-1,"lua/ai/chat-ai.lua",1172,1184)||matchAIRound45PinnedFunction(r,L,-1,"lua/ai/chat-ai.lua",1230,1260))ok=env(-1);
    else if(matchAIRound45PinnedFunction(r,L,-1,"lua/ai/original-hegemony/heg-common-ai.lua",269,297))ok=env(-1,"public_card_lack");
    if(!ok)return fail("Xufa unknown CardsMoveOneTime sibling");
   }lua_pop(L,1);
  }return true;
 }
 bool handPileBody(){
  return fail("Xufa hand-pile notification/controller graph not yet certified");
  // Full hand->pile branch: never inspect PresentCard or any individual flag.
  // Both give outcomes have m_playerId=="", and native roster identities are
  // nonempty, so BeMan returns nil before poisonCards/getUseValue/intention.
  if(!discardBody()||!native(nullptr,"ipairs")||!native("string","find"))return false;
  {
   Stack st(L);globals(L);field(L,-1,"string");
   if(!plain(L,-1))return fail("Xufa string library redirected");
   const int strings=lua_absindex(L,-1);field(L,strings,"contains");
   if(!code(-1,"lua/utilities.lua",91,93))return false;lua_pop(L,1);
   if(!primitiveMetatable(L,LUA_TSTRING)||!plain(L,-1))return fail("Xufa string metatable redirected");
   const int mt=lua_absindex(L,-1);int n=0;lua_pushnil(L);
   while(next(L,mt)){++n;
    if(lua_type(L,-2)!=LUA_TSTRING||std::strcmp(lua_tostring(L,-2),"__index")||!lua_rawequal(L,-1,strings))return fail("Xufa string lookup changed");
    lua_pop(L,1);
   }if(n!=1)return fail("Xufa string metatable shape changed");
  }
  for(const char*k:{"getCurrent","setCardFlag"})if(!method("Room",k))return false;
  // getFlags is a copied QStringList. Fixed source clears only visible_*;
  // native CardStateService -> Card/WrappedCard::setFlags -> notifier also
  // reaches controller cleanup. Its complete reentry union is required.
  if(!matchAIRound45NativeDerivedMethods(r,L,"Card",{"getFlags","toString"})
    ||!matchAIRound45NativeDerivedMethods(r,L,"ServerPlayer",{"getTag"})
    ||!method("QVariant","toString"))return fail("Xufa hand/pile scalar or transport binding changed");
  return true;
 }
 bool discardBody(){
  // Never inspect PresentCard, role, hidden hand, card flags, AIChat outcome or
  // callback branch results. This envelope has null to / all Special origins.
  if(!globalCode("BeMan","extensions/addFunction.lua",246,258)
   ||!globalCode("AIChat","lua/ai/chat-ai.lua",3,6)||!qlist()||!native(nullptr,"type")||!native("bit32","band"))return false;
  {Stack st(L);globals(L);field(L,-1,"global_delay");if(lua_type(L,-1)!=LUA_TNUMBER||!std::isfinite(lua_tonumber(L,-1)))return fail("Xufa configured chat delay is not scalar");}
  for(const char*k:{"Player_PlaceHand","Player_PlaceEquip","Player_PlaceDelayedTrick","Player_DiscardPile","Player_Discard","CardMoveReason_S_MASK_BASIC_REASON","CardMoveReason_S_REASON_GIVE","CardMoveReason_S_REASON_DISCARD","CardMoveReason_S_REASON_RULEDISCARD"})if(!constant(k))return false;
  for(const char*k:{"from","to","to_place","card_ids","from_places","reason"})if(!property("CardsMoveOneTimeStruct",k))return false;
  for(const char*k:{"m_reason","m_playerId","m_skillName"})if(!property("CardMoveReason",k))return false;
  if(!method("QVariant","toMoveOneTime")||!method("PlaceList","at")||!method("PlaceList","contains")
    ||!method("Room","getPlayers")||!method("Engine","getCard"))return false;
  if(!matchAIRound45NativeNamespaceProperty(r,L,"sgs","Sanguosha"))return fail("Xufa native Engine namespace getter differs");
  if(!matchAIRound45NativeDerivedMethods(r,L,"ServerPlayer",{"objectName","getPhase"}))return fail("Xufa player scalar methods differ");
  // In this discard envelope the discarded actor is empty; BeMan returns nil
  // for either event-target relation. No Card method is then reached. The
  // qiaobian elseif is bypassed (THROW has DISCARD basic reason).
  return true;
 }
};
}
struct AIRound45XufaMoveBodyScope::Data {
 Room*room=nullptr;QPointer<ServerPlayer>owner;QPointer<const Card>card;
 QList<int>ids;QList<Player::Place>places;bool discard=false,valid=false,entryConsumed=false;
};
struct AIRound45XufaMoveEntry::Data {
 Room*room=nullptr;const AIRound45XufaMoveBodyScope*scope=nullptr;bool valid=false;
};
struct AIRound45XufaBodyAccess {
 static AIRound45XufaMoveBodyScope::Data*top(Room&r){auto i=frames.constFind(&r);return i==frames.cend()||i->isEmpty()?nullptr:i->last()->d.get();}
 static bool currentEntry(Room&r){auto e=entries.constFind(&r);auto f=frames.constFind(&r);return e!=entries.cend()&&!e->isEmpty()&&f!=frames.cend()&&!f->isEmpty()&&e->last()->d->valid&&e->last()->d->scope==f->last();}
};
AIRound45XufaMoveBodyScope::AIRound45XufaMoveBodyScope(Room&r,ServerPlayer*p,const Card*c,bool discard):d(new Data){
 d->room=&r;d->owner=p;d->card=c;d->discard=discard;frames[&r].append(this);
 if(!p||p->getRoom()!=&r||!c||QString::fromLatin1(c->metaObject()->className())!="XufaCard"||p->getPhase()!=Player::Play)return;
 d->ids=c->getSubcards();QSet<int>seen;if(d->ids.isEmpty())return;
 for(int id:d->ids){if(id<0||seen.contains(id)||r.getCardOwner(id)!=p)return;seen.insert(id);
  if(discard){if(r.getCardPlace(id)!=Player::PlaceSpecial||!p->getPile("xufa").contains(id))return;}
  else if(r.getCardPlace(id)!=Player::PlaceHand&&r.getCardPlace(id)!=Player::PlaceEquip)return;
  d->places.append(r.getCardPlace(id));
 }d->valid=true;
}
AIRound45XufaMoveBodyScope::~AIRound45XufaMoveBodyScope(){auto i=frames.find(d->room);if(i==frames.end())return;i->removeAll(this);if(i->isEmpty())frames.erase(i);}
AIRound45XufaMoveEntry::AIRound45XufaMoveEntry(Room&r,const QList<CardsMoveStruct>&moves,bool visible,bool guanxing):d(new Data){
 d->room=&r;entries[&r].append(this);auto*scope=AIRound45XufaBodyAccess::top(r);
 if(!scope||scope->entryConsumed)return;scope->entryConsumed=true;
 if(!scope->valid||!scope->owner||!scope->card||!visible||guanxing||moves.size()!=1)return;
 const auto&move=moves.first();if(move.card_ids!=scope->ids||move.from||!move.reason.m_playerId.isEmpty()||!move.reason.m_eventName.isEmpty())return;
 if(scope->discard){
  if(move.to||move.to_place!=Player::DiscardPile||move.reason.m_reason!=CardMoveReason::S_REASON_THROW||move.reason.m_skillName!="xufa"
   ||move.reason.m_extraData.metaType().id()!=qMetaTypeId<const Card*>()||move.reason.m_extraData.value<const Card*>()!=scope->card)return;
 }else if(move.to!=scope->owner||move.to_place!=Player::PlaceSpecial||move.to_pile_name!="xufa"||move.reason.m_reason!=CardMoveReason::S_REASON_UNKNOWN||!move.reason.m_skillName.isEmpty())return;
 d->scope=frames.value(&r).last();d->valid=true;
}
AIRound45XufaMoveEntry::~AIRound45XufaMoveEntry(){auto i=entries.find(d->room);if(i==entries.end())return;i->removeAll(this);if(i->isEmpty())entries.erase(i);}
bool registerAIRound45XufaBodySources(Room&r,lua_State*L,QString*why){for(const auto&s:sources)if(!registerAIRound45PinnedSource(r,L,s.path,QByteArray(s.sha),why))return false;return true;}
bool certifyAIRound45XufaFilterBody(Room&r,lua_State*L,int selfIndex,const AIRound45NaturalFilterProof&proof,QString*reason){
 if(reason)reason->clear();const auto*scope=AIRound45XufaBodyAccess::top(r);
 const auto fail=[&](const QString&s){if(reason)*reason=s;return false;};
 if(!AIRound45XufaBodyAccess::currentEntry(r)||!scope||!scope->valid||!scope->owner||!scope->card||scope->owner->getRoom()!=&r||scope->card->getSubcards()!=scope->ids)return fail("Xufa exact compiled move custody absent/stale");
 // Only the exact compiled, one-use move entry admits these two envelopes.
 // No private tag/flag result can determine whether the proof succeeds.
 if(!AIRound45Inspect::capacity(L,512))return fail("Xufa metadata stack unavailable");Stack stack(L);selfIndex=lua_absindex(L,selfIndex);
 if(!pushAIRound45NaturalFilterSelf(r,L,4)||!lua_rawequal(L,selfIndex,-1))return fail("Xufa observer differs from actual native filter dispatch");lua_pop(L,1);
 if(scope->owner->getPhase()!=Player::Play)return fail("Xufa source public phase changed");
 const auto roster=r.getPlayers();for(const auto*p:roster)if(!p||p->objectName().isEmpty())return fail("Xufa empty roster identity invalidates BeMan exclusion");
 bool matched=inspectAIRound45NaturalFilterEntry(r,L,4,[&](int event,ServerPlayer*target,const QVariant&payload){
  return event==proof.event&&matchesAIRound47XufaMoveEvent(r,event,target,payload);
 });
 if(!matched)return fail("Xufa current authenticated public move envelope differs");
 Graph g{r,L};globals(L);field(L,-1,"sgs");g.sgs=lua_absindex(L,-1);
 if(!table(L,g.sgs)||!g.callbacks(proof.event)||(proof.event==int(CardsMoveOneTime)&&!(scope->discard?g.discardBody():g.handPileBody())))return fail(g.why.isEmpty()?QStringLiteral("Xufa graph metadata incomplete"):g.why);
 return true;
}

// This matches public source metadata only. Native notification transport
// additionally traverses controller cleanup before bot/no-wire suppression;
// it is NOT independently a nonwriter proof. R47 event preflight remains
// false until that transport union and the trigger roots are certified.
bool matchesAIRound47XufaMoveEvent(Room&r,int event,ServerPlayer*target,const QVariant&payload){
 const auto*scope=AIRound45XufaBodyAccess::top(r);
 if(!AIRound45XufaBodyAccess::currentEntry(r)||!scope||!scope->valid||!scope->owner||!scope->card
  ||scope->owner->getRoom()!=&r||scope->card->getSubcards()!=scope->ids||scope->owner->getPhase()!=Player::Play)return false;
 const auto roster=r.getPlayers();for(const auto*p:roster)if(!p||p->objectName().isEmpty())return false;

  if(!target||!roster.contains(target))return false;
  const bool single=event==int(CardsMoveOneTime)||event==int(BeforeCardsMove);
  const bool batch=event==int(BeforeCardsMoveBatch)||event==int(PreCardsMoveBatch)||event==int(CardsMoveBatch);
  if(!single&&!batch)return false;
  QVariantList list;
  if(single){if(payload.metaType().id()!=qMetaTypeId<CardsMoveOneTimeStruct>())return false;list<<payload;}
  else {if(payload.metaType().id()!=QMetaType::QVariantList)return false;list=payload.toList();}
  if(list.isEmpty())return false;QSet<int>allIds;
  for(const auto&value:list){
   if(value.metaType().id()!=qMetaTypeId<CardsMoveOneTimeStruct>())return false;
   const auto move=value.value<CardsMoveOneTimeStruct>();
   if(move.from!=scope->owner||!move.reason.m_playerId.isEmpty()||!move.reason.m_eventName.isEmpty()
      ||move.card_ids.isEmpty()||move.card_ids.size()!=move.from_places.size())return false;
   if(scope->discard){
    if(move.to||move.to_place!=Player::DiscardPile||move.reason.m_reason!=CardMoveReason::S_REASON_THROW||move.reason.m_skillName!="xufa")return false;
   }else if(move.to!=scope->owner||move.to_place!=Player::PlaceSpecial||move.to_pile_name!="xufa"
      ||move.reason.m_reason!=CardMoveReason::S_REASON_UNKNOWN||!move.reason.m_skillName.isEmpty())return false;
   int prior=-1;for(int i=0;i<move.card_ids.size();++i){int id=move.card_ids[i],at=scope->ids.indexOf(id);
    if(at<=prior||allIds.contains(id)||at>=scope->places.size()||move.from_places[i]!=scope->places[at])return false;prior=at;allIds.insert(id);
   }
  }return true;
}
