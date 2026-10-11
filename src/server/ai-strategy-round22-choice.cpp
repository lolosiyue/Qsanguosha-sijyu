#include "ai-strategy-round22-choice.h"
#include "ai-strategy-projection.h"
#include "ai-strategy-judge-context.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include "standard.h"
#include <memory>
namespace {
bool current(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.viewerObjectName==p->objectName()
    &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
bool sameFace(const AICardView&v,const Card*c){return c&&v.effectiveId==c->getEffectiveId()&&v.objectName==c->objectName()
    &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber();}
QMap<int,const AICardView*> disclosed(const AIRequest&r){
 QMap<int,const AICardView*> out;const auto add=[&](const QList<AICardView>&cs){for(const auto&c:cs)if(c.effectiveId>=0)out.insert(c.effectiveId,&c);};
 add(r.worldView.handCards);add(r.worldView.discardPile);add(r.choiceOptions.cards);
 const auto player=[&](const AIPlayerView&p){add(p.knownCards);add(p.equips);add(p.judgingArea);for(const auto&pile:p.piles)if(pile.open)add(pile.cards);};
 player(r.worldView.self);for(const auto&p:r.worldView.players)player(p);return out;
}
bool publicPattern(const QString&p){if(p.contains('$'))return false;for(const auto&branch:p.split('#')){const auto x=branch.split('|');if(x.size()>=4&&x[3]!=".")return false;}return true;}
bool knownNoFilter(const AIRequest&r,const ServerPlayer*p){const AIPlayerView*view=nullptr;
 if(p->objectName()==r.worldView.self.objectName)view=&r.worldView.self;
 else for(const auto&v:r.worldView.players)if(v.objectName==p->objectName()){view=&v;break;}
 if(!view||!view->skillsComplete)return false;
 for(const auto&s:view->skills)if(!s.invalid&&dynamic_cast<const FilterSkill*>(Sanguosha->getViewAsSkill(s.skillName)))return false;
 return true;
}
}
int aiRound22ChoicePrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){
 if(!current(r,p)||!p->hasSkill(skill))return -1;
 if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY
    &&(skill=="kexianchanxin"||skill=="kejiexianchanxin")&&prototype=="#"+skill+"Card")return 1;
 if(r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
  if(skill=="MeowLiuli"&&prototype=="#MeowLiuliCard"&&r.pattern=="@@MeowLiuli"&&r.handlingMethod==Card::MethodDiscard)return 1;
  if(skill=="Meowxianzhou"&&prototype=="#MeowxianzhouCard"&&r.pattern=="@@Meowxianzhou")return 0;
 }
 return -1;
}
void projectAIRound22ChoiceContext(AIRequest&r,Room&room,ServerPlayer*viewer,QJsonObject&context,
 const std::function<QJsonObject(const Card*)>&publicCard){
 if(!current(r,viewer)||viewer->getRoom()!=&room||!publicCard)return;
 auto facts=context.value("round22_choice").toObject();
 // Exact source static null-Slash call; never enumerate conversions first.
 if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&viewer->hasSkill("kexianchanxin"))
  facts.insert("chanxin_slash_available",Slash::IsAvailable(viewer,nullptr));
 if(r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern=="@@MeowLiuli"
    &&r.handlingMethod==Card::MethodDiscard&&viewer->hasSkill("MeowLiuli")){
  const auto tag=room.getTag("MeowLiuli");
  if(tag.canConvert<CardUseStruct>()){
   const auto use=tag.value<CardUseStruct>();
   // Fixed meow.lua1253-1259: public Slash use, actual target and source prompt.
   if(use.from&&room.findPlayerByObjectName(use.from->objectName(),true)==use.from&&use.to.contains(viewer)
      &&use.card&&use.card->isKindOf("Slash")&&r.prompt=="@liuli:"+use.from->objectName()){
    QJsonObject flags;for(const auto*p:room.getAllPlayers(true))flags.insert(p->objectName(),p->hasFlag("MeowLiuliSlashSource"));
    facts.insert("meow_liuli",QJsonObject{{"card",publicCard(use.card)},{"source_flags",flags}});
   }
  }
 }
 const QString skill=r.choiceOptions.reason;
 if(r.kind==AIRequest::AmazingGrace&&(skill=="kexianhuiyan"||skill=="kejiexianhuiyan")&&viewer->hasSkill(skill)){
  const auto tag=room.getTag(skill);const JudgeStruct*judge=tag.canConvert<JudgeStruct*>()?tag.value<JudgeStruct*>():nullptr;
  QJsonObject row{{"skill",skill}},indices;
  const auto known=disclosed(r);
  // Preserve donor ipairs indices, not offered values. Missing legal face stays
  // absent; an arbitrary ordinal is never authority to read a hidden wrapper.
  for(int index=1;index<=r.choiceOptions.cardIds.size();++index){const auto*c=room.getCard(index);
   if(!c){indices.insert(QString::number(index),false);continue;}
   const auto found=known.constFind(index);const auto place=room.getCardPlace(index);
   if((found!=known.constEnd()&&sameFace(**found,c))||place==Player::PlaceTable||place==Player::DiscardPile)
    indices.insert(QString::number(index),publicCard(c));
  }
  row.insert("index_cards",indices);
  if(!tag.isValid()||tag.isNull())row.insert("judge",false);
  else if(judge&&judge->who&&room.findPlayerByObjectName(judge->who->objectName(),true)==judge->who&&publicPattern(judge->pattern)){
   auto projected=projectAIStrategyJudgeContext(r,room,viewer,judge,publicCard);
   QJsonObject outcomes,unfiltered,suits,numbers,repairs;
   bool complete=judge->who==viewer||knownNoFilter(r,judge->who);
   for(const auto&face:r.choiceOptions.cards){
    if(!r.choiceOptions.cardIds.contains(face.effectiveId))continue;
    const QString id=QString::number(face.effectiveId);
    std::unique_ptr<Card> card(Sanguosha->cloneCard(face.objectName,Card::Suit(face.suit),face.number));
    if(!card){complete=false;continue;}
    unfiltered.insert(id,judge->isGood(card.get()));
    if(judge->who==viewer){const auto filtered=projectAIOwnerFilteredCard(room,viewer,face.effectiveId,judge,int(Player::PlaceJudge),&face);
     const auto visible=filtered.value("card").toObject();
     if(!filtered.value("complete").toBool()||!filtered.value("judge_outcome").isBool()){complete=false;continue;}
     outcomes.insert(id,filtered.value("judge_outcome"));suits.insert(id,visible.value("suit"));numbers.insert(id,visible.value("number"));
     if(filtered.contains("donor_error_repair"))repairs.insert(id,filtered.value("donor_error_repair"));
    }else if(complete){outcomes.insert(id,judge->isGood(card.get()));suits.insert(id,face.suit);numbers.insert(id,face.number);}
   }
   projected.insert("outcome_by_id",outcomes);projected.insert("outcomes_complete",complete);
   projected.insert("unfiltered_outcome_by_id",unfiltered);projected.insert("unfiltered_outcomes_complete",true);
   projected.insert("filtered_suit_by_id",suits);projected.insert("filtered_number_by_id",numbers);projected.insert("filtered_complete",complete);projected.insert("repair_by_id",repairs);
   // No original judge card is fabricated when StartJudge precedes its draw.
   if(!judge->card)projected.insert("card",false);
   row.insert("judge",projected);context.insert("judge",projected);
  }
  facts.insert("huiyan",row);
 }
 if(!facts.isEmpty())context.insert("round22_choice",facts);
}
