#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round45-context.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "card.h"
#include "engine.h"
#include "structs.h"
#include "skill.h"
#include "ai-card-query-scope.h"
#include "notified-pile-query-scope.h"
#include "ai-strategy-projection.h"
#include "ai-strategy-public-disclosure.h"
#include <QObject>
#include <QPointer>
#include <QJsonArray>
#include <QSet>
namespace {
const char *ledgerName="_ai_round45_newgenerals_frames";
struct Frame { quint64 serial=0; int event=-1; const QVariant *data=nullptr; QPointer<ServerPlayer> target; QPointer<Card> card; QPointer<ServerPlayer> from; qint64 history=0; };
class Frames final:public QObject { public: explicit Frames(Room &r):QObject(&r){setObjectName(ledgerName);} quint64 next=0; QList<Frame> stack; };
Frames *frames(Room &r,bool create=false){for(auto *c:r.children())if(c->objectName()==ledgerName)return dynamic_cast<Frames *>(c);return create?new Frames(r):nullptr;}
bool roster(Room &r,const ServerPlayer *p){return p&&r.getAllPlayers(true).contains(const_cast<ServerPlayer *>(p));}
QJsonValue name(Room &r,ServerPlayer *p){return roster(r,p)?QJsonValue(p->objectName()):QJsonValue(false);}
bool live(const AIRequest &q,Room &r,ServerPlayer *p){return roster(r,p)&&p->getRoom()==&r&&!r.roomRuntime()->isClosing()&&q.decisionId&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision();}
QJsonObject face(const AIWorldView &w,Room &r,const Card *c,const std::function<QJsonObject(const Card *)> &serialize){
 if(!c)return {};
 QList<AICardView> cards=w.handCards;cards.append(w.discardPile);cards.append(w.self.equips);
 for(const auto &p:w.self.piles)cards.append(p.cards);
 for(const auto &p:w.players){cards.append(p.knownCards);cards.append(p.equips);cards.append(p.judgingArea);for(const auto &pile:p.piles)if(pile.open)cards.append(pile.cards);}
 for(const auto &v:cards)if(!v.virtualCard&&!c->isVirtualCard()&&v.effectiveId==c->getEffectiveId()&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber())return serialize(c);
 return {};
}
const Frame *boundFrame(AIRequest &q,Room &r){
 const auto *s=frames(r);
 if(!q.round45NewgeneralsFrameBound){q.round45NewgeneralsFrameBound=true;q.round45NewgeneralsFrameSerial=s&&!s->stack.isEmpty()?s->stack.last().serial:0;}
 if(!s||s->stack.isEmpty())return nullptr;
 const auto &f=s->stack.last();
 return q.round45NewgeneralsFrameSerial==f.serial?&f:nullptr;
}
bool validUse(Room &r,const CardUseStruct &u){if(!u.card||!roster(r,u.from))return false;for(auto *t:u.to)if(!roster(r,t))return false;return true;}
void useRow(AIRequest &q,Room &r,ServerPlayer *p,QJsonObject &out,const QString &key,const std::function<QJsonObject(const Card *)> &serialize){
 const Frame *f=boundFrame(q,r);if(!f||!f->data||!f->data->canConvert<CardUseStruct>()||!f->card)return;
 const auto u=f->data->value<CardUseStruct>();
 const QVariant tagged=key=="moujiang"?r.getTag(key):p->getTag(key);
 if(!tagged.canConvert<CardUseStruct>())return;
 const auto t=tagged.value<CardUseStruct>();
 if(!validUse(r,u)||!validUse(r,t)||u.card!=f->card.data()||u.from!=f->from.data()||u.targetModReveal.useHistoryEventId!=f->history||u.card!=t.card||u.from!=t.from||u.to!=t.to||u.targetModReveal.useHistoryEventId!=t.targetModReveal.useHistoryEventId)return;
 bool allowed=false;
 if(key=="moujiang")allowed=f->event==PreCardUsed&&u.from==p&&f->target==p&&u.card->isKindOf("Duel");
 else if(key=="keolhuanfuData")allowed=u.card->isKindOf("Slash")&&((f->event==TargetConfirmed&&f->target==p&&u.to.contains(p))||(f->event==TargetSpecified&&u.from==p&&f->target==p));
 else if(key=="keolhetao")allowed=f->event==TargetSpecified&&f->target==u.from&&u.from!=p&&u.to.size()>1&&!u.card->isKindOf("SkillCard");
 else if(key=="qhfireluanji-AI")allowed=f->event==CardUsed&&f->target==p&&u.from==p;
 else if(key=="qhfiremengjin-AI")allowed=f->event==TargetSpecified&&f->target==p&&u.from==p;
 else if(key=="MTRenyuData")allowed=f->event==TargetSpecified&&f->target==u.from&&u.from!=p&&u.to.contains(p);
 if(!allowed)return;
 // Own pre-use card, or an actual public post-use frame. No raw tag grants a face.
 auto card=(u.from==p||f->event==TargetSpecified||f->event==TargetConfirmed)?serialize(u.card):face(q.worldView,r,u.card,serialize);
 if(card.isEmpty())return;QJsonArray targets;for(auto *to:u.to)targets.append(to->objectName());
 out.insert(key,QJsonObject{{"from",u.from->objectName()},{"to",targets},{"card",card}});
}
void pileRow(AIRequest &q,Room &r,ServerPlayer *p,QJsonObject &out,const QString &reason){
 const QString key=reason+"ForAI";const auto tag=p->getTag(key);const auto ids=tag.toList();
 // Known empty is the exact QVariant::toIntList result. Nonempty membership
 // additionally needs the native recipient notification; a copied tag is not it.
 const auto witnessed=readAIStrategyNotifiedPile(r,p,"#"+reason);
 if(!q.round45NewgeneralsNotificationBound){q.round45NewgeneralsNotificationBound=true;q.round45NewgeneralsNotificationWitness=witnessed;}
 if(q.round45NewgeneralsNotificationWitness!=witnessed)return;
 // Bind even an empty/no-notification observation before returning: a later
 // notification cannot be adopted by this same immutable request.
 if(ids.isEmpty()){out.insert(key,QJsonArray());return;}
 const auto cards=witnessed.value("cards").toObject();const auto current=witnessed.value("current_card_ids").toArray();
 QJsonArray ordered;QJsonObject lawful;
 for(const auto &v:ids){const int id=v.toInt();const QString k=QString::number(id);if(!cards.contains(k))return;ordered.append(id);if(current.contains(id))lawful.insert(k,cards.value(k));}
 out.insert(key,ordered);out.insert("notified_current_cards",lawful);
}
}
AIRound45NewgeneralsFrame::AIRound45NewgeneralsFrame(Room &r,int event,ServerPlayer *target,const QVariant &data){
 if(event!=PreCardUsed&&event!=TargetSpecified&&event!=TargetConfirmed&&event!=DamageInflicted&&event!=Damaged&&event!=CardUsed)return;
 auto *s=frames(r,true);Frame f;f.serial=++s->next;f.event=event;f.target=target;f.data=&data;
 if(data.canConvert<CardUseStruct>()){auto u=data.value<CardUseStruct>();if(validUse(r,u)){f.card=const_cast<Card *>(u.card);f.from=u.from;f.history=u.targetModReveal.useHistoryEventId;}}
 s->stack.append(f);room_=&r;serial_=f.serial;
}
AIRound45NewgeneralsFrame::~AIRound45NewgeneralsFrame(){if(!room_)return;auto *s=frames(*room_);if(!s)return;for(int i=s->stack.size()-1;i>=0;--i)if(s->stack[i].serial==serial_){s->stack.removeAt(i);break;}}
void clearAIRound45NewgeneralsFrames(Room &r){if(auto *s=frames(r))s->stack.clear();}
void projectAIRound45Newgenerals(AIRequest &q,Room &r,ServerPlayer *p,const std::function<QJsonObject(const Card *)> &serialize){
 auto out=q.choiceOptions.context.value("newgenerals").toObject();
 const QStringList owned={"JinZhaosongDrawer","keolzhubiForAI","keolzhuriForAI","keolhuanfuData","jisi_invoke","keolcihuang","keolhetao","mouhujiaDamage","TurnLengthCount","ren_pile_count","keolsaoguslashForAI","olxiaofanForAI","ol_shengsiyugongData","ol_luojingxiashiData","ol_xiongdiqixinForAI","ofjixuForAI","godshenjiCNS","moujiang","notified_current_cards","owner_keolzhubi_tip","olxiaofanPattern"};
 for(const auto &k:owned)out.remove(k);
 const auto publish=[&]{if(out.isEmpty())q.choiceOptions.context.remove("newgenerals");else q.choiceOptions.context.insert("newgenerals",out);if(out.isEmpty())q.worldView.decisionContext.remove("newgenerals");else q.worldView.decisionContext.insert("newgenerals",out);};publish();
 // R49: these current-question facts reuse the existing native frame lifetime;
 // a retained owner tag alone does not confer live-card disclosure or history.
 auto qh=q.worldView.strategyFacts.value("qhstandard").toObject();
 qh.remove("luanji_use");qh.remove("mengjin_use");
 const auto publishQh=[&]{if(qh.isEmpty())q.worldView.strategyFacts.remove("qhstandard");else q.worldView.strategyFacts.insert("qhstandard",qh);};publishQh();
 auto maotu=q.worldView.strategyFacts.value("maotu").toObject();maotu.remove("mtrenyu_use");
 const auto publishMaotu=[&]{if(maotu.isEmpty())q.worldView.strategyFacts.remove("maotu");else q.worldView.strategyFacts.insert("maotu",maotu);};publishMaotu();
 if(!live(q,r,p))return;const auto reason=q.choiceOptions.reason;
 if(q.kind==AIRequest::SkillInvoke&&reason=="mtrenyu_jin"&&q.choiceOptions.question=="askForSkillInvoke"){
  QJsonObject current;useRow(q,r,p,current,"MTRenyuData",serialize);
  if(current.contains("MTRenyuData"))maotu.insert("mtrenyu_use",current.value("MTRenyuData"));publishMaotu();
 }
 QJsonObject currentQh;
 if(q.kind==AIRequest::PlayersChosen&&reason=="qhfireluanji"&&q.choiceOptions.question=="askForPlayersChosen"){
  useRow(q,r,p,currentQh,"qhfireluanji-AI",serialize);
  if(currentQh.contains("qhfireluanji-AI"))qh.insert("luanji_use",currentQh.value("qhfireluanji-AI"));
 }
 if(q.kind==AIRequest::SkillInvoke&&reason=="qhfiremengjin"&&q.choiceOptions.question=="askForSkillInvoke"){
  useRow(q,r,p,currentQh,"qhfiremengjin-AI",serialize);
  if(currentQh.contains("qhfiremengjin-AI"))qh.insert("mengjin_use",currentQh.value("qhfiremengjin-AI"));
 }
 publishQh();
 if(q.kind==AIRequest::SkillInvoke&&reason=="jinzhaosong_fu")out.insert("JinZhaosongDrawer",name(r,p->getTag("JinZhaosongDrawer").value<ServerPlayer *>()));
 if(q.kind==AIRequest::PlayerChosen&&(reason=="jisi"||reason=="godshenji")){const QString key=reason=="jisi"?"jisi_invoke":"godshenjiCNS";out.insert(key,p->getTag(key).toString());}
 if(q.kind==AIRequest::SkillInvoke&&reason=="keolcihuang")out.insert("keolcihuang",p->getTag("keolcihuang").toString());
 if(q.kind==AIRequest::Discard&&(reason=="keolhuanfu"||reason=="keolhetao"))useRow(q,r,p,out,reason=="keolhuanfu"?"keolhuanfuData":"keolhetao",serialize);
 if(q.kind==AIRequest::PlayerChosen&&reason=="moujiang")useRow(q,r,p,out,"moujiang",serialize);
 if(q.kind==AIRequest::PlayerChosen&&reason=="mouhujia"){
  const auto *f=boundFrame(q,r);const auto tag=p->getTag("mouhujiaDamage");
  if(f&&f->event==DamageInflicted&&f->target==p&&f->data&&f->data->canConvert<DamageStruct>()&&tag.canConvert<DamageStruct>()){
   const auto d=f->data->value<DamageStruct>(),t=tag.value<DamageStruct>();
   if(d.to==p&&t.to==d.to&&t.from==d.from&&t.card==d.card&&t.damage==d.damage){
    QJsonObject row{{"from",name(r,d.from)},{"to",p->objectName()},{"damage",d.damage},{"nature",int(d.nature)}};
    if(!d.card)row.insert("card",false);else {auto c=face(q.worldView,r,d.card,serialize);if(c.isEmpty()){publish();return;}row.insert("card",c);}out.insert("mouhujiaDamage",row);
   }
  }
 }
 if(q.kind==AIRequest::Activate&&([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=r.roomRuntime()->lua().rawState();
        if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(r,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(r,p,gameLua));
        return p->hasSkill("mouzhangwu"); }()))out.insert("TurnLengthCount",r.getTag("TurnLengthCount").toInt());
 if((q.kind==AIRequest::SkillInvoke&&reason=="rensongshu")||(q.kind==AIRequest::Activate&&(([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=r.roomRuntime()->lua().rawState();
        if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(r,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(r,p,gameLua));
        return p->hasSkill("renbinglun"); }())||([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=r.roomRuntime()->lua().rawState();
        if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(r,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(r,p,gameLua));
        return p->hasSkill("mobile_youyi"); }()))))out.insert("ren_pile_count",r.getTag("ren_pile").toList().size());
 if(q.kind==AIRequest::UseCard){
  const QHash<QString,QString> piles={{"@@keolzhubi","keolzhubi"},{"@@keolzhuri","keolzhuri"},{"@@keolsaoguslash","keolsaoguslash"},{"@@olxiaofan2","olxiaofan"},{"@@ol_xiongdiqixin!","ol_xiongdiqixin"},{"@@ofjixu!","ofjixu"}};
  if(piles.contains(q.pattern))pileRow(q,r,p,out,piles.value(q.pattern));
  if(q.pattern=="@@olxiaofan2")out.insert("olxiaofanPattern",p->property("olxiaofanPattern").toString());
  if(q.pattern=="@@keolzhubi") {QJsonObject tips;for(const auto *c:p->getCards("he"))tips.insert(QString::number(c->getEffectiveId()),c->hasTip("keolzhubi"));out.insert("owner_keolzhubi_tip",tips);}
  for(const QString &skill:{QString("ol_shengsiyugong"),QString("ol_luojingxiashi")})if(q.pattern==skill&&q.prompt.startsWith(skill+"0:")){
   const QString key=skill+"Data";const auto tag=p->getTag(key);if(!tag.canConvert<DyingStruct>())out.insert(key,false);else {const auto d=tag.value<DyingStruct>();if(roster(r,d.who)&&q.prompt==skill+"0:"+d.who->objectName())out.insert(key,QJsonObject{{"who",d.who->objectName()}});}
  }
 }
 publish();
}
void projectAIRound45NewgeneralsEvent(AIWorldView &w,Room &r,ServerPlayer *p,const AIEventView &e){
 auto out=w.decisionContext.value("newgenerals").toObject();out.remove("moukanpo");out.remove("CurrentDamageStruct");
 if(p&&w.self.objectName==p->objectName()&&e.triggerEvent==ChoiceMade&&e.from==p->objectName()){
  const auto prompt=e.details.value("prompt_list").toArray();
  if(prompt.size()>=2&&prompt[0].toString()=="skillInvoke"){
   const auto reason=prompt[1].toString();
   const auto *state=frames(r);const Frame *f=state&&!state->stack.isEmpty()?&state->stack.last():nullptr;
   if(reason=="moukanpo"&&f&&f->event==CardUsed&&f->data&&f->data->canConvert<CardUseStruct>()){
    const auto live=f->data->value<CardUseStruct>();const auto t=p->getTag("moukanpo");
    if(t.canConvert<CardUseStruct>()){const auto u=t.value<CardUseStruct>();if(validUse(r,live)&&u.card==live.card&&u.from==live.from&&u.to==live.to&&f->card==live.card&&f->from==live.from&&u.targetModReveal.useHistoryEventId==f->history&&live.targetModReveal.useHistoryEventId==f->history)out.insert("moukanpo",QJsonObject{{"from",name(r,u.from)},{"to",QJsonArray()}});}
   }
   if(reason=="mouqiaoshi"&&f&&f->event==Damaged&&f->data&&f->data->canConvert<DamageStruct>()){
    const auto live=f->data->value<DamageStruct>();const auto t=r.getTag("CurrentDamageStruct");
    if(live.from==p&&roster(r,live.to)&&f->target==live.to&&t.canConvert<DamageStruct>()){
     const auto d=t.value<DamageStruct>();if(d.to==live.to&&d.from==live.from&&d.card==live.card&&d.damage==live.damage&&d.nature==live.nature)out.insert("CurrentDamageStruct",QJsonObject{{"to",name(r,d.to)}});
    }
   }
  }
 }
 if(out.isEmpty())w.decisionContext.remove("newgenerals");else w.decisionContext.insert("newgenerals",out);
}

