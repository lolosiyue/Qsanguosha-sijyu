#include "ai-strategy/diy.h"

// ---- meow-kexianbao-choice-choice-2 ----
#include "ai-strategy/runtime.h"
#include "ai-strategy/native-registry.h"
#include "ai-strategy/owner.h"
#include "ai-strategy/context.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include "standard.h"
#include <memory>
namespace {
bool meowKexianbaoChoiceChoice2Current(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.viewerObjectName==p->objectName()
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
int aiMeowLiuliChoicePrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){
 if(!meowKexianbaoChoiceChoice2Current(r,p)||!p->hasSkill(skill))return -1;
 if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY
    &&(skill=="kexianchanxin"||skill=="kejiexianchanxin")&&prototype=="#"+skill+"Card")return 1;
 if(r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
  if(skill=="MeowLiuli"&&prototype=="#MeowLiuliCard"&&r.pattern=="@@MeowLiuli"&&r.handlingMethod==Card::MethodDiscard)return 1;
  if(skill=="Meowxianzhou"&&prototype=="#MeowxianzhouCard"&&r.pattern=="@@Meowxianzhou")return 0;
 }
 return -1;
}
void projectAIMeowLiuliChoiceContext(AIRequest&r,Room&room,ServerPlayer*viewer,QJsonObject&context,
 const std::function<QJsonObject(const Card*)>&publicCard){
 if(!meowKexianbaoChoiceChoice2Current(r,viewer)||viewer->getRoom()!=&room||!publicCard)return;
 auto facts=context.value("round22_choice").toObject();
 // Exact source static null-Slash call; never enumerate conversions first.
 if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill("kexianchanxin"); }()))
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

// ---- meow-kexianbao-choice-choice-3 ----
#include <QJsonArray>

int aiMeowGuoseChoicePrototype(const AIRequest &request, const ServerPlayer *viewer,
                             const QString &skill, const QString &prototype)
{
    if (!viewer || !viewer->getRoom()
        || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != viewer->getRoom()->roomRuntime()->stateRevision()
        || !viewer->hasSkill(skill))
        return -1;
    if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        && skill == "MeowLieren" && prototype == "#MeowLierenCard" && request.pattern == "@@MeowLieren") return 0;
    if (request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return -1;
    if ((skill == "Meowxianzhou" || skill == "kexianfenshen"
         || skill == "kejiexianfenshen" || skill == "kexianjishi") && prototype == "#" + skill + "Card")
        return 1;
    if (skill == "MeowGuose" && (prototype == "#MeowGuoseCard" || prototype == "#MeowGuoseCard2"))
        return 1;
    return -1;
}

// Exactly cloneCard("Indulgence") + addSubcard(id). No skill name is assigned
// by the donor; the generic audited constructor handles the same clone and
// inherited one-material getter semantics. It grants no payment receipt.
bool aiMeowGuoseChoiceConstruction(const AIRequest &request, const ServerPlayer *viewer,
                                 const QJsonObject &spec)
{
    if (spec.value("source_key") == QJsonValue("MeowQingguo")) {
        if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
            || request.stateRevision != viewer->getRoom()->roomRuntime()->stateRevision()
            || request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || !viewer->hasSkill("MeowQingguo") || viewer->hasSkill("Meowdoumiao")
            || spec.size()!=6 || spec.value("skill")!=QJsonValue("MeowQingguo")
            || spec.value("name")!=QJsonValue("peach") || !spec.value("subcards").isArray()) return false;
        const auto ids=spec.value("subcards").toArray();
        if (ids.size()!=1 || !ids[0].isDouble() || ids[0].toDouble()!=ids[0].toInt(-1)
            || ids[0].toInt(-1)<0 || viewer->getRoom()->getCardOwner(ids[0].toInt())!=viewer) return false;
        const Card *material=viewer->getRoom()->getCard(ids[0].toInt());
        return material && material->isKindOf("Jink")
            && spec.value("suit")==QJsonValue(int(material->getSuit()))
            && spec.value("number")==QJsonValue(material->getNumber());
    }
    if (aiMeowGuoseChoicePrototype(request, viewer, "MeowGuose", "#MeowGuoseCard2") < 0
        || spec.size() != 6 || spec.value("source_key") != QJsonValue("MeowGuose")
        || spec.value("skill") != QJsonValue("") || spec.value("name") != QJsonValue("indulgence")
        || spec.value("suit") != QJsonValue(int(Card::NoSuit)) || spec.value("number") != QJsonValue(0)
        || !spec.value("subcards").isArray()) return false;
    const auto ids = spec.value("subcards").toArray();
    if (ids.size() != 1 || !ids[0].isDouble() || ids[0].toDouble() != ids[0].toInt(-1)
        || ids[0].toInt(-1) < 0) return false;
    return viewer->getRoom()->getCardOwner(ids[0].toInt()) == viewer;
}

void projectAIMeowLierenChoiceContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()) return;
    if (viewer->hasSkill("MeowLieren")
        && ((request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "MeowLieren")
            || (request.kind == AIRequest::UseCard && request.pattern == "@@MeowLieren"))) {
        QJsonObject flags;
        for (const auto *player : room.getAllPlayers(true))
            flags.insert(player->objectName(), QJsonObject{{"MeowLierenMarkto",player->hasFlag("MeowLierenMarkto")},
                {"MeowLierenMarkfrom",player->hasFlag("MeowLierenMarkfrom")}});
        auto row=context.value("round23_choice").toObject();row.insert("lieren_flags",flags);
        context.insert("round23_choice",row);
    }
    if (request.kind != AIRequest::Choice || request.choiceOptions.reason != "MeowBeige"
        || !viewer->hasSkill("MeowBeige") || !publicCard || !data.canConvert<DamageStruct>()) return;
    const QVariant tag = room.getTag("CurrentDamageStruct");
    if (!tag.canConvert<DamageStruct>()) return;
    const auto actual = tag.value<DamageStruct>(), asked = data.value<DamageStruct>();
    const auto inRoster = [&](const ServerPlayer *p) {
        return p && room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(p));
    };
    if (!inRoster(actual.to) || (actual.from && !inRoster(actual.from))
        || actual.from != asked.from || actual.to != asked.to || actual.card != asked.card
        || actual.damage != asked.damage || actual.nature != asked.nature
        || !actual.card || !actual.card->isKindOf("Slash")) return;
    // Both the actual Choice data and its scoped CurrentDamage tag identify the
    // already announced Slash damage. No unrelated observer/tag query is added.
    QJsonObject row{{"to", actual.to->objectName()}, {"from", actual.from ? QJsonValue(actual.from->objectName()) : QJsonValue(false)},
        {"card", publicCard(actual.card)}, {"amount", actual.damage}, {"damage", actual.damage},
        {"nature", int(actual.nature)}, {"chain", actual.chain}, {"transfer", actual.transfer},
        {"by_user", actual.by_user}, {"reason", actual.reason}};
    context.insert("damage", row);
}

// ---- meow-kexianbao-choice-choice ----
#include "maneuvering.h"

int aiMeowTianxiangChoicePrototype(const AIRequest &request, const ServerPlayer *viewer,
                             const QString &skill, const QString &prototype)
{
    if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != viewer->getRoom()->roomRuntime()->stateRevision()
        || !viewer->hasSkill(skill)) return -1;
    const bool play = request.kind == AIRequest::Activate
        && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    // b6025 ai/meow-ai.lua99-110; extensions/meow.lua177/253. Source seed
    // and selected product have zero materials; targets are chosen by usefunc.
    if (skill == QLatin1String("Meowlijian") && prototype == QLatin1String("#MeowlijianCard"))
        return play ? 0 : -1;
    // b6025 ai/keguibao-ai.lua252-300; extensions/keguibao.lua624/641.
    // Empty readonly seed is allowed. Nonempty selection feasibility, ordered
    // owner materials and target count are still checked by the real V2 rule.
    if (skill == QLatin1String("keguitiqi") && prototype == QLatin1String("#keguitiqiCard"))
        return play ? 1 : -1;
    // Exact live response-use inquiries; these masks permit the donor single
    // material proposal, never arbitrary skill/prototype aliases.
    if (request.kind == AIRequest::UseCard
        && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        if (skill == "MeowTianxiang" && prototype == "#MeowTianxiangCard"
            && request.pattern == "@@MeowTianxiang" && request.handlingMethod == Card::MethodDiscard) return 1;
        if ((skill == "kexiantianbian" || skill == "kejiexiantianbian")
            && prototype == "#" + skill + "_Card" && request.pattern == "@" + skill) return 1;
    }
    // b6025 ai/inovation-ai.lua1119-1186; inovation.cpp3900/3986. Source
    // declares four response Basic names and Tacos in Play; it pays no cards.
    // Do not repair its free-global `to_use` bug or bypass the native judge.
    if (skill == QLatin1String("inovation_fengzhu")
        && prototype == QLatin1String("InovationFengzhuCard")) {
        const bool response = (request.kind == AIRequest::RespondCard || request.kind == AIRequest::UseCard)
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && !request.pattern.startsWith(QLatin1String("@@"));
        return play || response ? 2 : -1;
    }
    return -1;
}

bool aiSourceAnalepticAvailable(const ServerPlayer *viewer)
{
    return viewer && Analeptic::IsAvailable(viewer, nullptr);
}

namespace {
bool sameKnownFace(const AICardView &row, const Card *card)
{
    return row.effectiveId == card->getEffectiveId() && row.objectName == card->objectName()
        && row.className == card->getClassName() && row.suit == int(card->getSuit())
        && row.number == card->getNumber();
}
bool lawfulPindianFace(const AIRequest &request, Room &room, const Card *card)
{
    if (!card || card->isVirtualCard() || card->getEffectiveId() < 0) return false;
    const int id=card->getEffectiveId();
    const Card *live=room.getCard(id);
    if (!live || live->objectName()!=card->objectName() || live->getClassName()!=card->getClassName()
        || live->getSuit()!=card->getSuit() || live->getNumber()!=card->getNumber()) return false;
    // serverplayer.cpp1098-1112 sends $PindianResult and a visible atomic
    // move to PlaceTable BEFORE finishPindian triggers PindianVerifying1137.
    // Live public zone is independent proof; the tag/prompt alone is not.
    const auto place=room.getCardPlace(id);
    if (place==Player::PlaceTable || place==Player::DiscardPile) return true;
    for (const auto &known:request.worldView.handCards) if (sameKnownFace(known,card)) return true;
    for (const auto &player:request.worldView.players)
        for (const auto &known:player.knownCards) if (sameKnownFace(known,card)) return true;
    return false;
}
bool rosterPlayer(Room &room, const ServerPlayer *player)
{
    return !player || room.findPlayerByObjectName(player->objectName(),true)==player;
}
}

void projectAIMeowTianxiangChoiceContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    QJsonObject &context, const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=room.roomRuntime()->stateRevision() || !publicCard
        || request.kind!=AIRequest::UseCard
        || request.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return;
    QJsonObject row=context.value("round21_choice").toObject();
    if (request.pattern=="@@MeowTianxiang" && request.prompt=="@tianxiang-card"
        && request.handlingMethod==Card::MethodDiscard && viewer->hasSkill("MeowTianxiang")) {
        // extensions/meow.lua1355 stores this owner's DamageInflicted data;
        // 1356 opens exactly this request. No other owner's tag is accessed.
        const QVariant value=viewer->getTag("MeowTianxiangDamage");
        if (!value.isValid() || value.isNull()) row.insert("meow_tianxiang_damage",false);
        else if (value.canConvert<DamageStruct>()) {
            const auto damage=value.value<DamageStruct>();
            if (rosterPlayer(room,damage.from) && rosterPlayer(room,damage.to)) {
                row.insert("meow_tianxiang_damage",QJsonObject{
                    {"from",damage.from?QJsonValue(damage.from->objectName()):QJsonValue(false)},
                    {"to",damage.to?QJsonValue(damage.to->objectName()):QJsonValue(false)},
                    {"amount",damage.damage},{"nature",int(damage.nature)},
                    {"chain",damage.chain},{"transfer",damage.transfer},
                    {"card",damage.card?QJsonValue(publicCard(damage.card)):QJsonValue(false)}});
            }
        }
    }
    const QString skill=request.pattern=="@kexiantianbian"?"kexiantianbian"
        :request.pattern=="@kejiexiantianbian"?"kejiexiantianbian":QString();
    if (!skill.isEmpty() && viewer->hasSkill(skill)
        && request.prompt.startsWith("@kexiantianbian_Pindian::"+skill+":")) {
        // Both fixed extension on_cost writers use public room:setPlayerFlag;
        // preserve the full getAllPlayers loop and last matching player in Lua.
        QJsonObject flags;
        for (const auto *p:room.getAllPlayers(true)) flags.insert(p->objectName(),QJsonObject{
            {"Source",p->hasFlag(skill+"_Source")},{"Target",p->hasFlag(skill+"_Target")}});
        row.insert("tianbian_skill",skill);row.insert("tianbian_flags",flags);
        const QVariant value=room.getTag("CurrentPindianStruct");
        if (!value.isValid() || value.isNull()) row.insert("tianbian_pindian",false);
        else if (value.canConvert<PindianStruct *>()) {
            const auto *p=value.value<PindianStruct *>();
            if (!p) row.insert("tianbian_pindian",false);
            else if (rosterPlayer(room,p->from) && rosterPlayer(room,p->to)
                && lawfulPindianFace(request,room,p->from_card) && lawfulPindianFace(request,room,p->to_card)) {
                row.insert("tianbian_pindian",QJsonObject{
                    {"from_card",publicCard(p->from_card)},{"to_card",publicCard(p->to_card)}});
            }
            // An unproved face leaves the field absent: never pretend no event.
        }
    }
    if (!row.isEmpty()) context.insert("round21_choice",row);
}

// ---- olddiy-keguibao-choice ----
#include "card.h"

namespace {
bool olddiyKeguibaoChoiceScope(const AIRequest &r,const ServerPlayer *p) {
 return p && p->getRoom() && r.viewerObjectName==p->objectName()
     && r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();
}
}
int aiOlddiyKeguibaoChoicePrototype(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QString &prototype) {
 if (!olddiyKeguibaoChoiceScope(r,p) || !p->hasSkill(skill)) return -1;
 if (r.kind==AIRequest::UseCard && r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
  if (skill=="cangji" && prototype=="CangjiCard" && r.pattern=="@@cangji") return 1;
  if (skill=="dangqian" && prototype=="#dangqianCard" && r.pattern=="@@dangqian") return 0;
 }
 if (r.kind!=AIRequest::Activate || r.reason!=CardUseStruct::CARD_USE_REASON_PLAY) return -1;
 if ((skill=="xiechan" && prototype=="XiechanCard") || (skill=="mouzhu" && prototype=="MouzhuCard")
     || (skill=="zfduanhe" && prototype=="#zfduanhe") || (skill=="bu_s2_benxi" && prototype=="#bu_s2_benxi")
     || (skill=="kejieguizhashi" && prototype=="#kejieguizhashiCard")) return 0;
 if ((skill=="puji" && prototype=="PujiCard") || (skill=="luajuao" && prototype=="#luajuaocard")
     || (skill=="lyduji" && prototype=="#lyduji") || (skill=="lyxiance" && prototype=="#lyxiance")
     || (skill=="LuaFangxian" && prototype=="#LuaFangxianCard")) return 1;
 for (const QString &name : {QStringLiteral("keguishouye"),QStringLiteral("keguijiehuo"),
                            QStringLiteral("kejieguishouye"),QStringLiteral("kejieguijiehuo")})
  if (skill==name && prototype=="#"+name+"Card") return 1;
 if ((skill=="keguixianjiVS" && prototype=="#keguixianjiCard")
     || (skill=="kejieguixianjiVS" && prototype=="#kejieguixianjiCard")) return 1;
 return -1;
}

bool aiOlddiyKeguibaoChoiceConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s) {
 if (!olddiyKeguibaoChoiceScope(r,p) || s.size()!=6 || !s.value("subcards").isArray()) return false;
 const QString key=s.value("source_key").toString(), skill=s.value("skill").toString();
 const auto costs=s.value("subcards").toArray();
 if (key=="krskitgjiamian" && r.kind==AIRequest::Activate && r.reason==CardUseStruct::CARD_USE_REASON_PLAY
     && p->hasSkill(key) && skill==key && s.value("name")==QJsonValue("slash") && costs.size()==1
     && costs[0].isDouble() && costs[0].toDouble()==costs[0].toInt(-1)) {
  const int id=costs[0].toInt(-1);Room *room=p->getRoom();const Card *c=id>=0?room->getCard(id):nullptr;
  return c && room->getCardOwner(id)==p && room->getCardPlace(id)==Player::PlaceHand && c->isKindOf("Jink")
      && s.value("suit")==QJsonValue(int(c->getSuit())) && s.value("number")==QJsonValue(c->getNumber());
 }
 if (key=="spwushen" && r.kind==AIRequest::PlayerChosen && r.choiceOptions.reason=="spwushen"
     && p->hasSkill("spwushen") && s.value("name")==QJsonValue("fire_slash")
     && s.value("suit")==QJsonValue(int(Card::Heart)) && s.value("number")==QJsonValue(0)
     && costs.isEmpty() && (skill.isEmpty() || skill=="spwushen")) return true;
 const bool niluanQuestion = (r.kind==AIRequest::UseCard && r.pattern=="@@niluan"
     && r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
     || (r.kind==AIRequest::RespondCard && r.choiceOptions.question=="askForCard");
 if (key!="@niluan-slash" || !niluanQuestion
     || r.prompt.section(':',0,0)!="@niluan-slash" || !p->hasSkill("niluan")
     || s.value("name")!=QJsonValue("slash") || s.value("suit")!=QJsonValue(int(Card::SuitToBeDecided))
     || s.value("number")!=QJsonValue(-1) || (!skill.isEmpty() && skill!="niluan") || costs.size()>1) return false;
 if (costs.isEmpty()) return true;
 if (!costs[0].isDouble() || costs[0].toDouble()!=costs[0].toInt(-1)) return false;
 const int id=costs[0].toInt(-1);Room *room=p->getRoom();
 return id>=0 && room->getCardOwner(id)==p
     && (room->getCardPlace(id)==Player::PlaceHand || room->getCardPlace(id)==Player::PlaceEquip);
}

void projectAIOlddiyKeguibaoChoiceContext(AIRequest &r,Room &room,ServerPlayer *p,const QVariant &data,QJsonObject &context,
                                  const std::function<QJsonObject(const Card *)> &publicCard) {
 if (!olddiyKeguibaoChoiceScope(r,p) || p->getRoom()!=&room || !publicCard) return;
 const QString reason=r.choiceOptions.reason;
 auto row=context.value("round25_choice").toObject();
 const auto roster=[&](const Player *v){return v && room.findPlayerByObjectName(v->objectName(),true)==v;};
 if (r.kind==AIRequest::PlayerChosen && reason=="LuaGaobi" && p->hasSkill(reason))
  row.insert("slash_available",Slash::IsAvailable(p,nullptr));
 if (r.kind==AIRequest::SkillInvoke && reason=="keguiqinwang" && p->hasSkill(reason)
     && data.canConvert<ServerPlayer *>()) {
  const auto tag=room.getTag("CurrentDamageStruct");
  if (tag.canConvert<DamageStruct>()) {
   const auto d=tag.value<DamageStruct>();
   if (roster(d.to) && d.to==data.value<ServerPlayer *>() && (!d.from || roster(d.from))) {
    QJsonObject damage{{"to",d.to->objectName()},{"from",d.from?QJsonValue(d.from->objectName()):QJsonValue(false)},
      {"amount",d.damage},{"nature",int(d.nature)},{"chain",d.chain},{"transfer",d.transfer}};
    if (d.card) damage.insert("card",publicCard(d.card));row.insert("current_damage",damage);
   }
  }
 }
 if (reason=="kejieguisheji" && p->hasSkill(reason)
     && (r.kind==AIRequest::SkillInvoke || r.choiceOptions.question=="askForPindian")) {
  const auto tag=room.getTag("CurrentUseStruct");
  if (tag.canConvert<CardUseStruct>()) {
   const auto use=tag.value<CardUseStruct>();bool valid=roster(use.from) && use.card && use.card->isKindOf("Slash");
   for (auto *to:use.to) valid=valid && roster(to);
   if (r.kind==AIRequest::SkillInvoke) valid=valid && data.canConvert<ServerPlayer *>() && use.to.contains(data.value<ServerPlayer *>());
   if (valid) {
    QJsonArray tos;for(auto *to:use.to)tos.append(to->objectName());
    row.insert("current_use",QJsonObject{{"from",use.from->objectName()},{"to",tos},{"card",publicCard(use.card)}});
    QJsonValue target(false);for(auto *to:room.getOtherPlayers(p))if(to->hasFlag("kejieguishejiPindianTarget")){target=to->objectName();break;}
    row.insert("sheji_pindian_target",target);
   }
  }
 }
 if (!row.isEmpty()) context.insert("round25_choice",row);
}

// ---- srkill-choice ----
#include <QObject>
#include <QSet>
#include <QHash>
namespace {
class FlagLedger final:public QObject {
public: explicit FlagLedger(Room&room):QObject(&room){setObjectName("isolated-round26-choice-flags");}
 QHash<QString,QJsonObject> rows;QSet<ServerPlayer*> connected;
};
FlagLedger*flagLedger(Room&room,bool create){
 auto*object=room.findChild<QObject*>("isolated-round26-choice-flags",Qt::FindDirectChildrenOnly);
 if(!object&&create)return new FlagLedger(room);
 return static_cast<FlagLedger*>(object);
}
bool witnessedFlag(Room&room,ServerPlayer*p,const QString&name){
 const auto*ledger=flagLedger(room,false);if(!ledger||!p)return false;
 const auto value=ledger->rows.value(p->objectName()).value(name);
 return value.isBool()&&value.toBool()&&p->hasFlag(name);
}
bool valid(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.viewerObjectName==p->objectName()&&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
bool owns(const ServerPlayer*p,const QString&s){return p->hasSkill(s)||(s=="jiwu_extarget"&&p->hasSkill("sr_jiwu"));}
}
void observeAISRkillChoiceFlag(Room&room,ServerPlayer*p,const QString&flag){
 if(!p||p->getRoom()!=&room||room.findPlayerByObjectName(p->objectName(),true)!=p)return;
 const bool clear=flag.startsWith('-');const QString name=clear?flag.mid(1):flag;
 if(name!="dailao_target"&&name!="youdisource")return;
 auto*ledger=flagLedger(room,true);
 if(!ledger->connected.contains(p)){
  ledger->connected.insert(p);const QString player=p->objectName();
  // A direct/private mutation (including clear then restore) must never revive
  // an old public witness. This conservative invalidation is local to evidence.
  QObject::connect(p,&Player::gameplay_property_changed,ledger,[ledger,player]{ledger->rows.remove(player);});
  QObject::connect(p,&QObject::destroyed,ledger,[ledger,p,player]{ledger->connected.remove(p);ledger->rows.remove(player);});
 }
 auto row=ledger->rows.value(p->objectName());
 if(p->hasFlag(name)==!clear)row.insert(name,!clear);else row.remove(name);
 ledger->rows.insert(p->objectName(),row);
}
int aiSRkillChoicePrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){
 if(!valid(r,p)||!owns(p,skill))return -1;
 // Exact source cardsview callbacks return these legacy SkillCards without
 // a declaration string. Native provider activation/cost validation still runs.
 if((r.kind==AIRequest::RespondCard||r.kind==AIRequest::UseCard)
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE)){
  const bool peach=r.pattern=="peach"||r.pattern=="peach+analeptic"||r.pattern=="analeptic+peach";
  if(skill=="sr_fangxin"&&prototype=="#sr_fangxincard"&&peach)return 1;
  if(skill=="sr_qingcheng"&&prototype=="#sr_qingchengcard"&&(r.pattern=="slash"||r.pattern=="jink"))return 0;
  if(skill=="sr_xiahou"&&prototype=="#sr_xiahoucard"&&(peach||r.pattern=="slash"||r.pattern=="jink"||r.pattern=="analeptic"))return 0;
 }
 if(r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
  if(skill=="sr_quanheng_slash"&&prototype=="#sr_quanheng_slashCard"&&r.pattern=="@@sr_quanheng_slash")return 1;
  if(skill=="sr_quanheng_ex_nihilo"&&prototype=="#sr_quanheng_ex_nihiloCard"&&r.pattern=="@@sr_quanheng_ex_nihilo")return 1;
  if(skill=="sr_shuixi"&&prototype=="#sr_shuixicard"&&r.pattern=="@@sr_shuixi")return 1;
  if(skill=="sr_fuzheng"&&prototype=="#sr_fuzhengcard"&&r.pattern=="@@sr_fuzheng")return 0;
  if(skill=="sr_youdi"&&prototype=="#sr_youdicard"&&r.pattern=="@@sr_youdi")return 1;
  if(skill=="jiwu_extarget"&&prototype=="#jiwu_extarget"&&r.pattern=="@@jiwu_extarget")return 0;
 }
 if(r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||p->getPhase()!=Player::Play)return -1;
 if(skill=="sr_chouxi"&&prototype=="#sr_chouxicard")return 1;
 if(skill=="sr_shouji"&&prototype=="#sr_shoujicard")return 1;
 if(skill=="sr_yaozhan"&&prototype=="#sr_yaozhancard")return 1;
 if(skill=="sr_wenjiu"&&prototype=="#sr_wenjiucard")return 1;
 if(skill=="sr_sanfen"&&prototype=="#sr_sanfencard")return 0;
 if(skill=="sr_quanheng"&&prototype=="#sr_quanhengCard")return 0;
 if(skill=="sr_xionglve"&&prototype=="#sr_xionglvecard")return 0;
 if(skill=="sr_dailao"&&prototype=="#sr_dailaocard")return 0;
 if(skill=="sr_weibao"&&prototype=="#sr_weibaocard")return 1;
 if(skill=="sr_choulve"&&prototype=="#sr_choulvecard")return 0;
 if(skill=="sr_jiexi"&&prototype=="#sr_jiexicard")return 1;
 if(skill=="sr_youxia"&&prototype=="#sr_youxiacard")return 0;
 if(skill=="sr_zhouyan"&&prototype=="#sr_zhouyancard")return 0;
 if(skill=="sr_zhaxiang"&&prototype=="#sr_zhaxiangcard")return 1;
 if(skill=="sr_fangxin"&&prototype=="#sr_fangxincard")return 1;
 if(skill=="sr_yinmeng"&&prototype=="#sr_yinmengcard")return 0;
 if(skill=="sr_juelie"&&prototype=="#sr_jueliecard")return 0;
 if(skill=="sr_zhishi"&&prototype=="#sr_zhishicard")return 0;
 if(skill=="sr_aozhan"&&prototype=="#sr_aozhanCard")return 0;
 if(skill=="sr_liuyun"&&prototype=="#sr_liuyuncard")return 1;
 if(skill=="sr_qingcheng"&&prototype=="#sr_qingchengcard")return 0;
 if(skill=="sr_xiahou"&&prototype=="#sr_xiahoucard")return 0;
 if(skill=="sr_lijian"&&prototype=="#sr_lijiancard")return 1;
 if(skill=="sr_manwu"&&prototype=="#sr_manwucard")return 0;
 if(skill=="sr_xingyi"&&prototype=="#sr_xingyicard")return 0;
 if(skill=="sr_jiwu"&&prototype=="#sr_jiwucard")return 0;
 return -1;
}
bool aiSRkillChoiceConstruction(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s){
 if(!valid(r,p)||s.size()!=6||!s.value("subcards").isArray())return false;
 const QString key=s.value("source_key").toString(),skill=s.value("skill").toString(),name=s.value("name").toString();
 const bool play=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&p->getPhase()==Player::Play;
 const bool responding=r.kind==AIRequest::RespondCard;
 const auto costs=s.value("subcards").toArray();
 if((key=="sr_hemouvs"||key=="sr_sheji")&&skill==key&&p->hasSkill(key)&&play&&costs.size()==1){
  if(!costs[0].isDouble()||costs[0].toDouble()!=costs[0].toInt(-1))return false;
  const int id=costs[0].toInt(-1);Room*room=p->getRoom();const Card*material=id>=0?room->getCard(id):nullptr;
  if(!material||room->getCardOwner(id)!=p||s.value("suit")!=QJsonValue(int(material->getSuit()))||s.value("number")!=QJsonValue(material->getNumber()))return false;
  if(key=="sr_sheji")return name=="slash"&&material->isKindOf("EquipCard")&&(room->getCardPlace(id)==Player::PlaceHand||room->getCardPlace(id)==Player::PlaceEquip);
  const int mark=p->getMark("hemousuit");const QStringList names{"duel","collateral","snatch","fire_attack"};
  return room->getCardPlace(id)==Player::PlaceHand&&mark>=1&&mark<=4&&int(material->getSuit())==mark-1&&name==names[mark-1];
 }
 if(!costs.isEmpty()||s.value("number")!=QJsonValue(0))return false;
 if(key=="sr_fangxin"&&p->hasSkill(key)&&(play||responding)&&skill.isEmpty())
  return (name=="indulgence"&&s.value("suit")==QJsonValue(int(Card::Diamond)))||(name=="supply_shortage"&&s.value("suit")==QJsonValue(int(Card::Club)));
 if(s.value("suit")!=QJsonValue(int(Card::NoSuit))||name!="slash")return false;
 if(key=="#sr_yaozhancard"&&p->hasSkill("sr_yaozhan")&&play)return skill=="_sr_yaozhan";
 if(key=="sr_jiuzhu"&&p->hasSkill(key)&&r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason==key)return skill=="_sr_jiuzhu";
 if(key=="@@sr_quanheng_slash"&&p->hasSkill("sr_quanheng_slash")&&r.kind==AIRequest::UseCard&&r.pattern==key)return skill=="sr_quanheng";
 return false;
}
void projectAISRkillChoiceContext(AIRequest&r,Room&room,ServerPlayer*p,const QVariant&,QJsonObject&context,const std::function<QJsonObject(const Card*)>&publicCard){
 if(!valid(r,p)||p->getRoom()!=&room||!publicCard)return;
 auto row=context.value("round26_choice").toObject();row.remove("flag_witness");const QString reason=r.choiceOptions.reason;
 const auto roster=[&](const Player*v){return v&&room.findPlayerByObjectName(v->objectName(),true)==v;};
 const bool play=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;
 if((play&&([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,p,gameLua));
        return (p->hasSkill("sr_xionglve")||p->hasSkill("sr_qingcheng")||p->hasSkill("sr_xiahou")||p->hasSkill("sr_jiwu")); }()))
  ||(r.kind==AIRequest::Choice&&(reason=="sr_xionglvebasic"||reason=="sr_xiahou"))
  ||(r.choiceOptions.question=="askForAG"&&reason=="sr_xionglvecard"))row.insert("slash_available",Slash::IsAvailable(p,nullptr));
 // srkill.lua178..184 broadcasts the two face-up moves before this exact cardask.
 // A table location/tag alone is not visibility proof; retain only exact public-disclosure records.
 if(r.kind==AIRequest::RespondCard&&r.choiceOptions.question=="askForCard"&&r.prompt.section(':',0,0)=="@srchouxi-discard"){
  const auto raw=room.getTag("agcards").toString();QJsonArray ids;QJsonObject cards;bool known=true;
  for(const QString&part:raw.split('+',Qt::SkipEmptyParts)){
   bool ok=false;const int id=part.toInt(&ok);const auto proof=ok&&id>=0?readAIStrategyPublicDisclosure(room,p,id):QJsonObject();
   if(!proof.value("known").toBool()){known=false;break;}
   ids.append(part);cards.insert(QString::number(id),proof.value("card"));
  }
  if(known){row.insert("agcards",ids);row.insert("ag_faces",cards);}
 }
 // These named payloads are exposed only to the actual prompted actor.
 if(r.kind==AIRequest::Discard&&reason=="sr_fuzheng1")row.insert("fuzheng_num",room.getTag("fuzheng_num").toInt());
 if(r.kind==AIRequest::PlayerChosen&&reason=="sr_zhaxiang"&&p->hasSkill(reason))row.insert("zhaxiang",room.getTag("zhaxiang").toString());
 if(r.kind==AIRequest::PlayerChosen&&reason=="sr_huiqu"&&p->hasSkill(reason)){
  auto*target=room.getTag("huiquTarget").value<ServerPlayer*>();
  if(!target||roster(target))row.insert("huiqu_target",target?QJsonValue(target->objectName()):QJsonValue(false));
 }
 if((r.kind==AIRequest::Choice&&reason=="sr_dailao")||(r.kind==AIRequest::UseCard&&r.pattern=="@@sr_youdi")){
  const QString flag=reason=="sr_dailao"?"dailao_target":"youdisource";
  row.remove(flag);
  // Static setters establish the scope, not disclosure. Require the live
  // first flagged target to match an actual public notification witness.
  for(auto*other:room.getOtherPlayers(p))if(other->hasFlag(flag)){
   if(witnessedFlag(room,other,flag))row.insert(flag,other->objectName());
   break;
  }
 }
 if(r.kind==AIRequest::UseCard&&r.pattern=="@@jiwu_extarget"&&p->hasSkill("sr_jiwu")){
  const auto raw=room.getTag("jiwudata");if(raw.canConvert<CardUseStruct>()){
   const auto use=raw.value<CardUseStruct>();bool known=use.from==p&&use.card&&use.card->isKindOf("Slash");
   QJsonArray targets;for(auto*to:use.to){known=known&&roster(to);if(to)targets.append(to->objectName());}
   if(known)row.insert("jiwu_use",QJsonObject{{"from",p->objectName()},{"to",targets},{"card",publicCard(use.card)}});
  }
 }
 if(r.kind==AIRequest::Choice&&reason=="sr_xionglvebasic"&&p->hasSkill("sr_xionglve"))
  row.insert("flag_witness",QJsonObject{{"source_key",reason},{"player",p->objectName()},{"name","xionglveused"},{"value",true}});
 if(r.kind==AIRequest::Choice&&reason=="sr_dailao"&&p->hasSkill("sr_dailao")&&row.value("dailao_target").isString())
  row.insert("flag_witness",QJsonObject{{"source_key",reason},{"player",row.value("dailao_target")},{"name","dailao_target"},{"value",false}});
 if(!row.isEmpty())context.insert("round26_choice",row);
}

// Preflight occurs before any pending effect is committed. Commit revalidates the
// same finite proof, then performs the single source-ordered public flag write.
bool stageAISRkillChoiceFlags(Room&room,const AIRequest&r,const AIResult&result,bool commit,QString*error){
 QJsonObject effect;int count=0;
 for(const auto&raw:result.strategyEffects)if(raw.isObject()&&raw.toObject().value("kind")=="srkill_choice_flag"){
  effect=raw.toObject();++count;
 }
 if(!count)return true;
 const auto fail=[&](const QString&why){if(error)*error=why;return false;};
 auto*p=room.findPlayerByObjectName(r.viewerObjectName,true);
 if(count!=1||!valid(r,p)||p->getRoom()!=&room||r.kind!=AIRequest::Choice||!result.handled
  ||result.kind!=AIResult::Answer||!result.errorCode.isEmpty()||result.decisionId!=r.decisionId
  ||result.stateRevision!=r.stateRevision)return fail("SRkill choice flag request/result mismatch");
 const QStringList required{"kind","source_key","player","name","value"};
 for(auto it=effect.begin();it!=effect.end();++it)
  if(!required.contains(it.key())&&!(it.key()=="after_delta"&&it.value()==QJsonValue(0)))return fail("SRkill choice flag shape");
 if(!effect.value("source_key").isString()||!effect.value("player").isString()
  ||!effect.value("name").isString()||!effect.value("value").isBool())return fail("SRkill choice flag types");
 const auto row=r.choiceOptions.context.value("round26_choice").toObject();
 const auto witness=row.value("flag_witness").toObject();
 for(const QString&key:QStringList{"source_key","player","name","value"})
  if(!witness.contains(key)||witness.value(key)!=effect.value(key))return fail("SRkill choice flag missing witness");
 const QString source=effect.value("source_key").toString(),name=effect.value("name").toString();
 auto*target=room.findPlayerByObjectName(effect.value("player").toString(),true);
 if(!target||!target->isAlive()||r.choiceOptions.reason!=source
  ||!r.choiceOptions.choices.contains(result.action.userString))return fail("SRkill choice flag actor/answer");
 if(source=="sr_xionglvebasic"){
  if(target!=p||!p->hasSkill("sr_xionglve")||name!="xionglveused"||!effect.value("value").toBool()
   ||result.action.userString!="cancel")return fail("SRkill Xionglve flag contract");
 }else if(source=="sr_dailao"){
  if(!p->hasSkill("sr_dailao")||target==p||name!="dailao_target"||effect.value("value").toBool()
   ||!witnessedFlag(room,target,name)||row.value("dailao_target")!=QJsonValue(target->objectName())
   ||(result.action.userString!="srdraw"&&result.action.userString!="srdiscard"))return fail("SRkill Dailao flag contract");
  ServerPlayer*first=nullptr;for(auto*other:room.getOtherPlayers(p))if(other->hasFlag(name)){first=other;break;}
  if(first!=target)return fail("SRkill Dailao original target order");
 }else return fail("SRkill choice flag source");
 if(commit)room.setPlayerFlag(target,effect.value("value").toBool()?name:"-"+name);
 return true;
}

// ---- srkill-response ----
void projectAIStrategyYinmengResponse(AIRequest &request, Room &room, ServerPlayer *viewer,
                                    QJsonObject &context)
{
    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.worldView.self.objectName!=viewer->objectName()
        || request.kind!=AIRequest::RespondCard || request.choiceOptions.question!="askForCardShow"
        || request.choiceOptions.reason!="sr_yinmeng") return;
    QJsonObject row{{"known",false}};
    // Fixed source srkill.lua4060 publicly shows the chosen card BEFORE tag,
    // flag and askForCardShow. Require that disclosure in the current snapshot;
    // do not call Engine::getCard on the room tag's possibly private ID.
    const QVariant raw=room.getTag("yinmengid");
    if (!raw.isValid()) { context.insert("sr_yinmeng",row); return; }
    const int id=raw.toInt();
    int type=-1;
    const auto scan=[&](const QList<AICardView> &cards) {
        for (const auto &card:cards) if (card.effectiveId==id) {type=card.typeId;return true;}
        return false;
    };
    for (const auto *cards:{&request.worldView.handCards,&request.worldView.discardPile,&request.choiceOptions.cards})
        if (scan(*cards)) break;
    const auto player=[&](const AIPlayerView &p) {
        for (const auto *cards:{&p.knownCards,&p.equips,&p.judgingArea}) if (scan(*cards)) return true;
        for (const auto &pile:p.piles) if (pile.open && scan(pile.cards)) return true;
        return false;
    };
    if (type<0) player(request.worldView.self);
    if (type<0) for (const auto &p:request.worldView.players) if (player(p)) break;
    if (type<0) {
        const auto witness=readAIStrategyPublicDisclosure(room,viewer,id);
        const auto card=witness.value("card").toObject();
        if (witness.value("known").toBool() && card.value("type_id").isDouble()) type=card.value("type_id").toInt(-1);
    }
    if (type<0) { context.insert("sr_yinmeng",row); return; }
    QString target;
    // The only fixed writers are srkill.lua4062/4065, both public setPlayerFlag.
    // Preserve Room::getOtherPlayers order and source's first-match selection.
    for (ServerPlayer *other:room.getOtherPlayers(viewer)) {
        if (other->hasFlag("yinmengname")) {target=other->objectName();break;}
    }
    row.insert("known",true);row.insert("target",target);
    row.insert("card_id",id);row.insert("card_type",type);
    context.insert("sr_yinmeng",row);
}

