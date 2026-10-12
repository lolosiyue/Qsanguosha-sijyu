#include "ai-strategy/newgenerals.h"

// ---- newgenerals-context ----
#include "ai-strategy/runtime.h"
#include "ai-strategy/native-registry.h"
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
#include "ai-strategy/owner.h"
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
 if(!q.newgeneralsFrameBound){q.newgeneralsFrameBound=true;q.newgeneralsFrameSerial=s&&!s->stack.isEmpty()?s->stack.last().serial:0;}
 if(!s||s->stack.isEmpty())return nullptr;
 const auto &f=s->stack.last();
 return q.newgeneralsFrameSerial==f.serial?&f:nullptr;
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
 if(!q.newgeneralsNotificationBound){q.newgeneralsNotificationBound=true;q.newgeneralsNotificationWitness=witnessed;}
 if(q.newgeneralsNotificationWitness!=witnessed)return;
 // Bind even an empty/no-notification observation before returning: a later
 // notification cannot be adopted by this same immutable request.
 if(ids.isEmpty()){out.insert(key,QJsonArray());return;}
 const auto cards=witnessed.value("cards").toObject();const auto current=witnessed.value("current_card_ids").toArray();
 QJsonArray ordered;QJsonObject lawful;
 for(const auto &v:ids){const int id=v.toInt();const QString k=QString::number(id);if(!cards.contains(k))return;ordered.append(id);if(current.contains(id))lawful.insert(k,cards.value(k));}
 out.insert(key,ordered);out.insert("notified_current_cards",lawful);
}
}
AINewgeneralsFrame::AINewgeneralsFrame(Room &r,int event,ServerPlayer *target,const QVariant &data){
 if(event!=PreCardUsed&&event!=TargetSpecified&&event!=TargetConfirmed&&event!=DamageInflicted&&event!=Damaged&&event!=CardUsed)return;
 auto *s=frames(r,true);Frame f;f.serial=++s->next;f.event=event;f.target=target;f.data=&data;
 if(data.canConvert<CardUseStruct>()){auto u=data.value<CardUseStruct>();if(validUse(r,u)){f.card=const_cast<Card *>(u.card);f.from=u.from;f.history=u.targetModReveal.useHistoryEventId;}}
 s->stack.append(f);room_=&r;serial_=f.serial;
}
AINewgeneralsFrame::~AINewgeneralsFrame(){if(!room_)return;auto *s=frames(*room_);if(!s)return;for(int i=s->stack.size()-1;i>=0;--i)if(s->stack[i].serial==serial_){s->stack.removeAt(i);break;}}
void clearAINewgeneralsFrames(Room &r){if(auto *s=frames(r))s->stack.clear();}
void projectAINewgenerals(AIRequest &q,Room &r,ServerPlayer *p,const std::function<QJsonObject(const Card *)> &serialize){
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
 if(q.kind==AIRequest::Activate&&([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=r.roomRuntime()->lua().rawState();
        if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(r,gameLua))
            source.reset(new AIWorldSkillQueriesScope(r,p,gameLua));
        return p->hasSkill("mouzhangwu"); }()))out.insert("TurnLengthCount",r.getTag("TurnLengthCount").toInt());
 if((q.kind==AIRequest::SkillInvoke&&reason=="rensongshu")||(q.kind==AIRequest::Activate&&(([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=r.roomRuntime()->lua().rawState();
        if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(r,gameLua))
            source.reset(new AIWorldSkillQueriesScope(r,p,gameLua));
        return p->hasSkill("renbinglun"); }())||([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=r.roomRuntime()->lua().rawState();
        if(q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(r,gameLua))
            source.reset(new AIWorldSkillQueriesScope(r,p,gameLua));
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
void projectAINewgeneralsEvent(AIWorldView &w,Room &r,ServerPlayer *p,const AIEventView &e){
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

QString aiNewgeneralsNotifiedSkill(const AIRequest &q){
 if(q.kind!=AIRequest::UseCard||(q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE&&q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE))return {};
 const QHash<QString,QString> names={{"@@keolzhubi","keolzhubi"},{"@@keolzhuri","keolzhuri"},{"@@keolsaoguslash","keolsaoguslash"},{"@@olxiaofan2","olxiaofan"},{"@@ol_xiongdiqixin!","ol_xiongdiqixin"},{"@@ofjixu!","ofjixu"}};
 return names.value(q.pattern);
}
bool authorizeAINewgeneralsNotifiedEvaluation(const AIRequest &q,Room &r,ServerPlayer *p,int id,QJsonObject *faceOut){
 if(!live(q,r,p)||id<0||!q.newgeneralsNotificationBound)return false;
 const auto skill=aiNewgeneralsNotifiedSkill(q);if(skill.isEmpty())return false;
 const auto witness=readAIStrategyNotifiedPile(r,p,"#"+skill);
 if(witness.isEmpty()||witness!=q.newgeneralsNotificationWitness||!witness.value("current_card_ids").toArray().contains(id))return false;
 const auto ids=p->getTag(skill+"ForAI").toList();if(!ids.contains(id))return false;
 const auto projected=q.choiceOptions.context.value("newgenerals").toObject().value(skill+"ForAI").toArray();
 QJsonArray currentIds;for(const auto &v:ids)currentIds.append(v.toInt());if(currentIds!=projected)return false;
 const auto f=witness.value("cards").toObject().value(QString::number(id)).toObject();
 const auto *c=r.getCard(id);if(!c||c->isVirtualCard()||c->getEffectiveId()!=id||f.isEmpty()||f.value("name").toString()!=c->objectName()||f.value("class_name").toString()!=c->getClassName()||f.value("suit").toInt()!=int(c->getSuit())||f.value("number").toInt()!=c->getNumber())return false;
 if(faceOut)*faceOut=f;return true;
}
bool authorizeAINewgeneralsNotifiedPayment(const AIRequest &q,Room &r,ServerPlayer *p,const SkillInstanceRef &activation,const SkillInstanceRef &source,const QList<int> &costs,QList<int> *foreign){
 const auto skill=aiNewgeneralsNotifiedSkill(q);
 // ofjixu's fixed native can_activate explicitly returns false in Response;
 // do not manufacture a scope or repair its reason-vs-pattern source error.
 if(skill.isEmpty()||skill=="ofjixu"||!live(q,r,p)||costs.isEmpty()||costs.size()>128||!activation.isValid()||!source.isValid()||activation.key.skillName!=skill||activation.ownerObjectName!=p->objectName()||!p->hasSkillInstance(skill,activation.key.instanceID)||p->isSkillInvalid(skill,activation.key.instanceID)||r.resolveSkillInstanceRootRef(activation)!=source)return false;
 bool issued=q.hasSkillActionContext&&q.skillActionContext.activationRef==activation&&q.skillActionContext.sourceRef==source;
 for(const auto &a:q.skillActions)if(a.activationRef==activation&&a.sourceRef==source)issued=true;
 if(!issued)return false;QSet<int> seen;QList<int> borrowed;int preview=0;
 for(int id:costs){if(id<0||seen.contains(id))return false;seen.insert(id);
  if(authorizeAINewgeneralsNotifiedEvaluation(q,r,p,id,nullptr)){++preview;if(r.getCardOwner(id)!=p||r.getCardPlace(id)!=Player::PlaceHand)borrowed.append(id);continue;}
  if(skill!="keolzhubi"&&skill!="ol_xiongdiqixin")return false;
  bool known=false;for(const auto &c:q.worldView.handCards)if(c.effectiveId==id)known=true;
  for(const auto &c:q.worldView.self.equips)if(c.effectiveId==id)known=true;
  if(!known||r.getCardOwner(id)!=p||(!p->handCards().contains(id)&&!p->getEquipsId().contains(id)))return false;
 }
 if(skill!="ol_xiongdiqixin"&&preview==0)return false;
 if((skill=="keolzhuri"||skill=="keolsaoguslash"||skill=="olxiaofan")&&costs.size()!=1)return false;
 if(foreign)*foreign=borrowed;return true;
}

int aiNewgeneralsPrototype(const AIRequest &q,const ServerPlayer *p,const QString &skill,const QString &prototype){
 if(!p||!p->getRoom()||!live(q,*p->getRoom(),const_cast<ServerPlayer *>(p))||aiNewgeneralsNotifiedSkill(q)!=skill)return -1;
 if(skill=="keolzhubi"&&prototype=="#keolzhubiVSCard")return 1;
 if(skill=="olxiaofan"&&prototype=="#olxiaofan0Card")return 3;
 if(skill=="ol_xiongdiqixin"&&prototype=="#ol_xiongdiqixinCard")return 1;
 return -1;
}
bool authorizeAINewgeneralsNotifiedPhysical(const AIRequest &q,Room &r,ServerPlayer *p,const SkillInstanceRef &a,const SkillInstanceRef &s,int id){
 const auto skill=aiNewgeneralsNotifiedSkill(q);if(skill!="keolzhuri"&&skill!="keolsaoguslash")return false;
 if(!authorizeAINewgeneralsNotifiedPayment(q,r,p,a,s,{id}))return false;
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
     &&authorizeAINewgeneralsNotifiedPayment(q,r,p,a,s,{id})&&r.getCard(id)==card;
}

const Card *aiMoujiangEvaluation(const AIRequest &q,Room &r,ServerPlayer *p){
 if(!live(q,r,p)||q.kind!=AIRequest::PlayerChosen||q.choiceOptions.reason!="moujiang"||!p->hasSkill("moujiang")||!q.newgeneralsFrameBound)return nullptr;
 const auto *s=frames(r);if(!s||s->stack.isEmpty())return nullptr;const auto &f=s->stack.last();
 if(f.serial!=q.newgeneralsFrameSerial||f.event!=PreCardUsed||f.target!=p||f.from!=p||!f.card||!f.data||!f.data->canConvert<CardUseStruct>())return nullptr;
 const auto t=r.getTag("moujiang");if(!t.canConvert<CardUseStruct>())return nullptr;
 const auto u=f.data->value<CardUseStruct>(),v=t.value<CardUseStruct>();
 if(!validUse(r,u)||u.card!=f.card.data()||u.from!=p||u.card!=v.card||u.from!=v.from||u.to!=v.to||u.targetModReveal.useHistoryEventId!=v.targetModReveal.useHistoryEventId||u.targetModReveal.useHistoryEventId!=f.history||!u.card->isKindOf("Duel"))return nullptr;
 return u.card;
}
QJsonObject aiMoujiangTargetFilter(Room &r,ServerPlayer *p,const AIRequest &q,const Card *c,const QJsonObject &query){
 const auto failure=[](const char *text){return QJsonObject{{"error",text},{"key","newgenerals.moujiang"}};};
 if(!c||c!=aiMoujiangEvaluation(q,r,p))return failure("current Moujiang card frame expired");
 if(query.size()!=2||!query.value("prefix").isArray()||!query.value("target").isString())return failure("malformed Moujiang target query");
 QList<const Player *> prefix;QSet<QString>seen;
 for(const auto &v:query.value("prefix").toArray()){auto *t=v.isString()?r.findPlayerByObjectName(v.toString()):nullptr;if(!t||!t->isAlive()||seen.contains(v.toString()))return failure("invalid Moujiang ordered prefix");seen.insert(v.toString());prefix.append(t);}
 auto *target=r.findPlayerByObjectName(query.value("target").toString());if(!target||!target->isAlive())return failure("Moujiang target absent");
 AITianyiQueryScope scope(p,c,99,false);const bool accepted=c->targetFilter(prefix,target,p);
 if(!live(q,r,p))return failure("Moujiang target query changed revision");return {{"accepted",accepted}};
}

bool aiKeolcihuangConstruction(const AIRequest &q,Room &r,ServerPlayer *p,const QJsonObject &spec){
 if(!live(q,r,p)||q.kind!=AIRequest::SkillInvoke||q.choiceOptions.reason!="keolcihuang"||spec.size()!=6||spec.value("source_key")!="newgenerals_context:keolcihuang"||spec.value("skill")!="keolcihuang"||spec.value("suit")!=int(Card::NoSuit)||spec.value("number")!=0||!spec.value("subcards").isArray()||!spec.value("subcards").toArray().isEmpty())return false;
 const auto choices=p->getTag("keolcihuang").toString().split('+');return spec.value("name").isString()&&!spec.value("name").toString().isEmpty()&&choices.contains(spec.value("name").toString());
}

// ---- newgenerals-planning-2 ----
#include <cmath>
#include <limits>
namespace {
bool same(const AICardView&v,const Card*c,int id,Room&room,const ServerPlayer*p){
 return c&&v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()
  &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()
  &&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
bool material(const AIRequest&r,Room&room,const ServerPlayer*p,int id){
 if(room.getCardOwner(id)!=p)return false;
 const Card*c=Sanguosha->getCard(id);if(!c)return false;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id))
  for(const auto&v:r.worldView.handCards)if(same(v,c,id,room,p))return true;
 // Fixed addHandPile = current owner hand plus wooden_ox / &-prefixed piles.
 // Native selection remains independently checked after the policy selects.
 if(room.getCardPlace(id)!=Player::PlaceSpecial||!p->getHandPile().contains(id))return false;
 for(const auto&pile:r.worldView.self.piles)
  if((pile.name=="wooden_ox"||pile.name.startsWith('&'))&&p->getPile(pile.name).contains(id))
   for(const auto&v:pile.cards)if(same(v,c,id,room,p))return true;
 return false;
}
}
bool aiMouluanjiConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 static const QSet<QString>keys{"source_key","name","skill","suit","number","subcards"};
 if(!p||p->getRoom()!=&room||room.roomRuntime()->isClosing()||r.decisionId==0
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.kind!=AIRequest::Activate
  ||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()||!p->hasSkill("mouluanji")
  ||s.size()!=keys.size())return false;
 for(auto it=s.begin();it!=s.end();++it)if(!keys.contains(it.key()))return false;
 if(s.value("source_key")!=QJsonValue("newgenerals.mouluanji")||s.value("name")!=QJsonValue("archery_attack")
  ||s.value("skill")!=QJsonValue("mouluanji")||s.value("suit")!=QJsonValue(int(Card::NoSuit))
  ||s.value("number")!=QJsonValue(0)||!s.value("subcards").isArray())return false;
 const auto ids=s.value("subcards").toArray();if(ids.size()!=2)return false;
 QSet<int>seen;for(const auto&v:ids){
  const double n=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(n)||n<0||n>std::numeric_limits<int>::max()||n!=std::floor(n))return false;
  const int id=int(n);if(seen.contains(id)||!material(r,room,p,id))return false;seen.insert(id);
 }
 // No mark/availability/payment check here: this is only the source's ordered
 // hypothetical clone. The original fill owns its early mark/search branches;
 // selected-only native canActivate/canSelect/createCard remain authoritative.
 return true;
}

// ---- newgenerals-planning-3 ----
namespace {
bool integer(const QJsonValue&v,int&n){const double d=v.toDouble();if(!v.isDouble()||!std::isfinite(d)||d!=std::floor(d)||d<std::numeric_limits<int>::min()||d>std::numeric_limits<int>::max())return false;n=int(d);return true;}
bool play(const AIRequest&r){return r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty();}
bool response(const AIRequest&r,const QString&pattern){return (r.kind==AIRequest::UseCard||r.kind==AIRequest::RespondCard)&&r.pattern==pattern&&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE);}
bool newgeneralsPlanning3Same(const AICardView&v,const Card*c,int id,Room&room,const ServerPlayer*p){return c&&v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);}
const Card*visibleMaterial(const AIWorldView&w,Room&room,const ServerPlayer*p,int id){
 const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 for(const auto&v:w.handCards)if(newgeneralsPlanning3Same(v,c,id,room,p))return c;
 for(const auto&v:w.discardPile)if(newgeneralsPlanning3Same(v,c,id,room,p))return c;
 QList<AIPlayerView>players=w.players;players.prepend(w.self);
 for(const auto&v:players){
  for(const auto&k:v.knownCards)if(newgeneralsPlanning3Same(k,c,id,room,p))return c;
  for(const auto&k:v.equips)if(newgeneralsPlanning3Same(k,c,id,room,p))return c;
  for(const auto&k:v.judgingArea)if(newgeneralsPlanning3Same(k,c,id,room,p))return c;
  for(const auto&pile:v.piles)if(v.objectName==p->objectName()||pile.open)for(const auto&k:pile.cards)if(newgeneralsPlanning3Same(k,c,id,room,p))return c;
 }
 return nullptr;
}
const Card*newgeneralsPlanning3Material(const AIRequest&r,Room&room,const ServerPlayer*p,int id,const QString&zones){
 const auto proof=r.worldView.strategyFacts.value("round45_hand_pile_ids");
 if(zones.contains('p')&&proof.isArray()&&proof.toArray().contains(id)&&p->getHandPile().contains(id))
  return visibleMaterial(r.worldView,room,p,id);
 if(room.getCardOwner(id)!=p)return nullptr;const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 if(zones.contains('h')&&room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id))for(const auto&v:r.worldView.handCards)if(newgeneralsPlanning3Same(v,c,id,room,p))return c;
 // Source getCards("he") enumerates Player::getEquips() inner objects.
 // The wrapper returned by Engine::getCard(id) is a different live identity.
 // This is ONLY the authenticated current owner's e branch, not permission
 // to substitute an inner object's face for any hand-pile getCard(id) read.
 if(zones.contains('e')&&room.getCardPlace(id)==Player::PlaceEquip)
  for(const Card*equipped:p->getEquips())if(equipped->getEffectiveId()==id)
   for(const auto&v:r.worldView.self.equips)if(newgeneralsPlanning3Same(v,equipped,id,room,p))return equipped;
 return nullptr;
}
bool publicName(const AIRequest&r,const QString&name){return !name.isEmpty()&&r.worldView.strategyFacts.value("pattern_names").toArray().contains(name);}
bool godMaterial(const AIRequest&r,Room&room,const ServerPlayer*p,int id){
 Q_UNUSED(room);
 return aiNewgeneralsGodshenjiMaterial(r,const_cast<ServerPlayer*>(p),id)!=nullptr;
}

