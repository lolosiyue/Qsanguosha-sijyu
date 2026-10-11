#include "ai-strategy-round26-original.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "general.h"
#include "skill.h"
#include <QJsonArray>
#include <QHash>
#include <QSet>
static bool admitted(const AIRequest &r,const ServerPlayer *p) {
    return p&&p->getRoom()&&r.viewerObjectName==p->objectName()&&r.worldView.enableHegemony
        &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();
}
int aiRound26OriginalPrototype(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QString &prototype) {
    if(!admitted(r,p)||!p->hasSkill(skill))return -1;
    // Fixed HAocai legacy response proposal is a zero-cost declared prototype.
    // Native V2 source instance admission and final create remain authoritative.
    if(skill=="heg_aocai" && prototype=="HAocaiCard") {
        static const QSet<QString> patterns{"slash","jink","peach","analeptic","peach+analeptic"};
        if((r.kind==AIRequest::RespondCard||r.kind==AIRequest::UseCard)
            &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            &&p->getPhase()==Player::NotActive&&patterns.contains(r.pattern))return 2;
        return -1;
    }
    if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY) {
        static const QHash<QString,QPair<QString,int>> rows{
            {"heg_daoshu",{"HDaoshuCard",0}},{"heg_jinghe",{"HJingheCard",1}},
            {"heg_huoqi",{"HHuoqiCard",1}},{"heg_xianshou",{"HXianshouCard",0}},
            {"heg_zhuangrong",{"HZhuangrongCard",1}},{"heg_hongyuan",{"HHongyuanCard",1}},
            {"shefu",{"ShefuCard",1}},{"heg_quanjin",{"HQuanjinCard",1}},
            {"heg_zaoyun",{"HZaoyunCard",1}},{"heg_diaogui",{"HDiaoguiCard",1}},
            {"heg_paiyi",{"HPaiyiCard",1}},{"heg_huaiyi",{"HHuaiyiCard",0}},
            {"heg_jinfa",{"HJinfaCard",1}},{"heg_duwu",{"HDuwuCard",1}},
            {"heg_qingyin",{"HQingyinCard",0}},{"heg_imperialedicttrick",{"HImperialEdictTrickCard",0}},
            {"heg_imperialedictattach",{"HImperialEdictAttachCard",1}}
        };
        auto it=rows.constFind(skill);return it!=rows.constEnd()&&it.value().first==prototype?it.value().second:-1;
    }
    if(r.kind==AIRequest::UseCard&&r.reason!=CardUseStruct::CARD_USE_REASON_PLAY) {
        static const QHash<QString,QPair<QString,QString>> rows{
            {"@@heg_jujian",{"heg_jujian","ActiveSkillCard"}},
            {"@@heg_jiansu",{"heg_jiansu","HJiansuCard"}},
            {"@@heg_midao",{"heg_midao","ActiveSkillCard"}},
            {"@@heg_shilu",{"heg_shilu","ActiveSkillCard"}},
            {"@@heg_consolidatecountrygive",{"heg_consolidatecountrygive","ActiveSkillCard"}}
        };
        auto it=rows.constFind(r.pattern);if(it!=rows.constEnd()&&it.value().first==skill&&it.value().second==prototype)return 1;
    }
    return -1;
}
bool aiRound26OriginalConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s) {
    if(!admitted(r,p)||!s.value("subcards").isArray())return false;
    const QString key=s.value("source_key").toString(),name=s.value("name").toString();
    const QJsonArray ids=s.value("subcards").toArray();
    const int suit=s.value("suit").toInt(-2),number=s.value("number").toInt(-1);
    if(s.value("suit")!=suit||s.value("number")!=number||suit<int(Card::Spade)||suit>int(Card::NoSuit)||number<0||number>15)return false;
    if(key=="heg_round26_printed") {
        if(!ids.isEmpty()||s.value("skill")!=QStringLiteral(""))return false;
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&p->hasSkill("heg_diaogui")&&name=="lure_tiger")return true;
        return p->hasSkill("heg_zhenxi")&&((r.kind==AIRequest::Choice&&r.choiceOptions.reason=="heg_zhenxi")||(r.kind==AIRequest::UseCard&&r.pattern=="@@heg_zhenxi_trick"))
            &&((name=="indulgence"&&suit==Card::Diamond)||(name=="supply_shortage"&&suit==Card::Club))&&number==0;
    }
    if(ids.size()!=1)return false;
    const int id=ids.first().toInt(-1);if(id<0||ids.first()!=id)return false;
    const QString skill=key=="_heg_zhenxi"?QStringLiteral("heg_zhenxi"):key;
    if(!p->hasSkill(skill)||s.value("skill")!=key)return false;
    if(key=="heg_guishu") {if(r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||(name!="known_both"&&name!="befriend_attacking"))return false;}
    else if(key=="mumeng") {if(r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||name!="befriend_attacking")return false;}
    else if(key=="_heg_zhenxi") {if(r.kind!=AIRequest::UseCard||r.pattern!="@@heg_zhenxi_trick"||(name!="indulgence"&&name!="supply_shortage"))return false;}
    else return false;
    bool disclosed=false;
    for(const AICardView &v:r.worldView.handCards)if(v.effectiveId==id)disclosed=true;
    for(const AICardView &v:r.worldView.self.equips)if(v.effectiveId==id)disclosed=true;
    for(const AICardPileView &pile:r.worldView.self.piles)for(const AICardView &v:pile.cards)if(v.effectiveId==id)disclosed=true;
    const Room *room=p->getRoom();if(!disclosed||room->getCardOwner(id)!=p||(room->getCardPlace(id)!=Player::PlaceHand&&room->getCardPlace(id)!=Player::PlaceEquip&&!p->getHandPile().contains(id)))return false;
    const Card *c=Sanguosha->getCard(id);return c&&int(c->getSuit())==suit&&c->getNumber()==number;
}
void projectAIRound26OriginalContext(AIRequest &r,Room &room,ServerPlayer *p,const QVariant &,const std::function<QJsonObject(const Card *)> &card) {
    if(!admitted(r,p))return;
    QJsonObject row,tags,properties;
    const QString reason=r.choiceOptions.reason;
    if(r.kind==AIRequest::SkillInvoke&&reason=="heg_fengshix") {
        const QVariant tag=p->getTag("FengshixUsedata");
        if(tag.canConvert<CardUseStruct>()) {const CardUseStruct use=tag.value<CardUseStruct>();QJsonArray to;for(const auto target:use.to)if(target)to.append(target->objectName());QJsonObject value{{"to",to}};if(use.card)value.insert("card",card(use.card));if(use.from)value.insert("from",use.from->objectName());tags.insert("FengshixUsedata",value);}
        else if(!tag.isValid())tags.insert("FengshixUsedata",false);
    }
    // Fixed source old tags absent in native catalog; restored values remain private/unknown.
    const QHash<QString,QString> absent{{"heg_jiancai","JiancaiDamagedata"},{"heg_jujian","jujianDyingdata"},{"naman","NamanUsedata"},{"shefu","ShefuUsedata"},{"#guowu_effect","GuowuUsedata"}};
    if((r.kind==AIRequest::SkillInvoke||r.kind==AIRequest::Choice||r.kind==AIRequest::PlayerChosen||r.kind==AIRequest::Discard)&&absent.contains(reason)) {const QString name=absent.value(reason);if(!p->getTag(name).isValid())tags.insert(name,false);}
    if(r.kind==AIRequest::Choice&&reason=="jinghe_skill"&&room.getCurrent()&&!room.getCurrent()->getTag("JingheTargets").isValid())tags.insert("JingheTargets",false);
    const QHash<QString,QString> ownProperties{{"@@heg_tongling_usecard","tongling_usetarget"},{"@@heg_zhenxi_trick","zhenxi_target"},{"@@heg_jiansu","jiansu_record"},{"@@heg_consolidatecountrygive","consolidate_country_cards"}};
    if(r.kind==AIRequest::UseCard&&ownProperties.contains(r.pattern)){const QString name=ownProperties.value(r.pattern);properties.insert(name,p->property(name.toUtf8().constData()).toString());}
    if(!tags.isEmpty())row.insert("tags",tags);if(!properties.isEmpty())row.insert("properties",properties);
    if(!row.isEmpty()){QJsonObject context=r.choiceOptions.context;context.insert("original26",row);r.choiceOptions.context=context;}
}
void projectAIRound26OriginalWorld(AIWorldView &w,Room &room,ServerPlayer *p) {
    if(!p||w.self.objectName!=p->objectName()||!w.enableHegemony)return;
    QJsonObject row,definitions,publicLords;
    for(const QString &kingdom : {QStringLiteral("wei"),QStringLiteral("shu"),QStringLiteral("wu"),QStringLiteral("qun")}) {
        const ServerPlayer *lord=room.getLord(kingdom);
        publicLords.insert(kingdom,lord?QJsonValue(lord->objectName()):QJsonValue(false));
    }
    row.insert("public_lords",publicLords);
    const QStringList names=p->getGeneralPile("massacre");
    for(const QString &name:names)if(const General*g=Sanguosha->getGeneral(name)) {
        QJsonArray skills;for(const Skill*s:g->getVisibleSkillList())skills.append(QJsonObject{{"name",s->objectName()},{"frequency",int(s->getFrequency())},{"limit_mark",s->getLimitMark()}});
        definitions.insert(name,QJsonObject{{"name",name},{"kingdom",g->getKingdom()},{"double_kingdoms",g->isDoubleKingdoms()},{"kingdoms",g->getKingdoms()},{"skills",skills}});
    }
    row.insert("massacre",QJsonArray::fromStringList(names));row.insert("general_definitions",definitions);
    row.insert("wenji_record",p->property("wenji_record").toString());
    w.strategyFacts.insert("original26",row);
}