// ---- loong-xuhe-response-2 ----
#include <cmath>
#include <limits>
namespace {
bool loongXuheResponse2Scope(const AIRequest &r,const ServerPlayer *p)
{
    return p&&p->getRoom()&&r.viewerObjectName==p->objectName()
        &&r.worldView.self.objectName==p->objectName()
        &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();
}
bool play(const AIRequest &r)
{
    return r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;
}
bool loongXuheResponse2Disclosed(const AIRequest &r,int id)
{
    const auto has=[&](const QList<AICardView>&cards){for(const auto&c:cards)if(c.effectiveId==id)return true;return false;};
    if(has(r.worldView.handCards)||has(r.worldView.self.knownCards)||has(r.worldView.self.equips))return true;
    for(const auto&pile:r.worldView.self.piles)if(has(pile.cards))return true;
    return false;
}
bool integer(const QJsonValue &v,int &out)
{
    const double n=v.toDouble(-1);
    if(!v.isDouble()||!std::isfinite(n)||std::floor(n)!=n||n<std::numeric_limits<int>::min()||n>std::numeric_limits<int>::max())return false;
    out=int(n);return true;
}
}
bool aiLoongResponseConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &spec)
{
    if(!loongXuheResponse2Scope(r,p))return false;
    // All five V2 providers retain the inherited false legacy response gate.
    // This constructor authorizes actual fill planning, not new response reachability.
    if(!play(r))return false;
    const QString source=spec.value("source_key").toString(),name=spec.value("name").toString();
    static const QSet<QString> sources{"lxtx_zhangwu","lxtx_shouyue_wusheng","lxtx_shouyue_longdan","lxtx_wupao","lxtx_yizan"};
    if(!sources.contains(source)||!p->hasSkill(source)||!spec.value("subcards").isArray())return false;
    if(spec.value("skill")!=(source=="lxtx_zhangwu"?QStringLiteral("lxtx_zhangwuu"):source))return false;
    if(source=="lxtx_zhangwu"&&!play(r))return false;
    if(source=="lxtx_yizan"){
        if(name!="slash"&&name!="jink"&&name!="peach"&&name!="analeptic")return false;
    }else if(source=="lxtx_shouyue_longdan"){
        if(name!="slash"&&name!="jink")return false;
    }else if(name!="slash")return false;
    int suit=-2,number=-1;
    if(!integer(spec.value("suit"),suit)||!integer(spec.value("number"),number))return false;
    const auto costs=spec.value("subcards").toArray();
    if(source=="lxtx_zhangwu"){
        // Exact source dummyCard default, with later material-derived identity.
        if(costs.isEmpty()||suit!=Card::NoSuit||number!=0)return false;
    }else if(source=="lxtx_yizan"){
        const int mark=p->getMark("lxtx_longyuan");
        if(mark<0||costs.size()!=(mark==0?2:1))return false;
    }else if(costs.size()!=1)return false;
    QList<const Card *> cards;QSet<int> unique;
    for(const auto &value:costs){
        int id=-1;if(!integer(value,id)||id<0||unique.contains(id)||!loongXuheResponse2Disclosed(r,id))return false;
        unique.insert(id);
        const auto *room=p->getRoom();const auto place=room->getCardPlace(id);
        if(room->getCardOwner(id)!=p)return false;
        const bool hand=place==Player::PlaceHand,pile=p->getHandPile().contains(id),equip=place==Player::PlaceEquip;
        // Union of these fixed fill and per-card view-as source domains.
        // Longdan's h/addHandPile and Zhangwu's addHandPile('h') exclude equips.
        if(!hand&&!pile&&(!(source!="lxtx_zhangwu"&&source!="lxtx_shouyue_longdan")||!equip))return false;
        const Card *c=Sanguosha->getCard(id);if(!c||c->isVirtualCard())return false;cards<<c;
    }
    if(source=="lxtx_zhangwu")return true;
    const Card *first=cards.first();
    if(source=="lxtx_shouyue_longdan"&&!(name=="slash"?first->isKindOf("Jink"):first->isKindOf("Slash")))return false;
    if(source=="lxtx_wupao"&&!first->isRed())return false;
    if(source=="lxtx_yizan"){
        const Card::Suit wanted=name=="slash"?Card::Diamond:name=="jink"?Card::Club:name=="peach"?Card::Heart:Card::Spade;
        for(const Card *c:cards)if(c->getSuit()!=wanted)return false;
        if(cards.size()==2)return number==0&&suit==(wanted==Card::Heart||wanted==Card::Diamond?Card::NoSuitRed:Card::NoSuitBlack);
    }
    return suit==int(first->getSuit())&&number==first->getNumber();
}
bool aiZhangwuGeneratedBranch(const AIRequest &r,const ServerPlayer *p,const QString &skill)
{
    return loongXuheResponse2Scope(r,p)&&play(r)&&skill=="lxtx_zhangwu"&&p->hasSkill(skill)
        &&p->getMark("lxtx_jiangchi_recast-Clear")<=0;
}
bool aiZhangwuGeneratedPayment(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QList<int> &costs)
{
    if(!aiZhangwuGeneratedBranch(r,p,skill)||costs.isEmpty())return false;
    QList<int> generated;
    for(const Card *c:p->getHandcards()){
        if(!c||!loongXuheResponse2Disclosed(r,c->getEffectiveId()))return false;
        if(p->getMark("lxtx_jiangchi_draw-Clear")<=0
            ||(c->isKindOf("BasicCard")&&!c->isKindOf("Peach")&&!c->isKindOf("Analeptic"))||c->isKindOf("EquipCard"))generated<<c->getEffectiveId();
    }
    return generated==costs;
}

// ---- loong-xuhe-response ----
#include "lua-runtime.h"
#include "lua.hpp"
#include <QScopeGuard>

namespace {
constexpr const char *flagLedgerProperty = "_ai_round26_public_flag_ledger";
constexpr const char *feilongFlag = "LxtxFeiLongDuoFeng";
struct PublicFlagLedger : QObject {
    explicit PublicFlagLedger(Room &room) : QObject(&room) {}
    QHash<ServerPlayer *, QHash<ServerPlayer *, bool>> values;
    QSet<ServerPlayer *> watched;
    void invalidate(ServerPlayer *target) {
        for (auto it=values.begin();it!=values.end();++it) it.value().remove(target);
    }
    void watch(ServerPlayer *target) {
        if(watched.contains(target))return;
        watched.insert(target);
        // No writer-specific private epoch is exposed. Any property mutation
        // conservatively retires this target's previous public witness.
        QObject::connect(target,&Player::gameplay_property_changed,this,[this,target]{invalidate(target);});
        QObject::connect(target,&QObject::destroyed,this,[this,target]{invalidate(target);values.remove(target);watched.remove(target);});
    }
};
PublicFlagLedger *loongXuheResponseFlagLedger(Room &room,bool create) {
    auto *result=static_cast<PublicFlagLedger *>(room.property(flagLedgerProperty).value<QObject *>());
    if(!result&&create){result=new PublicFlagLedger(room);room.setProperty(flagLedgerProperty,QVariant::fromValue(static_cast<QObject *>(result)));}
    return result;
}
constexpr const char *propertyName = "_ai_round26_scalar_initial";
bool pushRaw(lua_State *state, const char *registry, const char *name)
{
    lua_getglobal(state,"sgs");
    if (!lua_istable(state,-1)) return false;
    lua_pushstring(state,registry);lua_rawget(state,-2);
    if (!lua_istable(state,-1)) return false;
    lua_pushstring(state,name);lua_rawget(state,-2);return true;
}
bool sourceMatches(lua_State *state,const char *name,const char *source,int line)
{
    const int top=lua_gettop(state);
    const auto restore=qScopeGuard([&]{lua_settop(state,top);});
    if (!pushRaw(state,"ai_skill_invoke",name)||!lua_isfunction(state,-1)) return false;
    lua_Debug info{};
    return lua_getinfo(state,">S",&info) && QString::fromUtf8(info.source)==QString::fromLatin1(source)
        && info.linedefined==line && QString::fromUtf8(info.what)=="Lua";
}
}
void recordAIStrategyFeilongPublicFlag(Room &room,ServerPlayer *target,const QString &operation)
{
    if(!target||target->getRoom()!=&room||!room.getPlayers().contains(target))return;
    if(operation!=feilongFlag&&operation!=QStringLiteral("-")+feilongFlag&&operation!=".")return;
    const bool value=operation==feilongFlag;
    if(target->hasFlag(feilongFlag)!=value)return;
    auto *l=loongXuheResponseFlagLedger(room,true);l->watch(target);
    for(ServerPlayer *viewer:room.getPlayers()) {
        l->watch(viewer);
        l->values[viewer].insert(target,value);
    }
}
QJsonObject readAIStrategyFeilongPublicFlag(Room &room,ServerPlayer *viewer,ServerPlayer *target,const QString &flag)
{
    if(flag!=feilongFlag||!viewer||!target||viewer->getRoom()!=&room||target->getRoom()!=&room
        ||!room.getPlayers().contains(viewer)||!room.getPlayers().contains(target))return {{"known",false}};
    auto *l=loongXuheResponseFlagLedger(room,false);
    if(!l||!l->values.contains(viewer)||!l->values[viewer].contains(target))return {{"known",false}};
    const bool witnessed=l->values[viewer].value(target);
    if(target->hasFlag(feilongFlag)!=witnessed){l->invalidate(target);return {{"known",false}};}
    return {{"known",true},{"value",witnessed}};
}
void clearAIStrategyLoongXuheResponse(Room &room)
{
    auto *l=loongXuheResponseFlagLedger(room,false);room.setProperty(flagLedgerProperty,QVariant());delete l;
    room.setProperty(propertyName,QVariant());
    room.setProperty("_ai_round26_initial_snapshot",QVariant());
}
void captureAIStrategyXuheResponse(Room &room)
{
    if (room.property(propertyName).isValid()) return;
    const bool firstBootstrap=!room.property("_ai_round26_ever_captured").toBool();
    room.setProperty("_ai_round26_ever_captured",true);
    QJsonObject seeds;
    auto &runtime=room.roomRuntime()->lua();LuaRuntime::Binding binding(runtime,false);
    lua_State *state=runtime.state();
    for (const auto &entry : {qMakePair(QStringLiteral("xuhe"),8508),qMakePair(QStringLiteral("olxuhe"),3926)}) {
        bool known=false;
        if (state && firstBootstrap) {
            const int top=lua_gettop(state);const auto restore=qScopeGuard([&]{lua_settop(state,top);});
            const QByteArray name=entry.first.toLatin1();
            const char *source=entry.first=="xuhe"?"@lua/ai/sp-ai.lua":"@lua/ai/ol_sp-ai.lua";
            known=sourceMatches(state,name.constData(),source,entry.second)
                && pushRaw(state,"ai_skill_choice",name.constData()) && lua_isnil(state,-1);
        }
        seeds.insert(entry.first,known?QJsonObject{{"known",true},{"present",false}}:QJsonObject{{"known",false}});
    }
    room.setProperty(propertyName,seeds.toVariantMap());
}
void projectAIStrategyXuheResponse(AIRequest &request,Room &room,ServerPlayer *viewer)
{
    if (!viewer||request.worldView.self.objectName!=viewer->objectName())return;
    // The first request can use bootstrap nil. Later requests may follow
    // arbitrary private callbacks; no writer-specific epoch/value is exposed.
    // Re-projecting this identical request is harmless, but never refreshes it.
    const QString stamp=viewer->objectName()+QStringLiteral(":")+QString::number(request.decisionId);
    QVariant first=room.property("_ai_round26_initial_snapshot");
    if(!first.isValid()) {room.setProperty("_ai_round26_initial_snapshot",stamp);first=stamp;}
    QJsonObject seeds;
    if(first.toString()==stamp)seeds=QJsonObject::fromVariantMap(room.property(propertyName).toMap());
    else for(const auto &name:{QStringLiteral("xuhe"),QStringLiteral("olxuhe")})seeds.insert(name,QJsonObject{{"known",false}});
    request.worldView.strategyFacts.insert("response_scalar_choices",seeds);
}
void projectAIStrategyFeilongResponseEvent(AIWorldView &world,Room &room,ServerPlayer *viewer,const AIEventView &event)
{
    world.strategyFacts.remove("loong_feilong_role_witness");
    auto previousLoong=world.strategyFacts.value("Loong").toObject();
    auto previousFlags=previousLoong.value("public_flags").toObject();
    previousFlags.remove(feilongFlag);previousLoong.insert("public_flags",previousFlags);
    world.strategyFacts.insert("Loong",previousLoong);
    if (!viewer||world.self.objectName!=viewer->objectName()||event.triggerEvent!=ChoiceMade
        ||event.kind!="choice"||event.reason!="_lxtx_feilongduofeng_re"
        ||event.from!=viewer->objectName()||!event.privateEvent||event.privateViewer!=viewer->objectName())return;
    const auto fields=event.details.value("prompt_list").toArray();
    if(fields.size()!=3||fields.at(0)!="skillInvoke"||fields.at(1)!="_lxtx_feilongduofeng_re"||fields.at(2)!="yes")return;
    static const QSet<QString>roles{"lord","loyalist","rebel","renegade"};
    // The actor's role is already an owner DTO; no foreign role query occurs.
    if(!roles.contains(world.self.role))return;
    QJsonArray targets;QJsonObject flags;bool complete=true;
    for(ServerPlayer *other:room.getOtherPlayers(viewer)) {
        const auto observed=readAIStrategyFeilongPublicFlag(room,viewer,other,feilongFlag);
        if(!observed.value("known").toBool()){complete=false;continue;}
        const bool flagged=observed.value("value").toBool();
        flags.insert(other->objectName(),flagged);
        if(flagged)targets.append(other->objectName());
    }
    auto loong=world.strategyFacts.value("Loong").toObject();
    auto publicFlags=loong.value("public_flags").toObject();
    publicFlags.insert("LxtxFeiLongDuoFeng",flags);loong.insert("public_flags",publicFlags);
    world.strategyFacts.insert("Loong",loong);
    // The complete source traversal must be observed. A partial positive list
    // cannot authorize effects while silently omitting an unknown recipient.
    if(!complete)return;
    world.strategyFacts.insert("loong_feilong_role_witness",QJsonObject{
        {"actor",viewer->objectName()},{"role",world.self.role},{"targets",targets},
        {"event_sequence",QString::number(event.sequence)},{"revision",QString::number(event.revision)}});
}

// ---- feidiy ----
#include "structs.h"
namespace {
bool owner(const AIRequest &r,const ServerPlayer *p) {
    return p && p->getRoom() && r.viewerObjectName==p->objectName()
        && r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();
}
bool response(const AIRequest&r) {
    return (r.kind==AIRequest::UseCard||r.kind==AIRequest::RespondCard)
        && (r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);
}
}
int aiFeiDIYExtraTarget(const AIRequest&r,const ServerPlayer*p,const QJsonObject&spec) {
    const QString key=spec.value("source_key").toString();
    // Fixed ai/feiDIY-ai.lua311/350: both print feihujue, but each caller must
    // own its actual source. aiAuditedConstructionQuery checks all six fields.
    if ((key!="feihujue"&&key!="feilangduo")||!r.pattern.isEmpty()||!aiAuditedConstructionQuery(r,p,spec))return 0;
    return 99;
}
bool aiFeiDIYSource(const AIRequest&r,const ServerPlayer*p,const Card*c,const QString&key,QString*error) {
    auto reject=[&](const char*why){if(error)*error=QString::fromLatin1(why);return false;};
    if(!c||c->getId()>=0||c->objectName()!="duel"||c->getSkillName(false)!="feihujue"
        ||c->getSuit()!=Card::NoSuit||c->getNumber()!=0||!c->getSubcards().isEmpty())return reject("FeiDIY extra99 requires the original exact Duel clone");
    const QJsonObject spec{{"source_key",key},{"name","duel"},{"skill","feihujue"},
        {"suit",int(Card::NoSuit)},{"number",0},{"subcards",QJsonArray{}}};
    if(aiFeiDIYExtraTarget(r,p,spec)!=99)return reject("FeiDIY extra99 source owner or current request differs");
    return true;
}
QJsonObject aiFeiDIYTargetFilter(Room&room,ServerPlayer*p,const AIRequest&r,const Card*c,
    const QString&key,const QJsonObject&query) {
    auto reject=[](const QString&s){return QJsonObject{{"error",s},{"key","feiDIY.extra99"}};};
    QString error;if(!p||p->getRoom()!=&room||!aiFeiDIYSource(r,p,c,key,&error))return reject(error);
    if(query.size()!=2||!query.value("prefix").isArray()||!query.value("target").isString())return reject("malformed FeiDIY target query");
    QList<const Player*>prefix;QSet<QString>seen;const auto names=query.value("prefix").toArray();
    if(names.size()>room.getAlivePlayers().size())return reject("FeiDIY prefix exceeds live roster");
    for(const auto&name:names){
        if(!name.isString()||seen.contains(name.toString()))return reject("FeiDIY prefix is not a distinct roster list");
        auto*target=room.findPlayerByObjectName(name.toString());if(!target||!target->isAlive())return reject("FeiDIY prefix player absent");
        seen.insert(name.toString());prefix<<target;
    }
    auto*target=room.findPlayerByObjectName(query.value("target").toString());if(!target||!target->isAlive())return reject("FeiDIY target absent");
    const auto revision=room.roomRuntime()->stateRevision();
    AITianyiQueryScope extra(p,c,99,false);const bool accepted=c->targetFilter(prefix,target,p);
    if(revision!=room.roomRuntime()->stateRevision())return reject("FeiDIY readonly target query changed state");
    return {{"accepted",accepted}};
}
int aiFeiDIYPrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){
    if(!owner(r,p)||!response(r))return -1;
    if(skill=="feifenhu"&&prototype=="#feifenhuCard"&&r.pattern=="@@feifenhu"&&p->hasSkill(skill))return 0;
    // Targets, not the skill owner, receive this actual native response ask
    // (extensions/feiDIY.lua1305). Source presence is NOT payment authority;
    // the existing issued activation / selection / factory checks still apply.
    if(skill=="feifengpo"&&prototype=="#feifengpoCard"&&r.pattern=="@@feifengpo")return 1;
    return -1;
}
void projectAIFeiDIYContext(AIRequest&r,Room&room,const ServerPlayer*p,
    const std::function<QJsonObject(const Card*)>&publicCard){
    if(!owner(r,p)||p->getRoom()!=&room)return;
    const auto reason=r.choiceOptions.reason;const auto roster=room.getAllPlayers(true);
    QJsonObject rows=r.choiceOptions.context.value("feiDIY").toObject();bool touched=false;
    if(r.kind==AIRequest::SkillInvoke&&reason=="feiqianxun"){
        // Exact owner's Collateral target tag; never another player's private tag.
        auto*victim=p->getTag("collateralVictim").value<ServerPlayer*>();
        rows.insert("collateral_victim",victim&&roster.contains(victim)?QJsonValue(victim->objectName()):QJsonValue(false));touched=true;
    }
    if(r.kind==AIRequest::Choice&&reason=="feizhaohu"){
        QJsonObject moves;QJsonArray marked;
        // Only actor identity is projected. The underlying move's hidden IDs,
        // from_places, reason and private card face never cross this boundary.
        // Writer feiDIY.lua311–319 records the public last-hand loss actor.
        for(auto*player:roster){if(player->getMark("feizhaohup")<=0)continue;
            marked.append(player->objectName());const QVariant tag=player->getTag("feizhaohuData");
            if(!tag.isValid()||tag.isNull()){moves.insert(player->objectName(),false);continue;}
            if(!tag.canConvert<CardsMoveOneTimeStruct>())continue;
            const auto move=tag.value<CardsMoveOneTimeStruct>();
            if(!move.from)moves.insert(player->objectName(),QJsonObject{{"from",false}});
            else if(roster.contains(qobject_cast<ServerPlayer*>(move.from)))
                moves.insert(player->objectName(),QJsonObject{{"from",move.from->objectName()}});
        }
        rows.insert("feizhaohu_marked",marked);rows.insert("feizhaohu_moves",moves);touched=true;
    }
    if(response(r)&&r.pattern=="@@feifengpo"){
        const QVariant tag=room.getTag("feifengpo");
        if(!tag.isValid()||tag.isNull())rows.insert("feifengpo_use",false);
        else if(tag.canConvert<CardUseStruct>()){
            const auto use=tag.value<CardUseStruct>();
            if(!use.card)rows.insert("feifengpo_use",false);
            // Writer is exclusively TargetSpecified, after the use is publicly
            // declared. Require the actual recipient and public source shape;
            // no arbitrary Room tag or foreign hidden card is an input.
            else if(use.from&&roster.contains(use.from)&&use.from->hasSkill("feifengpo")
                &&use.to.contains(const_cast<ServerPlayer*>(p))
                &&(use.card->isKindOf("Slash")||use.card->isKindOf("Duel"))&&publicCard){
                QJsonArray targets;bool valid=true;for(auto*to:use.to){if(!to||!roster.contains(to)){valid=false;break;}targets.append(to->objectName());}
                const auto face=publicCard(use.card);if(valid&&!face.isEmpty())rows.insert("feifengpo_use",QJsonObject{{"from",use.from->objectName()},{"to",targets},{"card",face}});
            }
        }touched=true;
    }
    if(touched){r.choiceOptions.context.insert("feiDIY",rows);r.worldView.decisionContext.insert("feiDIY",rows);}
}

