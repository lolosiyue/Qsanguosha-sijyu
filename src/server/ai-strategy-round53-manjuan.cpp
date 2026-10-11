#include "ai-strategy-round53-manjuan.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua-runtime.h"
#include <cmath>
#include "ai-strategy-round45-inovation-providers.h"
#include "card-lifetime-manager.h"
#include "lua-wrapper.h"
#include "ai-strategy-round45-lua-inspect.h"
namespace {
QJsonObject no(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&aiRound44RangeRequestCurrent(r,q,p)&&Sanguosha&&Sanguosha->currentRoom()==&r;}
bool index(const QJsonValue&v,int&n){double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d<0||d>2147483647.0||d!=std::floor(d))return false;n=int(d);return true;}
bool publicId(Room&r,const AIRequest&q,ServerPlayer*p,int id){
 if(r.getCardPlace(id)!=Player::DiscardPile)return false;
 const Card*live=Sanguosha->getCard(id);if(!live)return false;
 const QString token=aiLegacyCardIdentity(r,p,live);
 for(const auto&v:q.worldView.discardPile)if(v.effectiveId==id&&!v.virtualCard&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==token)return true;
 return false;
}
}
QJsonObject queryAIRound53ManjuanPrinted(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&in){
 if(!current(r,q,p)||!p->hasSkill("kezhuanmanjuan",true)||!current(r,q,p))return no("Manjuan printed prefix needs actual current owner Play source");
 if(op=="printed_count"){if(!in.isEmpty())return no("Manjuan printed count takes no arguments");return{{"known",true},{"value",Sanguosha->getCardCount()}};}
 int id;if(!index(in.value("id"),id)||id>=Sanguosha->getCardCount())return no("Manjuan printed source index outside actual catalogue");
 // getEngineCard itself is reference acquisition. Do not read or project a face
 // before the source own-mark branch, and do not replace it by the live wrapper.
 const Card*c=Sanguosha->getEngineCard(id);
 if(op=="printed_step"){
  if(in.size()!=1)return no("Manjuan printed reference shape changed");
  return{{"known",true},{"exists",c!=nullptr},{"identity",c?aiLegacyCardIdentity(r,p,c):QString()}};
 }
 const bool available=op=="printed_available";
 if((op!="printed_number"&&!available)||in.size()!=(available?3:2)||!in.value("identity").isString())return no("Manjuan printed getter is outside finite source");
 if(available&&in.value("player")!=p->objectName())return no("Manjuan printed availability cannot query foreign private rules");
 if(!publicId(r,q,p,id)||!c||in.value("identity")!=aiLegacyCardIdentity(r,p,c))return no("Manjuan marked printed input lacks current public discard identity");
 QJsonValue v;
 if(available){auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);if(!vm.state())return no("Manjuan printed availability binding unavailable");v=c->isAvailable(p);}
 else v=c->getNumber();
 if(!current(r,q,p)||!publicId(r,q,p,id)||Sanguosha->getEngineCard(id)!=c)return no("Manjuan printed source changed during query");
 return{{"known",true},{"value",v}};
}

