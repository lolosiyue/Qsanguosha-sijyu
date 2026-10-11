#include "ai-strategy-round45-choice.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "ai-strategy-round23-olclan-planning.h"
#include "ai-strategy-round27-choice.h"
#include "ai-strategy-round28-choice.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QSet>
#include <cmath>
#include <limits>
#include "skill.h"
namespace {
bool danlveSkill(const QString&s){return s=="mk_danlve_yinyue"||s=="mk_danlve_qinggang";}
QJsonObject physicalLayer(const Card*c){
 QJsonArray costs;for(int id:c->getSubcards())costs.append(id);
 return {{"id",c->getId()},{"effective_id",c->getEffectiveId()},{"virtual",c->isVirtualCard()},
  {"name",c->objectName()},{"class",c->getClassName()},{"suit",int(c->getSuit())},{"number",c->getNumber()},
  {"raw_skill",c->rawSkillName()},{"skill_instance",c->getSkillInstanceId()},
  {"activation",c->getActivationSkillName()},{"activation_id",c->getActivationSkillInstanceId()},
  {"source",c->getSourceSkillName()},{"source_id",c->getSourceSkillInstanceId()},
  {"subcards",costs},{"flags",QJsonArray::fromStringList(c->getFlags())},
  {"skill_names",QJsonArray::fromStringList(c->getSkillNames(false))},
  {"can_recast",c->canRecast()},{"handling",int(c->getHandlingMethod())}};
}
QJsonObject physicalStamp(const AIRequest&r,Room&room,ServerPlayer*p,const Card*c,const QString&skill){
 return {{"decision",QString::number(r.decisionId)},{"revision",QString::number(r.stateRevision)},
  {"viewer",r.viewerObjectName},{"kind",int(r.kind)},{"reason",int(r.reason)},{"pattern",r.pattern},
  {"skill",skill},{"identity",aiLegacyCardIdentity(room,p,c)},
  {"inner_identity",aiLegacyCardIdentity(room,p,c->getRealCard())},
  {"outer_state",physicalLayer(c)},{"inner_state",physicalLayer(c->getRealCard())},
  {"name",c->objectName()},{"class",c->getClassName()},{"suit",int(c->getSuit())},{"number",c->getNumber()},
  {"activation",c->getActivationSkillName()},{"activation_id",c->getActivationSkillInstanceId()},
  {"source",c->getSourceSkillName()},{"source_id",c->getSourceSkillInstanceId()}};
}
const Card* currentDanlveWrite(Room&room,const AIRequest&r,ServerPlayer*p,int id){
 if(!p||p->getRoom()!=&room||!r.conversionLedger||r.viewerObjectName!=p->objectName()
  ||r.worldView.self.objectName!=p->objectName()||!aiRound44RangeRequestCurrent(room,r,p))return nullptr;
 const auto record=r.conversionLedger->round45PhysicalWrites.value(id);
 const QString skill=record.value("skill").toString();
 if(!danlveSkill(skill)||!p->hasSkill(skill)||room.getCardOwner(id)!=p
  ||room.getCardPlace(id)!=Player::PlaceSpecial||!p->getPile(skill).contains(id))return nullptr;
 const Card*c=Sanguosha->getCard(id);
 if(!c||c->isVirtualCard()||c->getEffectiveId()!=id||c->getSkillName(false)!=skill
  ||c->getSubcards()!=QList<int>{id}||record!=physicalStamp(r,room,p,c,skill))return nullptr;
 return c;
}
}
bool authorizeAIRound45DanlvePhysical(Room&room,const AIRequest&r,ServerPlayer*p,int id){
 if(r.worldView.strategyFacts.contains("readonly_play_context"))return false;
 if(r.kind!=AIRequest::Activate&&r.kind!=AIRequest::UseCard&&r.kind!=AIRequest::RespondCard)return false;
 if(r.kind==AIRequest::RespondCard){
  const auto q=r.choiceOptions.question;
  if(q!="askForCard"&&q!="askForNullification"&&q!="askForSinglePeach")return false;
 }
 return currentDanlveWrite(room,r,p,id)!=nullptr;
}
const Card* attachAIRound45DanlvePhysical(Room&room,const AIRequest&r,ServerPlayer*p,
 const QJsonObject&scope,const QString&registry,const QString&hook,int id,QString*error){
 const auto fail=[&](const char*s)->const Card*{if(error)*error=QString::fromLatin1(s);return nullptr;};
 const Card*physical=currentDanlveWrite(room,r,p,id);
 const QString skill=scope.value("skill_name").toString();
 if(!physical||registry!="ai_cardsview"||!danlveSkill(skill)||hook!=skill
  ||physical->getSkillName(false)!=skill||scope.value("version").toInt()!=2
  ||!scope.value("has_action").toBool()||scope.value("play").toBool()||scope.value("skill_action").toBool())
  return fail("Danlve physical cardsView attachment lacks its reached native write/source");
 bool issued=false;const auto scopes=r.conversionPolicy.value("scopes").toObject();
 for(auto i=scopes.begin();i!=scopes.end();++i)if(i.value().toObject()==scope){issued=true;break;}
 const auto v=scope.value("identity").toObject();
 const SkillInstanceRef activation(v.value("activation_owner").toString(),SkillInstanceKey(v.value("activation_skill").toString(),v.value("activation_instance").toInt(-1)));
 const SkillInstanceRef source(v.value("source_owner").toString(),SkillInstanceKey(v.value("source_skill").toString(),v.value("source_instance").toInt(-1)));
 if(!issued||!activation.isValid()||!source.isValid()||activation.ownerObjectName!=p->objectName()
  ||activation.key.skillName!=skill||!p->hasSkillInstance(skill,activation.key.instanceID)
  ||room.resolveSkillInstanceRootRef(activation)!=source
  ||!dynamic_cast<const ViewAsSkillV2*>(Sanguosha->getViewAsSkill(skill)))return fail("Danlve issued attachment identity changed");
 // Fixed SmartAI::insertCardsViewResult writes all three on the same Card.
 Card*c=const_cast<Card*>(physical);c->setSkillName(activation.key.skillName);
 c->setActivationSkill(activation.key.skillName,activation.key.instanceID);
 c->setSourceSkill(source.key.skillName,source.key.instanceID);
 r.conversionLedger->round45PhysicalWrites[id]=physicalStamp(r,room,p,c,skill);
 return c;
}

