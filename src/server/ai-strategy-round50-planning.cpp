#include "ai-strategy-round50-planning.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include "skill-runtime-coordinator.h"
#include "lua-wrapper.h"
#include "settings.h"
#include <QJsonArray>
#include <QPointer>
#include <cmath>
#include "lua.hpp"
namespace {
bool current(const AIRequest&r,Room&room,const ServerPlayer*p){return p&&p->getRoom()==&room&&room.roomRuntime()&&!room.roomRuntime()->isClosing()&&r.decisionId&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(room,r,p);}
bool response(const AIRequest&r,const QString&key){return r.kind==AIRequest::UseCard&&r.pattern=="@@"+key&&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE);}
bool same(const AICardView&v,const Card*c,Room&room,const ServerPlayer*p){return c&&v.effectiveId==c->getEffectiveId()&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);}
bool material(const AIRequest&r,Room&room,const ServerPlayer*p,int id){
 // Source getCards("he") yields the wrapper in hand and actual inner Equip.
 // Matching an ID alone does not authenticate either object's current face.
 if(p->handCards().contains(id))for(const auto&v:r.worldView.handCards)if(same(v,Sanguosha->getCard(id),room,p))return true;
 for(const Card*c:p->getEquips())if(c->getEffectiveId()==id)for(const auto&v:r.worldView.self.equips)if(same(v,c,room,p))return true;
 return false;
}
struct Frame{Room*room=nullptr;QPointer<ServerPlayer>target;const void*callback=nullptr;int depth=-1;};
thread_local QVector<Frame> frames;
}
bool aiRound50PlanningEvaluation(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!current(r,room,p)||s.size()!=4||s.value("kind")!="evaluate_skill_card"||!s.value("subcards").isArray())return false;
 const QString key=s.value("skill").toString();if(!response(r,key))return false;
 const auto ids=s.value("subcards").toArray();
 if(key=="jxyangjie")return s.value("name")=="fire_slash"&&ids.isEmpty();
 if(key!="gfengying"||s.value("name")!="thunder_slash"||ids.size()!=1||!ids.first().isDouble())return false;
 const double id=ids.first().toDouble();return id>=0&&id<Sanguosha->getCardCount()&&id==std::floor(id)&&material(r,room,p,int(id));
}
bool aiRound50PlanningRecipe(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&key,const Card*c){
 // The caller obtains c exclusively from the existing current-request store.
 // No new clone, ticket, payment or Lua descriptor can enter this predicate.
 if(!current(r,room,p)||!response(r,key)||!c||c->getSkillName(false)!=key)return false;
 if(key=="jxyangjie")return c->objectName()=="fire_slash"&&c->getSubcards().isEmpty()&&c->getSuit()==Card::NoSuit&&c->getNumber()==0;
 return key=="gfengying"&&c->objectName()=="thunder_slash"&&c->getSubcards().size()==1&&material(r,room,p,c->getSubcards().first());
}
int aiRound50PlanningPrototype(const AIRequest&r,const ServerPlayer*p,const QString&key,const QString&name){
 if(!p||!p->getRoom()||!current(r,*p->getRoom(),p)||!response(r,key))return -1;
 // Actual LuaSkillCard protocols. Native V2 selection/final material/target
 // checks still run; this only admits the exact source-declared prototype.
 return key=="gfengying"&&name=="#"+key?1:-1;
}
bool registerAIRound50PlanningSources(Room&room,lua_State*L,QString*reason){
 return registerAIRound45PinnedSource(room,L,"extensions/sgs10th.lua",QByteArray("356189a1f627c36a77521246ecfa6d583690eb49a511824b8fb90b8b12ceead6"),reason);
}
struct AIRound50YuxinScope::Data{int depth;};
AIRound50YuxinScope::AIRound50YuxinScope(Room&room,const LuaTriggerSkillV2*skill,ServerPlayer*p,int event,const SkillContext&ctx,bool effect):d(new Data{int(frames.size())}){
 Frame f;f.room=&room;
 if(effect&&event==EnterDying&&skill&&skill->objectName()=="ny_10th_yuxin"&&Sanguosha->getSkill(skill->objectName())==skill){
  auto&vm=room.roomRuntime()->lua();lua_State*L=vm.state();const int top=lua_gettop(L);
  if(skill->on_effect.state()==L&&skill->on_effect.push(L)){
   const void*id=lua_topointer(L,-1);
   if(matchAIRound45PinnedFunction(room,L,-1,"extensions/sgs10th.lua",14072,14099)){f.callback=id;f.depth=vm.protectedCallDepth()+1;f.target=ctx.invoker?ctx.invoker:p;}
  }lua_settop(L,top);
 }frames.append(f); // unrelated/nested callbacks shadow the outer producer
}
AIRound50YuxinScope::~AIRound50YuxinScope(){if(frames.size()==d->depth+1)frames.removeLast();else frames.clear();}
void projectAIRound50Planning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto ten=r.worldView.strategyFacts.value("sgs10th").toObject();ten.remove("ny_10th_yuxin");r.worldView.strategyFacts.insert("sgs10th",ten);
 r.worldView.strategyFacts.remove("round50_descriptions");
 if(!current(r,room,p))return;
 if(r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke"&&r.choiceOptions.reason=="ny_10th_yuxin"&&!frames.isEmpty()){
  const auto f=frames.last();auto&vm=room.roomRuntime()->lua();
  if(f.room==&room&&f.callback&&f.target&&room.findPlayerByObjectName(f.target->objectName(),true)==f.target&&vm.protectedCallDepth()==f.depth&&room.getTag("ny_10th_yuxin").value<ServerPlayer*>()==f.target){
   lua_State*L=vm.state();bool reached=false;
   for(int n=0;;++n){lua_Debug a{};if(!lua_getstack(L,n,&a)||!lua_getinfo(L,"Slf",&a))break;const void*id=lua_topointer(L,-1);lua_pop(L,1);if(a.what&&QString::fromLatin1(a.what)=="C")continue;reached=id==f.callback&&a.currentline==14091;break;}
   if(reached){ten.insert("ny_10th_yuxin",f.target->objectName());r.worldView.strategyFacts.insert("sgs10th",ten);}
  }
 }
 if(r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY)return;
 bool owner=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="chenfusui")owner=true;if(!owner)return;
 QJsonObject out;auto ps=r.worldView.players;ps.prepend(r.worldView.self);
 for(const auto&v:ps){auto*t=room.findPlayerByObjectName(v.objectName,true);if(!t)continue;QJsonObject row;
  for(const auto&sv:v.skills){const Skill*s=Sanguosha->getSkill(sv.skillName);if(!s||!s->isVisible())continue;
   bool visible=!Config.EnableHegemony||t==p;
   for(const auto&i:t->getSkillInstances())if(i.skillName==sv.skillName&&SkillRuntimeCoordinator::canReceiveSkillInstance(room,p,t,i))visible=true;
   // Match ServerPlayer::setSkillDescriptionSwap receiver visibility. The
   // source passes no instanceId: only public legacy translation/swap applies.
   if(visible)row.insert(sv.skillName,s->getDescription(t));
  }out.insert(v.objectName,row);
 }r.worldView.strategyFacts.insert("round50_descriptions",out);
}
