#include "ai-strategy-round39-response.h"
#include "ai.h"
#include "ai-strategy-public-disclosure.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <cmath>
#include <limits>
#include <memory>
namespace {
QJsonObject failure(const char*why){return{{"kind","god_youhua_source"},{"known",false},{"error",why}};}
bool integer(const QJsonValue&v,int&n){double d=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(d)||d<0||d>std::numeric_limits<int>::max()||std::floor(d)!=d)return false;n=int(d);return true;}
QJsonObject face(const AICardView&c){return{{"name",c.objectName},{"class_name",c.className},{"suit",c.suit},{"number",c.number},{"effective_id",c.effectiveId}};}
QJsonObject disclosed(const AIRequest&r,Room&room,ServerPlayer*p,int id){
 const auto scan=[id](const QList<AICardView>&cards){for(const auto&c:cards)if(c.effectiveId==id)return face(c);return QJsonObject();};
 for(const auto*cards:{&r.worldView.handCards,&r.worldView.discardPile,&r.choiceOptions.cards}){auto f=scan(*cards);if(!f.isEmpty())return f;}
 const auto player=[&](const AIPlayerView&v){for(const auto*cards:{&v.knownCards,&v.equips,&v.judgingArea}){auto f=scan(*cards);if(!f.isEmpty())return f;}for(const auto&pile:v.piles)if(pile.open){auto f=scan(pile.cards);if(!f.isEmpty())return f;}return QJsonObject();};
 auto f=player(r.worldView.self);if(!f.isEmpty())return f;
 for(const auto&v:r.worldView.players){f=player(v);if(!f.isEmpty())return f;}
 const auto witness=readAIStrategyPublicDisclosure(room,p,id);
 return witness.value("known")==QJsonValue(true)?witness.value("card").toObject():QJsonObject();
}
bool scope(const AIRequest&r,Room&room,ServerPlayer*p){
 return p&&p->getRoom()==&room&&room.findPlayerByObjectName(p->objectName(),true)==p
  &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&r.decisionId&&r.stateRevision==room.roomRuntime()->stateRevision()
  &&r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY
  &&p->hasSkill("GodYouhua")&&!p->hasFlag("YouhuaUsed")&&p->getMark("LastId")>0;
}
QJsonObject printed(const Card*c){return{{"name",c->objectName()},{"class_name",c->getClassName()},
 {"kind_of",QJsonArray::fromStringList(c->getKindOfNames())},{"suit",int(c->getSuit())},{"number",c->getNumber()},
 {"skill_name",c->getSkillName(false)},{"virtual_card",c->isVirtualCard()},
 {"legacy_string_key",c->toString()}};}
}
QJsonObject queryAIRound39GodYouhua(const AIRequest&r,Room&room,ServerPlayer*p,const QJsonObject&proposal){
 if(!scope(r,room,p)||proposal.size()!=4||proposal.value("kind")!="evaluate_skill_card"
  ||proposal.value("skill")!="GodYouhua"||!proposal.value("subcards").isArray())return failure("GodYouhua actual owner Play source scope mismatch");
 const auto op=proposal.value("name").toString();const auto costs=proposal.value("subcards").toArray();
 if((op!="source_reference"&&op!="source_parse")||(op=="source_reference"&&!costs.isEmpty())||(op=="source_parse"&&costs.size()!=1))return failure("GodYouhua source query shape mismatch");
 const int reference=p->getMark("LastId");
 // A mark is not disclosure. Prove the exact current face before native getCard.
 const auto f=disclosed(r,room,p,reference);
 if(f.isEmpty())return failure("GodYouhua LastId reference is not currently disclosed");
 const Card*ref=Sanguosha->getCard(reference);
 if(!ref||ref->getEffectiveId()!=reference||f.value("name")!=ref->objectName()
  ||f.value("class_name")!=ref->getClassName()||f.value("suit")!=int(ref->getSuit())||f.value("number")!=ref->getNumber())return failure("GodYouhua disclosed reference face changed");
 EngineRuntimeContextScope engine(*Sanguosha,&room);LuaRuntime::Binding game(room.roomRuntime()->lua(),false);
 std::unique_ptr<Card> clone(Sanguosha->cloneCard(ref->objectName(),Card::SuitToBeDecided,-1));
 QJsonObject out{{"kind","god_youhua_source"},{"known",true},{"reference_id",reference},
  {"viewer",r.viewerObjectName},{"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()},
  {"reference",f},{"clone_exists",bool(clone)}};
 if(clone)out.insert("clone",printed(clone.get()));
 if(op=="source_reference"){
  if(!scope(r,room,p)||p->getMark("LastId")!=reference)return failure("GodYouhua source observation changed its scope");
  return out;
 }
 if(!clone)return failure("GodYouhua parse follows absent source clone");
 if(clone->isKindOf("IronChain"))return failure("GodYouhua source declines IronChain before parsing");
 int id=-1;if(!integer(costs.first(),id))return failure("GodYouhua source material must be one physical ID");
 bool ownerKnown=false;for(const auto&c:r.worldView.handCards)if(c.effectiveId==id)ownerKnown=true;
 for(const auto&c:r.worldView.self.equips)if(c.effectiveId==id)ownerKnown=true;
 if(!ownerKnown||room.getCardOwner(id)!=p||(room.getCardPlace(id)!=Player::PlaceHand&&room.getCardPlace(id)!=Player::PlaceEquip))return failure("GodYouhua material is outside current disclosed owner HE");
 const Card*material=Sanguosha->getCard(id);if(!material||material->getEffectiveId()!=id)return failure("GodYouhua material is unavailable");
 const auto mf=disclosed(r,room,p,id);
 if(mf.value("name")!=material->objectName()||mf.value("class_name")!=material->getClassName()
  ||mf.value("suit")!=int(material->getSuit())||mf.value("number")!=material->getNumber())return failure("GodYouhua material face changed");
 // Exact fixed donor concatenation, including its nested first =. segment.
 clone->setSkillName("GodYouhua");
 const QString wire=clone->toString()+QString(":GodYouhua[%1:%2]=%3").arg(material->getSuitString()).arg(material->getNumberString()).arg(id);
 const Card*rawParsed=Card::Parse(wire);
 // Card::Parse can generally borrow physical cards. Do not own/delete one,
 // even on an unexpected parser branch; only the proven virtual result is ours.
 if(rawParsed&&(rawParsed->getId()>=0||!rawParsed->isVirtualCard()))return failure("GodYouhua parser unexpectedly returned a borrowed physical card");
 std::unique_ptr<const Card> parsed(rawParsed);
 out.insert("source_wire",wire);out.insert("exists",bool(parsed));out.insert("material_id",id);
 if(parsed){
  if(!parsed->isVirtualCard()||parsed->getSubcards()!=QList<int>{id}||parsed->getSkillName(false)!="GodYouhua")return failure("GodYouhua fixed parser returned an unexpected source product");
  auto card=printed(parsed.get());card.insert("subcards",QJsonArray{id});out.insert("card",card);
 }
 if(!scope(r,room,p)||p->getMark("LastId")!=reference)return failure("GodYouhua parsing changed source scope");
 return out;
}
bool aiRound39GodYouhuaConstruction(const AIRequest&r,Room&room,ServerPlayer*p,const QJsonObject&spec){
 if(spec.size()!=6||spec.value("source_key")!="GodYouhua"||spec.value("skill")!="GodYouhua"||!spec.value("subcards").isArray())return false;
 const auto row=queryAIRound39GodYouhua(r,room,p,{{"kind","evaluate_skill_card"},{"skill","GodYouhua"},{"name","source_parse"},{"subcards",spec.value("subcards")}});
 const auto card=row.value("card").toObject();
 return row.value("known")==QJsonValue(true)&&row.value("exists")==QJsonValue(true)
  &&card.value("name")==spec.value("name")&&card.value("skill_name")==spec.value("skill")
  &&card.value("suit")==spec.value("suit")&&card.value("number")==spec.value("number")
  &&card.value("subcards")==spec.value("subcards");
}