// ---- fcdiy-fc-frame ----
#include <QPointer>
namespace {
const char *ledgerName = "_ai_round44_fc_use_frames";
struct Frame { quint64 serial = 0; const QVariant *data = nullptr; QPointer<Card> card; QPointer<ServerPlayer> from; qint64 history = 0; };
class Frames final : public QObject {
public:
 explicit Frames(Room &room):QObject(&room){setObjectName(ledgerName);}
 quint64 next = 0;
 QList<Frame> stack;
};
Frames *frames(Room &room, bool create = false) {
 for(auto *child:room.children()) if(child->objectName()==ledgerName) return dynamic_cast<Frames *>(child);
 return create ? new Frames(room) : nullptr;
}
bool question(const AIRequest &r) {
 const auto k=r.choiceOptions.reason;
 if(k=="sp_hengsaoqianjun") return r.kind==AIRequest::Choice||r.kind==AIRequest::PlayersChosen;
 if(k=="sp_lvezhen"||k=="fcjielianhuan") return r.kind==AIRequest::PlayerChosen;
 return k=="fcmoujiang"&&r.kind==AIRequest::PlayersChosen;
}
bool currentOwner(const AIRequest &r,Room &room,const ServerPlayer *p) {
 return p&&p->getRoom()==&room&&r.viewerObjectName==p->objectName()
  &&r.stateRevision==room.roomRuntime()->stateRevision()&&question(r)&&p->hasSkill(r.choiceOptions.reason);
}
bool validUse(Room &room,const CardUseStruct &u) {
 if(!u.card||!u.from||!room.getAllPlayers(true).contains(u.from))return false;
 for(auto *t:u.to)if(!t||!room.getAllPlayers(true).contains(t))return false;
 return true;
}
}
AIFCUseFrame::AIFCUseFrame(Room &room,int event,ServerPlayer *target,const QVariant &data) {
 if(event!=PreCardUsed&&event!=CardUsed&&event!=TargetSpecified&&event!=CardFinished)return;
 auto *state=frames(room,true); Frame f; f.serial=++state->next; f.data=&data;
 if(data.canConvert<CardUseStruct>()) {
  const auto use=data.value<CardUseStruct>();
  if(validUse(room,use)&&use.from==target) { f.card=const_cast<Card *>(use.card); f.from=use.from; f.history=use.targetModReveal.useHistoryEventId; }
 }
 state->stack.append(f);m_room=&room;m_serial=f.serial;
}
AIFCUseFrame::~AIFCUseFrame() {
 if(!m_room)return;auto *s=frames(*m_room);if(!s)return;
 for(int i=s->stack.size()-1;i>=0;--i)if(s->stack[i].serial==m_serial){s->stack.removeAt(i);break;}
}
void clearAIFCUseFrames(Room &room) { if(auto *s=frames(room))s->stack.clear(); }
void bindAIFCUseFrame(AIRequest &r,Room &room,const ServerPlayer *p) {
 // Immutable native request token: re-projection cannot move it to an inner use.
 if(r.fcUseFrameBound||!currentOwner(r,room,p))return;
 const auto *s=frames(room);if(!s||s->stack.isEmpty())return;
 const auto &f=s->stack.last();
 if(f.from==p){r.fcUseFrameBound=true;r.fcUseFrameSerial=f.serial;}
}
int aiFCUseFrameWitness(const AIRequest &r,Room &room,const ServerPlayer *p,const CardUseStruct &use) {
 const auto *s=frames(room);const bool active=s&&!s->stack.isEmpty();
 if(!r.fcUseFrameBound&&!active)return 0;
 if(!active||!currentOwner(r,room,p)||!r.fcUseFrameBound)return -1;
 const auto &f=s->stack.last();
 if(r.fcUseFrameSerial!=f.serial||!f.card||f.from!=p||!f.data
  ||!f.data->canConvert<CardUseStruct>())return -1;
 const auto live=f.data->value<CardUseStruct>();
 if(!validUse(room,live)||!validUse(room,use)||live.card!=f.card.data()||live.from!=f.from.data()
  ||live.targetModReveal.useHistoryEventId!=f.history||use.card!=live.card||use.from!=live.from
  ||use.targetModReveal.useHistoryEventId!=live.targetModReveal.useHistoryEventId||use.to!=live.to)return -1;
 return 1;
}

// ---- fcdiy-planning-2 ----
namespace {
bool fcdiyPlanning2Owner(const AIRequest&r,Room&room,const ServerPlayer*p){return p&&p->getRoom()==&room&&r.viewerObjectName==p->objectName()&&r.stateRevision==room.roomRuntime()->stateRevision();}
bool absent(const QVariant&v){return !v.isValid()||v.isNull();}
bool ask(const AIRequest&r,AIRequest::DecisionKind kind,const QString&key){return r.kind==kind&&r.choiceOptions.reason==key;}
bool useAsk(const AIRequest&r,const QString&pattern){return r.kind==AIRequest::UseCard&&r.pattern==pattern;}
QString tagKey(const QString&key){return key.startsWith("fcDIY_context:")?key.mid(14):QString();}
bool extraQuestion(const AIRequest&r,const ServerPlayer*p,const QString&key,const std::function<bool()> &skillQuery={}){
 if(!p||!(skillQuery ? skillQuery() : p->hasSkill(key)))return false;
 if(key=="sp_hengsaoqianjun")return ask(r,AIRequest::Choice,key)||ask(r,AIRequest::PlayersChosen,key);
 if(key=="sp_lvezhen"||key=="fcjielianhuan")return ask(r,AIRequest::PlayerChosen,key);
 return key=="fcmoujiang"&&ask(r,AIRequest::PlayersChosen,key);
}
bool fcdiyPlanning2ValidUse(Room&room,const CardUseStruct&use){if(!use.card||!use.from||!room.getAllPlayers(true).contains(use.from))return false;for(auto*t:use.to)if(!t||!room.getAllPlayers(true).contains(t))return false;return true;}
QJsonArray names(const QList<ServerPlayer*>&players){QJsonArray a;for(auto*p:players)a.append(p->objectName());return a;}
bool faceMatches(const QJsonObject&v,const Card*c){return c&&v.value("name").toString()==c->objectName()&&v.value("class_name").toString()==c->getClassName()&&v.value("suit").toInt(-1)==int(c->getSuit())&&v.value("number").toInt(-1)==c->getNumber();}
bool fcdiyPlanning2Disclosed(const AIRequest&r,Room&room,const ServerPlayer*p,const Card*c){
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
bool aiMoutaozeiPlanningConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&spec){
 if(!fcdiyPlanning2Owner(r,room,p)||!useAsk(r,"@@ahmoutaozei_tm")||!p->hasSkill("ahmoutaozei")||spec.size()!=5)return false;
 const auto property=p->property("ahmoutaozei_tm");if(property.metaType().id()!=QMetaType::QString)return false;
 const QString name=spec.value("name").toString();
 if(name.isEmpty()||name!=property.toString()||name=="jink"||name=="nullification"||name=="jl_wuxiesy"
  ||spec.value("skill").toString()!="ahmoutaozei"||spec.value("suit").toInt(-1)!=int(Card::NoSuit)
  ||!spec.value("number").isDouble()||spec.value("number").toDouble()!=0
  ||!spec.value("subcards").isArray()||!spec.value("subcards").toArray().isEmpty())return false;
 std::unique_ptr<Card> card(Sanguosha->cloneCard(name,Card::NoSuit,0));
 return card&&(card->isKindOf("BasicCard")||card->isNDTrick());
}
const Card*aiFcDIYPlanningEvaluation(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&source){
 const QString key=tagKey(source);if(!fcdiyPlanning2Owner(r,room,p)||!extraQuestion(r,p,key))return nullptr;
 const auto value=room.getTag(key);if(!value.canConvert<CardUseStruct>())return nullptr;const auto use=value.value<CardUseStruct>();
 if(!fcdiyPlanning2ValidUse(room,use))return nullptr;
 const bool foreignDuel=key=="fcmoujiang"&&use.from!=p&&use.card->isKindOf("Duel");
 if(foreignDuel){if(!aiPlanningPublicCardWitness(r,room,p,use.card))return nullptr;}
 else {
  const int frame=aiFCUseFrameWitness(r,room,p,use);
  if(use.from!=p||frame<0||(frame==0&&!aiPlanningPublicUseWitness(r,room,p,use)))return nullptr;
 }
 const auto*c=use.card;
 if(key=="sp_hengsaoqianjun"&&(!c->isKindOf("Slash")||c->getSkillName()!="sp_hengsaoqianjun"))return nullptr;
 if(key=="sp_lvezhen"&&(!c->isKindOf("Dismantlement")||!c->getSkillNames().contains(key)))return nullptr;
 if(key=="fcjielianhuan"&&(!c->isKindOf("IronChain")||!c->getSkillNames().contains(key)))return nullptr;
 if(key=="fcmoujiang"&&!(c->isKindOf("Duel")||(c->isKindOf("Slash")&&c->isRed())))return nullptr;
 return c;
}
int aiFcDIYPlanningExtraTarget(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&source){
 if(!aiFcDIYPlanningEvaluation(r,room,p,source))return -1;const auto key=tagKey(source);
 return key=="sp_lvezhen"?1:key=="fcmoujiang"?qMin(p->getHp()-1,2):99;
}
QJsonObject aiFcDIYPlanningTargetFilter(Room&room,ServerPlayer*p,const AIRequest&r,const Card*c,const QString&key,const QJsonObject&q){
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","fcDIY.extra_targets"}};};
 if(!c||c!=aiFcDIYPlanningEvaluation(r,room,p,key))return fail("current authenticated FC use differs");
 const int extra=aiFcDIYPlanningExtraTarget(r,room,p,key);
 if(q.size()!=2||!q.value("prefix").isArray()||!q.value("target").isString())return fail("malformed FC prefix query");
 QList<const Player*>prefix;QSet<QString>seen;const auto a=q.value("prefix").toArray();if(a.size()>room.getAlivePlayers().size())return fail("FC prefix exceeds roster");
 for(const auto&v:a){if(!v.isString()||seen.contains(v.toString()))return fail("FC prefix duplicates or malformed name");auto*t=room.findPlayerByObjectName(v.toString());if(!t||!t->isAlive())return fail("FC prefix target absent");seen.insert(v.toString());prefix<<t;}
 auto*t=room.findPlayerByObjectName(q.value("target").toString());if(!t||!t->isAlive())return fail("FC target absent");
 const auto revision=room.roomRuntime()->stateRevision();AITianyiQueryScope scope(p,c,extra,false);const bool accepted=c->targetFilter(prefix,t,p);
 if(revision!=room.roomRuntime()->stateRevision())return fail("FC readonly query changed state");return {{"accepted",accepted}};
}
void projectAIFcDIYPlanningContext(AIRequest&r,Room&room,const ServerPlayer*p,const QVariant&,QJsonObject&context,
 const std::function<QJsonObject(const Card*)>&face,const AIFcDIYPlanningProfile&profile){
 if(!fcdiyPlanning2Owner(r,room,p))return;bindAIFCUseFrame(r,room,p);AIIdentityProjectionScope identity(room,const_cast<ServerPlayer*>(p));
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
 for(const QString&key:{QStringLiteral("sp_hengsaoqianjun"),QStringLiteral("sp_lvezhen"),QStringLiteral("fcjielianhuan"),QStringLiteral("fcmoujiang")})if(extraQuestion(r,p,key,[&] {
        std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill(key);
    })){
  const auto value=room.getTag(key);if(absent(value)){row.insert(key,false);continue;}
  const QString source="fcDIY_context:"+key;const Card*c=aiFcDIYPlanningEvaluation(r,room,p,source);if(!c||!profile)continue;
  const auto use=value.value<CardUseStruct>();const auto card=profile(c,aiFcDIYPlanningExtraTarget(r,room,p,source),source);if(card.isEmpty())continue;
  row.insert(key,useRow(use,card));
  // Invoke the real native method: order, vote-output predicate, Slash fallback
  // and exclusion semantics are all part of the source contract (Room9062).
  if(key=="sp_hengsaoqianjun"&&r.kind==AIRequest::Choice)row.insert("sp_hengsaoqianjun_extra_targets",names(room.getCardTargets(const_cast<ServerPlayer*>(p),c,use.to)));
 }
 if(ask(r,AIRequest::PlayerChosen,"fcbyajiaoequip")&&p->hasSkill("fcbyajiao")){
  const auto value=p->getTag("fcbyajiaoCard");if(absent(value))row.insert("fcbyajiaoCard",false);
  else if(value.canConvert<const Card*>()){const Card*c=value.value<const Card*>();if(!c)row.insert("fcbyajiaoCard",false);else if(c->isKindOf("EquipCard")&&fcdiyPlanning2Disclosed(r,room,p,c)&&face){auto card=face(c);const auto*equip=qobject_cast<const EquipCard*>(c->getRealCard());if(equip){card.insert("equip_slot",equip->location());row.insert("fcbyajiaoCard",card);}}}
 }
 const bool kangkai=ask(r,AIRequest::SkillInvoke,"fcj_kangkai")&&p->hasSkill("fcj_kangkai");
 const bool kangkaiUse=ask(r,AIRequest::SkillInvoke,"fcj_kangkai_use");
 if(kangkai||kangkaiUse){
  const auto value=p->getTag("fcj_kangkaiSlash");if(absent(value))row.insert("fcj_kangkaiSlash",false);
  else if(value.canConvert<CardUseStruct>()){const auto use=value.value<CardUseStruct>();if(!use.card)row.insert("fcj_kangkaiSlash",false);else if(fcdiyPlanning2ValidUse(room,use)&&face&&(fcdiyPlanning2Disclosed(r,room,p,use.card)||aiPlanningPublicCardWitness(r,room,p,use.card)))row.insert("fcj_kangkaiSlash",useRow(use,face(use.card)));}
  if(kangkaiUse){const auto value=p->getTag("fcj_kangkaiGivenCard");if(absent(value))row.insert("fcj_kangkaiGivenCard",false);else if(value.canConvert<const Card*>()){const Card*c=value.value<const Card*>();if(!c)row.insert("fcj_kangkaiGivenCard",false);else if(room.getCardOwner(c->getEffectiveId())==p&&fcdiyPlanning2Disclosed(r,room,p,c)&&face)row.insert("fcj_kangkaiGivenCard",face(c));}}
 }
 if(useAsk(r,"@@sp_fenying")&&p->hasSkill("sp_fenying")){
  const auto value=room.getTag("sp_fenying");if(absent(value))row.insert("sp_fenying",false);
  else if(value.canConvert<DamageStruct>()){const auto d=value.value<DamageStruct>();const auto roster=room.getAllPlayers(true);
   if((!d.from||roster.contains(d.from))&&(!d.to||roster.contains(d.to))&&(!d.card||fcdiyPlanning2Disclosed(r,room,p,d.card)||aiPlanningPublicCardWitness(r,room,p,d.card))){
    QJsonObject damage{{"from",d.from?QJsonValue(d.from->objectName()):QJsonValue(false)},{"to",d.to?QJsonValue(d.to->objectName()):QJsonValue(false)},{"damage",d.damage},{"nature",int(d.nature)},{"chain",d.chain},{"transfer",d.transfer},{"by_user",d.by_user}};
    damage.insert("card",d.card&&face?QJsonValue(face(d.card)):QJsonValue(false));row.insert("sp_fenying",damage);
   }
  }
 }
 if(!row.isEmpty()){context.insert("fcDIY",row);r.worldView.decisionContext.insert("fcDIY",row);}
}

// ---- fcdiy-planning-3 ----
namespace {
bool ownerPlay(const AIRequest&r,Room&room,const ServerPlayer*p){
 return p&&p->getRoom()==&room&&!room.roomRuntime()->isClosing()&&r.decisionId!=0
  &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&r.stateRevision==room.roomRuntime()->stateRevision()&&r.kind==AIRequest::Activate
  &&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty()
  &&p->hasSkill("tyshouli");
}
bool material(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonValue&v){
 if(!ownerPlay(r,room,p)||!v.isArray())return false;
 const auto ids=v.toArray();if(ids.size()!=1||!ids.first().isDouble())return false;
 const double n=ids.first().toDouble(-1);if(n<0||n>std::numeric_limits<int>::max()||n!=std::floor(n))return false;
 const int id=int(n);const auto place=room.getCardPlace(id);
 if(place!=Player::PlaceEquip&&place!=Player::PlaceDelayedTrick)return false;
 auto*holder=room.getCardOwner(id);if(!holder||!holder->isAlive())return false;
 // Read EXACT public e/j source card, preserving inner EquipCard identity.
 // Sanguosha->getCard(id)'s outer wrapper is not substituted for getCards().
 const Card*actual=nullptr;
 for(const Card*c:holder->getCards("ej"))if(c&&c->getEffectiveId()==id){actual=c;break;}
 if(!actual||!actual->isKindOf("OffensiveHorse"))return false;
 const AIPlayerView*view=nullptr;
 if(r.worldView.self.objectName==holder->objectName())view=&r.worldView.self;
 else for(const auto&other:r.worldView.players)if(other.objectName==holder->objectName()){view=&other;break;}
 if(!view||!view->alive)return false;
 const auto&faces=place==Player::PlaceEquip?view->equips:view->judgingArea;
 for(const AICardView&f:faces)if(f.effectiveId==id){
  return f.objectName==actual->objectName()&&f.className==actual->getClassName()
   &&f.suit==int(actual->getSuit())&&f.number==actual->getNumber()
   &&!f.legacyIdentityToken.isEmpty()
   &&f.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),actual);
 }
 return false;
}
}
bool aiTyshouliConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return false;
 for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return false;
 return s.value("source_key")==QJsonValue("tyshouli")&&s.value("name")==QJsonValue("slash")
  &&s.value("skill")==QJsonValue("tyshouli")&&s.value("suit")==QJsonValue(int(Card::NoSuit))
  &&s.value("number")==QJsonValue(0)&&material(r,room,p,s.value("subcards"));
}
bool aiTyshouliSeed(const AIRequest&r,Room&room,const ServerPlayer*p,
 const QString&skill,const QString&prototype,const QJsonObject&proposal){
 return skill=="tyshouli"&&prototype=="#tyshouli"
  &&proposal.value("user_string")==QJsonValue("slash")
  &&material(r,room,p,proposal.value("subcards"));
}

