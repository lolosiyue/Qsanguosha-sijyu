#include "ai-strategy-round28-planning.h"
#include "ai-strategy-round44-fc-frame.h"
#include "ai-strategy-round26-planning.h"
#include "ai-strategy-card-identity.h"
#include "ai-strategy-public-disclosure.h"
#include "ai-strategy-choice-context.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "standard.h"
#include "structs.h"
#include <QJsonArray>
#include <QSet>
#include <memory>
namespace {
bool owner(const AIRequest&r,Room&room,const ServerPlayer*p){return p&&p->getRoom()==&room&&r.viewerObjectName==p->objectName()&&r.stateRevision==room.roomRuntime()->stateRevision();}
bool absent(const QVariant&v){return !v.isValid()||v.isNull();}
bool ask(const AIRequest&r,AIRequest::DecisionKind kind,const QString&key){return r.kind==kind&&r.choiceOptions.reason==key;}
bool useAsk(const AIRequest&r,const QString&pattern){return r.kind==AIRequest::UseCard&&r.pattern==pattern;}
QString tagKey(const QString&key){return key.startsWith("fcDIY_context:")?key.mid(14):QString();}
bool extraQuestion(const AIRequest&r,const ServerPlayer*p,const QString&key){
 if(!p||!p->hasSkill(key))return false;
 if(key=="sp_hengsaoqianjun")return ask(r,AIRequest::Choice,key)||ask(r,AIRequest::PlayersChosen,key);
 if(key=="sp_lvezhen"||key=="fcjielianhuan")return ask(r,AIRequest::PlayerChosen,key);
 return key=="fcmoujiang"&&ask(r,AIRequest::PlayersChosen,key);
}
bool validUse(Room&room,const CardUseStruct&use){if(!use.card||!use.from||!room.getAllPlayers(true).contains(use.from))return false;for(auto*t:use.to)if(!t||!room.getAllPlayers(true).contains(t))return false;return true;}
QJsonArray names(const QList<ServerPlayer*>&players){QJsonArray a;for(auto*p:players)a.append(p->objectName());return a;}
bool faceMatches(const QJsonObject&v,const Card*c){return c&&v.value("name").toString()==c->objectName()&&v.value("class_name").toString()==c->getClassName()&&v.value("suit").toInt(-1)==int(c->getSuit())&&v.value("number").toInt(-1)==c->getNumber();}
bool disclosed(const AIRequest&r,Room&room,const ServerPlayer*p,const Card*c){
 if(!c||c->isVirtualCard()||c->getEffectiveId()<0)return false;
 const Card*outer=Sanguosha->getCard(c->getEffectiveId());
 if(!outer||(outer!=c&&outer->getRealCard()!=c))return false;
 auto*v=const_cast<ServerPlayer*>(p);const int id=c->getEffectiveId();
 for(const auto&row:{readAIStrategyPublicDisclosure(room,v,id),readAIStrategyChoiceAGDisclosure(room,v,id)})if(row.value("known").toBool()&&faceMatches(row.value("card").toObject(),c))return true;
 auto same=[&](const AICardView&x){return x.effectiveId==id&&x.objectName==c->objectName()&&x.className==c->getClassName()&&x.suit==int(c->getSuit())&&x.number==c->getNumber()&&!x.legacyIdentityToken.isEmpty()&&(x.legacyIdentityToken==aiLegacyCardIdentity(room,v,c)||x.legacyIdentityToken==aiLegacyCardIdentity(room,v,outer));};
 for(const auto&x:r.worldView.handCards)if(same(x)&&room.getCardOwner(id)==p)return true;
 auto players=r.worldView.players;players.prepend(r.worldView.self);
 for(const auto&x:players){for(const auto&c:x.knownCards)if(same(c))return true;for(const auto&c:x.equips)if(same(c))return true;for(const auto&c:x.judgingArea)if(same(c))return true;for(const auto&pile:x.piles)for(const auto&c:pile.cards)if(same(c))return true;}
 return false;
}
void playerTag(QJsonObject&row,const QString&key,const QVariant&value,Room&room){
 if(absent(value)){row.insert(key,false);return;}
 if(!value.canConvert<ServerPlayer*>())return;auto*p=value.value<ServerPlayer*>();
 if(!p)row.insert(key,false);else if(room.getAllPlayers(true).contains(p))row.insert(key,p->objectName());
}
QJsonObject useRow(const CardUseStruct&use,const QJsonObject&card){return {{"from",use.from->objectName()},{"to",names(use.to)},{"card",card}};}
}
bool aiRound28PlanningConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&spec){
 if(!owner(r,room,p)||!useAsk(r,"@@ahmoutaozei_tm")||!p->hasSkill("ahmoutaozei")||spec.size()!=5)return false;
 const auto property=p->property("ahmoutaozei_tm");if(property.metaType().id()!=QMetaType::QString)return false;
 const QString name=spec.value("name").toString();
 if(name.isEmpty()||name!=property.toString()||name=="jink"||name=="nullification"||name=="jl_wuxiesy"
  ||spec.value("skill").toString()!="ahmoutaozei"||spec.value("suit").toInt(-1)!=int(Card::NoSuit)
  ||!spec.value("number").isDouble()||spec.value("number").toDouble()!=0
  ||!spec.value("subcards").isArray()||!spec.value("subcards").toArray().isEmpty())return false;
 std::unique_ptr<Card> card(Sanguosha->cloneCard(name,Card::NoSuit,0));
 return card&&(card->isKindOf("BasicCard")||card->isNDTrick());
}
const Card*aiRound28PlanningEvaluation(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&source){
 const QString key=tagKey(source);if(!owner(r,room,p)||!extraQuestion(r,p,key))return nullptr;
 const auto value=room.getTag(key);if(!value.canConvert<CardUseStruct>())return nullptr;const auto use=value.value<CardUseStruct>();
 if(!validUse(room,use))return nullptr;
 const bool foreignDuel=key=="fcmoujiang"&&use.from!=p&&use.card->isKindOf("Duel");
 if(foreignDuel){if(!aiRound26PlanningPublicCardWitness(r,room,p,use.card))return nullptr;}
 else {
  const int frame=aiRound44FCUseFrameWitness(r,room,p,use);
  if(use.from!=p||frame<0||(frame==0&&!aiRound26PlanningPublicUseWitness(r,room,p,use)))return nullptr;
 }
 const auto*c=use.card;
 if(key=="sp_hengsaoqianjun"&&(!c->isKindOf("Slash")||c->getSkillName()!="sp_hengsaoqianjun"))return nullptr;
 if(key=="sp_lvezhen"&&(!c->isKindOf("Dismantlement")||!c->getSkillNames().contains(key)))return nullptr;
 if(key=="fcjielianhuan"&&(!c->isKindOf("IronChain")||!c->getSkillNames().contains(key)))return nullptr;
 if(key=="fcmoujiang"&&!(c->isKindOf("Duel")||(c->isKindOf("Slash")&&c->isRed())))return nullptr;
 return c;
}
int aiRound28PlanningExtraTarget(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&source){
 if(!aiRound28PlanningEvaluation(r,room,p,source))return -1;const auto key=tagKey(source);
 return key=="sp_lvezhen"?1:key=="fcmoujiang"?qMin(p->getHp()-1,2):99;
}
QJsonObject aiRound28PlanningTargetFilter(Room&room,ServerPlayer*p,const AIRequest&r,const Card*c,const QString&key,const QJsonObject&q){
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","fcDIY.extra_targets"}};};
 if(!c||c!=aiRound28PlanningEvaluation(r,room,p,key))return fail("current authenticated FC use differs");
 const int extra=aiRound28PlanningExtraTarget(r,room,p,key);
 if(q.size()!=2||!q.value("prefix").isArray()||!q.value("target").isString())return fail("malformed FC prefix query");
 QList<const Player*>prefix;QSet<QString>seen;const auto a=q.value("prefix").toArray();if(a.size()>room.getAlivePlayers().size())return fail("FC prefix exceeds roster");
 for(const auto&v:a){if(!v.isString()||seen.contains(v.toString()))return fail("FC prefix duplicates or malformed name");auto*t=room.findPlayerByObjectName(v.toString());if(!t||!t->isAlive())return fail("FC prefix target absent");seen.insert(v.toString());prefix<<t;}
 auto*t=room.findPlayerByObjectName(q.value("target").toString());if(!t||!t->isAlive())return fail("FC target absent");
 const auto revision=room.roomRuntime()->stateRevision();AITianyiQueryScope scope(p,c,extra,false);const bool accepted=c->targetFilter(prefix,t,p);
 if(revision!=room.roomRuntime()->stateRevision())return fail("FC readonly query changed state");return {{"accepted",accepted}};
}
void projectAIRound28PlanningContext(AIRequest&r,Room&room,const ServerPlayer*p,const QVariant&,QJsonObject&context,
 const std::function<QJsonObject(const Card*)>&face,const AIRound28PlanningProfile&profile){
 if(!owner(r,room,p))return;bindAIRound44FCUseFrame(r,room,p);AIIdentityProjectionScope identity(room,const_cast<ServerPlayer*>(p));
 auto row=context.value("fcDIY").toObject();const auto reason=r.choiceOptions.reason;
 // All setters of this one property use Room::setPlayerProperty, whose native
 // PlayerStateService247-248 broadcasts the value, including empty resets.
 auto facts=r.worldView.strategyFacts.value("fcDIY").toObject();auto props=facts.value("player_properties").toObject();
 for(auto*t:room.getAllPlayers(true)){auto q=props.value(t->objectName()).toObject();q.insert("fcmouliegongRecords",t->property("fcmouliegongRecords").toString());props.insert(t->objectName(),q);}
 if(useAsk(r,"@@ahmoutaozei_tm")&&p->hasSkill("ahmoutaozei")){auto q=props.value(p->objectName()).toObject();q.insert("ahmoutaozei_tm",p->property("ahmoutaozei_tm").toString());props.insert(p->objectName(),q);}
 facts.insert("player_properties",props);r.worldView.strategyFacts.insert("fcDIY",facts);
 if(ask(r,AIRequest::PlayerChosen,"sp_kongju")&&p->hasSkill("sp_kongju")){
  const auto value=room.getTag("sp_kongju_choose");if(absent(value)||value.metaType().id()==QMetaType::QString)row.insert("sp_kongju_choose",value.toString());
  playerTag(row,"sp_kongjuOneTarget",room.getTag("sp_kongjuOneTarget"),room);
 }
 if(ask(r,AIRequest::SkillInvoke,"f_kaigong")&&p->hasSkill("f_kaigong"))playerTag(row,"f_kaigong",room.getTag("f_kaigong"),room);
 if(ask(r,AIRequest::AmazingGrace,"fcgongxin")){
  // Fixed native fcDIY5238 omits doGongxin's skill argument, so its real AG
  // reason/tag are gongxin. Do not alias that independent question. If this
  // named callback is explicitly asked, retain the source's own missing tag.
  playerTag(row,"fcgongxin",p->getTag("fcgongxin"),room);
 }
 if(useAsk(r,"@@ahmoutaozei")&&p->hasSkill("ahmoutaozei")){
  const auto value=room.getTag("ahmoutaozei");if(absent(value))row.insert("ahmoutaozei",false);
  else if(value.canConvert<CardUseStruct>()){const auto use=value.value<CardUseStruct>();if(!use.from)row.insert("ahmoutaozei",QJsonObject{{"from",false},{"to",QJsonArray{}}});else if(room.getAllPlayers(true).contains(use.from))row.insert("ahmoutaozei",QJsonObject{{"from",use.from->objectName()},{"to",QJsonArray{}}});}
 }
 for(const QString&key:{QStringLiteral("sp_hengsaoqianjun"),QStringLiteral("sp_lvezhen"),QStringLiteral("fcjielianhuan"),QStringLiteral("fcmoujiang")})if(extraQuestion(r,p,key)){
  const auto value=room.getTag(key);if(absent(value)){row.insert(key,false);continue;}
  const QString source="fcDIY_context:"+key;const Card*c=aiRound28PlanningEvaluation(r,room,p,source);if(!c||!profile)continue;
  const auto use=value.value<CardUseStruct>();const auto card=profile(c,aiRound28PlanningExtraTarget(r,room,p,source),source);if(card.isEmpty())continue;
  row.insert(key,useRow(use,card));
  // Invoke the real native method: order, vote-output predicate, Slash fallback
  // and exclusion semantics are all part of the source contract (Room9062).
  if(key=="sp_hengsaoqianjun"&&r.kind==AIRequest::Choice)row.insert("sp_hengsaoqianjun_extra_targets",names(room.getCardTargets(const_cast<ServerPlayer*>(p),c,use.to)));
 }
 if(ask(r,AIRequest::PlayerChosen,"fcbyajiaoequip")&&p->hasSkill("fcbyajiao")){
  const auto value=p->getTag("fcbyajiaoCard");if(absent(value))row.insert("fcbyajiaoCard",false);
  else if(value.canConvert<const Card*>()){const Card*c=value.value<const Card*>();if(!c)row.insert("fcbyajiaoCard",false);else if(c->isKindOf("EquipCard")&&disclosed(r,room,p,c)&&face){auto card=face(c);const auto*equip=qobject_cast<const EquipCard*>(c->getRealCard());if(equip){card.insert("equip_slot",equip->location());row.insert("fcbyajiaoCard",card);}}}
 }
 const bool kangkai=ask(r,AIRequest::SkillInvoke,"fcj_kangkai")&&p->hasSkill("fcj_kangkai");
 const bool kangkaiUse=ask(r,AIRequest::SkillInvoke,"fcj_kangkai_use");
 if(kangkai||kangkaiUse){
  const auto value=p->getTag("fcj_kangkaiSlash");if(absent(value))row.insert("fcj_kangkaiSlash",false);
  else if(value.canConvert<CardUseStruct>()){const auto use=value.value<CardUseStruct>();if(!use.card)row.insert("fcj_kangkaiSlash",false);else if(validUse(room,use)&&face&&(disclosed(r,room,p,use.card)||aiRound26PlanningPublicCardWitness(r,room,p,use.card)))row.insert("fcj_kangkaiSlash",useRow(use,face(use.card)));}
  if(kangkaiUse){const auto value=p->getTag("fcj_kangkaiGivenCard");if(absent(value))row.insert("fcj_kangkaiGivenCard",false);else if(value.canConvert<const Card*>()){const Card*c=value.value<const Card*>();if(!c)row.insert("fcj_kangkaiGivenCard",false);else if(room.getCardOwner(c->getEffectiveId())==p&&disclosed(r,room,p,c)&&face)row.insert("fcj_kangkaiGivenCard",face(c));}}
 }
 if(useAsk(r,"@@sp_fenying")&&p->hasSkill("sp_fenying")){
  const auto value=room.getTag("sp_fenying");if(absent(value))row.insert("sp_fenying",false);
  else if(value.canConvert<DamageStruct>()){const auto d=value.value<DamageStruct>();const auto roster=room.getAllPlayers(true);
   if((!d.from||roster.contains(d.from))&&(!d.to||roster.contains(d.to))&&(!d.card||disclosed(r,room,p,d.card)||aiRound26PlanningPublicCardWitness(r,room,p,d.card))){
    QJsonObject damage{{"from",d.from?QJsonValue(d.from->objectName()):QJsonValue(false)},{"to",d.to?QJsonValue(d.to->objectName()):QJsonValue(false)},{"damage",d.damage},{"nature",int(d.nature)},{"chain",d.chain},{"transfer",d.transfer},{"by_user",d.by_user}};
    damage.insert("card",d.card&&face?QJsonValue(face(d.card)):QJsonValue(false));row.insert("sp_fenying",damage);
   }
  }
 }
 if(!row.isEmpty()){context.insert("fcDIY",row);r.worldView.decisionContext.insert("fcDIY",row);}
}
