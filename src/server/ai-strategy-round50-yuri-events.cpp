#include "ai-strategy-round50-yuri-events.h"
#include "ai.h"
#include "ai-strategy-round45-code-seal.h"
#include "lua-wrapper.h"
#include "lua.hpp"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "card.h"
#include "skill.h"
#include <QJsonArray>
#include <QPointer>
#include <QSet>
namespace {
thread_local AIRound50YuriScope *current=nullptr;
thread_local quint64 nextSerial=0; // Never reset/reused when a Room/runtime is recreated.
const QSet<QString> keys{"SlashMissed","owner_flexible","flexible","skeleton","bougairensha","kirisou","tomowodaji","japenknifejile","loyalm","bffeedingpoisoning","yuriganglie","suckblood"};
bool roster(Room&r,const ServerPlayer*p){return p&&r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p));}
QJsonValue name(Room&r,const ServerPlayer*p){return roster(r,p)?QJsonValue(p->objectName()):QJsonValue(false);}
void clear(QJsonObject&facts){auto out=facts.value("yuri").toObject();for(const auto&k:keys)out.remove(k);if(out.isEmpty())facts.remove("yuri");else facts.insert("yuri",out);}
void add(QJsonObject&facts,const QString&key,const QJsonObject&row){if(row.isEmpty())return;auto out=facts.value("yuri").toObject();out.insert(key,row);facts.insert("yuri",out);}
QJsonObject emptyUse(){return{{"r50_type","CardUse"},{"from",false},{"to",QJsonArray()},{"card",false}};}
QJsonObject emptyDamage(){DamageStruct d;return{{"r50_type","Damage"},{"from",false},{"to",false},{"card",false},{"amount",d.damage},{"nature",int(d.nature)},{"chain",d.chain},{"transfer",d.transfer},{"by_user",d.by_user}};}
QJsonObject emptySlash(){SlashEffectStruct e;return{{"r50_type","SlashEffect"},{"from",false},{"to",false},{"slash",false},{"jink_num",e.jink_num},{"drank",e.drank},{"nature",int(e.nature)}};}
}
struct AIRound50YuriScope::State {
 Room*room=nullptr;const TriggerSkillV2*definition=nullptr;SkillContext*ctx=nullptr;
 QVariant*data=nullptr;QPointer<ServerPlayer>owner,invoker;QPointer<Card>card;
 QString skill;int event=-1;quint64 serial=0;qint64 history=0;
 const void *callback=nullptr;int depth=-1;lua_State*luaState=nullptr;quint64 generation=0;
};
bool registerAIRound50YuriSource(Room&r,lua_State*L){return registerAIRound45PinnedSource(r,L,QStringLiteral("extensions/yuri.lua"),QByteArrayLiteral("406788870d21618cba643e2994bdb08e72329df80196e0fc3773d383d9756fd6"));}
AIRound50YuriScope::AIRound50YuriScope(Room&r,const TriggerSkillV2*d,SkillContext*c,int event):previous_(current){
 current=this;state_=new State;state_->serial=++nextSerial;
 static const QSet<QString>skills{"flexible","skeleton","bougairensha","kirisou","#kirisou_umaretuski","tomowodaji","japenknifejile","loyalm","bffeedingpoisoning","yuriganglie","suckblood"};
 if(!d||!c||!skills.contains(d->objectName())||Sanguosha->getSkill(d->objectName())!=d
  ||!roster(r,c->owner)||!roster(r,c->invoker)||!c->original_data
  ||c->activationRef.key.skillName!=d->objectName()
  ||c->activationRef.ownerObjectName!=c->owner->objectName())return;
 const auto*luaDefinition=dynamic_cast<const LuaTriggerSkillV2*>(d);if(!luaDefinition||!r.roomRuntime())return;
 int first=0,last=0;const auto n=d->objectName();
 if(n=="flexible"){first=6363;last=6397;}
 else if(n=="skeleton"){first=8049;last=8092;}
 else if(n=="bougairensha"){first=13830;last=13875;}
 else if(n=="kirisou"){first=15536;last=15576;}
 else if(n=="#kirisou_umaretuski"){first=15591;last=15612;}
 else if(n=="tomowodaji"){first=7691;last=7732;}
 else if(n=="japenknifejile"){first=7796;last=7928;}
 else if(n=="loyalm"){first=8723;last=8747;}
 else if(n=="bffeedingpoisoning"){first=5699;last=5744;}
 else if(n=="yuriganglie"){first=10582;last=10610;}
 else if(n=="suckblood"){first=12427;last=12465;}
 auto&vm=r.roomRuntime()->lua();auto*L=vm.state();if(!L)return;const int top=lua_gettop(L);
 const bool pinned=first&&luaDefinition->on_effect.state()==L&&luaDefinition->on_effect.push(L)
  &&matchAIRound45PinnedFunction(r,L,-1,QStringLiteral("extensions/yuri.lua"),first,last);
 const void*callback=pinned?lua_topointer(L,-1):nullptr;lua_settop(L,top);if(!callback)return;
 state_->callback=callback;state_->depth=vm.protectedCallDepth()+1;state_->luaState=L;state_->generation=vm.generation();
 auto&s=*state_;s.room=&r;s.definition=d;s.ctx=c;s.owner=c->owner;s.invoker=c->invoker;
 s.data=c->original_data;s.skill=d->objectName();s.event=event;
 if(s.data->canConvert<CardUseStruct>()){const auto u=s.data->value<CardUseStruct>();s.card=const_cast<Card*>(u.card);s.history=u.targetModReveal.useHistoryEventId;}
}
AIRound50YuriScope::~AIRound50YuriScope(){current=previous_;delete state_;}
namespace {
const AIRound50YuriScope::State* live(Room&r){
 const auto*s=current?current->state():nullptr;
 if(!s||s->room!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!s->ctx||!s->owner||!s->invoker||!roster(r,s->owner)||!roster(r,s->invoker)||s->ctx->original_data!=s->data
  ||s->ctx->owner!=s->owner||s->ctx->invoker!=s->invoker
  ||s->ctx->activationRef.key.skillName!=s->skill
  ||s->ctx->activationRef.ownerObjectName!=s->owner->objectName()
  ||Sanguosha->getSkill(s->skill)!=s->definition)return nullptr;
 auto&vm=r.roomRuntime()->lua();auto*L=vm.state();if(!L||L!=s->luaState||vm.generation()!=s->generation||!s->callback||vm.protectedCallDepth()!=s->depth)return nullptr;
 // First non-C frame must be the pinned actual source body, not a nested sibling helper.
 const int top=lua_gettop(L);int line=0;
 for(int level=0;;++level){lua_Debug ar{};if(!lua_getstack(L,level,&ar)||!lua_getinfo(L,"Slf",&ar))break;const void*fn=lua_topointer(L,-1);lua_pop(L,1);if(ar.what&&QString::fromLatin1(ar.what)=="C")continue;if(fn==s->callback)line=ar.currentline;break;}
 lua_settop(L,top);
 bool sourceAsk=false;
 if(s->skill=="flexible")sourceAsk=(line==6372||line==6374);
 else if(s->skill=="skeleton")sourceAsk=(line==8057);
 else if(s->skill=="bougairensha")sourceAsk=(line==13843);
 else if(s->skill=="kirisou")sourceAsk=(line==15547);
 else if(s->skill=="#kirisou_umaretuski")sourceAsk=(line==15603);
 else if(s->skill=="tomowodaji")sourceAsk=(line==7705);
 else if(s->skill=="japenknifejile")sourceAsk=(line==7887);
 else if(s->skill=="loyalm")sourceAsk=(line==8730);
 else if(s->skill=="bffeedingpoisoning")sourceAsk=(line==5705);
 else if(s->skill=="yuriganglie")sourceAsk=(line==10591);
 else if(s->skill=="suckblood")sourceAsk=(line==12444);
 return sourceAsk?s:nullptr;
}
QJsonObject useSlot(Room&r,const AIRound50YuriScope::State&s,ServerPlayer*viewer,const QString&key,bool owner,
 const std::function<QJsonObject(const Card*)>&serialize){
 const QVariant tag=owner?viewer->getTag(key):r.getTag(key);
 // Exact missing/wrong QVariant conversion remains the source default value.
 // Nonempty convertible payload needs current typed frame proof; never infer it from a tag name.
 if(!tag.canConvert<CardUseStruct>())return emptyUse();
 if(!s.data->canConvert<CardUseStruct>())return{};
 const auto t=tag.value<CardUseStruct>(),u=s.data->value<CardUseStruct>();
 if(!u.card||u.card!=s.card||u.targetModReveal.useHistoryEventId!=s.history||!roster(r,u.from)
  ||t.card!=u.card||t.from!=u.from||t.to!=u.to||t.targetModReveal.useHistoryEventId!=u.targetModReveal.useHistoryEventId)return{};
 bool exposed=(s.event==TargetConfirmed||s.event==TargetConfirming||s.event==TargetSpecified);
 if(s.event==PreCardUsed)exposed=u.from==viewer&&s.owner==viewer;
 if(!exposed)return{};
 QJsonArray tos;for(auto*p:u.to){if(!roster(r,p))return{};tos.append(p->objectName());}
 auto face=serialize(u.card);if(face.isEmpty())return{};
 return{{"r50_type","CardUse"},{"from",u.from->objectName()},{"to",tos},{"card",face}};
}
QJsonObject damageSlot(Room&r,const AIRound50YuriScope::State&s,ServerPlayer*viewer,const QString&key){
 const QVariant tag=r.getTag(key);if(!tag.canConvert<DamageStruct>())return emptyDamage();
 // Only this fixed named writer installs a Room DamageStruct. Other names
 // must not substitute their invoke argument for an unwritten Room slot.
 if(key!="bffeedingpoisoning"||s.event!=DamageCaused||s.owner!=viewer||!s.data->canConvert<DamageStruct>())return{};
 const auto d=s.data->value<DamageStruct>(),t=tag.value<DamageStruct>();
 if(d.from!=viewer||!roster(r,d.to)||t.from!=d.from||t.to!=d.to||t.card!=d.card||t.damage!=d.damage||t.nature!=d.nature||t.chain!=d.chain||t.transfer!=d.transfer)return{};
 // The actual intention body reads only damage.to; no pre-damage hidden card,
 // chain/filter metadata or amount is exported merely because this actor owns the skill.
 return{{"r50_type","Damage"},{"to",d.to->objectName()}};
}
bool eventDomain(const AIRound50YuriScope::State&s,const QString&k){
 if(s.skill!=k)return false;
 if(k=="tomowodaji")return s.event==DamageInflicted;
 if(k=="japenknifejile"||k=="bffeedingpoisoning")return s.event==DamageCaused;
 if(k=="loyalm"||k=="yuriganglie")return s.event==Damaged;
 if(k=="suckblood")return s.event==Damage;
 if(k=="skeleton")return s.event==TargetConfirmed;
 return k=="bougairensha"&&s.event==TargetConfirming;
}
}
void projectAIRound50YuriQuestion(AIRequest&q,Room&r,ServerPlayer*p,const std::function<QJsonObject(const Card*)>&serialize){
 clear(q.worldView.strategyFacts);
 // Bind the native-only tombstone before checking scope, including an absent frame.
 const auto*s=live(r);const quint64 serial=s?s->serial:0;
 if(!q.round50YuriFrameBound){q.round50YuriFrameBound=true;q.round50YuriFrameSerial=serial;}
 if(!s||!q.decisionId||q.round50YuriFrameSerial!=serial||!roster(r,p)||q.viewerObjectName!=p->objectName()
  ||q.worldView.self.objectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision())return;
 const auto k=q.choiceOptions.reason;
 if(q.kind==AIRequest::UseCard&&q.pattern=="@@flexible"&&s->skill=="flexible"&&s->event==PreCardUsed&&s->owner==p){
  add(q.worldView.strategyFacts,"flexible",useSlot(r,*s,p,"flexible",false,serialize));
  add(q.worldView.strategyFacts,"owner_flexible",useSlot(r,*s,p,"flexible",true,serialize));
 }else if(q.kind==AIRequest::Choice&&q.choiceOptions.question=="askForChoice"&&k=="kirisou"&&s->skill==k&&s->event==TargetSpecified&&s->owner==p){
  add(q.worldView.strategyFacts,k,useSlot(r,*s,p,k,false,serialize));
 }else if(q.kind==AIRequest::Discard&&q.choiceOptions.question=="askForDiscard"&&k=="kirisou_umaretuski"&&s->skill=="#kirisou_umaretuski"&&s->event==TargetSpecifying){
  // This current native question has no SlashMissed tag writer. Preserve the
  // default source SlashEffect; a retained nonempty tag has no lawful frame.
  if(!p->getTag("SlashMissed").canConvert<SlashEffectStruct>())add(q.worldView.strategyFacts,"SlashMissed",emptySlash());
 }
}
void projectAIRound50YuriEvent(AIWorldView&w,Room&r,ServerPlayer*viewer,ServerPlayer*actor,const AIEventView&e,const QVariant&data,const std::function<QJsonObject(const Card*)>&serialize){
 clear(w.strategyFacts);
 if(e.triggerEvent!=ChoiceMade||viewer!=actor||!roster(r,viewer)||w.self.objectName!=viewer->objectName()||!r.roomRuntime()||e.revision!=r.roomRuntime()->stateRevision()||data.userType()!=QMetaType::QString)return;
 const auto*s=live(r);if(!s)return;const auto parts=data.toString().split(':');
 if(parts.size()<3)return;const auto kind=parts[0],key=parts[1];
 if(kind=="skillInvoke"&&parts.size()==3&&(parts[2]=="yes"||parts[2]=="no")&&eventDomain(*s,key)){
  // Tomowodaji may ask each actual holder; the other source actors are ctx.owner.
  if(key=="tomowodaji"){if(!viewer->hasSkill(key))return;}else if(s->owner!=viewer)return;
  add(w.strategyFacts,key,key=="skeleton"?useSlot(r,*s,viewer,key,false,serialize):damageSlot(r,*s,viewer,key));
 }else if(kind=="cardResponded"&&parts.size()>=4&&parts[2]=="@bougairensha"&&eventDomain(*s,"bougairensha")&&viewer->hasSkill("bougairensha")){
  add(w.strategyFacts,"bougairensha",useSlot(r,*s,viewer,"bougairensha",false,serialize));
 }
}