// ---- fcdiy-planning-4 ----
namespace {
bool active(const AIRequest&r,Room&room,const ServerPlayer*p){
 return p&&p->getRoom()==&room&&!room.roomRuntime()->isClosing()&&r.decisionId!=0
  &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&r.stateRevision==room.roomRuntime()->stateRevision();
}
bool fcdiyPlanning4Play(const AIRequest&r){return r.kind==AIRequest::Activate
 &&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty();}
bool fcdiyPlanning4Response(const AIRequest&r,const QString&pattern){return r.kind==AIRequest::UseCard
 &&r.pattern==pattern&&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
 ||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);}
bool fcdiyPlanning4Integer(const QJsonValue&v,int&n){
 if(!v.isDouble())return false;const double d=v.toDouble();
 if(!std::isfinite(d)||d!=std::floor(d)||d<std::numeric_limits<int>::min()||d>std::numeric_limits<int>::max())return false;
 n=int(d);return true;
}
bool fcdiyPlanning4SameFace(const AICardView&v,const Card*c,int id){return c&&v.effectiveId==id
 &&v.objectName==c->objectName()&&v.className==c->getClassName()
 &&v.suit==int(c->getSuit())&&v.number==c->getNumber();}
const Card*fcdiyPlanning4Material(const AIRequest&r,Room&room,const ServerPlayer*p,int id,const QString&zone){
 if(id<0||room.getCardOwner(id)!=p)return nullptr;
 const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 if(zone.contains('h')&&p->handCards().contains(id)&&room.getCardPlace(id)==Player::PlaceHand)
  for(const auto&v:r.worldView.handCards)if(fcdiyPlanning4SameFace(v,c,id))return c;
 if(zone.contains('e')&&room.getCardPlace(id)==Player::PlaceEquip)
  for(const auto&v:r.worldView.self.equips)if(fcdiyPlanning4SameFace(v,c,id))return c;
 // Pile knowledge is required in the original snapshot as well as live state.
 for(const auto&pile:r.worldView.self.piles){
  if((zone=="ShenJian"&&pile.name==zone&&p->getPile(zone).contains(id))
   ||(zone=="he+handpile"&&p->getHandPile().contains(id)&&p->getPile(pile.name).contains(id)))
   for(const auto&v:pile.cards)if(fcdiyPlanning4SameFace(v,c,id))return c;
 }
 return nullptr;
}
}
bool aiFcDIYConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(!active(r,room,p)||s.size()!=fields.size())return false;
 for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return false;
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()
  ||!s.value("subcards").isArray())return false;
 const auto key=s.value("source_key").toString(),name=s.value("name").toString(),skill=s.value("skill").toString();
 int suit=0,number=0;if(!fcdiyPlanning4Integer(s.value("suit"),suit)||!fcdiyPlanning4Integer(s.value("number"),number))return false;
 const QString ownerSkill=skill=="mouduanliangv"?QStringLiteral("mouduanliangg"):skill;
 if(key!="fcDIY."+ownerSkill||(!p->hasSkill(ownerSkill)&&!p->hasLordSkill(ownerSkill)))return false;
 QList<int>ids;for(const auto&v:s.value("subcards").toArray()){
  int id=-1;if(!fcdiyPlanning4Integer(v,id)||id<0||ids.contains(id))return false;ids.append(id);
 }
 const bool standardFace=suit==int(Card::NoSuit)&&number==0;
 // ai/fcDIY-ai.lua3580,4399,5283. Named response probes, not activation rights.
 if(skill=="fcj_qinguo"||skill=="kjmoushifeng")
  return standardFace&&ids.isEmpty()&&name=="slash"&&fcdiyPlanning4Response(r,"@@"+skill);
 if(skill=="fcj_leixi"&&name=="duel")
  return standardFace&&ids.isEmpty()&&fcdiyPlanning4Response(r,"@@fcj_leixiDuel");
 // ai/fcDIY-ai.lua5211/5214: normal source argument is the empty NoSuit/0
 // mouduanliangg SkillCard. Neither hypothesis contains that SkillCard as cost.
 if(skill=="mouduanliangv")return fcdiyPlanning4Play(r)&&standardFace&&ids.isEmpty()
  &&(name=="supply_shortage"||name=="duel");
 // ai/fcDIY-ai.lua3056/3075/4374. Preserve -1 and SuitToBeDecided;
 // native Card getters derive the actual material face after addSubcard.
 if(skill=="sp_lvezhen"||skill=="fcj_leixi"){
  if(suit!=int(Card::SuitToBeDecided)||number!=-1||ids.size()!=1)return false;
  const Card*c=fcdiyPlanning4Material(r,room,p,ids.first(),"h");if(!c)return false;
  if(skill=="fcj_leixi")return name=="thunder_slash"&&c->isBlack()&&fcdiyPlanning4Response(r,"@@fcj_leixiSlash");
  return(name=="snatch"&&c->isRed()&&(fcdiyPlanning4Play(r)||fcdiyPlanning4Response(r,"@@sp_lvezhen_SSQY")))
   ||(name=="dismantlement"&&c->isBlack()&&(fcdiyPlanning4Play(r)||fcdiyPlanning4Response(r,"@@sp_lvezhen_GHCQ")));
 }
 if(!fcdiyPlanning4Play(r)||!standardFace)return false;
 // ai/fcDIY-ai.lua832: exact source pile order, INCLUDING the one-card
 // proposal at HP <= 0. Native selected feasibility remains stricter there.
 if(skill=="f_luanshe"){
  const auto pile=p->getPile("ShenJian");const int count=qMin(int(pile.size()),qMax(1,p->getHp()*2));
  if(name!="archery_attack"||pile.size()<=6||ids.size()!=count)return false;
  for(int i=0;i<ids.size();++i)if(ids[i]!=pile[i]||!fcdiyPlanning4Material(r,room,p,ids[i],"ShenJian"))return false;
  return true;
 }
 if(skill=="sp_hengsaoqianjun"){
  if(name!="slash"||ids.size()!=2)return false;
  const Card*a=fcdiyPlanning4Material(r,room,p,ids[0],"he+handpile"),*b=fcdiyPlanning4Material(r,room,p,ids[1],"he+handpile");
  return a&&b&&a->getColor()!=b->getColor();
 }
 if(ids.size()!=1)return false;
 const Card*c=fcdiyPlanning4Material(r,room,p,ids.first(),skill=="sp_taoyuanyi"?"h":"he+handpile");
 if(!c)return false;
 if(skill=="sp_taoyuanyi")return name=="god_salvation"&&c->getSuit()==Card::Heart;
 if(skill=="sp_qianlixing")return name=="slash"&&c->isRed()&&!c->isKindOf("TrickCard");
 return false;
}
bool aiFcDIYPileMaterial(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s,int id){
 return s.value("source_key")==QJsonValue("fcDIY.f_luanshe")
  &&aiFcDIYConstruction(r,room,p,s)&&s.value("subcards").toArray().contains(id)
  &&fcdiyPlanning4Material(r,room,p,id,"ShenJian");
}

// ---- fcdiy-planning ----
namespace {
bool fcdiyPlanningAbsent(const QVariant &v) { return !v.isValid() || v.isNull(); }
bool fcdiyPlanningSameFace(const AICardView &v, const Card *c) {
    return c && !c->isVirtualCard() && v.effectiveId == c->getEffectiveId()
        && v.objectName == c->objectName() && v.className == c->getClassName()
        && v.suit == int(c->getSuit()) && v.number == c->getNumber();
}
bool noKnownFilters(const AIRequest &r, const ServerPlayer *p) {
    const AIPlayerView *v = p->objectName() == r.worldView.self.objectName ? &r.worldView.self : nullptr;
    if (!v) for (const auto &row : r.worldView.players)
        if (row.objectName == p->objectName()) { v = &row; break; }
    if (!v || !v->skillsComplete) return false;
    for (const auto &s : v->skills)
        if (!s.invalid && dynamic_cast<const FilterSkill *>(Sanguosha->getViewAsSkill(s.skillName))) return false;
    return true;
}
bool fcdiyPlanningPublicPattern(const QString &pattern) {
    if (pattern.contains('$')) return false;
    for (const auto &branch : pattern.split('#')) {
        const auto factors = branch.split('|');
        if (factors.size() >= 4 && factors.at(3) != ".") return false;
    }
    return true;
}
}

void projectAIFcHuanshiPlanningContext(AIRequest &r, Room &room, const ServerPlayer *observer,
    const QVariant &data, QJsonObject &context, const std::function<QJsonObject(const Card *)> &face,
    const std::function<QJsonObject(const Card *)> &judgeProfile) {
    if (!observer || observer->getRoom() != &room || r.viewerObjectName != observer->objectName()
        || r.stateRevision != room.roomRuntime()->stateRevision() || r.choiceOptions.reason != "fcj_huanshi") return;
    const bool invoke = r.kind == AIRequest::SkillInvoke;
    const bool ag = r.kind == AIRequest::AmazingGrace;
    if (!invoke && !ag) return;
    auto *viewer = const_cast<ServerPlayer *>(observer);
    JudgeStruct *judge = nullptr;
    ServerPlayer *source = nullptr;
    auto fc = context.value("fcDIY").toObject();
    if (invoke) {
        // First makeChoiceRequest has no originalData yet. Do not manufacture
        // absent judge or erase the later projectDecisionContext result.
        if (!viewer->hasSkill("fcj_huanshi") || !data.canConvert<JudgeStruct *>()) return;
        judge = data.value<JudgeStruct *>();
        source = viewer;
    } else {
        const auto value = viewer->getTag("fcj_huanshiJudge");
        const auto from = viewer->getTag("fcj_huanshiFrom");
        if (fcdiyPlanningAbsent(value)) fc.insert("fcj_huanshiJudge", false);
        if (fcdiyPlanningAbsent(from)) fc.insert("fcj_huanshiFrom", false);
        context.insert("fcDIY", fc);
        if (!value.canConvert<JudgeStruct *>() || !from.canConvert<ServerPlayer *>()) return;
        judge = value.value<JudgeStruct *>(); source = from.value<ServerPlayer *>();
        // Native asks judge.who, not necessarily the Huanshi skill owner.
        if (!judge || judge->who != viewer || !source || !room.getAllPlayers(true).contains(source)
            || !source->hasSkill("fcj_huanshi")) return;
    }
    if (!judge || !judge->who || !judge->card || !face || !room.getAllPlayers(true).contains(judge->who)) return;
    const auto material = source->getCards("he");
    QList<AICardView> disclosed;
    if (invoke) {
        // Snapshot ownership and exact current face are both required. Preserve
        // all HE cards, INCLUDING Response-limited cards examined by source4147.
        for (const Card *c : material) {
            const AICardView *v = nullptr;
            for (const auto &row : r.worldView.handCards) if (fcdiyPlanningSameFace(row, c)) { v = &row; break; }
            if (!v) for (const auto &row : r.worldView.self.equips) if (fcdiyPlanningSameFace(row, c)) { v = &row; break; }
            if (!v || room.getCardOwner(c->getEffectiveId()) != viewer) return;
            disclosed << *v;
        }
    } else {
        if (!r.choiceOptions.candidatesComplete) return;
        for (int id : r.choiceOptions.cardIds) {
            const Card *c = Sanguosha->getCard(id);
            bool inHE = false; for (const Card *m : material) if (m && m->getEffectiveId() == id) { inHE = true; break; }
            if (!c || room.getCardOwner(id) != source || !inHE) return;
            const AICardView *v = nullptr;
            for (const auto &row : r.choiceOptions.cards) if (row.effectiveId == id && fcdiyPlanningSameFace(row, c)) { v = &row; break; }
            if (!v) return;
            disclosed << *v;
        }
    }
    if (ag) {
        // Fixed smart-ai5463 calls doDisCard(self.player, offeredId), whose
        // first query is player:canDiscard(player,id). Player.cpp1726 does
        // NOT require that id to belong to player: it tests the viewer's own
        // MethodDiscard restriction against that exact disclosed card.
        // Preserve that query, without moving the card or authorizing payment.
        auto permissions = r.worldView.self.canDiscard.value(viewer->objectName()).toObject();
        for (const auto &v : disclosed)
            permissions.insert(QString::number(v.effectiveId), viewer->canDiscard(viewer, v.effectiveId));
        r.worldView.self.canDiscard.insert(viewer->objectName(), permissions);
    }
    AIIdentityProjectionScope identity(room, viewer);
    const bool rawComplete = fcdiyPlanningPublicPattern(judge->pattern);
    bool filteredComplete = rawComplete && (judge->who == viewer || noKnownFilters(r, judge->who));
    QJsonObject raw, filtered, suits, numbers, repairs, candidates, filterRows, handRows;
    auto filters = r.worldView.strategyFacts.value("card_filter").toObject();
    auto handFilters = r.worldView.strategyFacts.value("card_filter_hand").toObject();
    filterRows = filters.value(viewer->objectName()).toObject();
    handRows = handFilters.value(viewer->objectName()).toObject();
    for (const auto &v : disclosed) {
        const int id = v.effectiveId;
        const QString key = QString::number(id);
        const Card *c = Sanguosha->getCard(id);
        auto candidateFace = invoke && judgeProfile ? judgeProfile(c) : face(c);
        if (invoke && judgeProfile) {
            // getCards("he") projects realEquip(inner), while fixed source4143
            // subsequently fetches Sanguosha:getCard(id), the WrappedCard outer.
            // Authenticate the observed material separately; do not merge their
            // native identities or substitute the inner object for source use.
            const QString materialIdentity = v.legacyIdentityToken.isEmpty()
                ? aiLegacyCardIdentity(room, viewer, v.nativeIdentity) : v.legacyIdentityToken;
            if (!materialIdentity.isEmpty()) candidateFace.insert("material_identity_token", materialIdentity);
        }
        candidates.insert(key, candidateFace);
        if (rawComplete) raw.insert(key, judge->isGood(c));
        if (judge->who == viewer) {
            const auto row = projectAIOwnerFilteredCard(room, viewer, id, rawComplete ? judge : nullptr,
                int(Player::PlaceJudge), &v);
            filterRows.insert(key, row);
            const auto printed = row.value("card").toObject();
            if (!row.value("complete").toBool() || !row.value("judge_outcome").isBool()) filteredComplete = false;
            else {
                filtered.insert(key, row.value("judge_outcome"));
                suits.insert(key, printed.value("suit")); numbers.insert(key, printed.value("number"));
                if (row.contains("donor_error_repair")) repairs.insert(key, row.value("donor_error_repair"));
            }
        } else if (filteredComplete) {
            filtered.insert(key, raw.value(key)); suits.insert(key, int(c->getSuit())); numbers.insert(key, c->getNumber());
        }
        // Beige's isCard(Peach,c,self.player) is a hypothetical classification
        // for AG. Reuse the existing audited disclosed-face, hand-place filter.
        handRows.insert(key, projectAIOwnerFilteredCard(room, viewer, id, nullptr, int(Player::PlaceHand), &v));
    }
    filters.insert(viewer->objectName(), filterRows); handFilters.insert(viewer->objectName(), handRows);
    r.worldView.strategyFacts.insert("card_filter", filters);
    r.worldView.strategyFacts.insert("card_filter_hand", handFilters);
    int holders = 0; bool reserveComplete = true; QJsonObject reserve;
    if (judge->reason != "lightning") for (auto *p : room.getAllPlayers()) if (p->containsTrick("lightning")) {
        ++holders;
        if (!noKnownFilters(r, p)) reserveComplete = false;
    }
    if (reserveComplete && holders > 0) for (const auto &v : disclosed)
        if (v.suit == int(Card::Spade) && v.number >= 2 && v.number <= 9) reserve.insert(QString::number(v.effectiveId), true);
    if (!filteredComplete) filtered = {};
    QJsonObject row{{"scope_key", "fcj_huanshi"}, {"who", judge->who->objectName()}, {"reason", judge->reason},
        {"pattern", judge->pattern}, {"good", judge->isGood()}, {"predicate_good", judge->good}, {"negative", judge->negative},
        {"card", judgeProfile ? judgeProfile(judge->card) : face(judge->card)}, {"raw_outcome_by_id", raw}, {"raw_outcomes_complete", rawComplete},
        {"candidate_faces", candidates}, {"outcome_by_id", filtered}, {"outcomes_complete", filteredComplete},
        {"unfiltered_outcome_by_id", raw}, {"unfiltered_outcomes_complete", rawComplete},
        {"filtered_suit_by_id", suits}, {"filtered_number_by_id", numbers}, {"filtered_complete", filteredComplete},
        {"repair_by_id", repairs}, {"lightning_holder_count", holders}, {"lightning_reserve_complete", reserveComplete},
        {"lightning_candidate_ids", reserve}};
    if (judge->reason.contains("beige")) {
        // smart-ai5409 reads only CurrentDamageStruct.from. A missing tag
        // converts to the source's empty DamageStruct; do not substitute a
        // previous damage event or disclose its card/private fields.
        const auto current = room.getTag("CurrentDamageStruct");
        if (fcdiyPlanningAbsent(current)) {
            row.insert("damage_context_complete", true); row.insert("damage_from", "");
        } else if (current.canConvert<DamageStruct>()) {
            const auto damage = current.value<DamageStruct>();
            if (!damage.from || room.getAllPlayers(true).contains(damage.from)) {
                row.insert("damage_context_complete", true);
                row.insert("damage_from", damage.from ? damage.from->objectName() : QString());
            }
        }
    }
    fc.insert("fcj_huanshiJudge", row); fc.insert("fcj_huanshiFrom", source->objectName());
    context.insert("fcDIY", fc);
    // Existing retrial helpers consume this same typed judge/filter projection.
    context.insert("judge", row);
}

// ---- jieyingh ----
#include "room-state.h"
#include "roomthread.h"
#include <atomic>

