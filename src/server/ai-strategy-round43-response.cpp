#include "ai-strategy-round43-response.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-public-disclosure.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include "engine.h"
#include "lua-wrapper.h"
#include "lua.hpp"
#include <QObject>
#include <QPointer>
#include <QVector>
#include <QJsonArray>
namespace {
struct Frame {QString skill;QPointer<ServerPlayer> actor;int event=NonTrigger;bool cost=false;QVariant data;int depth=-1;const void*callback=nullptr;};
class Ledger final:public AIRoomLedger {public:explicit Ledger(Room&r):AIRoomLedger(r){}QVector<Frame>frames;};
Ledger*ledger(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return new Ledger(r);}
bool sameData(const QVariant&a,const QVariant&b){
 if(a.userType()!=b.userType())return false;
 if(a.userType()==qMetaTypeId<QStringList>())return a.toStringList()==b.toStringList();
 if(a.userType()==qMetaTypeId<CardUseStruct>()){const auto x=a.value<CardUseStruct>(),y=b.value<CardUseStruct>();return x.card==y.card&&x.from==y.from&&x.to==y.to;}
 if(a.userType()==qMetaTypeId<CardResponseStruct>()){const auto x=a.value<CardResponseStruct>(),y=b.value<CardResponseStruct>();return x.m_card==y.m_card&&x.m_who==y.m_who&&x.m_isUse==y.m_isUse;}
 return false;
}
bool knownId(const AIRequest&r,Room&room,ServerPlayer*p,int id){
 const auto scan=[id](const QList<AICardView>&cs){for(const auto&c:cs)if(c.effectiveId==id)return true;return false;};
 if(scan(r.worldView.handCards)||scan(r.worldView.discardPile)||scan(r.choiceOptions.cards))return true;
 QList<AIPlayerView>ps=r.worldView.players;ps.prepend(r.worldView.self);
 for(const auto&v:ps){if(scan(v.knownCards)||scan(v.equips)||scan(v.judgingArea))return true;for(const auto&pile:v.piles)if(pile.open&&scan(pile.cards))return true;}
 return readAIStrategyPublicDisclosure(room,p,id).value("known")==QJsonValue(true);
}
void tagRow(AIRequest&r,const QString&key,const QJsonObject&row){auto g=r.worldView.strategyFacts.value("goddess").toObject();auto tags=g.value("tag_cards").toObject();tags.insert(key,row);g.insert("tag_cards",tags);r.worldView.strategyFacts.insert("goddess",g);}
bool roster(Room&r,const ServerPlayer*p){return !p||r.findPlayerByObjectName(p->objectName(),true)==p;}
}
struct AIRound43GoddessScope::Data {Ledger*l;int depth;};
AIRound43GoddessScope::AIRound43GoddessScope(Room&r,const LuaTriggerSkillV2*skill,ServerPlayer*p,int e,const SkillContext&c,bool cost):d(new Data{ledger(r),0}){
 d->depth=d->l->frames.size();Frame f;f.actor=p;f.event=e;f.cost=cost;if(c.original_data)f.data=*c.original_data;
 // Only actual registered fixed Lua callback identities; no general name/tag
 // permission. All other Lua V2 callbacks install a denying shadow frame.
 if(skill&&Sanguosha->getSkill(skill->objectName())==skill){
  const auto name=skill->objectName();const auto&callback=cost?skill->on_cost:skill->on_effect;
  int first=0,last=0;
  if(cost&&name=="GodManwu"){first=608;last=616;}
  if(!cost&&name=="#Kai_Skill"){first=1088;last=1201;}
  if(!cost&&name=="#Weikai_Skill"){first=1249;last=1425;}
  auto&runtime=r.roomRuntime()->lua();lua_State*L=runtime.state();const int top=L?lua_gettop(L):0;
  if(L&&first&&callback.state()==L&&callback.push(L)){
   lua_Debug info{};const void*identity=lua_topointer(L,-1);
   if(lua_getinfo(L,">S",&info)&&info.source&&QString::fromUtf8(info.source)=="@extensions/goddess.lua"
    &&info.linedefined==first&&info.lastlinedefined==last){f.skill=name;f.callback=identity;f.depth=runtime.protectedCallDepth()+1;}
  }
  if(L)lua_settop(L,top);
 }
 d->l->frames.append(f);
}
AIRound43GoddessScope::~AIRound43GoddessScope(){if(d->l->frames.size()==d->depth+1)d->l->frames.removeLast();else d->l->frames.clear();}
void projectAIRound43GoddessContext(AIRequest&r,Room&room,ServerPlayer*p,const QVariant&data,QJsonObject&context,const std::function<QJsonObject(const Card*)>&face){
 // This derived default-value proof belongs to the current producer only.
 {auto g=context.value("goddess").toObject();g.remove("po_ask_empty_use");
  if(g.isEmpty())context.remove("goddess");else context.insert("goddess",g);}
 if(!p||p->getRoom()!=&room||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||!face)return;
 const auto*l=ledger(room);if(l->frames.isEmpty())return;const Frame f=l->frames.last();
 if(!f.actor||!roster(room,f.actor)||!f.callback||room.roomRuntime()->lua().protectedCallDepth()!=f.depth)return;
 // The innermost Lua frame must be the source writer's actual ask call site.
 // Nested protected callbacks cannot borrow an outer producer, including direct
 // execution helpers that do not go through RoomThread's dispatcher.
 lua_State*L=room.roomRuntime()->lua().state();if(!L)return;int line=0;const int top=lua_gettop(L);
 for(int level=0;;++level){lua_Debug ar{};if(!lua_getstack(L,level,&ar))break;
  if(!lua_getinfo(L,"Slf",&ar))break;const void*id=lua_topointer(L,-1);lua_pop(L,1);
  if(ar.what&&QString::fromLatin1(ar.what)=="C")continue;
  if(id==f.callback)line=ar.currentline;break;
 }
 lua_settop(L,top);if(!line)return;
 // Fixed goddess.lua609-614: own cost, exact public table->discard move and
 // already disclosed physical face. The tag itself grants no face authority.
 if(r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason=="GodManwu"&&f.skill=="GodManwu"&&f.cost
  &&!data.isValid()&&line==613&&f.actor==p&&f.event==BeforeCardsMove&&p->hasSkill("GodManwu")&&f.data.canConvert<CardsMoveOneTimeStruct>()){
  const auto m=f.data.value<CardsMoveOneTimeStruct>();
  if(m.card_ids.size()!=1||!m.from_places.contains(Player::PlaceTable)||m.to_place!=Player::DiscardPile
   ||m.reason.m_reason!=CardMoveReason::S_REASON_USE||!m.from||m.from==p)return;
  const int id=m.card_ids.first();if(room.getTag("GodManwu").toString()!=QString::number(id)||!knownId(r,room,p,id))return;
  const Card*c=room.getCard(id);if(!c||c->getEffectiveId()!=id)return;
  tagRow(r,"GodManwu",{{"known",true},{"present",true},{"card",face(c)}});return;
 }
 if(r.kind!=AIRequest::RespondCard||f.cost)return;
 // Kai's source string list is exactly the native CardAsked input, not a
 // free-form history index. Non-use/no-fourth-field keeps donor nil errors.
 if(r.prompt=="Kai-Jink"&&r.pattern=="Kai"&&r.handlingMethod==Card::MethodResponse
  &&line==1102&&f.skill=="#Kai_Skill"&&f.event==CardAsked&&f.actor==p&&sameData(data,f.data)){
  const auto fields=data.toStringList();if(fields.isEmpty()||fields.first()!="jink")return;
  // The actual CardAsked strings are source input, including short/non-use
  // lists whose later source nil-use access is an ordinary error.
  QJsonArray asked;for(const auto&field:fields)asked.append(field);
  auto askedContext=context.value("goddess").toObject();
  askedContext.insert("kai_asked",QJsonObject{{"known",true},{"fields",asked}});
  context.insert("goddess",askedContext);
  if(fields.size()<4||fields.at(2)!="use")return;
  const QString index=fields.at(3);const QVariant history=room.getTag("UseHistory"+index);
  QJsonObject row{{"known",true},{"index",index},{"present",false}};
  const auto use=history.value<CardUseStruct>();
  if(history.isValid()&&!history.canConvert<CardUseStruct>())return;
  if(use.card){
   if(use.card->toString()!=index||!use.to.contains(p)||!roster(room,use.from))return;
   auto card=face(use.card);if(card.isEmpty())return;
   // Native NosJiefan publicly announces this used rescue Slash (yjcm2012).
   auto flags=card.value("known_flags").toObject();flags.insert("NosJiefanUsed",use.card->hasFlag("NosJiefanUsed"));card.insert("known_flags",flags);
   row.insert("present",true);row.insert("use",QJsonObject{{"from",use.from?QJsonValue(use.from->objectName()):QJsonValue(QJsonValue::Null)},{"card",card}});
  }
  auto g=context.value("goddess").toObject();g.insert("use_history",row);context.insert("goddess",g);return;
 }
 // Fixed PoAskPo first performs QVariant::value<CardUseStruct>(). The Kai
 // QStringList and Weikai CardResponseStruct questions have no such converter:
 // native CardUseStruct() has null from/card. Its entire policy returns dot
 // independently of the ambient Weikai tag. Prove that default conversion,
 // not the private tag's value/presence, at the actual question call site.
 if(r.prompt=="PoAskPo"&&r.pattern=="Weikai"&&r.handlingMethod==Card::MethodUse&&sameData(data,f.data)){
  const bool kai=f.skill=="#Kai_Skill"&&f.event==CardAsked&&(line==1129||line==1146)
   &&data.userType()==qMetaTypeId<QStringList>();
  const bool response=f.skill=="#Weikai_Skill"&&f.event==CardResponded&&(line==1355||line==1377)
   &&data.userType()==qMetaTypeId<CardResponseStruct>();
  if((kai||response)&&!data.canConvert<CardUseStruct>()){
   const auto empty=data.value<CardUseStruct>();
   if(!empty.from&&!empty.card){auto g=context.value("goddess").toObject();
    g.insert("po_ask_empty_use",QJsonObject{{"known",true},{"source",kai?"Kai.CardAsked":"Weikai.CardResponded"}});
    context.insert("goddess",g);return;
   }
  }
 }
 // PoAskPo only tests pointer truthiness: never publish unused card face/IDs.
 // Only the two adjacent source-local setTag(temp)->ask calls attest this
 // value, including known nil temp. Ambient first-loop/Kai reads do not acquire
 // authority from a skill name or absence of a differently scoped writer.
 if(r.prompt=="PoAskPo"&&r.pattern=="Weikai"&&r.handlingMethod==Card::MethodUse&&sameData(data,f.data)
  &&f.skill=="#Weikai_Skill"&&((f.event==CardUsed&&line==1298)||(f.event==CardResponded&&line==1377))){
  if((f.event==CardUsed&&!f.data.canConvert<CardUseStruct>())
    ||(f.event==CardResponded&&!f.data.canConvert<CardResponseStruct>()))return;
  const Card*c=room.getTag("Weikai").value<const Card*>();
  tagRow(r,"Weikai",{{"known",true},{"present",c!=nullptr}});
 }
}
