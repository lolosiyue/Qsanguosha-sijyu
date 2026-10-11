#include "ai-strategy-round39-choice.h"
#include "ai-strategy-choice-context.h"
#include "ai-strategy-public-disclosure.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
std::unique_ptr<Card> aiRound39GuicaiEvaluation(const AIRequest &q,Room &room,ServerPlayer *viewer,int id,
    QJsonObject *disclosedFace,QString *error)
{
 auto deny=[&](const char *text)->std::unique_ptr<Card>{if(error)*error=QString::fromLatin1(text);return {};};
 if(error)error->clear();
 if(!viewer||viewer->getRoom()!=&room||q.viewerObjectName!=viewer->objectName()
  ||q.worldView.self.objectName!=viewer->objectName()||!q.decisionId
  ||q.stateRevision!=room.roomRuntime()->stateRevision()||q.kind!=AIRequest::UseCard
  ||q.pattern!="@@bu_stwo_guicai"||q.prompt.section(':',0,0)!="@bu_stwo_guicai"||!viewer->hasSkill("bu_stwo_guicai")
  ||(q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE))
  return deny("Guicai physical hypothesis is outside its actual owner inquiry");
 if(id<0||id>=Sanguosha->getCardCount()||viewer->getMark("bu_stwo_guicai")!=id)return deny("Guicai marked physical identity differs");
 // Fixed olddiy HpChanged does not show its draw card. Owner mark and prompt
 // name alone are NOT face authority. Only independently reached real AG
 // disclosure can admit this conditional known-face path.
 // Existing real fillAG recipient witness survives clearAG, but any move,
 // identity change or changed disclosed face revokes it. No hidden deck read.
 auto witness=readAIStrategyChoiceAGDisclosure(room,viewer,id);
 if(witness.value("known")!=QJsonValue(true))witness=readAIStrategyPublicDisclosure(room,viewer,id);
 // Current native snapshot faces are lawful too. Merely matching the mark or
 // prompt's printed name is deliberately insufficient.
 const AICardView *known=nullptr;
 const auto find=[&](const QList<AICardView> &rows){for(const auto &row:rows)
  if(row.effectiveId==id&&!row.virtualCard&&!row.legacyIdentityToken.isEmpty()){known=&row;break;}};
 if(witness.value("known")!=QJsonValue(true)) {
  find(q.worldView.handCards);if(!known)find(q.worldView.discardPile);
  if(!known)find(q.choiceOptions.cards);if(!known)find(q.worldView.self.equips);
  if(!known)find(q.worldView.self.judgingArea);
  for(const auto &pile:q.worldView.self.piles)if(!known)find(pile.cards);
  for(const auto &p:q.worldView.players){if(!known)find(p.equips);if(!known)find(p.judgingArea);if(!known)find(p.knownCards);}
  if(!known)return deny("Guicai prompt names a card but its complete physical face is not disclosed");
 }
 const Card *source=room.getCard(id);
 if(!source||source->isVirtualCard()||source->getEffectiveId()!=id)
  return deny("Guicai actual physical source changed");
 if(known&&(known->legacyIdentityToken!=aiLegacyCardIdentity(room,viewer,source)
  ||known->objectName!=source->objectName()||known->className!=source->getClassName()
  ||known->suit!=int(source->getSuit())||known->number!=source->getNumber()))
  return deny("Guicai current disclosed face or lifetime identity changed");
 std::unique_ptr<Card> card(Card::CloneReadonlyPhysicalPolicy(source));
 if(!card)return deny("Guicai exact physical factory cannot form a readonly hypothesis");
 // This is the only source mutation. Never setSkillName on Room's real card.
 card->setSkillName("bu_stwo_guicai");
 if(card->isVirtualCard()||card->getEffectiveId()!=id||card->objectName()!=source->objectName()
  ||card->getClassName()!=source->getClassName()||card->getSuit()!=source->getSuit()
  ||card->getNumber()!=source->getNumber()||card->toString()!=source->toString()
  ||card->getSubcards()!=source->getSubcards())
  return deny("Guicai readonly factory changed physical source metadata");
 if(disclosedFace){*disclosedFace=QJsonObject{{"id",id},{"effective_id",id},{"name",source->objectName()},
   {"class_name",source->getClassName()},{"engine_class_name",source->getClassName()},
   {"kind_of",QJsonArray::fromStringList(source->getKindOfNames())},{"subtype",source->getSubtype()},
   {"suit",int(source->getSuit())},{"number",source->getNumber()},{"type_id",int(source->getTypeId())},
   {"red",source->isRed()},{"black",source->isBlack()},{"virtual_card",false},
   {"damage_card",source->isDamageCard()},{"single_target",source->isSingleTargetCard()},
   {"legacy_string_key",QString::number(id)},{"legacy_sort_key",QString::number(id)},
   {"legacy_identity_token",aiLegacyCardIdentity(room,viewer,source)}};}
 return card;
}