namespace {
thread_local QHash<Room *, QList<const AIJieyinghQuestion *>> questions;
std::atomic<quint64> nextQuestion{0};
bool sameUse(const CardUseStruct &a, const CardUseStruct &b) {
    return a.card == b.card && a.from == b.from && a.to == b.to;
}
}
struct AIJieyinghQuestion::Data {
    Room *room = nullptr;
    QPointer<ServerPlayer> viewer;
    quint64 serial = 0, frame = 0;
    QString prompt;
    int mark = 0;
    AIReadonlyCardLease observed;
    CardUseStruct use;
    bool valid = false;
};
struct AIJieyinghAccess {
    static const AIJieyinghQuestion::Data *current(Room &room) {
        const auto it = questions.constFind(&room);
        return it == questions.cend() || it->isEmpty() ? nullptr : it->last()->d.get();
    }
    static bool valid(const AIRequest &r, Room &room, ServerPlayer *p, quint64 serial) {
        const auto *d = current(room);
        if (!d || !d->valid || d->viewer != p || !p || d->serial != serial
            || r.jieyinghQuestionSerial != serial || !serial
            || r.viewerObjectName != p->objectName() || r.stateRevision != room.roomRuntime()->stateRevision()
            || r.kind != AIRequest::UseCard || r.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            || r.pattern != "@@jieyingh" || r.prompt != d->prompt || r.handlingMethod != Card::MethodUse
            || !p->isAlive() || !p->hasFlag("CurrentPlayer") || room.getCurrent() != p
            || p->getMark("&jieyingh-SelfClear") != d->mark
            || !p->getSkillInstanceIds("jieyingh").isEmpty()
            || room.roomState()->getCurrentCardUseReason() != r.reason
            || room.roomState()->getCurrentCardUsePattern() != r.pattern) return false;
        const Card *live = d->observed ? d->observed() : nullptr;
        if (!live || live != d->use.card) return false;
        CardUseStruct active; quint64 frame = 0;
        if (!room.getThread() || !room.getThread()->currentJieyinghQuestionUse(p, active, frame)
            || frame != d->frame || !sameUse(active, d->use)) return false;
        const auto tag = p->getTag("jieyinghData");
        if (!tag.canConvert<CardUseStruct>() || !sameUse(tag.value<CardUseStruct>(), d->use)) return false;
        const auto disclosed = r.choiceOptions.context.value("nested_delegate").toObject();
        return disclosed.value("source_key") == "@@jieyingh" && disclosed.value("known") == true
            && disclosed.value("use").isObject();
    }
};
AIJieyinghQuestion::AIJieyinghQuestion(Room &room, ServerPlayer *p,
    const QString &pattern, const QString &prompt, int method) : d(new Data) {
    d->room = &room; d->viewer = p; d->serial = ++nextQuestion; d->prompt = prompt;
    questions[&room].append(this); // A nested unrelated question masks the outer capability.
    if (!p || pattern != "@@jieyingh" || method != int(Card::MethodUse)
        || !p->isAlive() || room.getCurrent() != p || !p->hasFlag("CurrentPlayer")
        || !p->getSkillInstanceIds("jieyingh").isEmpty()
        || (d->mark = p->getMark("&jieyingh-SelfClear")) <= 0 || !room.getThread()) return;
    if (!room.getThread()->currentJieyinghQuestionUse(p, d->use, d->frame)
        || d->use.from != p || !d->use.card || d->use.to.isEmpty()) return;
    d->observed = AIOwnerReadonlyCardStore::observe(d->use.card);
    const Card *live = d->observed ? d->observed() : nullptr;
    if (!live || (!live->isKindOf("Slash") && !live->isNDTrick()) || live->isKindOf("Collateral")
        || prompt != "@jieyingh:" + live->objectName()) return;
    const auto tag = p->getTag("jieyinghData");
    d->valid = tag.canConvert<CardUseStruct>() && sameUse(tag.value<CardUseStruct>(), d->use);
}
AIJieyinghQuestion::~AIJieyinghQuestion() {
    auto it = questions.find(d->room);
    if (it == questions.end()) return;
    it->removeAll(this);
    if (it->isEmpty()) questions.erase(it);
}
void projectAIJieyinghQuestion(AIRequest &r, Room &room, ServerPlayer *p) {
    const auto *d = AIJieyinghAccess::current(room);
    if (!d || !d->valid) return;
    r.jieyinghQuestionSerial = d->serial;
    if (!AIJieyinghAccess::valid(r, room, p, d->serial)) { r.jieyinghQuestionSerial = 0; return; }
    r.choiceOptions.context.insert("jieyingh_question", QJsonObject{{"known", true},
        {"kind", "jieyingh_native_question"}, {"prototype", "JieyinghCard"}});
    r.worldView.decisionContext.insert("jieyingh_question", r.choiceOptions.context.value("jieyingh_question"));
}
Card *createAIJieyinghProduct(const AIRequest &r, Room &room, ServerPlayer *p,
                                    quint64 serial, QString *error) {
    const auto fail = [&](const char *reason)->Card* { if (error) *error=QString::fromLatin1(reason); return nullptr; };
    if (!AIJieyinghAccess::valid(r, room, p, serial)) return fail("Jieyingh native recipient question is not active");
    const ViewAsSkill *vs = Sanguosha->getViewAsSkill("jieyingh");
    if (!vs || dynamic_cast<const ViewAsSkillV2 *>(vs) || !vs->isEnabledAtResponse(p, "@@jieyingh"))
        return fail("Jieyingh native V1 response producer changed");
    Card *card = const_cast<Card *>(vs->viewAs(QList<const Card *>()));
    if (!card) return fail("Jieyingh native product unavailable");
    const auto *skillCard = dynamic_cast<const SkillCard *>(card);
    if (!card->isVirtualCard() || card->getClassName() != "JieyinghCard" || !card->getSubcards().isEmpty()
        || !skillCard || !skillCard->getUserString().isEmpty()
        || !AIJieyinghAccess::valid(r, room, p, serial)) {
        card->deleteLater(); return fail("Jieyingh native product or live question changed");
    }
    // Exact donor response literal. Native Parse attributes an otherwise
    // unnamed C++ SkillCard; copy only that verified finite skill name, never
    // manufacture an activation/source instance or reinterpret caller input.
    std::unique_ptr<const Card> parsed(Card::Parse(QStringLiteral("@JieyinghCard=.")));
    if (!parsed || parsed->getClassName() != card->getClassName()
        || parsed->objectName() != card->objectName() || parsed->getSuit() != card->getSuit()
        || parsed->getNumber() != card->getNumber() || parsed->getSubcards() != card->getSubcards()
        || parsed->getSkillName(false) != "jieyingh") {
        card->deleteLater(); return fail("fixed Jieyingh source Parse attribution changed");
    }
    card->setSkillName(parsed->getSkillName(false));
    if (p->isLocked(card) || p->isCardLimited(card, Card::MethodUse)
        || !AIJieyinghAccess::valid(r, room, p, serial)) {
        card->deleteLater(); return fail("source-attributed Jieyingh question became invalid");
    }
    if (error) error->clear();
    return card;
}
Card *rebuildAIJieyinghProduct(const AIRequest &r, Room &room, ServerPlayer *p,
                                     const AICardConversionView &ticket) {
    if (!ticket.jieyinghQuestion || ticket.producerVersion != 1 || ticket.policyClass != "@jieyingh_question"
        || ticket.producerReason != r.reason || ticket.producerPattern != r.pattern
        || ticket.costCount || !ticket.subcardIds.isEmpty() || !ticket.declaredName.isEmpty()) return nullptr;
    Card *card = createAIJieyinghProduct(r, room, p, ticket.jieyinghQuestionSerial);
    if (!card) return nullptr;
    if (card->objectName() != ticket.name || card->getClassName() != ticket.className
        || card->getSuit() != ticket.suit || card->getNumber() != ticket.number
        || card->getSkillName(false) != ticket.skillName || card->toString() != ticket.legacyStringKey) {
        card->deleteLater(); return nullptr;
    }
    return card; // Exact fixed Parse attribution, no fabricated activation/source IDs.
}

// ---- mountainrumour-public-uses ----
namespace {
struct PublicUseWitness {QPointer<Card> card;QString face;QString from;QStringList targets;qint64 historyId=0;bool publicStage=false;};
class PublicUses final:public QObject {public:explicit PublicUses(Room&r):QObject(&r){}QList<PublicUseWitness> uses;};
PublicUses*ledger(Room&r,bool create){for(auto*c:r.children())if(auto*v=dynamic_cast<PublicUses*>(c))return v;return create?new PublicUses(r):nullptr;}
QString faceKey(const Card*c){return c->objectName()+"|"+c->getClassName()+"|"+QString::number(int(c->getSuit()))+"|"+QString::number(c->getNumber())+"|"+c->getSkillName(false)+"|"+c->toString();}
bool mountainrumourPublicUsesOwner(const AIRequest&r,Room&room,const ServerPlayer*p){return p&&p->getRoom()==&room&&r.viewerObjectName==p->objectName()&&r.stateRevision==room.roomRuntime()->stateRevision();}
bool mountainrumourPublicUsesAbsent(const QVariant&v){return !v.isValid()||v.isNull();}
bool useQuestion(const AIRequest&r,const char*pattern){return r.kind==AIRequest::UseCard&&r.pattern==QLatin1String(pattern);}
bool caokongQuestion(const AIRequest&r){return useQuestion(r,"@@luacaokong")||(r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason=="luacaokong");}
bool mountainrumourPublicUsesFaceMatches(const QJsonObject&v,const Card*c){return v.value("name").toString()==c->objectName()&&v.value("class_name").toString()==c->getClassName()&&v.value("suit").toInt(-1)==int(c->getSuit())&&v.value("number").toInt(-1)==c->getNumber();}
bool mountainrumourPublicUsesDisclosed(const AIRequest&r,Room&room,const ServerPlayer*p,const Card*c){
 if(!c||c->isVirtualCard()||c->getEffectiveId()<0||Sanguosha->getCard(c->getEffectiveId())!=c)return false;
 const int id=c->getEffectiveId();auto*viewer=const_cast<ServerPlayer*>(p);
 // Real move/fillAG ledgers authenticate both current mapping and face. Mere
 // PlaceTable presence is not disclosure (mountainrumour.lua1039/1041).
 for(const auto&v:{readAIStrategyPublicDisclosure(room,viewer,id),readAIStrategyChoiceAGDisclosure(room,viewer,id)})
  if(v.value("known").toBool()&&mountainrumourPublicUsesFaceMatches(v.value("card").toObject(),c))return true;
 const auto matches=[&](const AICardView&v){return v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,viewer,c);};
 for(const auto&v:r.worldView.handCards)if(matches(v)&&room.getCardOwner(id)==p)return true;
 auto players=r.worldView.players;players.prepend(r.worldView.self);
 for(const auto&v:players){for(const auto&x:v.knownCards)if(matches(x))return true;for(const auto&x:v.equips)if(matches(x))return true;for(const auto&x:v.judgingArea)if(matches(x))return true;for(const auto&pile:v.piles)for(const auto&x:pile.cards)if(matches(x))return true;}
 return false;
}
bool publicUse(const AIRequest&r,Room&room,const ServerPlayer*p,const Card*c){
 if(mountainrumourPublicUsesDisclosed(r,room,p,c))return true;
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
void clearAIPlanningPublicUses(Room&room){if(auto*l=ledger(room,false))l->uses.clear();}
void recordAIPlanningPublicUse(Room&room,int event,const QVariant&data){
 if(event==GameReady){clearAIPlanningPublicUses(room);return;}
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
bool aiPlanningPublicUseWitness(const AIRequest&r,Room&room,const ServerPlayer*p,const CardUseStruct&use){
 if(!mountainrumourPublicUsesOwner(r,room,p)||!use.card||!use.from||!room.getAllPlayers(true).contains(use.from))return false;
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
bool aiPlanningPublicCardWitness(const AIRequest&r,Room&room,const ServerPlayer*p,const Card*card){
 if(!mountainrumourPublicUsesOwner(r,room,p)||!card)return false;
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
const Card*aiCaokongPlanningEvaluation(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&key){
 if(key!="luacaokong_context"||!mountainrumourPublicUsesOwner(r,room,p)||!caokongQuestion(r)||!p->hasSkill("luacaokong"))return nullptr;
 const auto v=room.getTag("caokong_carduse");if(mountainrumourPublicUsesAbsent(v)||!v.canConvert<const Card*>())return nullptr;
 const auto*c=v.value<const Card*>();return mountainrumourPublicUsesDisclosed(r,room,p,c)?c:nullptr;
}
void projectAIMountainrumourPlanningContext(AIRequest&r,Room&room,const ServerPlayer*p,const QVariant&,QJsonObject&context,const std::function<QJsonObject(const Card*)>&face){
 if(!mountainrumourPublicUsesOwner(r,room,p))return;
 // makeRequest calls this after buildWorldView's identity scope has ended.
 // The actual public face must use THIS viewer's equality namespace too.
 AIIdentityProjectionScope identityScope(room,const_cast<ServerPlayer*>(p));
 auto row=context.value("mountainrumour").toObject();
 if(useQuestion(r,"@@luaxianhai")&&p->hasSkill("luaxianhai")){
  const auto v=room.getTag(p->objectName()+"xianhai_fromcard");
  if(mountainrumourPublicUsesAbsent(v))row.insert("xianhai_card",false);
  else if(v.canConvert<const Card*>()){const auto*c=v.value<const Card*>();if(!c)row.insert("xianhai_card",false);else if(publicUse(r,room,p,c)&&face)row.insert("xianhai_card",face(c));}
 }
 if(r.kind==AIRequest::Choice&&r.choiceOptions.reason=="luahuanying"){
  const auto target=room.getTag("huanying_target"),value=room.getTag("huanying_data");
  if(mountainrumourPublicUsesAbsent(target))row.insert("huanying_target",false);
  if(mountainrumourPublicUsesAbsent(value))row.insert("huanying_damage",false);
  if(target.canConvert<ServerPlayer*>()&&value.canConvert<DamageStruct>()){
   auto*t=target.value<ServerPlayer*>();const auto damage=value.value<DamageStruct>();
   // Source1182-1185 asks the damage SOURCE, not the hidden-pile owner.
   // No selected huanyingpile id/face is disclosed before source1194.
   if(t&&room.getAllPlayers(true).contains(t)&&damage.from==p&&damage.to==t&&t->hasSkill("luahuanying")){
    row.insert("huanying_target",t->objectName());row.insert("huanying_damage",QJsonObject{{"damage",damage.damage}});
   }
  }
 }
 if(([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,const_cast<ServerPlayer *>(p),gameLua));
        return p->hasSkill("luacaokong"); }())&&(caokongQuestion(r)||(r.kind==AIRequest::PlayerChosen&&r.choiceOptions.reason=="luacaokong"))){
  const auto v=room.getTag("caokong_tos");if(mountainrumourPublicUsesAbsent(v))row.insert("caokong_targets",false);
  else if(v.canConvert<CardUseStruct>()){
   const auto use=v.value<CardUseStruct>();QJsonArray names;bool valid=true;
   for(auto*t:use.to){if(!t||!room.getAllPlayers(true).contains(t)){valid=false;break;}names.append(t->objectName());}
   if(valid)row.insert("caokong_targets",QJsonObject{{"to",names}});
  }
  if(caokongQuestion(r)){
   const auto v=room.getTag("caokong_carduse");if(mountainrumourPublicUsesAbsent(v))row.insert("caokong_card",false);
   else if(const auto*c=aiCaokongPlanningEvaluation(r,room,p,"luacaokong_context")){if(face)row.insert("caokong_card",face(c));}
  }
 }
 if(useQuestion(r,"@@shisheng")&&p->hasSkill("shisheng")){
  const auto v=p->property("shisheng_sel");if(v.metaType().id()==QMetaType::QString)row.insert("shisheng_sel",v.toString());
 }
 if(!row.isEmpty()){context.insert("mountainrumour",row);r.worldView.decisionContext.insert("mountainrumour",row);}
}

// ---- sijyu-fengyi ----
#include "ai-runtime.h"
#include <QJsonObject>
namespace {
thread_local QHash<Room*,QList<AIFengyiQuestion*>> sijyuFengyiFrames;
}
struct AIFengyiQuestion::Data {
 Room *room=nullptr;QPointer<ServerPlayer> owner,judgeWho;
 const QVariant *data=nullptr;JudgeStruct *judge=nullptr;const Card *judgeCard=nullptr;
 AIReadonlyCardLease judgeLease,selectedLease;
 QString prompt,reason,pattern;bool good=false,judgeResult=false,valid=false,attempted=false,consumed=false;
 quint64 decision=0,revision=0,gameGeneration=0,isolatedGeneration=0;int selected=-1;
 const void *gameState=nullptr,*isolatedState=nullptr;
};
struct AIFengyiAccess {
 static AIFengyiQuestion::Data *top(Room&r){auto i=sijyuFengyiFrames.find(&r);return i==sijyuFengyiFrames.end()||i->isEmpty()?nullptr:i->last()->d.get();}
 static bool current(Room&r,const AIRequest&q,ServerPlayer*p,AIFengyiQuestion::Data*d){
  const auto *runtime=r.roomRuntime();
  if(!runtime||runtime->isClosing()||!d)return false;
  const auto &game=runtime->lua();const auto &isolated=runtime->ai().lua();
  if(game.isClosing()||game.isClosed()||isolated.isClosing()||isolated.isClosed()
   ||!d->gameState||!d->isolatedState||game.rawState()!=d->gameState||isolated.rawState()!=d->isolatedState
   ||game.generation()!=d->gameGeneration||isolated.generation()!=d->isolatedGeneration)return false;
  if(!d||top(r)!=d||!d->valid||!p||d->owner!=p||p->getRoom()!=&r||!p->isAlive()
   ||q.kind!=AIRequest::RespondCard||q.choiceOptions.question!="askForCard"
   ||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE||q.handlingMethod!=Card::MethodResponse
   ||q.pattern!="@@sijyu_fengyi"||q.prompt!=d->prompt||q.viewerObjectName!=p->objectName()
   ||!aiRangeRequestCurrent(r,q,p)||!d->data||!d->data->canConvert<JudgeStruct*>()
   ||d->data->value<JudgeStruct*>()!=d->judge||!d->judge||d->judgeWho.isNull()
   ||d->judge->who!=d->judgeWho||d->judge->card!=d->judgeCard
   ||d->judge->isGood()!=d->judgeResult
   ||d->judge->reason!=d->reason||d->judge->pattern!=d->pattern||d->judge->good!=d->good
   ||!d->judgeLease||d->judgeLease()!=d->judgeCard
   ||r.roomState()->getCurrentCardUseReason()!=q.reason||r.roomState()->getCurrentCardUsePattern()!=q.pattern)return false;
  return true;
 }
 static bool material(Room&r,const AIRequest&q,ServerPlayer*p,AIFengyiQuestion::Data*d,int id){
  if(!current(r,q,p,d)||id<0||!p->hasSkill("sijyu_fengyi")||p->getPile("sijyu_xing").isEmpty())return false;
  bool disclosed=false;for(const auto &c:q.worldView.handCards)if(c.effectiveId==id)disclosed=true;
  for(const auto &pile:q.worldView.self.piles)if(pile.name=="sijyu_xing"&&pile.cardIds.contains(id))disclosed=true;
  if(!disclosed)return false;
  if(r.getMode().contains("_mini_46")&&!d->judge->isGood()){
   const auto hand=p->handCards();if(hand.isEmpty()||hand.first()!=id||r.getCardOwner(id)!=p||r.getCardPlace(id)!=Player::PlaceHand)return false;
  }else if(!p->getPile("sijyu_xing").contains(id)||r.getCardOwner(id)!=p||r.getCardPlace(id)!=Player::PlaceSpecial)return false;
  const Card*c=Sanguosha->getCard(id);
  return c&&!c->isVirtualCard()&&c->getEffectiveId()==id&&!p->isCardLimited(c,Card::MethodResponse)&&current(r,q,p,d);
 }
};
AIFengyiQuestion::AIFengyiQuestion(Room&r,ServerPlayer*p,const QString&pattern,const QString&prompt,const QVariant&data,int method,bool retrial):d(new Data){
 d->room=&r;d->owner=p;d->data=&data;d->prompt=prompt;
 // Any nested native askForCard permanently revokes suspended authority, even
 // if it returns without changing the room revision or request certificate.
 for(auto *outer:sijyuFengyiFrames.value(&r))outer->d->valid=false;
 sijyuFengyiFrames[&r].append(this);
 auto *runtime=r.roomRuntime();
 if(!runtime||runtime->isClosing())return;
 const auto &game=runtime->lua();const auto &isolated=runtime->ai().lua();
 if(game.isClosing()||game.isClosed()||isolated.isClosing()||isolated.isClosed()
  ||!game.rawState()||!isolated.rawState())return;
 d->gameState=game.rawState();d->isolatedState=isolated.rawState();
 d->gameGeneration=game.generation();d->isolatedGeneration=isolated.generation();
 if(!p||p->getRoom()!=&r||!retrial||method!=Card::MethodResponse||pattern!="@@sijyu_fengyi"||!data.canConvert<JudgeStruct*>())return;
 auto*j=data.value<JudgeStruct*>();if(!j||!j->who||j->who->getRoom()!=&r||!j->card)return;
 if(prompt!="@sijyu_fengyi-card:"+j->who->objectName()+":sijyu_fengyi:"+j->reason)return;
 d->judge=j;d->judgeWho=j->who;d->judgeCard=j->card;d->reason=j->reason;d->pattern=j->pattern;d->good=j->good;d->judgeResult=j->isGood();
 d->judgeLease=AIOwnerReadonlyCardStore::observe(j->card);d->valid=bool(d->judgeLease)&&d->judgeLease()==j->card;
}
AIFengyiQuestion::~AIFengyiQuestion(){auto i=sijyuFengyiFrames.find(d->room);if(i!=sijyuFengyiFrames.end()){i->removeAll(this);if(i->isEmpty())sijyuFengyiFrames.erase(i);}}
QJsonObject selectAIFengyiPhysical(Room&r,const AIRequest&q,ServerPlayer*p,int id){
 auto*d=AIFengyiAccess::top(r);
 const auto fail=[](){return QJsonObject{{"error","Fengyi physical answer lacks current native retrial authority"},{"native_rejected",true}};};
 if(!AIFengyiAccess::current(r,q,p,d)||d->attempted)return fail();
 d->attempted=true; // Selected failure is terminal; no new material scan/ticket.
 if(!AIFengyiAccess::material(r,q,p,d,id))return fail();
 auto lease=AIOwnerReadonlyCardStore::observe(Sanguosha->getCard(id));
 if(!lease||!AIFengyiAccess::current(r,q,p,d))return fail();
 d->selected=id;d->decision=q.decisionId;d->revision=q.stateRevision;d->selectedLease=std::move(lease);
 return{{"accepted",true},{"card_id",id}};
}
const Card *consumeAIFengyiPhysical(Room&r,const AIRequest&q,ServerPlayer*p,int id){
 auto*d=AIFengyiAccess::top(r);
 if(!d||d->consumed||d->selected!=id||d->decision!=q.decisionId||d->revision!=q.stateRevision
  ||!d->selectedLease||!AIFengyiAccess::material(r,q,p,d,id))return nullptr;
 const Card*c=d->selectedLease();if(!c||c!=Sanguosha->getCard(id)||!AIFengyiAccess::current(r,q,p,d))return nullptr;
 d->consumed=true;return c;
}

// ---- sijyu ----
int aiSijyuXuanmengPrototype(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QString &name)
{
 // Fixed sijyu.lua1003: owner PhaseChanging invokes its V2 zero-card response
 // through askForUseCard(MethodNone). No general Lua SkillCard alias admission.
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
  ||!aiRangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::UseCard||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  ||r.pattern!="@@sijyu_xuanmeng"||r.prompt!="@sijyu_xuanmeng"
  ||r.handlingMethod!=Card::MethodNone||skill!="sijyu_xuanmeng"||name!="#sijyu_xuanmeng"
  ||!p->hasSkill(skill))return -1;
 return aiRangeRequestCurrent(*p->getRoom(),r,p)?0:-1;
}

// ---- sijyuoffline-planning ----
#include "card-lifetime-manager.h"

namespace {
constexpr const char *skillName = "sfofl_zhaobing";
constexpr const char *pileName = "sfofl_zhoubing";
bool fail(QString *error, const char *why) { if (error) *error=QString::fromLatin1(why);return false; }
bool liveProduct(Room &room, ServerPlayer *viewer, const SkillInstanceRef &activation,
    const SkillInstanceRef &source, const QList<int> &ids, const Card *card)
{
    if (!viewer || viewer->getRoom()!=&room || !viewer->isAlive()
        || viewer->getPhase()!=Player::Play || room.getCurrent()!=viewer
        || !activation.isValid() || activation.ownerObjectName!=viewer->objectName()
        || activation.key.skillName!=skillName || source!=activation
        || !viewer->hasSkill(skillName)
        || !viewer->hasSkillInstance(skillName,activation.key.instanceID)
        || viewer->isSkillInvalid(skillName,activation.key.instanceID)
        || room.resolveSkillInstanceRootRef(activation)!=source || ids.size()!=1 || ids.first()<0)
        return false;
    const int id=ids.first();
    return card && card==Sanguosha->getCard(id) && !card->isVirtualCard()
        && card->getEffectiveId()==id && card->getSubcards()==ids
        && card->isKindOf("EquipCard") && card->appliedPhysicalEffectSource().isEmpty()
        && viewer->getPile(pileName).contains(id)
        && room.getCardOwner(id)==viewer && room.getCardPlace(id)==Player::PlaceSpecial;
}
bool sijyuofflinePlanningSameFace(const AICardView &face, Room &room, ServerPlayer *viewer,const Card *card)
{
    return card && face.cardId==card->getId() && face.effectiveId==card->getEffectiveId()
        && !face.virtualCard && face.typeId==Card::TypeEquip
        && face.objectName==card->objectName() && face.className==card->getClassName()
        && face.suit==int(card->getSuit()) && face.number==card->getNumber()
        && face.skillName==card->getSkillName(false)
        && !face.legacyIdentityToken.isEmpty()
        && face.legacyIdentityToken==aiLegacyCardIdentity(room,viewer,card);
}
const AICardView *snapshotFace(const AIRequest &request,int id)
{
    for (const auto &pile:request.worldView.self.piles) if (pile.name==pileName) {
        if (!pile.open || !pile.cardIds.contains(id)) return nullptr;
        for (const auto &face:pile.cards) if (face.cardId==id) return &face;
    }
    return nullptr;
}
}

bool aiZhaobingPhysicalScope(const AIRequest &request,const QString &skill)
{
    return skill==skillName && request.kind==AIRequest::Activate
        && request.reason==CardUseStruct::CARD_USE_REASON_PLAY && request.pattern.isEmpty()
        && request.handlingMethod==Card::MethodUse;
}
bool authorizeAIZhaobingPhysicalProduct(const AIRequest &request,Room &room,
    ServerPlayer *viewer,const SkillInstanceRef &activation,const SkillInstanceRef &source,
    const QList<int> &selected,const Card *produced,QString *error)
{
    if (!aiZhaobingPhysicalScope(request,activation.key.skillName)
        || request.decisionId==0 || !viewer || request.viewerObjectName!=viewer->objectName()
        || request.worldView.self.objectName!=viewer->objectName()
        || request.stateRevision!=request.worldView.revision
        || request.stateRevision!=room.roomRuntime()->stateRevision())
        return fail(error,"sfofl physical product requires its current owner Play request");
    if (!liveProduct(room,viewer,activation,source,selected,produced))
        return fail(error,"sfofl physical Equip product left its owned skill/pile/selection domain");
    const auto *face=snapshotFace(request,selected.first());
    if (!face || !sijyuofflinePlanningSameFace(*face,room,viewer,produced))
        return fail(error,"sfofl physical Equip product differs from the disclosed owner snapshot");
    return true;
}

class AIZhaobingPhysicalUseProof final {
public:
    QPointer<Room> room;
    QPointer<ServerPlayer> actor;
    QPointer<const Card> card;
    quint64 revision=0;
    SkillInstanceRef activation,source;
    QList<int> selected;
    QStringList targets;
    AICardView face;
};

bool AIZhaobingPhysicalUseAccess::attach(const AIRequest &request,Room &room,
    ServerPlayer *viewer,const AICardConversionView &ticket,CardUseStruct &use)
{
    if (!authorizeAIZhaobingPhysicalProduct(request,room,viewer,
            ticket.activationRef,ticket.sourceRef,ticket.subcardIds,use.card)
        || use.from!=viewer || !use.hasSkillActivationRequest
        || use.bypass_cost || use.skipSkillEffect || use.skillExecutionID!=0
        || use.activationRef!=ticket.activationRef || use.sourceRef!=ticket.sourceRef
        || ticket.producerVersion!=2 || ticket.producerReason!=request.reason
        || !ticket.producerPattern.isEmpty() || ticket.name!=use.card->objectName()
        || ticket.className!=use.card->getClassName() || ticket.suit!=int(use.card->getSuit())
        || ticket.number!=use.card->getNumber()
        || ticket.legacyIdentityToken!=aiLegacyCardIdentity(room,viewer,use.card)) return false;
    auto proof=std::make_shared<AIZhaobingPhysicalUseProof>();
    proof->room=&room;proof->actor=viewer;proof->card=use.card;proof->revision=request.stateRevision;
    proof->activation=ticket.activationRef;proof->source=ticket.sourceRef;proof->selected=ticket.subcardIds;
    proof->face=*snapshotFace(request,ticket.subcardIds.first());
    for (const auto *target:use.to) {
        if (!target || target->getRoom()!=&room) return false;
        proof->targets<<target->objectName();
    }
    use.m_aiZhaobingPhysicalUse=std::move(proof);
    return true;
}

int AIZhaobingPhysicalUseAccess::validate(Room &room,CardUseStruct &use)
{
    const auto proof=use.m_aiZhaobingPhysicalUse;
    if (!proof) return -1;
    auto *viewer=proof->actor.data();
    if (proof->room!=&room || !viewer || proof->card.isNull() || proof->card!=use.card
        || use.from!=viewer || !use.hasSkillActivationRequest
        || use.bypass_cost || use.skipSkillEffect || use.skillExecutionID!=0
        || use.activationRef!=proof->activation || use.sourceRef!=proof->source
        || room.roomRuntime()->stateRevision()!=proof->revision
        || room.roomState()->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_PLAY
        || !room.roomState()->getCurrentCardUsePattern().isEmpty()) return 0;
    QStringList targets;
    for (const auto *target:use.to) {
        if (!target || target->getRoom()!=&room) return 0;
        targets<<target->objectName();
    }
    if (targets!=proof->targets) return 0;
    EngineRuntimeContextScope engine(*Sanguosha,&room);
    LuaRuntime::Binding game(room.roomRuntime()->lua(),false);
    if (!liveProduct(room,viewer,proof->activation,proof->source,proof->selected,use.card)
        || !sijyuofflinePlanningSameFace(proof->face,room,viewer,use.card)) return 0;
    const auto *skill=dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(skillName));
    if (!skill || skill->isEquipSkill() || room.isAcceptedViewAsEffect(proof->activation)) return 0;
    ActiveSkillRequest request;
    request.initiator=viewer;request.reason=CardUseStruct::CARD_USE_REASON_PLAY;
    request.activationRef=proof->activation;request.sourceRef=proof->source;
    request.selectedCardIds=proof->selected;request.selectedTargetNames=targets;
    const auto stable = [&]() {
        return proof->room==&room && proof->actor==viewer && !proof->card.isNull()
            && proof->card==use.card && room.roomRuntime()->stateRevision()==proof->revision;
    };
    // Same applicable native admission order as Room::resolveActiveSkillRequest.
    // This fixed producer has no declaration/custom cost/pay/on_effect hooks.
    if (!request.generatedCardSelectionValid() || !skill->canActivateRequest(request)
        || !skill->cardSelectionFeasible(request)) return 0;
    if (!stable()) return 0;
    SkillContext context;context.invoker=viewer;context.owner=viewer;context.initiator=viewer;
    context.activationRef=proof->activation;context.instanceID=proof->activation.key.instanceID;
    if (!skill->isUsable(context) || !stable()) return 0;
    const auto expanded=skill->getExpandPileCardIds(viewer);
    if (!stable() || !expanded.contains(proof->selected.first())) return 0;
    ActiveSkillRequest prefix=request;prefix.selectedCardIds.clear();
    if (!skill->canSelectCard(prefix,use.card) || !stable()) return 0;
    prefix.selectedCardIds=proof->selected;
    const Card *produced=skill->createCard(prefix);
    const bool producedLive=produced && globalCardLifetimeManager().isLive(produced);
    const bool exact=stable() && producedLive && produced==use.card && !produced->isVirtualCard()
        && request.generatedCardProductMatches(produced)
        && sijyuofflinePlanningSameFace(proof->face,room,viewer,produced)
        && liveProduct(room,viewer,proof->activation,proof->source,proof->selected,produced);
    if (producedLive && produced!=use.card && produced->isVirtualCard()) const_cast<Card *>(produced)->deleteLater();
    // No gameplay state/skill attribution/card ownership writes. After this
    // entry gate, Room's existing target/limit/validate/onUse/movement runs.
    return exact?1:0;
}