QString aiRound45NotifiedSkill(const AIRequest &q){
 if(q.kind!=AIRequest::UseCard||(q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE&&q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE))return {};
 const QHash<QString,QString> names={{"@@keolzhubi","keolzhubi"},{"@@keolzhuri","keolzhuri"},{"@@keolsaoguslash","keolsaoguslash"},{"@@olxiaofan2","olxiaofan"},{"@@ol_xiongdiqixin!","ol_xiongdiqixin"},{"@@ofjixu!","ofjixu"}};
 return names.value(q.pattern);
}
bool authorizeAIRound45NotifiedEvaluation(const AIRequest &q,Room &r,ServerPlayer *p,int id,QJsonObject *faceOut){
 if(!live(q,r,p)||id<0||!q.round45NewgeneralsNotificationBound)return false;
 const auto skill=aiRound45NotifiedSkill(q);if(skill.isEmpty())return false;
 const auto witness=readAIStrategyNotifiedPile(r,p,"#"+skill);
 if(witness.isEmpty()||witness!=q.round45NewgeneralsNotificationWitness||!witness.value("current_card_ids").toArray().contains(id))return false;
 const auto ids=p->getTag(skill+"ForAI").toList();if(!ids.contains(id))return false;
 const auto projected=q.choiceOptions.context.value("newgenerals").toObject().value(skill+"ForAI").toArray();
 QJsonArray currentIds;for(const auto &v:ids)currentIds.append(v.toInt());if(currentIds!=projected)return false;
 const auto f=witness.value("cards").toObject().value(QString::number(id)).toObject();
 const auto *c=r.getCard(id);if(!c||c->isVirtualCard()||c->getEffectiveId()!=id||f.isEmpty()||f.value("name").toString()!=c->objectName()||f.value("class_name").toString()!=c->getClassName()||f.value("suit").toInt()!=int(c->getSuit())||f.value("number").toInt()!=c->getNumber())return false;
 if(faceOut)*faceOut=f;return true;
}
bool authorizeAIRound45NotifiedPayment(const AIRequest &q,Room &r,ServerPlayer *p,const SkillInstanceRef &activation,const SkillInstanceRef &source,const QList<int> &costs,QList<int> *foreign){
 const auto skill=aiRound45NotifiedSkill(q);
 // ofjixu's fixed native can_activate explicitly returns false in Response;
 // do not manufacture a scope or repair its reason-vs-pattern source error.
 if(skill.isEmpty()||skill=="ofjixu"||!live(q,r,p)||costs.isEmpty()||costs.size()>128||!activation.isValid()||!source.isValid()||activation.key.skillName!=skill||activation.ownerObjectName!=p->objectName()||!p->hasSkillInstance(skill,activation.key.instanceID)||p->isSkillInvalid(skill,activation.key.instanceID)||r.resolveSkillInstanceRootRef(activation)!=source)return false;
 bool issued=q.hasSkillActionContext&&q.skillActionContext.activationRef==activation&&q.skillActionContext.sourceRef==source;
 for(const auto &a:q.skillActions)if(a.activationRef==activation&&a.sourceRef==source)issued=true;
 if(!issued)return false;QSet<int> seen;QList<int> borrowed;int preview=0;
 for(int id:costs){if(id<0||seen.contains(id))return false;seen.insert(id);
  if(authorizeAIRound45NotifiedEvaluation(q,r,p,id,nullptr)){++preview;if(r.getCardOwner(id)!=p||r.getCardPlace(id)!=Player::PlaceHand)borrowed.append(id);continue;}
  if(skill!="keolzhubi"&&skill!="ol_xiongdiqixin")return false;
  bool known=false;for(const auto &c:q.worldView.handCards)if(c.effectiveId==id)known=true;
  for(const auto &c:q.worldView.self.equips)if(c.effectiveId==id)known=true;
  if(!known||r.getCardOwner(id)!=p||(!p->handCards().contains(id)&&!p->getEquipsId().contains(id)))return false;
 }
 if(skill!="ol_xiongdiqixin"&&preview==0)return false;
 if((skill=="keolzhuri"||skill=="keolsaoguslash"||skill=="olxiaofan")&&costs.size()!=1)return false;
 if(foreign)*foreign=borrowed;return true;
}