const Card *writeAIRound45PhysicalSkill(Room &room,const AIRequest &r,ServerPlayer *p,const QJsonObject &s,QString *error) {
 const auto fail=[&](const char *why)->const Card*{if(error)*error=QString::fromLatin1(why);return nullptr;};
 static const QSet<QString> keys{"kind","skill","id","identity","name","class_name","suit","number","previous_skill"};
 if(s.size()!=keys.size())return fail("RushB mutation shape differs");
 for(auto i=s.begin();i!=s.end();++i)if(!keys.contains(i.key()))return fail("RushB mutation extra field");
 if((s.value("kind")!="rushb_physical_skill"&&s.value("kind")!="rushb_physical_skill_read")||!p||p->getRoom()!=&room||!r.decisionId||!r.conversionLedger
    ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
    ||!aiRound44RangeRequestCurrent(room,r,p))return fail("RushB mutation requires current owner request");
 const bool ordinary=r.kind==AIRequest::Activate||r.kind==AIRequest::UseCard
    ||r.kind==AIRequest::RespondCard;
 const bool nested=r.worldView.strategyFacts.contains("readonly_play_context")
    &&(aiRound23OlClanPlanningQuestion(r,room,p)||aiRound27ChoicePlanningQuestion(r,room,p)||aiRound28ChoicePlanningQuestion(r,room,p));
 if(!ordinary&&!nested)return fail("RushB mutation inquiry is outside planning/response family");
 // Guhuo/cardsview may inspect another class while planning this inquiry; its
 // requested class is not a payment pattern and must not be used as one.
 const QString skill=s.value("skill").toString();
 const bool he=skill=="diy_m_jichou"||skill=="diy_m_jilun";
 if(!he&&skill!="mk_danlve_yinyue"&&skill!="mk_danlve_qinggang")return fail("RushB mutation skill is outside fixed family");
 if(!p->hasSkill(skill))return fail("RushB mutation source skill absent");
 const double number=s.value("id").toDouble(-1);
 if(!s.value("id").isDouble()||!std::isfinite(number)||number<0||number!=std::floor(number)||number>std::numeric_limits<int>::max())return fail("RushB physical ID invalid");
 const int id=int(number);Card *card=Sanguosha->getCard(id);
 if(!card||card->isVirtualCard()||card->getEffectiveId()!=id||room.getCardOwner(id)!=p)return fail("RushB physical object no longer owned");
 const auto place=room.getCardPlace(id);
 if(he){if(place!=Player::PlaceHand&&place!=Player::PlaceEquip)return fail("RushB source requires owner HE");if(!card->isNDTrick())return fail("RushB source requires NDTrick");}
 else if(place!=Player::PlaceSpecial||!p->getPile(skill).contains(id))return fail("RushB source requires its actual owner pile");
 const AICardView *face=nullptr;
 const auto find=[&](const QList<AICardView>&cards){for(const auto &v:cards)if(v.effectiveId==id){face=&v;return;}};
 find(r.worldView.handCards);if(!face)find(r.worldView.self.equips);
 if(!face)for(const auto &pile:r.worldView.self.piles)if(pile.name==skill)find(pile.cards);
 const QString token=aiLegacyCardIdentity(room,p,card);
 if(!face||face->legacyIdentityToken.isEmpty()||face->legacyIdentityToken!=token
    ||face->objectName!=card->objectName()||face->className!=card->getClassName()
    ||face->suit!=int(card->getSuit())||face->number!=card->getNumber())return fail("RushB current lawful face/identity differs");
 if(s.value("identity")!=token||s.value("name")!=card->objectName()||s.value("class_name")!=card->getClassName()
    ||s.value("suit")!=int(card->getSuit())||s.value("number")!=card->getNumber()
    ||!s.value("previous_skill").isString()||s.value("previous_skill")!=card->getSkillName(false))return fail("RushB mutation input is stale or forged");
 // Fixed Card::setSkillName is virtual. The generated SWIG wrapper calls
 // (arg1)->setSkillName, so WrappedCard's override updates wrapper AND inner.
 // Preserve that same-object dispatch; do not qualify away the source effect.
 // No extra revision, payment, cleanup or rollback is introduced.
 if(s.value("kind")=="rushb_physical_skill"){
  card->setSkillName(skill);
  if(danlveSkill(skill))r.conversionLedger->round45PhysicalWrites[id]=physicalStamp(r,room,p,card,skill);
 }
 else if(card->getSkillName(false)!=skill)return fail("RushB physical refresh skill differs");
 return card;
}
