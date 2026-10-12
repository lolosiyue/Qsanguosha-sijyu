#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round26-planning.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include "ai-strategy-card-identity.h"
#include "ai-strategy-public-disclosure.h"
#include "ai-strategy-choice-context.h"
#include <QJsonArray>
#include <QObject>
#include <QPointer>
namespace {
struct PublicUseWitness {QPointer<Card> card;QString face;QString from;QStringList targets;qint64 historyId=0;bool publicStage=false;};
class PublicUses final:public QObject {public:explicit PublicUses(Room&r):QObject(&r){}QList<PublicUseWitness> uses;};
PublicUses*ledger(Room&r,bool create){for(auto*c:r.children())if(auto*v=dynamic_cast<PublicUses*>(c))return v;return create?new PublicUses(r):nullptr;}
QString faceKey(const Card*c){return c->objectName()+"|"+c->getClassName()+"|"+QString::number(int(c->getSuit()))+"|"+QString::number(c->getNumber())+"|"+c->getSkillName(false)+"|"+c->toString();}
bool owner(const AIRequest&r,Room&room,const ServerPlayer*p){return p&&p->getRoom()==&room&&r.viewerObjectName==p->objectName()&&r.stateRevision==room.roomRuntime()->stateRevision();}
bool absent(const QVariant&v){return !v.isValid()||v.isNull();}
bool useQuestion(const AIRequest&r,const char*pattern){return r.kind==AIRequest::UseCard&&r.pattern==QLatin1String(pattern);}
bool caokongQuestion(const AIRequest&r){return useQuestion(r,"@@luacaokong")||(r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason=="luacaokong");}
bool faceMatches(const QJsonObject&v,const Card*c){return v.value("name").toString()==c->objectName()&&v.value("class_name").toString()==c->getClassName()&&v.value("suit").toInt(-1)==int(c->getSuit())&&v.value("number").toInt(-1)==c->getNumber();}
bool disclosed(const AIRequest&r,Room&room,const ServerPlayer*p,const Card*c){
 if(!c||c->isVirtualCard()||c->getEffectiveId()<0||Sanguosha->getCard(c->getEffectiveId())!=c)return false;
 const int id=c->getEffectiveId();auto*viewer=const_cast<ServerPlayer*>(p);
 // Real move/fillAG ledgers authenticate both current mapping and face. Mere
 // PlaceTable presence is not disclosure (mountainrumour.lua1039/1041).
 for(const auto&v:{readAIStrategyPublicDisclosure(room,viewer,id),readAIStrategyChoiceAGDisclosure(room,viewer,id)})
  if(v.value("known").toBool()&&faceMatches(v.value("card").toObject(),c))return true;
 const auto matches=[&](const AICardView&v){return v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,viewer,c);};
 for(const auto&v:r.worldView.handCards)if(matches(v)&&room.getCardOwner(id)==p)return true;
 auto players=r.worldView.players;players.prepend(r.worldView.self);
 for(const auto&v:players){for(const auto&x:v.knownCards)if(matches(x))return true;for(const auto&x:v.equips)if(matches(x))return true;for(const auto&x:v.judgingArea)if(matches(x))return true;for(const auto&pile:v.piles)for(const auto&x:pile.cards)if(matches(x))return true;}
 return false;
}
bool publicUse(const AIRequest&r,Room&room,const ServerPlayer*p,const Card*c){
 if(disclosed(r,room,p,c))return true;
 if(!c)return false;
 const auto*l=ledger(room,false);if(!l)return false;
 for(const auto&w:l->uses){
  if(w.card.data()!=c||w.face!=faceKey(c)||!w.targets.contains(p->objectName()))continue;
  if(w.historyId>0){const auto event=room.resolutionHistory().event(w.historyId);
   if(event.value("kind").toString()!="use_card"||event.value("status").toString()!="active")continue;
  }
  return true;
 }
 return false;
}
}
void clearAIRound26PlanningPublicUses(Room&room){if(auto*l=ledger(room,false))l->uses.clear();}
void recordAIRound26PlanningPublicUse(Room&room,int event,const QVariant&data){
 if(event==GameReady){clearAIRound26PlanningPublicUses(room);return;}
 if((event!=PreCardUsed&&event!=CardUsed&&event!=TargetSpecified&&event!=CardFinished)||!data.canConvert<CardUseStruct>())return;
 const auto use=data.value<CardUseStruct>();if(!use.card)return;
 auto*l=ledger(room,event==PreCardUsed);if(!l)return;
 // CardFinished is recorded after its callbacks. Those callbacks can reuse
 // this same canonical card recursively (fixed scarletayuhuo11802/15395).
 // Retain one frame per PreCardUsed and pop only its matching innermost use.
 for(auto it=l->uses.begin();it!=l->uses.end();)if(it->card.isNull())it=l->uses.erase(it);else ++it;
 if(event==CardUsed||event==TargetSpecified){
  for(qsizetype i=l->uses.size();i>0;--i){auto&w=l->uses[i-1];
   if(w.card.data()!=use.card||w.historyId!=use.targetModReveal.useHistoryEventId)continue;
   if(!use.from||!room.getAllPlayers(true).contains(use.from))return;
   QStringList targets;for(auto*p:use.to){if(!p||!room.getAllPlayers(true).contains(p))return;targets<<p->objectName();}
   w.face=faceKey(use.card);w.from=use.from->objectName();w.targets=targets;w.publicStage=true;return;
  }
  return;
 }
 if(event==CardFinished){
  for(qsizetype i=l->uses.size();i>0;--i){const auto&w=l->uses.at(i-1);
   if(w.card.data()==use.card&&(use.targetModReveal.useHistoryEventId<=0||w.historyId==use.targetModReveal.useHistoryEventId)){
    l->uses.removeAt(i-1);break;
   }
  }
  return;
 }
 if(event==PreCardUsed){PublicUseWitness w;w.card=const_cast<Card*>(use.card);w.face=faceKey(use.card);w.historyId=use.targetModReveal.useHistoryEventId;
  if(use.from&&room.getAllPlayers(true).contains(use.from))w.from=use.from->objectName();
  for(auto*p:use.to)if(p&&room.getAllPlayers(true).contains(p))w.targets<<p->objectName();l->uses<<w;
 }
}
bool aiRound26PlanningPublicUseWitness(const AIRequest&r,Room&room,const ServerPlayer*p,const CardUseStruct&use){
 if(!owner(r,room,p)||!use.card||!use.from||!room.getAllPlayers(true).contains(use.from))return false;
 QStringList targets;
 for(auto*t:use.to){if(!t||!room.getAllPlayers(true).contains(t))return false;targets<<t->objectName();}
 if(use.from!=p&&!targets.contains(p->objectName()))return false;
 const auto*l=ledger(room,false);if(!l)return false;
 for(auto it=l->uses.crbegin();it!=l->uses.crend();++it){const auto&w=*it;
  if(w.card.data()!=use.card||w.face!=faceKey(use.card)||w.from!=use.from->objectName()
      ||w.targets!=targets||w.historyId!=use.targetModReveal.useHistoryEventId)continue;
  if(w.historyId>0){const auto event=room.resolutionHistory().event(w.historyId);
   if(event.value("kind").toString()!="use_card"||event.value("status").toString()!="active")continue;
  }
  return true;
 }
 return false;
}
bool aiRound26PlanningPublicCardWitness(const AIRequest&r,Room&room,const ServerPlayer*p,const Card*card){
 if(!owner(r,room,p)||!card)return false;
 // Nonthrowing SkillCard logs may conceal their materials. Their public name
 // alone cannot authorize the full Card DTO used by this accessor.
 if(card->getTypeId()==Card::TypeSkill&&!card->willThrow())return false;
 const auto*l=ledger(room,false);if(!l)return false;
 for(auto it=l->uses.crbegin();it!=l->uses.crend();++it){const auto&w=*it;
  if(!w.publicStage||w.card.data()!=card||w.face!=faceKey(card))continue;
  if(w.historyId>0){const auto event=room.resolutionHistory().event(w.historyId);
   if(event.value("kind").toString()!="use_card"||event.value("status").toString()!="active")continue;
  }
  return true;
 }
 return false;
}
const Card*aiRound26PlanningEvaluation(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&key){
 if(key!="luacaokong_context"||!owner(r,room,p)||!caokongQuestion(r)||!p->hasSkill("luacaokong"))return nullptr;
 const auto v=room.getTag("caokong_carduse");if(absent(v)||!v.canConvert<const Card*>())return nullptr;
 const auto*c=v.value<const Card*>();return disclosed(r,room,p,c)?c:nullptr;
}
void projectAIRound26PlanningContext(AIRequest&r,Room&room,const ServerPlayer*p,const QVariant&,QJsonObject&context,const std::function<QJsonObject(const Card*)>&face){
 if(!owner(r,room,p))return;
 // makeRequest calls this after buildWorldView's identity scope has ended.
 // The actual public face must use THIS viewer's equality namespace too.
 AIIdentityProjectionScope identityScope(room,const_cast<ServerPlayer*>(p));
 auto row=context.value("mountainrumour").toObject();
 if(useQuestion(r,"@@luaxianhai")&&p->hasSkill("luaxianhai")){
  const auto v=room.getTag(p->objectName()+"xianhai_fromcard");
  if(absent(v))row.insert("xianhai_card",false);
  else if(v.canConvert<const Card*>()){const auto*c=v.value<const Card*>();if(!c)row.insert("xianhai_card",false);else if(publicUse(r,room,p,c)&&face)row.insert("xianhai_card",face(c));}
 }
 if(r.kind==AIRequest::Choice&&r.choiceOptions.reason=="luahuanying"){
  const auto target=room.getTag("huanying_target"),value=room.getTag("huanying_data");
  if(absent(target))row.insert("huanying_target",false);
  if(absent(value))row.insert("huanying_damage",false);
  if(target.canConvert<ServerPlayer*>()&&value.canConvert<DamageStruct>()){
   auto*t=target.value<ServerPlayer*>();const auto damage=value.value<DamageStruct>();
   // Source1182-1185 asks the damage SOURCE, not the hidden-pile owner.
   // No selected huanyingpile id/face is disclosed before source1194.
   if(t&&room.getAllPlayers(true).contains(t)&&damage.from==p&&damage.to==t&&t->hasSkill("luahuanying")){
    row.insert("huanying_target",t->objectName());row.insert("huanying_damage",QJsonObject{{"damage",damage.damage}});
   }
  }
 }
 if(([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("luacaokong"); }())&&(caokongQuestion(r)||(r.kind==AIRequest::PlayerChosen&&r.choiceOptions.reason=="luacaokong"))){
  const auto v=room.getTag("caokong_tos");if(absent(v))row.insert("caokong_targets",false);
  else if(v.canConvert<CardUseStruct>()){
   const auto use=v.value<CardUseStruct>();QJsonArray names;bool valid=true;
   for(auto*t:use.to){if(!t||!room.getAllPlayers(true).contains(t)){valid=false;break;}names.append(t->objectName());}
   if(valid)row.insert("caokong_targets",QJsonObject{{"to",names}});
  }
  if(caokongQuestion(r)){
   const auto v=room.getTag("caokong_carduse");if(absent(v))row.insert("caokong_card",false);
   else if(const auto*c=aiRound26PlanningEvaluation(r,room,p,"luacaokong_context")){if(face)row.insert("caokong_card",face(c));}
  }
 }
 if(useQuestion(r,"@@shisheng")&&p->hasSkill("shisheng")){
  const auto v=p->property("shisheng_sel");if(v.metaType().id()==QMetaType::QString)row.insert("shisheng_sel",v.toString());
 }
 if(!row.isEmpty()){context.insert("mountainrumour",row);r.worldView.decisionContext.insert("mountainrumour",row);}
}