bool shisheng(const ServerPlayer*p){if(p->hasSkill("shisheng"))return true;for(int i=1;i<=10;++i)if(p->hasSkill("shisheng"+QString::number(i)))return true;return false;}
}
bool aiNewgeneralsPlanningConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||room.roomRuntime()->isClosing()||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()||!aiRangeRequestCurrent(room,r,p))return false;
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 const auto key=s.value("source_key").toString(),name=s.value("name").toString(),raw=s.value("skill").toString();
 const bool recast=key=="newgenerals._xdyifu";
 if(s.size()!=fields.size()+int(recast))return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key())&&!(recast&&i.key()=="can_recast"))return false;
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()||!s.value("subcards").isArray()||(recast&&s.value("can_recast")!=QJsonValue(false)))return false;
 int suit=0,number=0;if(!integer(s.value("suit"),suit)||!integer(s.value("number"),number))return false;
 QList<int>ids;for(const auto&v:s.value("subcards").toArray()){int id=-1;if(!integer(v,id)||id<0)return false;ids<<id;}
 // No canActivate/canSelect/using/jilei checks: these are source hypotheses,
 // including proposals which the source never chooses. The later native bind
 // independently checks the current real provider, ordered prefix and product.
 if(key=="mountainrumour.shisheng_preview"){
  if(raw!=""||suit!=int(Card::NoSuit)||number!=-1||!ids.isEmpty()||!shisheng(p))return false;
  static const QSet<QString>names{"slash","fire_slash","thunder_slash","jink","peach","analeptic","nullification","snatch","dismantlement","collateral","ex_nihilo","duel","fire_attack","amazing_grace","savage_assault","archery_attack","god_salvation","iron_chain"};
  if(play(r))return names.contains(name);
  const auto reason=r.choiceOptions.reason;bool choice=reason=="shisheng_card";for(int i=1;i<=10;++i)choice=choice||reason=="shisheng_sc_re"+QString::number(i);
  return r.kind==AIRequest::Choice&&choice&&name!="esc"&&r.choiceOptions.choices.contains(name)&&publicName(r,name);
 }
 if(key=="mountainrumour.shisheng"){
  if(!shisheng(p)||!response(r,"@@shisheng")||raw!="shisheng"||suit!=int(Card::SuitToBeDecided)||number!=0||ids.size()>3||name.isEmpty()||name!=p->property("shisheng_sel").toString())return false;
  QSet<int>seen;for(int id:ids){if(seen.contains(id)||!newgeneralsPlanning3Material(r,room,p,id,"he"))return false;seen.insert(id);}
  // x is viewer strategy memory, not a gameplay mark: permit the exact source
  // recipe union 0..3 distinct HE materials. No trust in x for native payment.
  return true;
 }
 if(key!="newgenerals."+raw||suit!=int(Card::NoSuit)||number!=0||!publicName(r,name))return false;
 QString skill=raw;if(raw=="_xdyifu")skill="xdyifuvs";else if(raw=="_ofyouchong")skill="ofyouchongvs";else if(raw=="_ofsifen")skill="ofsifen";
 if(!p->hasSkill(skill)&&!p->hasSkill(skill+"&"))return false;
 QString zones="hep";bool admitted=false;
 if(raw=="keolkenshang")admitted=ids.size()>=2&&ids.size()<=999;
 else if(raw=="keolyilie"){zones="h";admitted=ids.size()==2;}
 else if(raw=="moujiang"){zones="h";admitted=play(r)&&name=="duel"&&!ids.isEmpty()&&ids==p->handCards();}
 else if(raw=="mobilexiaoni"){zones="he";admitted=play(r)&&ids.size()==1;}
 else if(raw=="keolweilin")admitted=ids.size()==1;
 else if(raw=="_xdyifu"){zones="h";admitted=play(r)&&ids.size()==1&&(name=="lightning"||name=="iron_chain"||name==p->property("Suijiyingbian").toString());}
 else if(raw=="ofsifen")admitted=play(r)&&name=="duel"&&!ids.isEmpty()&&ids.size()==p->getMark("ofsifenRed-PlayClear");
 else if(raw=="_ofsifen"){zones="h";admitted=response(r,"@@ofsifen!")&&name=="duel"&&(ids.size()==1||ids.size()==(p->getHandcardNum()+1)/2)&&!ids.isEmpty();}
 else if(raw=="ofjunwei")admitted=name=="nullification"&&ids.size()==2; // Source does not compare colors.
 else if(raw=="_ofyouchong"){zones="he";admitted=response(r,"@@ofyouchong")&&ids.size()==3&&name==p->property("ofyouchongCN").toString();}
 else if(raw=="godshenji")return r.kind==AIRequest::PlayerChosen&&r.choiceOptions.reason=="godshenji"&&ids.size()==1&&p->getTag("godshenjiCNS").toString().split('+').contains(name)&&godMaterial(r,room,p,ids.first());
 if(!admitted)return false;
 QMap<int,int>seen,limit;
 if(aiNewgeneralsPlanningRepeatedMaterials(s)){
  for(const Card*c:p->getCards("he"))++limit[c->getEffectiveId()];
  for(int id:p->getHandPile())++limit[id];
 }
 for(int id:ids)if(++seen[id]>(aiNewgeneralsPlanningRepeatedMaterials(s)?limit.value(id):1))return false;
 QList<const Card*>cards;for(int id:ids){const auto*c=newgeneralsPlanning3Material(r,room,p,id,zones);if(!c)return false;cards<<c;}
 if(raw=="keolyilie"&&cards[0]->getColor()!=cards[1]->getColor())return false;
 if(raw=="_xdyifu"&&!cards[0]->isKindOf("BasicCard"))return false;
 if(raw=="ofsifen")for(const auto*c:cards)if(!c->isRed())return false;
 return true;
}
bool aiNewgeneralsPlanningProduct(const QJsonObject&s,const Card&c){
 const auto skill=s.value("skill").toString();
 // Guhuo callback also constructs its caller-supplied public toname, without
 // the fill callback's Basic filter. Do not narrow that readonly source union.
 if(skill=="mobilexiaoni")return c.isDamageCard()&&!c.isKindOf("DelayedTrick");
 // keolweilin cardsview uses dummyCard(class_name), even when a caller asks a
 // class its selected native provider does not support. Do not pre-filter it.
 return true;
}
QJsonValue aiNewgeneralsPlanningHandPile(const AIWorldView&w,Room&room,const ServerPlayer*p){
 if(!p||p->getRoom()!=&room||w.self.objectName!=p->objectName())return false;
 QJsonArray ids;
 for(int id:p->getHandPile()){
  if(!visibleMaterial(w,room,p,id))return false;
  ids.append(id); // Native order/duplicates; no partial list or hidden member ID.
 }
 return ids;
}
void projectAINewgeneralsPlanningFacts(AIWorldView&w,Room&room,const ServerPlayer*p){
 if(!p||p->getRoom()!=&room||w.self.objectName!=p->objectName())return;
 w.strategyFacts.insert("round45_hand_pile_ids",aiNewgeneralsPlanningHandPile(w,room,p));
 auto facts=w.strategyFacts.value("newgenerals").toObject();
 auto by=facts.value("player_properties").toObject();auto own=by.value(p->objectName()).toObject();
 // Fixed newgenerals-ai6138/6936 reads only self.player's values. QVariant's
 // absent toString() is the source's known empty string, never a foreign guess.
 if(([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("xdyifuvs"); }())||([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("xdyifuvs&"); }()))own.insert("Suijiyingbian",p->property("Suijiyingbian").toString());
 if(([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("ofyouchongvs"); }())||([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("ofyouchongvs&"); }()))own.insert("ofyouchongCN",p->property("ofyouchongCN").toString());
 by.insert(p->objectName(),own);facts.insert("player_properties",by);w.strategyFacts.insert("newgenerals",facts);
}

