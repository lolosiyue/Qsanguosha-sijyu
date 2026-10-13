#include "ai-strategy-round56-projection.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round28-choice.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include "ai-strategy-public-disclosure.h"
#include <QStringList>
#include <QHash>
#include <QPointer>
#include "ai-strategy-readonly-card-store.h"
#include "card-lifetime-manager.h"
namespace {
bool current(const AIRequest &r, Room &room, const ServerPlayer *p) {
 return p && p->getRoom()==&room && p->isAlive() && r.viewerObjectName==p->objectName()
  && r.worldView.self.objectName==p->objectName()
  && aiRound44RangeRequestCurrent(room,r,p);
}
bool disclosed(const AIRequest &r, Room &room, ServerPlayer *p, int id) {
 if(id<0)return false;
 for(const auto &c:r.worldView.handCards)if(c.effectiveId==id)return true;
 for(const auto &c:r.worldView.self.equips)if(c.effectiveId==id)return true;
 for(const auto &pile:r.worldView.self.piles)for(const auto &c:pile.cards)if(c.effectiveId==id)return true;
 return readAIStrategyPublicDisclosure(room,p,id).value("known")==true;
}
}
bool aiRound28NiwoResponseQuestion(const AIRequest &r, Room &room, const ServerPlayer *p) {
 return current(r,room,p) && room.getCurrent()==p && ([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("ov_niwo"); }())
  && p->getPhase()==Player::Play && p->getHandcardNum()>0
  && r.kind==AIRequest::UseCard && r.pattern=="@@ov_niwo" && r.prompt=="ov_niwo0"
  && r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE && r.handlingMethod==Card::MethodUse;
}
bool aiRound28ChoicePlanningQuestion(const AIRequest &r, Room &room, const ServerPlayer *p) {
 if(aiRound28NiwoResponseQuestion(r,room,p))return true;
 // Fixed ov_shelie asks before replacing Finish -> NotActive, after its mark
 // increments. The consumed suit tag has already been removed; do not read it.
 return current(r,room,p) && room.getCurrent()==p && ([&] {
        std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("ov_shelie");
    }())
  && p->getPhase()==Player::Finish && p->getMark("ov_shelieUse")>0
  && r.kind==AIRequest::Choice && r.choiceOptions.question=="askForChoice"
  && r.choiceOptions.reason=="ov_shelie"
  && r.choiceOptions.choices==QStringList{"Player_Draw","Player_Play"};
}
QJsonObject aiRound28ChoicePlanningContext(const AIRequest &r, Room &room, const ServerPlayer *p) {
 if(!aiRound28ChoicePlanningQuestion(r,room,p))return{};
 const QString source=r.kind==AIRequest::UseCard?"OverseasVersion.ov_niwo":"OverseasVersion.ov_shelie";
 return{{"known",true},{"readonly",true},{"source",source},{"viewer",r.viewerObjectName},
  {"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()},{"kind","activate"},
  {"reason",int(CardUseStruct::CARD_USE_REASON_PLAY)},{"pattern",""},{"handling_method",int(Card::MethodUse)}};
}
std::unique_ptr<Card> createAIRound28LihuoHypothesis(const AIRequest &r, Room &room, ServerPlayer *p, const QVariant &data) {
 if(!current(r,room,p) || r.kind!=AIRequest::SkillInvoke || r.choiceOptions.question!="askForSkillInvoke"
  || r.choiceOptions.reason!="ov_lihuo" || !p->hasSkill("ov_lihuo") || !data.canConvert<CardUseStruct>())return{};
 const auto use=data.value<CardUseStruct>();
 if(!use.card || use.from!=p || !use.card->isKindOf("Slash") || use.card->isKindOf("NatureSlash"))return{};
 for(const auto *target:use.to)if(!target || !room.getAllPlayers(true).contains(target))return{};
 const QList<int> costs=use.card->isVirtualCard()?use.card->getSubcards():QList<int>{use.card->getEffectiveId()};
 for(int id:costs)if(!disclosed(r,room,p,id))return{};
 std::unique_ptr<Card> result(Sanguosha->cloneCard("fire_slash",Card::NoSuit,0));
 if(!result)return{};
 result->setSkillName("mobilelihuo");result->addSubcards(costs);
 return result;
}

namespace {
// The generic borrowed-card store deliberately accepts only physical cards.
// This source-only capture is reached after lihQuestion has verified the actual
// owner inquiry and disclosed ordered materials. It never exposes the original
// graph or transfers ownership: the recipe reads only this exact live Card's
// class/face/material scalars, all rechecked before each reached construction.
AIReadonlyCardLease observeLihuoIncoming(const Card *card) {
 if(!card)return{};
 if(!card->isVirtualCard())return AIOwnerReadonlyCardStore::observe(card);
 auto &manager=globalCardLifetimeManager();
 auto token=manager.observeCard(const_cast<Card *>(card));
 if(!token||!token->live)return{};
 auto pin=std::make_shared<CardLifetimeLease>(manager,token);
 if(!pin->isValid())return{};
 const QPointer<Card> watched(const_cast<Card *>(card));
 return [watched,token,pin]() -> const Card * {
  if(watched.isNull()||!token->live||!pin->isValid())return nullptr;
  return watched.data();
 };
}
struct LihuoInput {
 QPointer<ServerPlayer> viewer;
 quint64 decision=0,revision=0;
 AIReadonlyCardLease source;
 QString name,klass;int suit=0,number=0;
 QList<int> costs;QStringList targets;
};
class LihuoInputs final:public AIRoomLedger {
public:explicit LihuoInputs(Room &room):AIRoomLedger(room){}
 QHash<QString,LihuoInput> values;
};
LihuoInputs *lihInputs(Room &room) {
 for(auto *child:room.children())if(auto *found=dynamic_cast<LihuoInputs *>(child))return found;
 return new LihuoInputs(room);
}
bool lihQuestion(const AIRequest &r,Room &room,ServerPlayer *p,const CardUseStruct &u) {
 if(!current(r,room,p)||r.kind!=AIRequest::SkillInvoke||r.choiceOptions.question!="askForSkillInvoke"
  ||r.choiceOptions.reason!="ov_lihuo"||!p->hasSkill("ov_lihuo")||u.from!=p||!u.card
  ||!u.card->isKindOf("Slash")||u.card->isKindOf("NatureSlash"))return false;
 for(auto *target:u.to)if(!target||!room.getAllPlayers(true).contains(target))return false;
 const auto costs=u.card->isVirtualCard()?u.card->getSubcards():QList<int>{u.card->getEffectiveId()};
 for(int id:costs)if(!disclosed(r,room,p,id))return false;
 return true;
}
}
void projectAIRound28LihuoQuestion(AIRequest &r,Room &room,ServerPlayer *p,const QVariant &data) {
 if(!p || r.kind!=AIRequest::SkillInvoke || r.choiceOptions.reason!="ov_lihuo")return;
 QJsonObject row{{"known",false},{"source_key","ov_lihuo_hypothesis"}};
 auto *state=lihInputs(room);state->values.remove(p->objectName());
 if(data.canConvert<CardUseStruct>()) {
  const auto u=data.value<CardUseStruct>();
  if(lihQuestion(r,room,p,u)) {
   LihuoInput input;input.viewer=p;input.decision=r.decisionId;input.revision=r.stateRevision;
   input.source=observeLihuoIncoming(u.card);
   input.name=u.card->objectName();input.klass=u.card->getClassName();input.suit=int(u.card->getSuit());input.number=u.card->getNumber();
   input.costs=u.card->isVirtualCard()?u.card->getSubcards():QList<int>{u.card->getEffectiveId()};
   for(auto *target:u.to)input.targets<<target->objectName();
   if(input.source&&input.source()){state->values.insert(p->objectName(),input);row.insert("known",true);}
  }
 }
 r.choiceOptions.context.insert("overseas_lihuo_hypothesis",row);
}
std::unique_ptr<Card> createAIRound28CapturedLihuoHypothesis(const AIRequest &r,Room &room,ServerPlayer *p,CardUseStruct *out,QString *error) {
 auto fail=[&](const char *message)->std::unique_ptr<Card>{if(error)*error=QString::fromLatin1(message);return{};};
 if(!p||!out||!current(r,room,p))return fail("Lihuo current owner inquiry unavailable");
 const auto input=lihInputs(room)->values.value(p->objectName());
 if(input.viewer!=p||input.decision!=r.decisionId||input.revision!=r.stateRevision||!input.source)
  return fail("Lihuo incoming source capture is stale or absent");
 const Card *source=input.source();
 if(!source||source->objectName()!=input.name||source->getClassName()!=input.klass
  ||int(source->getSuit())!=input.suit||source->getNumber()!=input.number)
  return fail("Lihuo incoming source identity or face changed");
 const auto costs=source->isVirtualCard()?source->getSubcards():QList<int>{source->getEffectiveId()};
 if(costs!=input.costs)return fail("Lihuo source ordered materials changed");
 CardUseStruct u;u.card=source;u.from=p;
 for(const auto &name:input.targets){auto *target=room.findPlayerByObjectName(name,true);if(!target)return fail("Lihuo original target left roster");u.to<<target;}
 if(!lihQuestion(r,room,p,u))return fail("Lihuo source question or disclosure no longer valid");
 auto card=createAIRound28LihuoHypothesis(r,room,p,QVariant::fromValue(u));
 if(!card)return fail("Lihuo exact hypothetical native clone unavailable");
 u.card=card.get();*out=u;if(error)error->clear();return card;
}

void clearAIRound28ChoiceState(Room &room) {
 for(auto *child:room.children())if(auto *state=dynamic_cast<LihuoInputs *>(child))state->values.clear();
}
