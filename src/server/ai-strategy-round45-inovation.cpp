#include "ai-strategy-round45-inovation.h"
#include "ai-strategy-round45-goutong.h"
#include "ai-strategy-round45-inovation-state.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include "card.h"
#include "engine.h"
#include "ai-strategy-round44-certificate.h"
#include <cmath>
#include <limits>
namespace {
bool inovationMaterialDisclosed(const AIRequest&r,int id,bool expanded)
{
 for(const auto&c:r.worldView.handCards)if(c.effectiveId==id)return true;
 if(!expanded)return false;
 for(const auto&c:r.worldView.self.equips)if(c.effectiveId==id)return true;
 for(const auto&c:r.worldView.self.judgingArea)if(c.effectiveId==id)return true;
 for(const auto&pile:r.worldView.self.piles)for(const auto&c:pile.cards)if(c.effectiveId==id)return true;
 return false;
}
bool inovationMaterialSpec(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s)
{
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()||!aiRound44RangeRequestCurrent(*p->getRoom(),r,p)
  ||s.size()!=6||!s.value("source_key").isString()||s.value("skill")!=s.value("source_key")
  ||!s.value("subcards").isArray()||s.value("subcards").toArray().size()!=1)return false;
 const auto key=s.value("source_key").toString();
 if(key=="touzhi"){
  if(r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!p->hasSkill(key)||s.value("name")!=QJsonValue("slash"))return false;
 }else if(key=="youjiao"||key=="guangyu"){
  if(r.kind!=AIRequest::UseCard||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.pattern!="@@"+key
   ||!p->hasSkill(key)||s.value("name")!=QJsonValue("key_trick"))return false;
 }else return false;
 const auto idValue=s.value("subcards").toArray().first();if(!idValue.isDouble())return false;
 const double raw=idValue.toDouble();if(!std::isfinite(raw)||raw<0||raw>std::numeric_limits<int>::max()||raw!=std::floor(raw))return false;
 const int id=int(raw);
 if(!inovationMaterialDisclosed(r,id,key=="guangyu")||p->getRoom()->getCardOwner(id)!=p)return false;
 if(key=="guangyu"){
  if(!p->property("guangyu").toString().split('+').contains(QString::number(id)))return false;
 }else if(!p->handCards().contains(id))return false;
 const Card*c=Sanguosha->getCard(id);if(!c)return false;
 // Source class predicates are not native payment checks: notably do not
 // reject using here. Final V2 receipt will reject that selected cost.
 if(key=="touzhi"&&(!c->isKindOf("TrickCard")||c->isKindOf("AOE")||c->isKindOf("GodSalvation")||c->isKindOf("AmazingGrace")||c->isKindOf("Collateral")))return false;
 if(key=="youjiao"&&(!c->isKindOf("BasicCard")||c->isKindOf("Peach")))return false;
 const int printedNumber=c->getNumber()>13?0:c->getNumber();
 return s.value("suit")==QJsonValue(int(c->getSuit()))&&s.value("number")==QJsonValue(printedNumber);
}
}