// R53: the fixed source returns a raw LuaSkillCard wire, whose inferred skill
// is kezhuanmanjuanvs. It is not the GUI ViewAsSkillV2 kezhuanmanjuan product.
namespace {
bool manjuanResponseCurrent(const AIRequest&q,ServerPlayer*p){
 return p&&p->getRoom()&&p->getRoom()->getAllPlayers(true).contains(p)
  &&p->getRoom()->roomRuntime()&&!p->getRoom()->roomRuntime()->isClosing()
  &&q.decisionId&&q.kind==AIRequest::RespondCard
  &&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE&&q.handlingMethod==Card::MethodResponse
  &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
  &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom()
  &&aiRound44RangeRequestCurrent(*p->getRoom(),q,p);
}
bool manjuanActualIndex(const AIRequest&q,ServerPlayer*p,int&id){
 auto&r=*p->getRoom();auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();
 if(!L||!lua_checkstack(L,4096))return false;int top=lua_gettop(L);struct Restore{lua_State*L;int n;~Restore(){lua_settop(L,n);}}restore{L,top};
 if(!pushAIRound45ActualAISelf(r,p,L))return false;
 // Fixed writer always creates this own raw field. No metatable evaluation or
 // inherited/global guess grants a selection; missing is a refusal.
 AIRound45Inspect::field(L,-1,"kezhuanmanjuan_id");if(lua_type(L,-1)!=LUA_TNUMBER)return false;
 double n=lua_tonumber(L,-1);if(!std::isfinite(n)||n<0||n>2147483647.0||n!=std::floor(n))return false;
 id=int(n);return manjuanResponseCurrent(q,p);
}
bool manjuanSelection(const AIRequest&q,ServerPlayer*p,const QString&declaration,int&id){
 if(!manjuanResponseCurrent(q,p)||!p->isKongcheng()||!p->hasSkill("kezhuanmanjuan",true)
  ||!manjuanResponseCurrent(q,p)||!manjuanActualIndex(q,p,id))return false;
 auto&r=*p->getRoom();if(r.getCardPlace(id)!=Player::DiscardPile)return false;
 const Card*c=Sanguosha->getCard(id);if(!c)return false;bool seen=false;
 for(const auto&v:q.worldView.discardPile)if(v.effectiveId==id&&v.nativeIdentity
  &&globalCardLifetimeManager().isLive(v.nativeIdentity)&&v.nativeIdentity->address==c
  &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())
  &&v.number==c->getNumber()&&v.skillName==c->getSkillName(false)&&v.subcardIds==c->getSubcards()
  &&v.virtualCard==c->isVirtualCard()){seen=true;break;}
 if(!seen||declaration!=c->objectName()||declaration.isEmpty()
  ||p->getMark(QString::number(id)+"manjuanPile-Clear")<1
  ||p->getMark(QString::number(c->getNumber())+"manjuanNumber-Clear")>0)return false;
 // The ordinary cardsview selected this disclosed card. The original native
 // validator, not this receipt, still performs live locked/pattern/AG checks.
 return manjuanResponseCurrent(q,p);
}
bool rawManjuan(const Card*c,const QString&declaration,ServerPlayer*p){
 const auto*lua=dynamic_cast<const LuaSkillCard*>(c);
 if(!lua||c->objectName()!="kezhuanmanjuanVsCard"||c->getClassName()!="LuaSkillCard"
  ||c->rawSkillName()!="kezhuanmanjuanvs"||c->getActivationSkillName()!="kezhuanmanjuanvs"
  ||c->getSkillInstanceId()!=0||c->getActivationSkillInstanceId()!=0||c->getSourceSkillInstanceId()!=0
  ||!c->getSubcards().isEmpty()||lua->getUserString()!=declaration
  ||Sanguosha->getViewAsSkill("kezhuanmanjuanvs"))return false;
 // Preserve the original resolver's ordinary no-provider early return; do not
 // enter private attached-instance resolution under an inferred alias.
 for(const auto&i:p->getSkillInstances())if(i.skillName=="kezhuanmanjuanvs")return false;
 return true;
}
bool sameManjuanCallback(const LuaFunction&a,const LuaFunction&b){
 if(a.isValid()!=b.isValid())return false;
 if(a.isValid()&&(!a.state()||!b.state()))return false;
 return a.state()==b.state()&&a.generation()==b.generation()&&a.registryRef()==b.registryRef();
}
bool sameRawManjuan(const Card*a,const Card*b){
 const auto*x=dynamic_cast<const LuaSkillCard*>(a);const auto*y=dynamic_cast<const LuaSkillCard*>(b);if(!x||!y)return false;
 return a->toString()==b->toString()&&a->getSuit()==b->getSuit()&&a->getNumber()==b->getNumber()
  &&a->getFlags()==b->getFlags()&&a->getHandlingMethod()==b->getHandlingMethod()
  &&a->willThrow()==b->willThrow()&&a->canRecast()==b->canRecast()&&a->targetFixed()==b->targetFixed()
  &&sameManjuanCallback(x->filter,y->filter)&&sameManjuanCallback(x->feasible,y->feasible)&&sameManjuanCallback(x->about_to_use,y->about_to_use)
  &&sameManjuanCallback(x->on_use,y->on_use)&&sameManjuanCallback(x->on_effect,y->on_effect)&&sameManjuanCallback(x->on_validate,y->on_validate)
  &&sameManjuanCallback(x->on_validate_in_response,y->on_validate_in_response);
}
}
const Card*constructAIRound53ManjuanRaw(const AIRequest&q,ServerPlayer*p,const QString&declaration,int*selected){
 int id;if(!manjuanSelection(q,p,declaration,id))return nullptr;
 const Card*c=Card::Parse("#kezhuanmanjuanVsCard:.:"+declaration);
 if(!rawManjuan(c,declaration,p)||!manjuanResponseCurrent(q,p))return nullptr;
 if(selected)*selected=id;return c;
}
bool hasAIRound53ManjuanRaw(const AIRequest&q,int id){
 if(!q.conversionLedger)return false;for(const auto&t:q.conversionLedger->tickets)if(t.conversionId==id&&t.manjuanRawSource)return true;return false;
}
bool applyAIRound53ManjuanRaw(const AIRequest&q,ServerPlayer*p,const AICardSpec&s,const QStringList&targets,
 const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!manjuanResponseCurrent(q,p)||!q.conversionLedger||!targets.isEmpty()||!admit)return false;
 for(const auto&t:q.conversionLedger->tickets)if(t.conversionId==s.conversionId&&t.manjuanRawSource){
  if(s.name!=t.name||s.skillName!=t.activationRef.key.skillName||s.suit!=t.suit||s.number!=t.number||s.subcardIds!=t.subcardIds
   ||!s.subcardIds.isEmpty()||t.activationRef.ownerObjectName!=p->objectName()
   ||t.activationRef.key.skillName!="kezhuanmanjuan"||!p->hasSkillInstance(t.activationRef.key.skillName,t.activationRef.key.instanceID))return false;
  int id;if(!manjuanSelection(q,p,t.declaredName,id)||id!=t.manjuanRawSelectedId)return false;
  const auto lease=q.conversionLedger->observedProducts.value(t.conversionId);const Card*original=lease?lease():nullptr;
  if(!original||!rawManjuan(original,t.declaredName,p))return false;
  const Card*parsed=Card::Parse("#kezhuanmanjuanVsCard:.:"+t.declaredName);
  if(!rawManjuan(parsed,t.declaredName,p)||!sameRawManjuan(original,parsed)||!manjuanResponseCurrent(q,p))return false;
  CardUseStruct actual(parsed,p);
  if(!admit(actual)||actual.card!=parsed||actual.activationRef.isValid()||actual.sourceRef.isValid()
   ||actual.hasSkillActivationRequest||!manjuanResponseCurrent(q,p))return false;
  int after;if(!manjuanSelection(q,p,t.declaredName,after)||after!=id)return false;
  actual.m_validateTargets=true;use=actual;return true;
 }
 return false;
}