bool aiNewgeneralsPlanningRepeatedMaterials(const QJsonObject&s){
 const auto raw=s.value("skill").toString();return s.value("source_key").toString()=="newgenerals."+raw&&(raw=="keolkenshang"||raw=="ofsifen"||raw=="ofjunwei");
}

// ---- newgenerals-planning-4 ----
namespace {
bool newgeneralsPlanning4Same(const AICardView &v,const Card *c,Room &room,const ServerPlayer *p){
 return c&&v.effectiveId==c->getEffectiveId()&&v.objectName==c->objectName()
  &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
  &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
const Card *observed(const AIRequest &r,Room &room,const ServerPlayer *p,int id){
 if(id<0||id>=Sanguosha->getCardCount())return nullptr;
 const Card *c=Sanguosha->getCard(id);if(!c)return nullptr;
 for(const auto &v:r.worldView.handCards)if(newgeneralsPlanning4Same(v,c,room,p))return c;
 for(const auto &v:r.worldView.discardPile)if(newgeneralsPlanning4Same(v,c,room,p))return c;
 QList<AIPlayerView> players=r.worldView.players;players.prepend(r.worldView.self);
 for(const auto &player:players){
  for(const auto &v:player.knownCards)if(newgeneralsPlanning4Same(v,c,room,p))return c;
  for(const auto &v:player.equips)if(newgeneralsPlanning4Same(v,c,room,p))return c;
  for(const auto &v:player.judgingArea)if(newgeneralsPlanning4Same(v,c,room,p))return c;
  for(const auto &pile:player.piles)if(player.objectName==p->objectName()||pile.open)
   for(const auto &v:pile.cards)if(newgeneralsPlanning4Same(v,c,room,p))return c;
 }
 return nullptr;
}
}
bool aiFangchanYantaoluanConstruction(const AIRequest &r,Room &room,const ServerPlayer *p,const QJsonObject &s){
 if(!p||!room.roomRuntime()||p->getRoom()!=&room||room.roomRuntime()->isClosing()||!r.decisionId
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||!aiRangeRequestCurrent(room,r,p))return false;
 const QString key=s.value("source_key").toString();
 const bool fang=key=="newgenerals._xdfangchan",yan=key=="newgenerals._yantaoluan";
 if(!fang&&!yan)return false;
 const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size()+int(fang))return false;
 for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key())&&!(fang&&i.key()=="can_recast"))return false;
 if(!s.value("name").isString()||s.value("skill")!=QJsonValue(fang?"_xdfangchan":"_yantaoluan")
  ||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 if(yan)return r.kind==AIRequest::Choice&&r.choiceOptions.reason=="yantaoluan"
  &&s.value("name")==QJsonValue("fire_slash");
 if(r.kind!=AIRequest::UseCard||r.pattern!="@@xdfangchan"
  ||(r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE&&r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
  ||s.value("can_recast")!=QJsonValue(false))return false;
 const int id=p->getMark("xdfangchanId");
 // Match the actual owner mark read by PlayerView:getStrategyMark. There is
 // no newly invented hasSkill gate: native source can yield to AG/invoke before
 // asking this exact question and does not recheck skill ownership afterward.
 const auto &marks=r.worldView.self.publicMarks;
 if((!marks.contains("xdfangchanId")&&!r.worldView.self.marksComplete)
  ||marks.value("xdfangchanId",0)!=id)return false;
 const auto strategy=r.worldView.modePolicy.value("strategy_marks").toObject()
  .value(p->objectName()).toObject();
 if(strategy.contains("xdfangchanId")&&strategy.value("xdfangchanId")!=QJsonValue(id))return false;
 // The native producer marks an actual public discard/AG card. Require its
 // CURRENT independent full-face/identity observation; the mark is not a hidden
 // ID oracle. An inner Equipment face never authenticates a different wrapper.
 const Card *c=observed(r,room,p,id);
 return c&&s.value("name")==QJsonValue(c->objectName());
}
QJsonObject aiYantaoluanSlash(const AIRequest &r,Room &room,const ServerPlayer *p,const QJsonObject &s,const Card *c){
 if(s.value("source_key")!=QJsonValue("newgenerals._yantaoluan")
  ||!aiFangchanYantaoluanConstruction(r,room,p,s)||!c||c->objectName()!="fire_slash"
  ||c->getSkillName(false)!="_yantaoluan"||!c->getSubcards().isEmpty()
  ||c->getSuit()!=Card::NoSuit||c->getNumber()!=0)return {};
 // This is the actual native Choice's typed Judge who, already projected by
 // projectDecisionContext from data. No arbitrary Lua target or foreign actor.
 QJsonObject context=r.worldView.decisionContext;
 for(auto i=r.choiceOptions.context.begin();i!=r.choiceOptions.context.end();++i)context.insert(i.key(),i.value());
 const QString who=context.value("judge").toObject().value("who").toString();
 ServerPlayer *target=room.findPlayerByObjectName(who,true);if(!target)return {};
 const bool value=p->canSlash(target,c,false,0);
 if(!aiRangeRequestCurrent(room,r,p))return {};
 return {{"actor",p->objectName()},{"target",who},{"distance_limit",false},{"range_fix",0},{"value",value}};
}

// ---- newgenerals-planning-5 ----
namespace {
bool newgeneralsPlanning5Exact(const AICardView&v,const Card*c,Room&room,const ServerPlayer*p,int id){
 return c&&v.effectiveId==id&&c->getEffectiveId()==id&&v.objectName==c->objectName()
  &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
  &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
}
bool aiKeolshuangxiongConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(s.value("source_key")!=QJsonValue("newgenerals.keolshuangxiong")||s.value("skill")!=QJsonValue("keolshuangxiong")
  ||s.value("name")!=QJsonValue("duel")||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)
  ||!s.value("subcards").isArray()||s.value("subcards").toArray().size()!=1)return false;
 // Readonly source presence, not canActivate/invalidity/color/using admission.
 // The source fill observes a proposal before selected native V2 guards.
 bool own=false;for(const auto&instance:p->getSkillInstances())if(instance.skillName=="keolshuangxiong")own=true;
 if(!own)return false;
 const auto v=s.value("subcards").toArray().first();const double n=v.toDouble(-1);
 if(!v.isDouble()||!std::isfinite(n)||n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max())return false;
 const int id=int(n);if(room.getCardOwner(id)!=p)return false;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){
  const Card*c=Sanguosha->getCard(id);for(const auto&face:r.worldView.handCards)if(newgeneralsPlanning5Exact(face,c,room,p,id))return true;
 }
 // getCards("he") returns actual inner Equip objects. This narrowly proves
 // the current owner's source object; no wrapper/foreign/pile substitution.
 if(room.getCardPlace(id)==Player::PlaceEquip)for(const Card*c:p->getEquips())if(c->getEffectiveId()==id)
  for(const auto&face:r.worldView.self.equips)if(newgeneralsPlanning5Exact(face,c,room,p,id))return true;
 return false;
}

