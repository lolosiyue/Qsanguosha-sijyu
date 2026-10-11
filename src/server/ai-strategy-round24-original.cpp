#include "ai-strategy-round24-original.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>

bool aiRound24OriginalConstruction(const AIRequest &request,const ServerPlayer *viewer,const QJsonObject &spec)
{
    const QString key=spec.value("source_key").toString();
    if(key!="heg_miewu_plan" && key!="heg_miewu")return false;
    static const QSet<QString> names={"alliance_feast","burning_camps","god_salvation","savage_assault","archery_attack",
        "befriend_attacking","ex_nihilo","duel","snatch","dismantlement","drowning","iron_chain","await_exhausted",
        "amazing_grace","fight_together","lure_tiger","imperial_order","collateral","known_both","fire_attack",
        "heg_threaten_emperor","indulgence","supply_shortage","lightning","slash","fire_slash","thunder_slash","peach","analeptic"};
    if(!viewer || !viewer->getRoom() || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=viewer->getRoom()->roomRuntime()->stateRevision()
        || request.kind!=AIRequest::Activate || request.reason!=CardUseStruct::CARD_USE_REASON_PLAY
        || !viewer->hasSkill("heg_miewu") || !names.contains(spec.value("name").toString())
        || !spec.value("subcards").isArray() || spec.value("subcards").toArray().size()!=1)return false;
    const QJsonValue value=spec.value("subcards").toArray().first();const double n=value.toDouble(-1);
    if(!value.isDouble() || !std::isfinite(n) || n<0 || std::floor(n)!=n || n>std::numeric_limits<int>::max())return false;
    const int id=int(n);const Room &room=*viewer->getRoom();
    if(room.getCardOwner(id)!=viewer || (room.getCardPlace(id)!=Player::PlaceHand && room.getCardPlace(id)!=Player::PlaceEquip))return false;
    bool disclosed=false;
    for(const AICardView &c:request.worldView.handCards)if(c.effectiveId==id)disclosed=true;
    for(const AICardView &c:request.worldView.self.equips)if(c.effectiveId==id)disclosed=true;
    if(!disclosed)return false;
    const Card *material=Sanguosha->getCard(id);if(!material)return false;
    if(key=="heg_miewu_plan")return spec.value("skill").isString()&&spec.value("skill").toString().isEmpty()
        &&spec.value("suit")==int(Card::NoSuit)&&spec.value("number")==0;
    return spec.value("skill")==QStringLiteral("heg_miewu")&&spec.value("suit")==int(material->getSuit())
        &&spec.value("number")==material->getNumber();
}


void projectAIRound24OriginalContext(AIRequest &request,Room &room,ServerPlayer *viewer,const QVariant &incoming,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if(!viewer || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=room.roomRuntime()->stateRevision())return;
    QJsonObject context=request.choiceOptions.context,row;
    if(request.kind==AIRequest::UseCard && request.pattern=="@@heg_ziliang") {
        const QVariant data=viewer->getTag("ziliang_aidata");
        if(data.canConvert<DamageStruct>()) {
            const auto d=data.value<DamageStruct>();
            row.insert("ziliang_damage",d.to ? QJsonValue(QJsonObject{{"to",d.to->objectName()}}) : QJsonValue(false));
        } else row.insert("ziliang_damage",false);
    } else if(request.kind==AIRequest::UseCard && request.pattern=="@@heg_qianhuan") {
        const QVariant data=viewer->getTag("qianhuan_data");
        if(data.canConvert<CardUseStruct>()) {
            const auto use=data.value<CardUseStruct>();QJsonObject u;QJsonArray targets;
            if(use.from)u.insert("from",use.from->objectName());
            if(use.card)u.insert("card",publicCard(use.card));
            for(const auto *to:use.to)if(to)targets.append(to->objectName());
            u.insert("to",targets);row.insert("qianhuan_use",u);
        } else row.insert("qianhuan_use",false); // source QVariantList->CardUse has no card.
    }
    if(request.kind==AIRequest::Choice && request.choiceOptions.reason=="heg_yingyang"
        && incoming.canConvert<PindianStruct *>()) {
        const auto *pd=incoming.value<PindianStruct *>();
        if(pd && (pd->from==viewer || pd->to==viewer) && pd->from && pd->to && pd->from_card && pd->to_card
            && room.getCardPlace(pd->from_card->getEffectiveId())==Player::PlaceTable
            && room.getCardPlace(pd->to_card->getEffectiveId())==Player::PlaceTable)
            context.insert("pindian",QJsonObject{{"reason",pd->reason},{"from",pd->from->objectName()},
                {"to",pd->to->objectName()},{"from_number",pd->from_number},{"to_number",pd->to_number}});
    }
    if(!row.isEmpty()) {context.insert("original_formation",row);}
    request.choiceOptions.context=context;
}

int aiRound24OriginalPrototype(const AIRequest &request,const ServerPlayer *viewer,
    const QString &skill,const QString &prototype)
{
    if(!viewer || !viewer->getRoom() || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=viewer->getRoom()->roomRuntime()->stateRevision()
        || prototype!="ActiveSkillCard" || !viewer->hasSkill(skill))return -1;
    static const QSet<QString> zeroPlay{"heg_shangyi","heg_heyi","heg_tianfu","heg_niaoxiang",
        "heg_cunsi","heg_duanxie","heg_guixiu","heg_fengshi"};
    if(request.kind==AIRequest::Activate && request.reason==CardUseStruct::CARD_USE_REASON_PLAY) {
        if(zeroPlay.contains(skill))return 0;
        if(skill=="heg_wendao")return 1;
    }
    if(request.kind==AIRequest::UseCard && request.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        && (skill=="heg_ziliang" || skill=="heg_huyuan" || skill=="heg_qianhuan")
        && request.pattern=="@@"+skill)return 1;
    return -1;
}
