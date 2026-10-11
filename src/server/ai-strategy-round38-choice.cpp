#include "ai-strategy-round38-choice.h"
#include "ai-strategy-choice-context.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
std::unique_ptr<Card> aiRound38ZhongerhxEvaluation(const AIRequest &q,Room &room,ServerPlayer *viewer,int id,
    QJsonObject *disclosedFace,QString *error)
{
 auto deny=[&](const char *text)->std::unique_ptr<Card>{if(error)*error=QString::fromLatin1(text);return {};};
 if(error)error->clear();
 if(!viewer||viewer->getRoom()!=&room||q.viewerObjectName!=viewer->objectName()
  ||q.worldView.self.objectName!=viewer->objectName()||!q.decisionId
  ||q.stateRevision!=room.roomRuntime()->stateRevision()||q.kind!=AIRequest::UseCard
  ||q.pattern!="@@zhongerhx"||!viewer->hasSkill("zhongerhx")
  ||(q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE))
  return deny("Zhongerhx physical hypothesis is outside its actual owner inquiry");
 if(id<0||id>=Sanguosha->getCardCount()||viewer->getMark("zhongerhuan")!=id+1)return deny("Zhongerhx marked physical identity differs");
 // Existing real fillAG recipient witness survives clearAG, but any move,
 // identity change or changed disclosed face revokes it. No hidden deck read.
 const auto witness=readAIStrategyChoiceAGDisclosure(room,viewer,id);
 if(witness.value("known")!=QJsonValue(true))return deny("Zhongerhx selected face lacks retained AG disclosure");
 const Card *source=room.getCard(id);
 if(!source||source->isVirtualCard()||source->getEffectiveId()!=id)
  return deny("Zhongerhx actual physical source changed");
 std::unique_ptr<Card> card(Card::CloneReadonlyPhysicalPolicy(source));
 if(!card)return deny("Zhongerhx exact physical factory cannot form a readonly hypothesis");
 // This is the only source mutation. Never setSkillName on Room's real card.
 card->setSkillName("zhongerhx");
 if(card->isVirtualCard()||card->getEffectiveId()!=id||card->objectName()!=source->objectName()
  ||card->getClassName()!=source->getClassName()||card->getSuit()!=source->getSuit()
  ||card->getNumber()!=source->getNumber()||card->toString()!=source->toString()
  ||card->getSubcards()!=source->getSubcards())
  return deny("Zhongerhx readonly factory changed physical source metadata");
 if(disclosedFace){*disclosedFace=witness.value("card").toObject();
  disclosedFace->insert("legacy_identity_token",aiLegacyCardIdentity(room,viewer,source));}
 return card;
}
