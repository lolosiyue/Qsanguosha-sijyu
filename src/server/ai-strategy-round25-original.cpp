#include "ai-strategy-round25-original.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "engine.h"
#include "general.h"
#include "skill.h"
#include "settings.h"
#include <QJsonArray>
#include <QHash>
#include <QSet>

int aiRound25OriginalPrototype(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QString &prototype)
{
    if(!p || !p->getRoom() || r.viewerObjectName!=p->objectName()
        || r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision() || !p->hasSkill(skill))return -1;
    if(!r.worldView.enableHegemony && r.kind==AIRequest::UseCard
        &&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern=="@@heg_heyi"
        &&skill=="heg_heyi"&&prototype=="ActiveSkillCard")return 0;
    if(r.worldView.enableHegemony && r.kind==AIRequest::UseCard && r.reason!=CardUseStruct::CARD_USE_REASON_PLAY) {
        static const QSet<QString> responseSkills{"heg_zhengbi","heg_jieyue","heg_jieyue_egf","heg_keshou"};
        if(responseSkills.contains(skill)&&r.pattern=="@@"+skill&&prototype=="ActiveSkillCard")return 1;
        if(skill=="heg_zhengbigive"&&r.pattern=="@@heg_zhengbigive!"&&prototype=="ActiveSkillCard")return 1;
    }
    if(!r.worldView.enableHegemony || r.kind!=AIRequest::Activate || r.reason!=CardUseStruct::CARD_USE_REASON_PLAY)return -1;
    // Value is a source constructor capability mask: bit0 costs, bit1 declaration.
    // Native skill-instance canActivate/canSelect/create/targets still authorize every action.
    static const QHash<QString,QPair<QString,int>> rows{
        {"heg_boyan",{"HBoyanCard",0}},{"heg_boyanzongheng",{"HBoyanZonghengCard",0}},
        {"heg_weimeng",{"HWeimengCard",0}},{"heg_weimengzongheng",{"HWeimengZonghengCard",0}},
        {"heg_fenglve",{"HFenglveCard",0}},{"heg_fenglvezongheng",{"HFenglveZonghengCard",0}},
        {"heg_mingfa",{"HMingfaCard",0}},{"heg_mingfazongheng",{"HMingfaZonghengCard",1}},
        {"heg_yigui",{"HYiguiCard",2}},{"heg_qice",{"HQiceCard",3}},{"heg_xiongsuan",{"HXiongsuanCard",1}},
        {"heg_sanyao",{"HSanyaoCard",1}},{"heg_yongjin",{"HYongjinCard",0}},
        {"heg_lianzi",{"HLianziCard",1}},{"heg_flamemap",{"HFlameMapCard",1}},
        {"heg_LuminousPearl",{"HZhihengLPCard",1}},
        {"heg_fengying",{"HFengyingCard",0}},{"heg_jianglve",{"HJianglveCard",0}},
        {"heg_xuanhuoattach",{"HXuanhuoAttachCard",1}},{"heg_ganlu",{"HGanluCard",0}},
        {"heg_weidi",{"HWeidiCard",0}},{"heg_huibian",{"HHuibianCard",0}}
    };
    const auto it=rows.constFind(skill);return it!=rows.constEnd()&&it.value().first==prototype?it.value().second:-1;
}
static QJsonObject damageRow(const DamageStruct &d,const std::function<QJsonObject(const Card *)>&card)
{
    QJsonObject v{{"damage",d.damage},{"nature",int(d.nature)},{"chain",d.chain},{"transfer",d.transfer},{"by_user",d.by_user},{"reason",d.reason}};
    if(d.from)v.insert("from",d.from->objectName());if(d.to)v.insert("to",d.to->objectName());if(d.card)v.insert("card",card(d.card));return v;
}
void projectAIRound25OriginalContext(AIRequest &r,Room &room,ServerPlayer *p,const QVariant &,
 const std::function<QJsonObject(const Card *)>&card)
{
    if(!p||r.viewerObjectName!=p->objectName()||r.stateRevision!=room.roomRuntime()->stateRevision())return;
    QJsonObject row,tags;const QString reason=r.choiceOptions.reason;
    if(r.kind==AIRequest::SkillInvoke&&reason=="heg_zhiman") {
        // h-transformation.cpp1673: cost scopes this exact owner's current public DamageStruct.
        const QVariant tag=p->getTag("zhiman_data");
        tags.insert("zhiman_data",tag.canConvert<DamageStruct>()?QJsonValue(damageRow(tag.value<DamageStruct>(),card)):QJsonValue(false));
    }
    // These old owner tags have no writer in the fixed native/Lua catalog.
    // Expose only actual absence, never import an arbitrary restored private payload.
    static const QHash<QString,QString> absentTags{{"heg_zhente","ZhenteUsedata"},{"heg_qiao","QiaoUsedata"},{"heg_shejian","ShejianUsedata"},{"heg_anyong","AnyongDamagedata"}};
    if(r.kind==AIRequest::SkillInvoke&&absentTags.contains(reason)) {
        const QString key=absentTags.value(reason);if(!p->getTag(key).isValid())tags.insert(key,false);
    }
    if(!tags.isEmpty())row.insert("tags",tags);
    if(!row.isEmpty()){QJsonObject c=r.choiceOptions.context;c.insert("original25",row);r.choiceOptions.context=c;}
}
void projectAIRound25OriginalWorld(AIWorldView &w,Room &room,ServerPlayer *viewer)
{
    if(!viewer||w.self.objectName!=viewer->objectName()||!w.enableHegemony)return;
    QJsonObject players,definitions;
    const auto describe=[&](const General *g){if(!g||definitions.contains(g->objectName()))return;QJsonArray skills;
        for(const Skill*s:g->getVisibleSkillList())skills.append(QJsonObject{{"name",s->objectName()},{"frequency",int(s->getFrequency())},{"limit_mark",s->getLimitMark()}});
        definitions.insert(g->objectName(),QJsonObject{{"name",g->objectName()},{"kingdom",g->getKingdom()},{"double_kingdoms",g->isDoubleKingdoms()},{"kingdoms",g->getKingdoms()},{"skills",skills}});};
    describe(Sanguosha->getGeneral("anjiang"));
    for(const ServerPlayer *p:room.getAllPlayers(true)) {
        QJsonObject row,skillSlots;
        const General *head=p==viewer?p->getActualGeneral1():(p->hasShownGeneral()?p->getGeneral():nullptr);
        const General *deputy=p==viewer?p->getActualGeneral2():(p->hasShownGeneral2()?p->getGeneral2():nullptr);
        // Exact originalHegemonyGeneral fallback: only this viewer's own KnownBoth knowledge.
        if(p!=viewer&&(!head||!deputy)) {
            const QStringList known=viewer->getTag("KnownBoth_"+p->objectName()).toString().split('+');
            if(!head&&!known.value(0).isEmpty())head=Sanguosha->getGeneral(known.value(0));
            if(!deputy&&!known.value(1).isEmpty())deputy=Sanguosha->getGeneral(known.value(1));
        }
        if(!head)head=Sanguosha->getGeneral("anjiang");if(!deputy)deputy=Sanguosha->getGeneral("anjiang");
        // Printed public faces are distinct from the owner's lawful concealed actual identity.
        const General *publicHead=(p==viewer||p->hasShownGeneral())?p->getGeneral():nullptr;
        const General *publicDeputy=(p==viewer||p->hasShownGeneral2())?p->getGeneral2():nullptr;
        row.insert("public_head",publicHead?publicHead->objectName():QStringLiteral("anjiang"));
        row.insert("public_deputy",publicDeputy?publicDeputy->objectName():QStringLiteral("anjiang"));
        if(publicHead)describe(publicHead);if(publicDeputy)describe(publicDeputy);
        if(head){describe(head);row.insert("head",head->objectName());}
        if(deputy){describe(deputy);row.insert("deputy",deputy->objectName());}
        for(const Skill*s:p->getVisibleSkillList())if(p==viewer||p->hasShownSkill(s->objectName()))
            skillSlots.insert(s->objectName(),QJsonObject{{"head",p->inHeadSkills(s->objectName())},{"deputy",p->inDeputySkills(s->objectName())}});
        row.insert("slots",skillSlots);
        if(p==viewer){row.insert("can_show",QJsonObject{{"h",p->canShowGeneral("h")},{"d",p->canShowGeneral("d")}});row.insert("duanchang",QJsonArray::fromStringList(p->property("Duanchang").toStringList()));}
        players.insert(p->objectName(),row);
    }
    w.strategyFacts.insert("original25",QJsonObject{{"enable_lord_conversion",Config.value("EnableLordConvertion",true).toBool()},{"players",players},{"general_definitions",definitions},{"owner_attack_range_correction",Sanguosha->correctAttackRange(viewer,true,false)}});
}
bool aiRound25OriginalConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
    if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()||!r.worldView.enableHegemony
        ||r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision()||r.kind!=AIRequest::Activate
        ||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!s.value("subcards").isArray())return false;
    const QString key=s.value("source_key").toString(),name=s.value("name").toString();
    const auto costs=s.value("subcards").toArray();
    if(key=="heg_family_printed") {
        static const QSet<QString> names{"alliance_feast","burning_camps","god_salvation","savage_assault","archery_attack","befriend_attacking","ex_nihilo","duel","snatch","dismantlement","drowning","iron_chain","await_exhausted","amazing_grace","fight_together","lure_tiger","imperial_order","collateral","known_both","fire_attack","heg_threaten_emperor"};
        const int suit=s.value("suit").toInt(-1),number=s.value("number").toInt(-1);
        // HQice's source use_func clones the declaration with the source wrapper face,
        // and no subcards. This readonly constructor grants no payable object.
        return p->hasSkill("heg_qice")&&names.contains(name)&&costs.isEmpty()
            &&s.value("skill")==QStringLiteral("")&&s.value("suit")==suit&&s.value("number")==number
            &&suit>=int(Card::Spade)&&suit<=int(Card::NoSuit)&&number>=0&&number<=15;
    }
    static const QHash<QString,QString> names{{"heg_duoshi_flamemap","await_exhausted"},{"heg_wusheng_xh","slash"},{"heg_longdan_xh","slash"},{"heg_duanliang_egf","supply_shortage"}};
    if(!names.contains(key)||names.value(key)!=name||s.value("skill")!=key||!p->hasSkill(key)||costs.size()!=1)return false;
    const int id=costs.first().toInt(-1);if(id<0||costs.first()!=id)return false;
    const Room &room=*p->getRoom();
    if(room.getCardOwner(id)!=p || (room.getCardPlace(id)!=Player::PlaceHand&&room.getCardPlace(id)!=Player::PlaceEquip&&!p->getHandPile().contains(id)))return false;
    bool disclosed=false;for(const AICardView &c:r.worldView.handCards)if(c.effectiveId==id)disclosed=true;
    for(const AICardView &c:r.worldView.self.equips)if(c.effectiveId==id)disclosed=true;
    for(const AICardPileView &pile:r.worldView.self.piles)for(const AICardView &c:pile.cards)if(c.effectiveId==id)disclosed=true;
    const Card *c=disclosed?Sanguosha->getCard(id):nullptr;
    return c&&s.value("suit")==int(c->getSuit())&&s.value("number")==c->getNumber();
}
