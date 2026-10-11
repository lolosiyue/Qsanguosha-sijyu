#include "ai-strategy-round50-htms-events.h"
#include "ai-strategy-round45-code-seal.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include "lua-wrapper.h"
#include "lua.hpp"
#include <QObject>
#include <QPointer>
#include <QVector>
#include <QJsonArray>
namespace {
const char*path="extensions/htms.lua";
struct Frame {QString name;QPointer<ServerPlayer>actor;int event=NonTrigger,depth=-1;const QVariant*data=nullptr;const void*callback=nullptr;};
struct DamageFrame {QVector<QVariant>publicValues;QVariant previous;};
struct Pending {const QVariant*value=nullptr;bool allowed=false;};
class Ledger final:public QObject {public:explicit Ledger(Room&r):QObject(&r){}QVector<Frame>frames;QVector<DamageFrame>damages;QVector<Pending>pending;QVariant currentDamage;};
Ledger*find(Room&r){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return nullptr;}
Ledger*ledger(Room&r){auto*l=find(r);return l?l:new Ledger(r);}
bool roster(Room&r,const ServerPlayer*p){return !p||r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p));}
bool sameDamage(const QVariant&a,const QVariant&b){
 if(a.userType()!=qMetaTypeId<DamageStruct>()||b.userType()!=qMetaTypeId<DamageStruct>())return false;
 const auto x=a.value<DamageStruct>(),y=b.value<DamageStruct>();
 return x.from==y.from&&x.to==y.to&&x.card==y.card&&x.damage==y.damage&&x.nature==y.nature&&x.chain==y.chain&&x.transfer==y.transfer&&x.by_user==y.by_user&&x.reason==y.reason&&x.transfer_reason==y.transfer_reason&&x.prevented==y.prevented&&x.tips==y.tips&&x.ignore_hujia==y.ignore_hujia;
}
bool sameUse(const QVariant&a,const QVariant&b){
 if(a.userType()!=qMetaTypeId<CardUseStruct>()||b.userType()!=qMetaTypeId<CardUseStruct>())return false;
 const auto x=a.value<CardUseStruct>(),y=b.value<CardUseStruct>();return x.from==y.from&&x.to==y.to&&x.card==y.card;
}
void clear(QJsonObject&facts){auto h=facts.value("htms").toObject();for(const char*k:{"CurrentDamageStruct","CurrentUseStruct","huanggui","tcmfs","htms_lunbian"})h.remove(k);if(h.isEmpty())facts.remove("htms");else facts.insert("htms",h);}
void publish(QJsonObject&facts,const QString&key,const QJsonObject&row){auto h=facts.value("htms").toObject();h.insert(key,row);facts.insert("htms",h);}
bool frame(Room&r,Frame&out,int&line){
 auto*l=find(r);if(!l||l->frames.isEmpty()||!r.roomRuntime()||r.roomRuntime()->isClosing())return false;
 out=l->frames.last();if(!out.actor||!roster(r,out.actor)||!out.callback||!out.data||r.roomRuntime()->lua().protectedCallDepth()!=out.depth)return false;
 lua_State*L=r.roomRuntime()->lua().state();if(!L)return false;const int top=lua_gettop(L);line=0;
 for(int level=0;;++level){lua_Debug ar{};if(!lua_getstack(L,level,&ar)||!lua_getinfo(L,"Slf",&ar))break;const void*id=lua_topointer(L,-1);lua_pop(L,1);if(ar.what&&QString::fromLatin1(ar.what)=="C")continue;if(id==out.callback)line=ar.currentline;break;}
 lua_settop(L,top);return line!=0;
}
QJsonValue player(const ServerPlayer*p){return p?QJsonValue(p->objectName()):QJsonValue(false);}
QJsonObject cardFace(Room&r,ServerPlayer*p,const Card*c,const std::function<QJsonObject(const Card*)>&face){
 auto out=face(c);if(!out.isEmpty())out.insert("legacy_identity_token",aiLegacyCardIdentity(r,p,c));return out;
}
QJsonObject useRow(Room&r,ServerPlayer*p,const QVariant&v,const std::function<QJsonObject(const Card*)>&face){
 const auto u=v.value<CardUseStruct>();if(!roster(r,u.from))return{};QJsonArray to;for(auto*t:u.to){if(!t||!roster(r,t))return{};to.append(t->objectName());}
 QJsonValue c(false);if(u.card){const auto f=cardFace(r,p,u.card,face);if(f.isEmpty())return{};c=f;}
 return{{"current_event",true},{"from",player(u.from)},{"to",to},{"card",c}};
}
QJsonObject damageRow(Room&r,ServerPlayer*p,const QVariant&v,const std::function<QJsonObject(const Card*)>&face){
 const auto x=v.value<DamageStruct>();if(!roster(r,x.from)||!roster(r,x.to))return{};
 QJsonValue c(false);if(x.card){const auto f=cardFace(r,p,x.card,face);if(f.isEmpty())return{};c=f;}
 return{{"current_event",true},{"from",player(x.from)},{"to",player(x.to)},{"card",c},{"amount",x.damage},{"nature",int(x.nature)},{"chain",x.chain},{"transfer",x.transfer}};
}
}
void clearAIRound50HTMSWitnesses(Room&r){if(auto*l=find(r)){l->frames.clear();l->pending.clear();l->damages.clear();l->currentDamage.clear();}}
bool registerAIRound50HTMSSource(Room&r,lua_State*L){clearAIRound50HTMSWitnesses(r);return registerAIRound45PinnedSource(r,L,QString::fromLatin1(path),QByteArrayLiteral("03d463d67c11da985d0cfa369c1dfbc625c564a79b1d7b00b74c0bd050ed11bc"));}
struct AIRound50HTMSFrame::Data {Ledger*l;int depth;};
AIRound50HTMSFrame::AIRound50HTMSFrame(Room&r,const LuaTriggerSkillV2*s,ServerPlayer*p,int e,const SkillContext&ctx,bool cost):d(new Data{ledger(r),0}){
 d->depth=d->l->frames.size();Frame f;f.actor=ctx.invoker?ctx.invoker:p;f.event=e;f.data=ctx.original_data;
 if(!cost&&s&&Sanguosha->getSkill(s->objectName())==s){
  int first=0,last=0;const auto n=s->objectName();
  if(n=="shilianEX"){first=2978;last=3023;}else if(n=="chuszy"){first=4215;last=4246;}
  else if(n=="meihuomoyan"){first=6712;last=6756;}else if(n=="anni"){first=14677;last=14746;}
  else if(n=="huanggui"){first=15344;last=15378;}else if(n=="tcmfs"){first=25302;last=25323;}else if(n=="htms_lunbian"){first=31307;last=31346;}
  auto&runtime=r.roomRuntime()->lua();lua_State*L=runtime.state();const int top=L?lua_gettop(L):0;
  if(L&&first&&s->on_effect.state()==L&&s->on_effect.push(L)&&matchAIRound45PinnedFunction(r,L,-1,QString::fromLatin1(path),first,last)){f.name=n;f.callback=lua_topointer(L,-1);f.depth=runtime.protectedCallDepth()+1;}
  if(L)lua_settop(L,top);
 }
 d->l->frames.append(f);
}
AIRound50HTMSFrame::~AIRound50HTMSFrame(){if(d->l->frames.size()==d->depth+1)d->l->frames.removeLast();else d->l->frames.clear();}
void projectAIRound50HTMSEvents(AIRequest&q,Room&r,ServerPlayer*p,const std::function<QJsonObject(const Card*)>&face){
 clear(q.worldView.strategyFacts);
 if(!p||!roster(r,p)||p->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!q.decisionId||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision()||!face)return;
 Frame f;int line=0;if(!frame(r,f,line))return;
 const auto reason=q.choiceOptions.reason;QString key;bool damage=false;
 if(f.name=="chuszy"&&f.event==Damaged&&line==4235&&q.kind==AIRequest::Discard&&reason=="chuszy"){
  auto*l=find(r);if(!l||l->damages.isEmpty()||!l->currentDamage.isValid())return;const auto row=damageRow(r,p,l->currentDamage,face);if(!row.isEmpty())publish(q.worldView.strategyFacts,"CurrentDamageStruct",row);return;
 }
 if(f.name=="meihuomoyan"&&f.event==CardUsed&&(line==6735||line==6736)&&q.kind==AIRequest::PlayerChosen&&reason=="meihuomoyan")key="CurrentUseStruct";
 else if(f.name=="anni"&&f.event==TargetConfirmed&&reason=="anni"&&((q.kind==AIRequest::PlayerChosen&&(line==14695||line==14696)&&f.actor==p)||(q.kind==AIRequest::Choice&&line==14698)))key="CurrentUseStruct";
 else if(f.name=="huanggui"&&f.event==TargetConfirming&&line==15365&&q.kind==AIRequest::PlayerChosen&&reason=="huanggui"&&f.actor==p)key="huanggui";
 else if(f.name=="tcmfs"&&f.event==DamageCaused&&line==25315&&q.kind==AIRequest::Discard&&reason=="tcmfs"&&f.actor==p){key="tcmfs";damage=true;}
 else if(f.name=="htms_lunbian"&&f.event==TargetConfirmed&&line==31318&&q.kind==AIRequest::Discard&&reason=="htms_lunbian")key="htms_lunbian";
 if(key.isEmpty())return;const auto tag=r.getTag(key);if(damage?!sameDamage(tag,*f.data):!sameUse(tag,*f.data))return;
 const auto row=damage?damageRow(r,p,tag,face):useRow(r,p,tag,face);if(!row.isEmpty())publish(q.worldView.strategyFacts,key,row);
}
void projectAIRound50HTMSIntention(AIWorldView&w,Room&r,ServerPlayer*p,const AIEventView&e,const std::function<QJsonObject(const Card*)>&face){
 clear(w.strategyFacts);if(!p||!roster(r,p)||!r.roomRuntime()||r.roomRuntime()->isClosing()||w.self.objectName!=p->objectName()||e.revision!=r.roomRuntime()->stateRevision()||e.triggerEvent!=ChoiceMade||e.from!=p->objectName()||!face)return;
 const auto a=e.details.value("prompt_list").toArray();if(a.size()!=3||a.at(0)!="skillInvoke"||a.at(1)!="shilianEX"||(a.at(2)!="yes"&&a.at(2)!="no"))return;
 Frame f;int line=0;if(!frame(r,f,line)||f.name!="shilianEX"||f.event!=Damaged||f.actor!=p||line!=2988)return;
 auto*l=find(r);if(!l||l->damages.isEmpty()||!l->currentDamage.isValid())return;const auto row=damageRow(r,p,l->currentDamage,face);if(!row.isEmpty())publish(w.strategyFacts,"CurrentDamageStruct",row);
}
struct AIRound50DamageScope::Data {Ledger*l;int depth;};
AIRound50DamageScope::AIRound50DamageScope(Room&r):d(new Data{ledger(r),0}){d->depth=d->l->damages.size();DamageFrame f;f.previous=d->l->currentDamage;if(f.previous.isValid())f.publicValues.append(f.previous);d->l->damages.append(f);}
AIRound50DamageScope::~AIRound50DamageScope(){if(d->l->damages.size()==d->depth+1)d->l->damages.removeLast();else d->l->damages.clear();if(d->l->damages.isEmpty())d->l->currentDamage.clear();}
struct AIRound50DamageTagWrite::Data {Ledger*l;int depth;};
AIRound50DamageTagWrite::AIRound50DamageTagWrite(Room&r,const QVariant&v,int kind):d(new Data{ledger(r),0}){
 d->depth=d->l->pending.size();bool allowed=false;
 if(v.userType()==qMetaTypeId<DamageStruct>()&&!d->l->damages.isEmpty()){
  if(kind==0)allowed=true;
  else if(kind==1){for(const auto&f:d->l->damages)for(const auto&old:f.publicValues)if(sameDamage(v,old))allowed=true;}
  else if(kind==2)allowed=sameDamage(v,d->l->damages.last().previous);
 }
 d->l->pending.append({&v,allowed});
}
AIRound50DamageTagWrite::~AIRound50DamageTagWrite(){if(d->l->pending.size()==d->depth+1)d->l->pending.removeLast();else{d->l->pending.clear();d->l->currentDamage.clear();}}
void observeAIRound50DamageTag(Room&r,const QString&key,const QVariant*value){
 if(key!="CurrentDamageStruct")return;auto*l=find(r);if(!l)return;l->currentDamage.clear();
 if(!value||l->pending.isEmpty()||l->damages.isEmpty())return;
 auto&pending=l->pending.last();const bool allowed=pending.allowed&&pending.value==value;pending.allowed=false;
 if(allowed){l->currentDamage=*value;l->damages.last().publicValues.append(*value);}
}