// ---- newgenerals-planning-6 ----
namespace {
bool newgeneralsPlanning6Exact(const AICardView&v,const Card*c,Room&room,const ServerPlayer*p,int id){
 return c&&v.effectiveId==id&&c->getEffectiveId()==id&&v.objectName==c->objectName()
  &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
  &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
}
bool aiKeolsujiConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::UseCard||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.pattern!="@@keolsujislash"
  ||!aiRangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(s.value("source_key")!=QJsonValue("newgenerals.keolsuji")||s.value("skill")!=QJsonValue("keolsuji")
  ||s.value("name")!=QJsonValue("slash")||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)
  ||!s.value("subcards").isArray()||s.value("subcards").toArray().size()!=1)return false;
 // The exact actual response question authorizes a readonly source proposal.
 // Source clones even red/locked materials; selected V2 independently checks both.
 // No extra hasSkill/invalidity/black/using gate is added to source observation.
 const auto v=s.value("subcards").toArray().first();const double n=v.toDouble(-1);
 if(!v.isDouble()||!std::isfinite(n)||n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max())return false;
 const int id=int(n);if(room.getCardOwner(id)!=p)return false;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){
  const Card*c=Sanguosha->getCard(id);for(const auto&face:r.worldView.handCards)if(newgeneralsPlanning6Exact(face,c,room,p,id))return true;
 }
 // getCards("he") returns actual inner Equip objects. This narrowly proves
 // the current owner's source object; no wrapper/foreign/pile substitution.
 if(room.getCardPlace(id)==Player::PlaceEquip)for(const Card*c:p->getEquips())if(c->getEffectiveId()==id)
  for(const auto&face:r.worldView.self.equips)if(newgeneralsPlanning6Exact(face,c,room,p,id))return true;
 return false;
}