// ---- sijyuoffline-response ----
void projectAIStrategyHaiyouResponse(AIRequest &request, Room &room,
                                    ServerPlayer *viewer, const QVariant &data,
                                    QJsonObject &context)
{
    // sijyuoffline.lua:35953-35977: actual TargetConfirming -> on_cost ->
    // askForSkillInvoke(player, "sfofl_haiyou", ctx.original_data).
    if (!viewer || viewer->getRoom() != &room
        || request.kind != AIRequest::SkillInvoke
        || request.choiceOptions.reason != QStringLiteral("sfofl_haiyou")
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()
        || !viewer->hasSkill("sfofl_haiyou")
        || !data.canConvert<CardUseStruct>() || !context.value("use").isObject())
        return;
    const CardUseStruct use = data.value<CardUseStruct>();
    if (!use.card || !use.card->isKindOf("Slash") || !use.from
        || use.from->getRoom() != &room) return;
    QJsonObject row = context.value("use").toObject();
    row.remove("expected_jink_num");
    row.remove("source_expected_jink_error");
    if (!use.to.contains(viewer)) {
        // Source standard_cards-ai.lua:1140 never dereferences a tag entry for
        // a non-target viewer. No foreign tag read is necessary to return one.
        row.insert("expected_jink_num", 1);
        context.insert("use", row);
        return;
    }
    // The question recipient is a current target. Read only its required slots;
    // never export another player's count or the source's complete tag vector.
    const QStringList numbers = use.from->getTag("Jink_" + use.card->toString()).toStringList();
    double expected = 1;
    bool sourceError = false;
    LuaRuntime::Binding binding(room.roomRuntime()->lua(), false);
    lua_State *state = room.roomRuntime()->lua().state();
    if (!state) return;
    for (int index = 0; index < use.to.size(); ++index) {
        if (use.to[index] != viewer) continue;
        if (index >= numbers.size()) { sourceError = true; break; }
        const QByteArray value = numbers[index].toUtf8();
        // Lua C numeric conversion is the exact tonumber(string) operation,
        // including hexadecimal/exponents. It executes no Lua function or hook.
        lua_pushlstring(state, value.constData(), size_t(value.size()));
        int numeric = 0;
        const double number = lua_tonumberx(state, -1, &numeric);
        lua_pop(state, 1);
        if (!numeric) { sourceError = true; break; }
        if (!std::isfinite(number)) return; // JSON cannot attest nonfinite numbers.
        if (number == 0) { expected = 0; break; }
        if (number > expected) expected = number;
    }
    if (sourceError) row.insert("source_expected_jink_error", true);
    else row.insert("expected_jink_num", expected);
    context.insert("use", row);
}

// ---- maotu-yinhu-bozhi ----
namespace {
bool maotuYinhuBozhiQuestion(const AIRequest&r,Room&room,const ServerPlayer*p){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||!room.getPlayers().contains(const_cast<ServerPlayer*>(p)))return false;
 const bool invoke=r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke"&&r.choiceOptions.reason=="yhbozhi";
 const bool response=r.kind==AIRequest::UseCard&&r.pattern=="@@yhbozhi"
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);
 if(!invoke&&!response)return false;
 for(const auto&i:p->getSkillInstances())if(i.skillName=="yhbozhi")return true;
 return false;
}
}
void projectAIBozhi(AIRequest&r,Room&room,ServerPlayer*p){
 auto row=r.worldView.strategyFacts.value("owner_skill_properties").toObject();row.remove("YHBozhiRecordTrick");
 if(maotuYinhuBozhiQuestion(r,room,p))row.insert("YHBozhiRecordTrick",p->property("YHBozhiRecordTrick").toString());
 if(row.isEmpty())r.worldView.strategyFacts.remove("owner_skill_properties");else r.worldView.strategyFacts.insert("owner_skill_properties",row);
}
bool aiBozhiConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!maotuYinhuBozhiQuestion(r,room,p))return false;
 const QSet<QString>keys{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=keys.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!keys.contains(i.key()))return false;
 const auto projected=r.worldView.strategyFacts.value("owner_skill_properties").toObject().value("YHBozhiRecordTrick");
 if(!projected.isString()||projected.toString()!=p->property("YHBozhiRecordTrick").toString())return false;
 // Fixed yinhu-ai.lua1005–1028 constructs before availability/recast checks.
 // This attests a current owner source name, including empty -> nil clone.
 // It does not query hidden history, native canActivate or mint payment.
 return s.value("source_key")==QJsonValue("yhbozhi")&&s.value("skill")==QJsonValue("yhbozhi")
  &&s.value("name").isString()&&s.value("name")==projected
  &&s.value("suit")==QJsonValue(int(Card::NoSuit))&&s.value("number")==QJsonValue(0)
  &&s.value("subcards").isArray()&&s.value("subcards").toArray().isEmpty();
}

// ---- maotu-yinhu-choice ----
namespace {
bool scope(const AIRequest &r,const ServerPlayer *p) {
    if (!p || !p->getRoom() || !p->isAlive() || r.decisionId==0
        || r.viewerObjectName!=p->objectName() || r.worldView.self.objectName!=p->objectName()
        || r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision()
        || r.kind!=AIRequest::Activate || r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
        || p->getPhase()!=Player::Play || !p->hasSkill("mtjieli")) return false;
    // The shared seed/issuer independently validates the chosen exact scope.
    // This finite catalogue never creates an activation or payment permission.
    QList<AiSkillActionContext> actions=r.skillActions;
    if(r.hasSkillActionContext)actions.prepend(r.skillActionContext);
    for(const auto &a:actions)
        if(a.isValid() && a.activationRef.ownerObjectName==p->objectName()
            && a.activationRef.key.skillName=="mtjieli"
            && p->hasSkillInstance("mtjieli",a.activationRef.key.instanceID)
            && !p->isSkillInvalid("mtjieli",a.activationRef.key.instanceID)) return true;
    return false;
}
}
// maotu506-519: zero selected costs, literal colour on an ActiveSkillCard.
// Bit 1 permits a declaration; bit 0 stays clear: no paid source material.
int aiMtjieliPrototype(const AIRequest &r,const ServerPlayer *p,
                            const QString &skill,const QString &prototype) {
    return skill=="mtjieli" && prototype=="ActiveSkillCard" && scope(r,p) ? 2 : -1;
}
bool aiMtjieliDeclaration(const AIRequest &r,const ServerPlayer *p,const QJsonObject &spec) {
    return scope(r,p) && spec.value("subcards").isArray() && spec.value("subcards").toArray().isEmpty()
        && spec.value("user_string").isString()
        && (spec.value("user_string")==QJsonValue("red") || spec.value("user_string")==QJsonValue("black"));
}
// maotu521-537: dummyCard("duel") => NoSuit/0, setSkillName, then every
// hand card of the declared colour in native hand order. No availability,
// jilei, limit or payment filtering is inserted into this hypothesis.
bool aiMtjieliConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &spec) {
    if (!scope(r,p) || spec.size()!=6 || spec.value("source_key")!=QJsonValue("mtjieli")
        || spec.value("skill")!=QJsonValue("mtjieli") || spec.value("name")!=QJsonValue("duel")
        || spec.value("suit")!=QJsonValue(int(Card::NoSuit)) || spec.value("number")!=QJsonValue(0)
        || !spec.value("subcards").isArray()) return false;
    const auto hand=p->getHandcards();
    if (hand.size()!=r.worldView.handCards.size()) return false;
    QJsonArray red,black;
    for(int i=0;i<hand.size();++i) {
        const Card *c=hand[i];const AICardView &face=r.worldView.handCards[i];
        if(!c || face.effectiveId!=c->getEffectiveId() || face.objectName!=c->objectName()
            || face.className!=c->getClassName() || face.suit!=int(c->getSuit()) || face.number!=c->getNumber()
            || p->getRoom()->getCardOwner(c->getEffectiveId())!=p
            || p->getRoom()->getCardPlace(c->getEffectiveId())!=Player::PlaceHand) return false;
        if(c->isRed())red.append(c->getEffectiveId());
        if(c->isBlack())black.append(c->getEffectiveId());
    }
    // Empty is a legitimate pure source construction for a colour with no
    // current material; native selected activation remains separately checked.
    const QJsonArray ids=spec.value("subcards").toArray();
    return ids==red || ids==black;
}

// ---- maotu-yinhu-context ----
void projectAIQingsi(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    auto family = request.worldView.strategyFacts.value("yinhu").toObject();
    family.remove("qingsi_num");
    const auto publish = [&] {
        if (family.isEmpty()) request.worldView.strategyFacts.remove("yinhu");
        else request.worldView.strategyFacts.insert("yinhu", family);
    };
    publish();
    if (!viewer || viewer->getRoom() != &room || room.roomRuntime()->isClosing()
        || !room.getPlayers().contains(viewer) || !request.decisionId
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()
        || request.kind != AIRequest::PlayerChosen
        || request.choiceOptions.reason != "yhchuzhu") return;
    QSet<QString> offered;
    for (const QString &name : request.choiceOptions.playerNames) {
        ServerPlayer *candidate = room.findPlayerByObjectName(name, true);
        if (!candidate || !room.getPlayers().contains(candidate) || offered.contains(name)) return;
        offered.insert(name);
    }
    // Native YHChuzhu and donor QVariant:toInt use this exact conversion:
    // absent/unconvertible values are known zero; negative values are not clamped.
    // Only this viewer's real accumulated native tag is read. No foreign writer
    // occurrence/status, guessed shared history, or extra public mark is exposed.
    family.insert("qingsi_num", viewer->getTag("YHQingsiNum").toInt());
    publish();
}