void projectAIRound45InovationWorld(AIWorldView &world,Room &room,const ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom()!=&room || world.self.objectName!=viewer->objectName()) return;
    QJsonObject observed;
    // PlayerStateService::akarinPlayer/removeAkarinEffect send the exact
    // membership Boolean to this observer (targeted notifyAkarinVisibility).
    // Never serialize the underlying tag/list or another observer's value.
    for (ServerPlayer *target:room.getAllPlayers(true))
        observed.insert(target->objectName(),room.isAkarin(target,const_cast<ServerPlayer *>(viewer)));
    auto row=world.strategyFacts.value("inovation").toObject();
    row.insert("akarin",observed);world.strategyFacts.insert("inovation",row);
}
void projectAIRound45InovationContext(AIRequest &request,Room &room,ServerPlayer *viewer,const QVariant &data)
{
    projectAIRound45Goutong(request,room,viewer,data);
    projectAIRound45ShoushiInput(request,room,viewer,data);
    if(viewer&&viewer->getRoom()==&room&&request.viewerObjectName==viewer->objectName()
        &&aiRound44RangeRequestCurrent(room,request,viewer)&&request.kind==AIRequest::UseCard
        &&request.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&request.pattern=="@@guangyu"&&viewer->hasSkill("guangyu")){
        QJsonArray ids;bool known=true;
        for(const QString&word:viewer->property("guangyu").toString().split('+')){
            bool ok=false;const int id=word.toInt(&ok);const bool disclosed=inovationMaterialDisclosed(request,id,true);
            if(!ok||id<0||word!=QString::number(id)||!disclosed||room.getCardOwner(id)!=viewer){known=false;break;}
            ids.append(id);
        }
        // Native writer ListI2S never supplies empty/malformed lists to this
        // question. Restored/unwitnessed shapes stay unknown, not fabricated [].
        request.choiceOptions.context.insert("inovation_guangyu",known?QJsonObject{{"known",true},{"ids",ids}}:QJsonObject{{"known",false}});
    }

    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=room.roomRuntime()->stateRevision()
        || request.kind!=AIRequest::SkillInvoke || request.choiceOptions.reason!="kuisi") return;
    // QVariant::value<DeathStruct>() has a null damage pointer for other types;
    // this is source cast behavior, not an inferred zero/false strategy result.
    const DeathStruct death=data.value<DeathStruct>();
    QJsonObject row{{"known",true},{"has_damage",death.damage!=nullptr}};
    if (death.damage) {
        auto *from=death.damage->from;
        if (from && (from->getRoom()!=&room || !room.getAllPlayers(true).contains(from))) return;
        row.insert("from",from?QJsonValue(from->objectName()):QJsonValue(false));
    }
    request.choiceOptions.context.insert("inovation_kuisi",row);
}
namespace {
bool currentOwner(const AIRequest &r,const ServerPlayer *p)
{
    return p && p->getRoom() && r.viewerObjectName==p->objectName()
        && r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();
}
}
int aiRound45InovationPrototype(const AIRequest &r,const ServerPlayer *p,
                               const QString &skill,const QString &prototype)
{
    if (!currentOwner(r,p) || r.kind!=AIRequest::Activate
        || r.reason!=CardUseStruct::CARD_USE_REASON_PLAY || !p->hasSkill(skill)) return -1;
    // Readonly source prototypes only. Neither cost feasibility nor targets
    // are paid here; the existing selected V2 path performs final admission.
    static const QMap<QString,QString> names{{"inovation_zhuren","InovationZhurenCard"},
        {"diangong","DiangongCard"},{"zhiling","ZhilingCard"},{"taxian","TaxianCard"},
        {"ningju","NingjuCard"},{"jiguan","JiguanCard"}};
    if (!names.contains(skill) || names.value(skill)!=prototype) return -1;
    return (skill=="inovation_zhuren" || skill=="diangong") ? 1 : 0;
}
bool aiRound45InovationConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
    if(inovationMaterialSpec(r,p,s))return true;
    // Exact source778–786 Slash(NoSuit,0) before the target-loop. No inferred
    // availability and no early cost validation; this unskilled clone cannot pay.
    return currentOwner(r,p) && r.kind==AIRequest::Activate
        && r.reason==CardUseStruct::CARD_USE_REASON_PLAY && p->hasSkill("ningju")
        && s.size()==6 && s.value("source_key")==QJsonValue("inovation.ningju")
        && s.value("name")==QJsonValue("slash") && s.value("skill")==QJsonValue("")
        && s.value("suit")==QJsonValue(int(Card::NoSuit)) && s.value("number")==QJsonValue(0)
        && s.value("subcards").isArray() && s.value("subcards").toArray().isEmpty();
}