int aiRound45NewgeneralsPrototype(const AIRequest &q,const ServerPlayer *p,const QString &skill,const QString &prototype){
 if(!p||!p->getRoom()||!live(q,*p->getRoom(),const_cast<ServerPlayer *>(p))||aiRound45NotifiedSkill(q)!=skill)return -1;
 if(skill=="keolzhubi"&&prototype=="#keolzhubiVSCard")return 1;
 if(skill=="olxiaofan"&&prototype=="#olxiaofan0Card")return 3;
 if(skill=="ol_xiongdiqixin"&&prototype=="#ol_xiongdiqixinCard")return 1;
 return -1;
}
bool authorizeAIRound45NotifiedPhysical(const AIRequest &q,Room &r,ServerPlayer *p,const SkillInstanceRef &a,const SkillInstanceRef &s,int id){
 const auto skill=aiRound45NotifiedSkill(q);if(skill!="keolzhuri"&&skill!="keolsaoguslash")return false;
 if(!authorizeAIRound45NotifiedPayment(q,r,p,a,s,{id}))return false;
 const auto *v=dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(skill));if(!v)return false;
 ActiveSkillRequest request;request.initiator=p;request.reason=q.reason;request.pattern=q.pattern;request.activationRef=a;request.sourceRef=s;
 NotifiedPileQueryScope scope(request,v);
 const auto *card=r.getCard(id);
 if(!card||!v->canActivateRequest(request)||!v->canSelectCard(request,card))return false;
 request.selectedCardIds={id};
 if(!v->cardSelectionFeasible(request))return false;
 // Physical identity is borrowed. An unexpected virtual product is temporary,
 // but no physical product may be deleted or mutated here.
 const Card *produced=v->createCard(request);
 const bool same=produced==card;
 if(produced&&produced!=card&&produced->isVirtualCard())const_cast<Card *>(produced)->deleteLater();
 // Native providers may execute Lua. Recheck the request, issued identities,
 // disclosure and actual registered provider after returning from createCard.
 return same&&live(q,r,p)&&Sanguosha->getViewAsSkill(skill)==v
     &&authorizeAIRound45NotifiedPayment(q,r,p,a,s,{id})&&r.getCard(id)==card;
}