// ---- newgenerals-planning-7 ----
namespace {
bool newgeneralsPlanning7Exact(const AICardView&v,const Card*c,Room&room,const ServerPlayer*p,int id){
 return c&&v.effectiveId==id&&c->getEffectiveId()==id&&v.objectName==c->objectName()
  &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
  &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
}
bool aiKeolxiejuConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::UseCard||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.pattern!="@@keolxiejuslash"
  ||!aiRangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(s.value("source_key")!=QJsonValue("newgenerals._keolxieju")||s.value("skill")!=QJsonValue("_keolxieju")
  ||s.value("name")!=QJsonValue("slash")||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)
  ||!s.value("subcards").isArray()||s.value("subcards").toArray().size()!=1)return false;
 // The exact actual response question authorizes a readonly source proposal.
 // Source clones even red/locked materials; selected V2 independently checks both.
 // No extra hasSkill/invalidity/black/using gate is added to source observation.
 const auto v=s.value("subcards").toArray().first();const double n=v.toDouble(-1);
 if(!v.isDouble()||!std::isfinite(n)||n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max())return false;
 const int id=int(n);if(room.getCardOwner(id)!=p)return false;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){
  const Card*c=Sanguosha->getCard(id);for(const auto&face:r.worldView.handCards)if(newgeneralsPlanning7Exact(face,c,room,p,id))return true;
 }
 // getCards("he") returns actual inner Equip objects. This narrowly proves
 // the current owner's source object; no wrapper/foreign/pile substitution.
 if(room.getCardPlace(id)==Player::PlaceEquip)for(const Card*c:p->getEquips())if(c->getEffectiveId()==id)
  for(const auto&face:r.worldView.self.equips)if(newgeneralsPlanning7Exact(face,c,room,p,id))return true;
 return false;
}