// ---- maotu-yinhu ----
namespace {
bool current(const AIRequest&r,Room&room,const ServerPlayer*p){
 return p&&p->getRoom()==&room&&room.roomRuntime()&&!room.roomRuntime()->isClosing()
  &&r.decisionId&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&room.getPlayers().contains(const_cast<ServerPlayer*>(p))&&aiRangeRequestCurrent(room,r,p);
}
bool own(const ServerPlayer*p,const QString&s){for(const auto&i:p->getSkillInstances())if(i.skillName==s)return true;return false;}
bool maotuYinhuPlay(const AIRequest&r){return r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;}
bool prompt(const AIRequest&r,const QString&s){
 return r.kind==AIRequest::UseCard&&r.pattern=="@@"+s
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);
}
bool maotuYinhuScope(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&s){
 if(!current(r,room,p))return false;
 if(s=="mtweiqie")return r.kind==AIRequest::RespondCard&&r.pattern=="@@mtweiqie"
   &&r.prompt.section(':',0,0)=="@mtweiqie"&&r.handlingMethod==Card::MethodNone;
 if(s=="mtxianzheng"||s=="yhshanhai")return prompt(r,s);
 if(s=="yhxinghan")return maotuYinhuPlay(r)&&own(p,s);
 if(s=="yhduwei")return (maotuYinhuPlay(r)||prompt(r,s))&&own(p,s);
 if(s=="mtzhihe")return (maotuYinhuPlay(r)||r.kind==AIRequest::RespondCard||r.kind==AIRequest::UseCard)&&own(p,s);
 return false;
}
bool maotuYinhuInteger(const QJsonValue&v,int&n){
 const double d=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(d)||d<0||d!=std::floor(d)||d>std::numeric_limits<int>::max())return false;n=int(d);return true;
}
bool exact(const AICardView&v,const Card*c,Room&room,const ServerPlayer*p,int id){
 return c&&v.effectiveId==id&&c->getEffectiveId()==id&&v.objectName==c->objectName()
  &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
  &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
const Card*maotuYinhuMaterial(const AIRequest&r,Room&room,const ServerPlayer*p,int id,bool handOnly){
 if(room.getCardOwner(id)!=p)return nullptr;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){
  const Card*c=Sanguosha->getCard(id);for(const auto&v:r.worldView.handCards)if(exact(v,c,room,p,id))return c;
 }
 if(handOnly)return nullptr;
 if(room.getCardPlace(id)==Player::PlaceEquip)for(const Card*c:p->getEquips())if(c->getEffectiveId()==id)
  for(const auto&v:r.worldView.self.equips)if(exact(v,c,room,p,id))return c;
 if(p->getHandPile().contains(id))for(const auto&pile:r.worldView.self.piles)for(const auto&v:pile.cards)
  if(v.effectiveId==id){const Card*c=Sanguosha->getCard(id);if(exact(v,c,room,p,id))return c;}
 return nullptr;
}
QJsonArray shown(const AIRequest&r,Room&room,const ServerPlayer*p,bool&known,QJsonObject *disclosedFaces=nullptr){
 known=false;if(!maotuYinhuScope(r,room,p,"mtweiqie"))return{};
 const auto row=readAIStrategyNotifiedPile(room,const_cast<ServerPlayer*>(p),"#mtweiqie");
 if(!row.value("ordered_ids").isArray())return{};
 const auto ids=row.value("ordered_ids").toArray();const auto locations=row.value("locations").toObject();QJsonObject faces;
 // notifyMoveToPile writes the exact old tag as QVariantList. Order and
 // duplicates are authenticated by the actual notification, never a subset.
 QJsonArray tag;for(const auto&v:p->getTag("mtweiqieForAI").toList())tag.append(v.toInt());
 if(ids!=tag)return{};
 for(const auto&v:ids){int id;if(!maotuYinhuInteger(v,id))return{};
  // The notification provides order; the actual public table move provides
  // the current wrapped face. Printed-face equality alone grants no knowledge.
  const auto disclosure=readAIStrategyPublicDisclosure(room,const_cast<ServerPlayer*>(p),id);
  if(!disclosure.value("known").toBool()||disclosure.value("place")!=QJsonValue(int(Player::PlaceTable)))return{};
  const auto face=disclosure.value("card").toObject();const auto loc=locations.value(QString::number(id)).toObject();
  if(face.isEmpty()||loc.value("place")!=QJsonValue(int(Player::PlaceTable))||room.getCardPlace(id)!=Player::PlaceTable)return{};
  const Card*c=Sanguosha->getCard(id);
  if(!c||face.value("name")!=QJsonValue(c->objectName())||face.value("class_name")!=QJsonValue(c->getClassName())
   ||face.value("suit")!=QJsonValue(int(c->getSuit()))||face.value("number")!=QJsonValue(c->getNumber()))return{};
  faces.insert(QString::number(id),face);
 }
 if(!current(r,room,p))return{};if(disclosedFaces)*disclosedFaces=faces;known=true;return ids;
}
}
void projectAIMaotuYinhu(AIRequest&r,Room&room,ServerPlayer*p){
 auto props=r.worldView.strategyFacts.value("owner_skill_properties").toObject();
 props.remove("yhduwei_damage_card");props.remove("YHShanhaiGetIds");
 if(maotuYinhuScope(r,room,p,"yhduwei"))props.insert("yhduwei_damage_card",p->property("yhduwei_damage_card").toString());
 if(maotuYinhuScope(r,room,p,"yhshanhai"))props.insert("YHShanhaiGetIds",p->property("YHShanhaiGetIds").toString());
 if(props.isEmpty())r.worldView.strategyFacts.remove("owner_skill_properties");else r.worldView.strategyFacts.insert("owner_skill_properties",props);
 auto m=r.worldView.strategyFacts.value("maotu").toObject();m.remove("mtweiqie_ids");m.remove("mtweiqie_cards");bool ok=false;QJsonObject cards;const auto ids=shown(r,room,p,ok,&cards);
 if(ok){m.insert("mtweiqie_ids",ids);m.insert("mtweiqie_cards",cards);}
 if(m.isEmpty())r.worldView.strategyFacts.remove("maotu");else r.worldView.strategyFacts.insert("maotu",m);
}
bool aiMaotuYinhuConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return false;
 const auto skill=s.value("source_key").toString();
 if(s.value("skill")!=QJsonValue(skill)||!s.value("name").isString()||s.value("suit")!=QJsonValue(int(Card::NoSuit))
  ||s.value("number")!=QJsonValue(0)||!s.value("subcards").isArray()||!maotuYinhuScope(r,room,p,skill))return false;
 const auto ids=s.value("subcards").toArray();if(ids.isEmpty())return false;
 QSet<int>seen;QList<const Card*>materials;
 if(skill=="mtweiqie"){
  bool known=false;const auto all=shown(r,room,p,known);if(!known||s.value("name")!=QJsonValue("slash"))return false;
  QList<int> remaining;for(const auto&v:all)remaining.append(v.toInt());
  for(const auto&v:ids){int id;if(!maotuYinhuInteger(v,id)||!remaining.removeOne(id))return false;}
  // Preserve repeated source material only up to the actual notification's
  // multiplicity. This readonly path never changes selected native gates.
  return current(r,room,p);
 }
 for(const auto&v:ids){int id;if(!maotuYinhuInteger(v,id)||seen.contains(id))return false;seen.insert(id);
  const Card*c=maotuYinhuMaterial(r,room,p,id,skill=="mtzhihe"||skill=="yhshanhai");if(!c)return false;materials<<c;}
 if(skill=="mtzhihe"){
  QSet<int>suits;bool offered=false;const auto pileIds=p->getPile("yhjyye");
  for(int id:pileIds){const Card*c=nullptr;
   for(const auto&pile:r.worldView.self.piles)if(pile.name=="yhjyye")for(const auto&v:pile.cards)if(v.effectiveId==id){
    const Card*live=Sanguosha->getCard(id);if(exact(v,live,room,p,id))c=live;}
   if(!c)return false;suits.insert(int(c->getSuit()));
   if((c->isKindOf("BasicCard")||c->isNDTrick())&&s.value("name")==QJsonValue(c->objectName()))offered=true;
  }
  if(ids.size()!=qMax(1,4-suits.size()))return false;
  for(const Card*c:materials)if(c->getSuit()!=materials.first()->getSuit())return false;
  // Fill names come from the witnessed pile. Guhuo's requested cname is a
  // public printing name, but its native selected declaration stays separate.
  if(maotuYinhuPlay(r)&&!offered)return false;
  if(!maotuYinhuPlay(r)&&!r.worldView.strategyFacts.value("pattern_names").toArray().contains(s.value("name")))return false;
 }else{
  if(ids.size()!=1)return false;
  if(skill=="yhduwei"){
   const auto name=r.worldView.strategyFacts.value("owner_skill_properties").toObject().value("yhduwei_damage_card");
   if(!name.isString()||name!=s.value("name")||name.toString()!=p->property("yhduwei_damage_card").toString())return false;
  }else{
   if(s.value("name")!=QJsonValue("slash"))return false;
   if(skill=="yhshanhai"){
    const auto value=r.worldView.strategyFacts.value("owner_skill_properties").toObject().value("YHShanhaiGetIds");
    if(!value.isString()||value.toString()!=p->property("YHShanhaiGetIds").toString()
     ||!value.toString().split('+').contains(QString::number(*seen.begin())))return false;
   }
  }
 }
 // No card-limited/type/availability test here: source constructs before those
 // tests, notably yhxinghan's source non-Basic versus native Basic mismatch.
 return current(r,room,p);
}
bool aiMaotuYinhuPublicMaterial(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 return s.value("source_key")==QJsonValue("mtweiqie")&&aiMaotuYinhuConstruction(r,room,p,s);
}

// ---- keqiwendao ----
namespace {
thread_local AIKeqiwendaoScope *activeWendao = nullptr;
QString keqiwendaoQuestion(AIRequest::DecisionKind kind)
{
    if (kind == AIRequest::PlayersChosen) return "players_chosen";
    if (kind == AIRequest::Discard) return "discard";
    if (kind == AIRequest::AmazingGrace) return "amazing_grace";
    return {};
}
bool publicFacePattern(const QString &pattern)
{
    if (pattern.contains('$')) return false;
    for (const QString &branch : pattern.split('#')) {
        const QStringList factors = branch.split('|');
        if (factors.size() >= 4 && factors.at(3) != ".") return false;
    }
    return true;
}
}

AIKeqiwendaoScope::AIKeqiwendaoScope(Room &room,
    const TriggerSkillV2 *definition, SkillContext *context)
    : previous_(activeWendao)
{
    // Always push, even for an unrelated nested skill: it must not inherit the
    // outer skill's private decision capability.
    activeWendao = this;
    if (!definition || definition->objectName() != "keqiwendao" || !context
        || context->current_event != AskForRetrial || !context->owner
        || context->owner->getRoom() != &room || !context->original_data
        || context->activationRef.key.skillName != "keqiwendao"
        || context->activationRef.ownerObjectName != context->owner->objectName()
        || !context->original_data->canConvert<JudgeStruct *>()) return;
    JudgeStruct *judge = context->original_data->value<JudgeStruct *>();
    if (!judge || judge->who != context->owner) return;
    room_ = &room; context_ = context; judge_ = judge;
}

AIKeqiwendaoScope::~AIKeqiwendaoScope()
{
    activeWendao = previous_;
}

void projectAIKeqiwendao(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    const QString kind = keqiwendaoQuestion(request.kind);
    if (kind.isEmpty() || request.choiceOptions.reason != "keqiwendao") return;
    // Explicit unknown for the exact named question outside its native frame.
    QJsonObject result{{"known", false}};
    const auto publish = [&]() {
        request.choiceOptions.context.insert("keqiwendao_judge", result);
    };
    publish();
    const auto *scope = activeWendao;
    if (!viewer || viewer->getRoom() != &room || room.roomRuntime()->isClosing()
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()
        || !scope || scope->room_ != &room || !scope->context_ || !scope->judge_)
        return;
    const SkillContext &context = *scope->context_;
    if (!context.original_data || context.original_data->value<JudgeStruct *>() != scope->judge_
        || context.owner != scope->judge_->who) return;
    if (request.kind == AIRequest::Discard) {
        if (!context.targets.contains(viewer)) return;
    } else if (viewer != context.owner) return;
    result = QJsonObject{{"known", true}, {"viewer", viewer->objectName()},
        {"decision_id", request.getDecisionId()}, {"state_revision", request.getStateRevision()},
        {"kind", kind}, {"reason", "keqiwendao"}};
    const QVariant value = room.getTag("keqiwendaoJudge");
    JudgeStruct *tagJudge = value.canConvert<JudgeStruct *>() ? value.value<JudgeStruct *>() : nullptr;
    if (!tagJudge) {
        result.insert("present", false); publish(); return; // known source null; not unknown
    }
    if (tagJudge != scope->judge_) { result = QJsonObject{{"known", false}}; publish(); return; }
    result.insert("present", true);
    result.insert("who", tagJudge->who->objectName());
    result.insert("good", tagJudge->isGood()); // cached result, including native UNKNOWN => false
    QList<AICardView> disclosed;
    if (request.kind == AIRequest::AmazingGrace) {
        for (const AICardView &card : request.choiceOptions.cards)
            if (request.choiceOptions.cardIds.contains(card.effectiveId)) disclosed.append(card);
    } else if (request.kind == AIRequest::Discard) {
        disclosed = request.worldView.handCards;
        disclosed.append(request.worldView.self.equips);
    } else {
        if (request.choiceOptions.playerNames.contains(viewer->objectName())) {
            disclosed = request.worldView.handCards;
            disclosed.append(request.worldView.self.equips);
        }
        for (const AIPlayerView &player : request.worldView.players) {
            if (!request.choiceOptions.playerNames.contains(player.objectName)) continue;
            disclosed.append(player.knownCards);
            disclosed.append(player.equips);
        }
    }
    QJsonObject outcomes;
    const bool predicateVisible = tagJudge->who == viewer || publicFacePattern(tagJudge->pattern);
    QSet<int> seen;
    for (const AICardView &view : disclosed) {
        const int id = view.effectiveId;
        if (id < 0 || view.virtualCard || seen.contains(id)) continue;
        seen.insert(id);
        const Card *card = room.getCard(id);
        if (!card || card->isVirtualCard() || card->getEffectiveId() != id
            || card->objectName() != view.objectName || card->getClassName() != view.className
            || int(card->getSuit()) != view.suit || card->getNumber() != view.number) continue;
        QJsonObject row{{"card_id", id}, {"name", view.objectName}, {"class_name", view.className},
            {"suit", view.suit}, {"number", view.number}};
        // DIRECT JudgeStruct::isGood(card), never CardFilter or ordinary
        // hypothetical retrial outcomes. No new authority, flags or skill facts.
        if (predicateVisible) row.insert("good", tagJudge->isGood(card));
        outcomes.insert(QString::number(id), row);
    }
    result.insert("outcomes", outcomes);
    publish();
}

// ---- yongjian-kearcane-events ----
namespace {
class History final : public QObject {
public:
    explicit History(Room &r) : QObject(&r) {}
    bool initialized=false, present=false;
    quint64 serial=0;
    QPointer<ServerPlayer> actor;
};
History *history(Room &room, bool create=true)
{
    for (QObject *child:room.children())
        if (auto *row=dynamic_cast<History *>(child)) return row;
    return create ? new History(room) : nullptr;
}
bool role(const QString &s)
{ return s=="lord" || s=="loyalist" || s=="rebel" || s=="renegade"; }
}
void clearAITargetHistory(Room &room)
{
    auto *h=history(room);
    h->initialized=true; h->present=false; h->serial=0; h->actor.clear();
}
void observeAITargetHistoryFilterEvent(Room &room, int event,
    ServerPlayer *player, const QVariant &data)
{
    if (event!=TargetSpecified || !player || player->getRoom()!=&room) return;
    auto *h=history(room);
    // Every source write replaces the old record, including malformed or foreign
    // newest values. Never resurrect a previous own record after nested use.
    h->present=true; ++h->serial; h->actor.clear();
    if (!data.canConvert<CardUseStruct>()) return;
    const auto use=data.value<CardUseStruct>();
    if (!use.from || use.from->getRoom()!=&room) return;
    h->actor=use.from;
    // The source stores a full QVariant, but no nullification list is copied to
    // this observer ledger. Its writers are not all public (e.g. goddess1311),
    // and actor ownership alone does not authorize the mutable native member.

}
void projectAITargetHistory(AIRequest &r, Room &room, ServerPlayer *viewer)
{
    if (r.kind!=AIRequest::SkillInvoke || r.choiceOptions.reason!="zd_zhungangshuo") return;
    QJsonObject row{{"known",false}};
    auto publish=[&](){auto y=r.choiceOptions.context.value("yongjian").toObject();
        y.insert("target_specified_use",row);r.choiceOptions.context.insert("yongjian",y);};
    publish();
    if (!viewer || viewer->getRoom()!=&room || room.roomRuntime()->isClosing()
        || r.viewerObjectName!=viewer->objectName()
        || r.worldView.self.objectName!=viewer->objectName()
        || r.stateRevision!=room.roomRuntime()->stateRevision()) return;
    const auto *h=history(room,false);
    if (!h || !h->initialized) return;
    row=QJsonObject{{"known",true},{"present",h->present},{"viewer",viewer->objectName()},
        {"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()},
        {"serial",QString::number(h->serial)}};
    if (!h->present) {publish();return;}
    // Full source history exists. Card::onUse public logs carry card/from/to,
    // not this list. No source-safe complete membership witness exists yet.
    row=QJsonObject{{"known",false}}; publish();
}
void projectAIAzirshenjiEvent(AIWorldView &world, Room &room, ServerPlayer *viewer,
    ServerPlayer *eventPlayer, const AIEventView &event, const QVariant &data)
{
    world.strategyFacts.remove("azirshenji_role_witness");
    if (!viewer || viewer->getRoom()!=&room || !eventPlayer || eventPlayer->getRoom()!=&room
        || world.self.objectName!=viewer->objectName() || event.triggerEvent!=CardFinished
        || !data.canConvert<CardUseStruct>()) return;
    const auto use=data.value<CardUseStruct>();
    if (!use.card || use.card->objectName()!="azirshenjiCard") return;
    QJsonArray rows;
    for (ServerPlayer *p:room.getOtherPlayers(eventPlayer)) {
        const AIPlayerView *view=nullptr;
        if (p==viewer) view=&world.self;
        else for (const auto &v:world.players) if (v.objectName==p->objectName()) {view=&v;break;}
        QJsonObject row{{"player",p->objectName()},{"head_known",false}};
        // Raw head name, not actualGeneral replacement. Foreign raw face is
        // lawful only when the ordinary/public snapshot disclosed that head.
        if (view && (p==viewer || (!view->generalName.isEmpty()
            && view->generalName!="anjiang" && view->generalName==p->getGeneralName()))) {
            row.insert("head_known",true);
            const bool solar=p->getGeneralName()=="kesolardisk";
            row.insert("solar",solar);
            if (solar) {
                const bool known=view->roleRevealed && role(view->role);
                row.insert("role_known",known);
                if (known) row.insert("role",view->role);
            }
        }
        rows.append(row);
    }
    world.strategyFacts.insert("azirshenji_role_witness",
        QJsonObject{{"actor",eventPlayer->objectName()},{"rows",rows}});
}

// ---- bu-stwo-guicai ----
std::unique_ptr<Card> aiBuStwoGuicaiEvaluation(const AIRequest &q,Room &room,ServerPlayer *viewer,int id,
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