const Card *aiRound45MoujiangEvaluation(const AIRequest &q,Room &r,ServerPlayer *p){
 if(!live(q,r,p)||q.kind!=AIRequest::PlayerChosen||q.choiceOptions.reason!="moujiang"||!p->hasSkill("moujiang")||!q.round45NewgeneralsFrameBound)return nullptr;
 const auto *s=frames(r);if(!s||s->stack.isEmpty())return nullptr;const auto &f=s->stack.last();
 if(f.serial!=q.round45NewgeneralsFrameSerial||f.event!=PreCardUsed||f.target!=p||f.from!=p||!f.card||!f.data||!f.data->canConvert<CardUseStruct>())return nullptr;
 const auto t=r.getTag("moujiang");if(!t.canConvert<CardUseStruct>())return nullptr;
 const auto u=f.data->value<CardUseStruct>(),v=t.value<CardUseStruct>();
 if(!validUse(r,u)||u.card!=f.card.data()||u.from!=p||u.card!=v.card||u.from!=v.from||u.to!=v.to||u.targetModReveal.useHistoryEventId!=v.targetModReveal.useHistoryEventId||u.targetModReveal.useHistoryEventId!=f.history||!u.card->isKindOf("Duel"))return nullptr;
 return u.card;
}
QJsonObject aiRound45MoujiangTargetFilter(Room &r,ServerPlayer *p,const AIRequest &q,const Card *c,const QJsonObject &query){
 const auto failure=[](const char *text){return QJsonObject{{"error",text},{"key","newgenerals.moujiang"}};};
 if(!c||c!=aiRound45MoujiangEvaluation(q,r,p))return failure("current Moujiang card frame expired");
 if(query.size()!=2||!query.value("prefix").isArray()||!query.value("target").isString())return failure("malformed Moujiang target query");
 QList<const Player *> prefix;QSet<QString>seen;
 for(const auto &v:query.value("prefix").toArray()){auto *t=v.isString()?r.findPlayerByObjectName(v.toString()):nullptr;if(!t||!t->isAlive()||seen.contains(v.toString()))return failure("invalid Moujiang ordered prefix");seen.insert(v.toString());prefix.append(t);}
 auto *target=r.findPlayerByObjectName(query.value("target").toString());if(!target||!target->isAlive())return failure("Moujiang target absent");
 AITianyiQueryScope scope(p,c,99,false);const bool accepted=c->targetFilter(prefix,target,p);
 if(!live(q,r,p))return failure("Moujiang target query changed revision");return {{"accepted",accepted}};
}

bool aiRound45KeolcihuangConstruction(const AIRequest &q,Room &r,ServerPlayer *p,const QJsonObject &spec){
 if(!live(q,r,p)||q.kind!=AIRequest::SkillInvoke||q.choiceOptions.reason!="keolcihuang"||spec.size()!=6||spec.value("source_key")!="newgenerals_context:keolcihuang"||spec.value("skill")!="keolcihuang"||spec.value("suit")!=int(Card::NoSuit)||spec.value("number")!=0||!spec.value("subcards").isArray()||!spec.value("subcards").toArray().isEmpty())return false;
 const auto choices=p->getTag("keolcihuang").toString().split('+');return spec.value("name").isString()&&!spec.value("name").toString().isEmpty()&&choices.contains(spec.value("name").toString());
}