// ---- newgenerals-planning-slots ----
#include "ai-strategy/lua-seal.h"
#include "ai-strategy/inovation-ai.h"
#include "card-lifetime-manager.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QHash>
#include <QUuid>
#include <memory>
#include <cstring>
#include "ai-strategy/lua-raw-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
struct Row {QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;};
class Ledger final:public QObject {public:explicit Ledger(Room&r):QObject(&r){} quint64 generation=0;QHash<const ServerPlayer*,QHash<QString,Row>>rows;};
Ledger*ledger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return create?new Ledger(r):nullptr;}
bool current(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool sourceScope(const AIRequest&q,const ServerPlayer*p){
 auto exact=[&](const AiSkillActionContext&a){return a.isValid()&&a.activationRef==a.sourceRef
 &&a.getActivationOwner()==p->objectName()&&(a.getActivationSkillName()=="xdyifuvs"||a.getActivationSkillName()=="xdyifuvs&");};
 if(q.hasSkillActionContext&&exact(q.skillActionContext))return true;
 for(const auto&a:q.skillActions)if(exact(a))return true;return false;
}
bool newgeneralsPlanningSlotsScope(const AIRequest&q,ServerPlayer*p,const QString&slot,bool write){
 if(slot=="godshenji_use")return write?(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="godshenji")
   :(q.kind==AIRequest::UseCard&&q.pattern=="@@godshenji");
 if(slot!="xdyifuvsUse"&&slot!="xdyifuvsTo")return false;
 if(!write&&slot=="xdyifuvsUse")return q.kind==AIRequest::UseCard&&q.pattern=="@@xdyifuvs";
 return q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&sourceScope(q,p);
}
const Card*equippedMaterial(const AIRequest&q,ServerPlayer*p,int id){
 Room&r=*p->getRoom();const auto*owner=r.getCardOwner(id);
 if(!owner||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 const Card*outer=Sanguosha->getCard(id);if(!outer)return nullptr;
 const Card*inner=nullptr;for(const Card*c:owner->getEquips())if(c->getEffectiveId()==id)inner=c;
 if(!inner||outer->getRealCard()!=inner)return nullptr;
 auto players=q.worldView.players;players.prepend(q.worldView.self);
 for(const auto&v:players)if(v.objectName==owner->objectName())for(const auto&e:v.equips){
  if(e.effectiveId!=id||!e.nativeIdentity||!globalCardLifetimeManager().isLive(e.nativeIdentity)
   ||e.nativeIdentity->address!=inner)continue;
  if(owner==p)return outer; // Current owner knows its wrapped equipment face.
  // Foreign public inner equipment alone does not disclose a private transform.
  if(e.cardId==outer->getId()&&e.objectName==outer->objectName()&&e.className==outer->getClassName()
   &&e.suit==int(outer->getSuit())&&e.number==outer->getNumber()&&e.skillName==outer->getSkillName(false)
   &&e.subcardIds==outer->getSubcards()&&e.virtualCard==outer->isVirtualCard())return outer;
 }
 return nullptr;
}
bool shown(const AIRequest&q,ServerPlayer*p,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact){
  if(p&&equippedMaterial(q,p,id))return true;
  exact=Sanguosha->getCard(id);
 }if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void field(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool self(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool absentNewIndex(lua_State*L,int t){Stack st(L);if(!lua_getmetatable(L,t))return true;field(L,-1,"__newindex");return lua_isnil(L,-1);}
bool plain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool env(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool code(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&env(L,f);}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool lookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));field(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool sourceReadBindings(Room&r,lua_State*L,const QString&slot){
 Stack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 field(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 field(L,sgs,"ai_skill_use");if(!plain(L,-1))return false;
 field(L,-1,slot=="godshenji_use"?"@@godshenji":"@@xdyifuvs");
 if(!code(r,L,-1,"lua/ai/newgenerals-ai.lua",slot=="godshenji_use"?7155:6179,slot=="godshenji_use"?7164:6188))return false;
 field(L,sgs,"list");if(!code(r,L,-1,"lua/utilities.lua",38,41))return false;
 field(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=code(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=code(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){field(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 field(L,globals,"table");if(!plain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){field(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Only this native thunk allocates game-VM values. LuaAI policy is never called.
struct Write {const AIRequest*q;ServerPlayer*owner;QByteArray key;const Card*card=nullptr;
 AIReadonlyCardLease lease;QList<ServerPlayer*>targets;ServerPlayer*player=nullptr;bool playerSlot=false,literal=false;int extra=0;};
int assign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"saved plan assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 if(w->playerSlot){if(!pushAISlotPlayer(L,w->player))return luaL_error(L,"saved player wrapper unavailable");}
 else {
  lua_newtable(L);const int d=lua_absindex(L,-1);
  if(!w->literal){lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");lua_newtable(L);lua_setfield(L,d,"current_targets");}
  if(!pushAISlotCard(L,w->card))return luaL_error(L,"saved card wrapper unavailable");lua_setfield(L,d,"card");
  if(w->literal)lua_newtable(L);
  else if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"saved target list unavailable");
  lua_setfield(L,d,"to");
 }
 const int value=lua_absindex(L,-1);
 if(!current(*w->q,w->owner)||(!w->playerSlot&&w->lease()!=w->card)
  ||!self(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))return luaL_error(L,"saved plan actual receiver changed during allocation");lua_pop(L,1);
 field(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,1))return luaL_error(L,"saved plan assignment has dynamic __newindex");
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());
 return 0;
}
bool target(Room&r,lua_State*L,int index,ServerPlayer*&out){
 for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,index,p)){
  if(!matchAINativeObjectMethods(r,L,index,"ServerPlayer",{"objectName"}))return false;out=p;return true;
 }return false;
}
}
bool registerAINewgeneralsPlanningSlotSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/newgenerals-ai.lua",QByteArray("df00146274a0ba5391347756be89053d93bc614b45c007226b4fb4b7d509d8e5"),error)
  &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
int aiNewgeneralsXdyifuPrototype(const AIRequest&q,ServerPlayer*p,const QString&skill,const QString&prototype){
 if(!current(q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||!q.pattern.isEmpty()||skill!="xdyifuvs"||prototype!="#xdyifucard"||!sourceScope(q,p))return -1;
 return 3; // Fixed source: one Basic hand material and declared ordinary name.
}
const Card*aiNewgeneralsGodshenjiMaterial(const AIRequest&q,ServerPlayer*p,int id){
 if(!current(q,p)||q.kind!=AIRequest::PlayerChosen||q.choiceOptions.reason!="godshenji")return nullptr;
 const auto*owner=p->getRoom()->getCardOwner(id);
 if(!owner||!q.choiceOptions.playerNames.contains(owner->objectName()))return nullptr;
 return equippedMaterial(q,p,id);
}
void clearAINewgeneralsPlanningSlots(Room&r){if(auto*l=ledger(r))delete l;}
QJsonObject queryAINewgeneralsPlanningSlot(const AIRequest&q,ServerPlayer*p,const QString&slot,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","newgenerals.saved_plan"}};};
 if(op!="write"&&op!="read")return fail("unknown saved-plan operation");const bool writing=op=="write";
 if(!current(q,p)||!newgeneralsPlanningSlotsScope(q,p,slot,writing))return fail("saved plan requires exact current owner source inquiry");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("saved plan game runtime unavailable");Stack stack(L);
 if(!self(L,r,p))return fail("saved plan actual LuaAI self unavailable");const int receiver=lua_absindex(L,-1);
 const QByteArray key=slot.toUtf8();
 if(writing){
  Write w;w.q=&q;w.owner=p;w.key=key;w.playerSlot=slot=="xdyifuvsTo";
  if(w.playerSlot){
   if(payload.size()!=1||!payload.value("target").isString())return fail("saved player shape malformed");
   const auto name=payload.value("target").toString();
   for(auto*t:r.getAllPlayers(true))if(t->objectName()==name)w.player=t;
   if(!w.player)return fail("saved player outside current roster");
   bool visible=q.worldView.self.objectName==name;for(const auto&v:q.worldView.players)visible=visible||v.objectName==name;
   if(!visible)return fail("saved player not projected to this owner");
  }else{
   if(payload.size()!=3||!payload.value("selector").isObject()||!payload.value("targets").isArray()||!payload.value("source_fields").isObject()||!lease)return fail("saved source plan shape malformed");
   const auto f=payload.value("source_fields").toObject();w.literal=f.value("literal")==QJsonValue(true);
   if(w.literal){if(slot!="godshenji_use"||f.size()!=1||!payload.value("targets").toArray().isEmpty())return fail("literal saved plan differs from fixed source");}
   else if(f.size()!=3||f.value("isDummy")!=QJsonValue(true)||f.value("current_targets_empty")!=QJsonValue(true)
    ||!f.value("extra_target").isDouble()||f.value("extra_target").toDouble()!=f.value("extra_target").toInt())return fail("saved dummy fields malformed");
   w.extra=f.value("extra_target").toInt();w.lease=std::move(lease);w.card=w.lease();if(!w.card)return fail("saved source card custody expired");
   if(!w.card->isVirtualCard()&&!shown(q,p,w.card->getEffectiveId(),w.card))return fail("saved physical source card not currently disclosed");
   for(int id:w.card->getSubcards())if(!shown(q,p,id))return fail("saved source material not currently disclosed");
   if(payload.value("targets").toArray().size()>128)return fail("saved target budget exceeded");
   for(const auto&n:payload.value("targets").toArray()){
    if(!n.isString())return fail("saved target is not a name");ServerPlayer*found=nullptr;
    for(auto*t:r.getAllPlayers(true))if(t->objectName()==n.toString())found=t;
    if(!found)return fail("saved target outside roster");w.targets<<found;
   }
  }
  // Stack function and arguments do not allocate a captured C closure.
  lua_pushcfunction(L,assign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("exact native saved-plan assignment failed");
  // Reached assignment is not rolled back on errors.
  if(!current(q,p)||(!w.playerSlot&&w.lease()!=w.card))return fail("saved-plan assignment changed request or custody");
  if(!w.playerSlot){auto*l=ledger(r,true);if(l->generation&&l->generation!=rt->lua().generation())return fail("saved plan game generation changed");
   l->generation=rt->lua().generation();l->rows[p][slot]=Row{p,std::move(w.lease)};}
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("saved read accepts no descriptor or prior authority");
 if(!lookup(L,receiver,key.constData()))return fail("saved slot requires unproved dynamic getter");const int saved=lua_absindex(L,-1);
 if(slot=="xdyifuvsTo"){
  if(!lua_toboolean(L,saved))return{{"status","absent"}};ServerPlayer*t=nullptr;
  if(!target(r,L,saved,t))return fail("saved player identity or source dispatch changed");
  if(!current(q,p))return fail("saved player read changed request");return{{"status","player"},{"target",t->objectName()}};
 }
 if(lua_isnil(L,saved)||lua_isboolean(L,saved)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,saved)))return fail("saved primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source plan)").arg(lua_isnil(L,saved)?"nil":"boolean")}};
 }
 if(!lua_istable(L,saved)||!lookup(L,saved,"card"))return fail("saved card field unavailable");const int cardIndex=lua_absindex(L,-1);
 if(!lua_toboolean(L,cardIndex))return{{"status","pass"}};
 if(!lookup(L,saved,"to"))return fail("saved targets require unproved getter");const int list=lua_absindex(L,-1);
 if(!sourceReadBindings(r,L,slot))return fail("saved consumer serializer/iterator bindings changed");
 // Source iterates targets before calling card:toString. Preserve that order.
 QList<ServerPlayer*>targets;
 if(lua_istable(L,list)){
  if(!plain(L,list))return fail("saved plain target list has unproved metatable");
  for(int n=1;n<=129;++n){lua_rawgeti(L,list,n);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
   if(n==129)return fail("saved target budget exceeded");ServerPlayer*t=nullptr;
   if(!target(r,L,-1,t))return fail("saved table target identity or method changed");targets<<t;lua_pop(L,1);
  }
 }else if(!readAISlotTargets(L,list,targets))return fail("saved native target list custody or dispatch changed");
 auto*l=ledger(r);if(!l||l->generation!=rt->lua().generation())return fail("saved actual Card custody unavailable");
 const auto row=l->rows.value(p).value(slot);const Card*c=row.lease?row.lease():nullptr;
 if(!c||row.owner!=p||!matchAISlotCard(L,cardIndex,c)
  ||!matchAINativeObjectMethods(r,L,cardIndex,"Card",{"toString"}))return fail("saved actual Card identity or native serializer changed");
 if(!c->isVirtualCard()&&!shown(q,p,c->getEffectiveId(),c))return fail("saved physical face no longer disclosed");
 for(int id:c->getSubcards())if(!shown(q,p,id))return fail("saved current material printing not lawfully observable");
 QJsonArray names;const auto roster=r.getAllPlayers(true);for(auto*t:targets){if(!t||!roster.contains(t))return fail("saved target left roster");names.append(t->objectName());}
 const QString wire=c->toString();
 if(!current(q,p)||row.lease()!=c)return fail("saved observation changed request or custody");
 return{{"status","plan"},{"wire",wire},{"targets",names},{"evaluation_only",true}};
}

// ---- newgenerals-planning ----
#include "ai-strategy/context.h"
namespace {
bool inquiry(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&skill){
 return p&&p->getRoom()==&room&&!room.roomRuntime()->isClosing()&&r.decisionId!=0
  &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&r.stateRevision==room.roomRuntime()->stateRevision()&&p->hasSkill(skill)
  &&r.kind==AIRequest::UseCard&&r.pattern=="@@"+skill
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);
}
}
bool aiNewgeneralsMouConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return false;
 for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return false;
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()
  ||!s.value("suit").isDouble()||s.value("suit").toDouble()!=int(Card::NoSuit)
  ||!s.value("number").isDouble()||s.value("number").toDouble()!=0
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 const QString key=s.value("source_key").toString();
 QString source,raw,name;
 if(key=="newgenerals.moulveying"){source=raw="moulveying";name="dismantlement";}
 else if(key=="newgenerals.mouyingwu"){source=raw="mouyingwu";name="slash";}
 else if(key=="newgenerals.mouxieji"){source="mouxieji";raw="_mouxieji";name="slash";}
 else return false;
 return s.value("name")==QJsonValue(name)&&s.value("skill")==QJsonValue(raw)&&inquiry(r,room,p,source);
}
int aiMouxiejiExtraTarget(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 return s.value("source_key")==QJsonValue("newgenerals.mouxieji")&&aiNewgeneralsMouConstruction(r,room,p,s)?2:0;
}
bool aiMouxiejiSource(const AIRequest&r,Room&room,const ServerPlayer*p,const Card*c,const QString&key){
 if(!c||key!="newgenerals.mouxieji"||c->getId()>=0||!c->isVirtualCard()
  ||c->objectName()!="slash"||c->getSkillName(false)!="_mouxieji"
  ||c->getSuit()!=Card::NoSuit||c->getNumber()!=0||!c->getSubcards().isEmpty())return false;
 return aiNewgeneralsMouConstruction(r,room,p,{{"source_key",key},{"name","slash"},{"skill","_mouxieji"},
  {"suit",int(Card::NoSuit)},{"number",0},{"subcards",QJsonArray{}}});
}
QJsonObject aiMouxiejiTargetFilter(Room&room,ServerPlayer*p,const AIRequest&r,const Card*c,const QString&key,const QJsonObject&q){
 auto reject=[](const char*s){return QJsonObject{{"error",s},{"key","newgenerals.mouxieji"}};};
 if(!aiMouxiejiSource(r,room,p,c,key))return reject("Mouxieji readonly source identity changed");
 if(q.size()!=2||!q.value("prefix").isArray()||!q.value("target").isString())return reject("Mouxieji prefix query malformed");
 QList<const Player*>prefix;QSet<QString>seen;
 const auto names=q.value("prefix").toArray();if(names.size()>room.getAlivePlayers().size())return reject("Mouxieji prefix exceeds live roster");
 for(const auto&n:names){
  if(!n.isString()||seen.contains(n.toString()))return reject("Mouxieji prefix is not distinct");
  auto*to=room.findPlayerByObjectName(n.toString());if(!to||!to->isAlive())return reject("Mouxieji prefix player absent");
  seen.insert(n.toString());prefix<<to;
 }
 auto*to=room.findPlayerByObjectName(q.value("target").toString());if(!to||!to->isAlive())return reject("Mouxieji target absent");
 const auto revision=room.roomRuntime()->stateRevision();
 AITianyiQueryScope scope(p,c,2,false);const bool accepted=c->targetFilter(prefix,to,p);
 if(revision!=room.roomRuntime()->stateRevision())return reject("Mouxieji readonly query changed state");
 return{{"accepted",accepted}};
}
int aiMouxiejiPrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){
 if(!p||!p->getRoom()||skill!="mouxieji"||prototype!="#mouxiejiCard"||!inquiry(r,*p->getRoom(),p,skill))return -1;
 // Fixed source returns an empty, undeclared SkillCard after Slash simulation.
 // Existing native instance/prefix/factory/final checks still admit or reject it.
 return 0;
}
