#include "ai-strategy/original-hegemony.h"

// ---- original-hegemony-choice ----
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QDebug>
#include <QJsonArray>
#include <QSet>

bool aiOriginalHegemonyQuestion(const AIRequest &r)
{
    const auto h=r.worldView.strategyFacts.value("hegemony").toObject();
    return r.decisionId!=0 && !r.viewerObjectName.isEmpty()
        && r.worldView.self.objectName==r.viewerObjectName
        && h.value("enabled")==QJsonValue(true)
        && h.value("original_overrides_loaded")==QJsonValue(true);
}

bool aiOriginalGuanxingConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
    if(!aiOriginalHegemonyQuestion(r) || !p || !p->getRoom()
        || p->objectName()!=r.viewerObjectName
        || r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision()
        || r.kind!=AIRequest::Guanxing || s.size()!=6
        || s.value("source_key")!=QJsonValue(QStringLiteral("original_guanxing"))
        || s.value("skill")!=QJsonValue(QString()) || !s.value("subcards").isArray()
        || !s.value("subcards").toArray().isEmpty())return false;
    const QString name=s.value("name").toString();
    // Source715 makes a fresh default clone even though its local is unused.
    if(name=="slash" && s.value("suit")==QJsonValue(int(Card::SuitToBeDecided))
        && s.value("number")==QJsonValue(-1))return true;
    static const QSet<QString> names{"snatch","dismantlement","indulgence","supply_shortage",
        "collateral","duel","drowning","archery_attack","savage_assault","fire_attack",
        "god_salvation","lightning"};
    if(!names.contains(name))return false;
    // A zero-material class dummy takes its printed suit/number from one
    // already-disclosed offered face. This is not ownership or payment.
    for(const auto &c:r.choiceOptions.cards)
        if(r.choiceOptions.cardIds.contains(c.effectiveId)
            && s.value("suit")==QJsonValue(c.suit)
            && s.value("number")==QJsonValue(c.number))return true;
    return false;
}

bool emitAIOriginalSourceWarning(Room &room,const AIRequest &r,const ServerPlayer *p,
    const QString &source,const QString &message)
{
    if(!aiOriginalHegemonyQuestion(r)||!p||p->getRoom()!=&room
        ||p->objectName()!=r.viewerObjectName||r.stateRevision!=room.roomRuntime()->stateRevision()
        ||message.size()>16384)return false;
    const bool pindian=source=="Original askForPindian"&&r.kind==AIRequest::RespondCard
        &&r.choiceOptions.question=="askForPindian"
        &&message==QStringLiteral("Pindian card for %1 not found!!").arg(r.choiceOptions.reason);
    const bool guanxing=source=="Original Guanxing"&&r.kind==AIRequest::Guanxing
        &&message.contains(QStringLiteral("stack traceback:"));
    const bool round=source=="Original playerGetRound"
        &&message.contains(QStringLiteral("stack traceback:"));
    const bool dispatch=source=="Original useCardByClassName"
        &&message==QStringLiteral("stack traceback:\n\tOriginal useCardByClassName: nil card");
    if(!pindian&&!guanxing&&!round&&!dispatch)return false;
    // Exact SWIG Room.writeToConsole sink; prefix remains even if policy errors.
    room.output(message);
    qWarning("%s",qPrintable(message));
    return true;
}

// ---- original-hegemony-formation ----
#include "hegemony-mode.h"
namespace {
struct Relation{bool known;bool value;};
Relation publicFriend(const ServerPlayer*a,const ServerPlayer*b,const ServerPlayer*viewer)
{
 if(a==b)return{true,true};
 // The recruitment exception requires both roles already publicly shown.
 const bool publicRoles=a->hasShownRole()&&b->hasShownRole();
 if(publicRoles){const auto ar=a->getRole(),br=b->getRole();
  if(ar.startsWith("careerist_")||br.startsWith("careerist_"))return{true,ar==br};}
 // With considerAnjiang=false this public short circuit is exact even when
 // neither hidden role can be inspected. No hidden negative-role predicate.
 if(!a->hasShownOneGeneral()||!b->hasShownOneGeneral())return{true,false};
 if((a!=viewer&&!a->hasShownRole())||(b!=viewer&&!b->hasShownRole()))return{false,false};
 const auto ar=a->getRole(),br=b->getRole();
 if(ar=="careerist"||br=="careerist")return{true,false};
 if(ar.startsWith("careerist_")||br.startsWith("careerist_"))return{true,ar==br};
 // Both generals have a public shown slot at this point, so kingdom is public.
 return{true,a->getKingdom()==b->getKingdom()};
}
bool completeRing(const QList<ServerPlayer*>&roster)
{
 if(roster.isEmpty())return false;QSet<const ServerPlayer*> seen;QSet<QString> names;
 auto*p=roster.first();for(int i=0;i<roster.size();++i){
  if(!p||!roster.contains(p)||seen.contains(p)||p->objectName().isEmpty()||names.contains(p->objectName()))return false;
  seen.insert(p);names.insert(p->objectName());p=p->getNext();}
 return p==roster.first();
}
QJsonObject unknown(const char*reason){return{{"known",false},{"reason",QString::fromLatin1(reason)}};}
}
void projectAIOriginalFormation(AIWorldView&world,Room&room,const ServerPlayer*viewer)
{
 if(!viewer||viewer->getRoom()!=&room||world.self.objectName!=viewer->objectName()||!HegemonyMode::enabled())return;
 const auto h=world.strategyFacts.value("hegemony").toObject();
 if(h.value("original_overrides_loaded")!=QJsonValue(true))return;
 const auto roster=room.getAllPlayers(true);if(!roster.contains(const_cast<ServerPlayer*>(viewer)))return;
 QJsonObject rows;const bool ring=completeRing(roster);
 for(const auto*from:roster){
  if(!ring){rows.insert(from->objectName(),unknown("public seating ring is incomplete"));continue;}
  QJsonArray names{from->objectName()};int n=from->aliveCount(false),num=n;bool known=true;
  // Deliberately identical loop bounds/order to Player.cpp4046. Native public
  // seat traversal preserves removed/dead/self and wrap-around semantics.
  for(int i=1;i<n;++i){const auto*to=from->getNextAlive(i);auto rel=publicFriend(from,to,viewer);
   if(!rel.known){known=false;break;}if(rel.value)names.append(to->objectName());else{num=i;break;}}
  n-=num;
  if(known)for(int i=1;i<n;++i){const auto*to=from->getLastAlive(i);auto rel=publicFriend(from,to,viewer);
   if(!rel.known){known=false;break;}if(rel.value)names.append(to->objectName());else break;}
  // getLastAlive(n) currently repeats getPreviousAlive(1) from the original
  // receiver: duplicates for n>1 are source behavior, not repaired/deduplicated.
  rows.insert(from->objectName(),known?QJsonObject{{"known",true},{"names",names}}:unknown("reached formation relation requires an undisclosed role"));
 }
 world.strategyFacts.insert("original_public_formation",QJsonObject{{"complete",true},{"rows",rows}});
}

// ---- original-hegemony-original-2 ----
#include "engine.h"
#include <cmath>
#include <limits>

bool aiMiewuOriginalConstruction(const AIRequest &request,const ServerPlayer *viewer,const QJsonObject &spec)
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


void projectAIFormationOriginalContext(AIRequest &request,Room &room,ServerPlayer *viewer,const QVariant &incoming,
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

int aiFormationOriginalPrototype(const AIRequest &request,const ServerPlayer *viewer,
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

// ---- original-hegemony-original-3 ----
#include "general.h"
#include "skill.h"
#include "settings.h"
#include <QHash>

int aiTransformationOriginalPrototype(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QString &prototype)
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
void projectAITransformationOriginalContext(AIRequest &r,Room &room,ServerPlayer *p,const QVariant &,
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
void projectAIGeneralDefinitionsOriginalWorld(AIWorldView &w,Room &room,ServerPlayer *viewer)
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
bool aiTransformationOriginalConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
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

// ---- original-hegemony-original-4 ----
static bool admitted(const AIRequest &r,const ServerPlayer *p) {
    return p&&p->getRoom()&&r.viewerObjectName==p->objectName()&&r.worldView.enableHegemony
        &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();
}
int aiLordExOriginalPrototype(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QString &prototype) {
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
bool aiLordExOriginalConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s) {
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
void projectAILordExOriginalContext(AIRequest &r,Room &room,ServerPlayer *p,const QVariant &,const std::function<QJsonObject(const Card *)> &card) {
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
void projectAILordExOriginalWorld(AIWorldView &w,Room &room,ServerPlayer *p) {
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

// ---- original-hegemony-original-5 ----
#include <memory>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>

namespace {
class OriginalViews final : public QObject {
public:
    explicit OriginalViews(Room &room) : QObject(&room) { setObjectName("isolated_original27_views"); }
    QMutex mutex;
    QHash<QString,QHash<QString,QSet<int>>> observed;
};
OriginalViews &views(Room &room)
{
    static QMutex creation;
    QMutexLocker lock(&creation);
    auto *child=room.findChild<QObject *>("isolated_original27_views",Qt::FindDirectChildrenOnly);
    if(auto *state=dynamic_cast<OriginalViews *>(child))return *state;
    return *new OriginalViews(room);
}
void projectObserved(AIWorldView &world,Room &room,ServerPlayer *viewer)
{
    auto &state=views(room);QMutexLocker lock(&state.mutex);
    auto visible=world.strategyFacts.value("source_visible_hand_ids").toObject();
    auto counts=world.strategyFacts.value("original_known_both_visible_counts").toObject();
    const auto rows=state.observed.value(viewer->objectName());
    for(const auto &p:world.players){
        const auto *live=room.findPlayerByObjectName(p.objectName,true);if(!live)continue;
        const auto ids=rows.value(p.objectName);if(ids.isEmpty())continue;
        QSet<int> lawful;for(const auto &c:p.knownCards)lawful.insert(c.effectiveId);
        if(p.objectName==viewer->objectName())for(const auto &c:world.handCards)lawful.insert(c.effectiveId);
        QSet<int> prior;for(const auto &v:visible.value(p.objectName).toArray())prior.insert(v.toInt(-1));
        QJsonArray ordered;int added=0;
        for(int id:live->handCards()){
            const bool observed=ids.contains(id)&&lawful.contains(id);
            if(prior.contains(id)||observed)ordered.append(id);
            if(observed&&!prior.contains(id))++added;
        }
        visible.insert(p.objectName,ordered);
        if(counts.contains(p.objectName))counts.insert(p.objectName,counts.value(p.objectName).toInt()+added);
    }
    world.strategyFacts.insert("source_visible_hand_ids",visible);
    world.strategyFacts.insert("original_known_both_visible_counts",counts);
}
}


int aiBasaraOriginalPrototype(const AIRequest &r, const ServerPlayer *p,
                              const QString &skill, const QString &prototype)
{
    if (!p || !p->getRoom() || !r.worldView.enableHegemony
        || r.viewerObjectName != p->objectName()
        || r.stateRevision != p->getRoom()->roomRuntime()->stateRevision()
        || r.kind != AIRequest::Activate || r.reason != CardUseStruct::CARD_USE_REASON_PLAY
        || prototype != QStringLiteral("ActiveSkillCard") || !p->hasSkill(skill)) return -1;
    // heg-basara-ai.lua:24 creates the actual generic V2 card. HFirstShowCard
    // and HCareermanCard are policy/history keys, never invented prototypes.
    return skill == QStringLiteral("heg_firstshow") || skill == QStringLiteral("heg_careerman") ? 0 : -1;
}

void projectAIBasaraOriginalWorld(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom()!=&room || room.roomRuntime()->isClosing() || world.self.objectName != viewer->objectName() || !world.enableHegemony) return;
    projectObserved(world,room,viewer);
    // TurnStart reads the owner's current raw general objects (not another
    // observer's actual hidden general). No foreign-general definitions leak.
    const auto gender = [](const General *g) -> QJsonValue {
        if (!g) return false;
        return QJsonObject{{"name", g->objectName()}, {"male", g->isMale()}, {"female", g->isFemale()}};
    };
    QJsonObject prototypes;
    for (const QString &name : {QStringLiteral("HFanjianCard"), QStringLiteral("HFangquanCard"),
                               QStringLiteral("HTianxiangCard"), QStringLiteral("HWoodenOxCard")}) {
        std::unique_ptr<SkillCard> prototype(Sanguosha->cloneSkillCard(name));
        prototypes.insert(name, bool(prototype));
    }
    world.strategyFacts.insert(QStringLiteral("original27"), QJsonObject{
        {"legacy_prototypes", prototypes},
        {"owner_raw_head", gender(viewer->getGeneral())},
        {"owner_raw_deputy", gender(viewer->getGeneral2())},
        {"owner_actual_head", gender(viewer->getActualGeneral1()?viewer->getActualGeneral1():Sanguosha->getGeneral("anjiang"))},
        {"owner_actual_deputy", gender(viewer->getActualGeneral2()?viewer->getActualGeneral2():Sanguosha->getGeneral("anjiang"))}
    });
}

void recordAIViewCardsOriginalEvent(const AIWorldView &world, Room &room, ServerPlayer *viewer,
    ServerPlayer *eventPlayer, const AIEventView &event, const QVariant &data)
{
    if(!viewer||viewer->getRoom()!=&room||room.roomRuntime()->isClosing()
        ||!world.enableHegemony||world.self.objectName!=viewer->objectName())return;
    auto &state=views(room);QMutexLocker lock(&state.mutex);
    auto &owned=state.observed[viewer->objectName()];
    if(event.triggerEvent==GameStart){owned.clear();return;}
    if(event.triggerEvent==CardsMoveOneTime&&data.canConvert<CardsMoveOneTimeStruct>()){
        const auto move=data.value<CardsMoveOneTimeStruct>();
        // Same source clear point as filterEvent: hand cards leaving while
        // the event player is current clear every viewer/target visibility key.
        if(eventPlayer==room.getCurrent())for(int i=0;i<move.card_ids.size();++i)
            if(move.from_places.value(i,Player::PlaceUnknown)==Player::PlaceHand)
                for(auto it=owned.begin();it!=owned.end();++it)it->remove(move.card_ids.at(i));
        return;
    }
    if(event.triggerEvent!=ChoiceMade||eventPlayer!=viewer||event.from!=viewer->objectName()
        ||!event.privateEvent||event.privateViewer!=viewer->objectName()
        ||event.details.value("choice_kind")!="viewCards"||data.userType()!=QMetaType::QString)return;
    const auto prompt=data.toString().split(':');
    if(prompt.size()!=3||prompt.first()!="viewCards")return;
    const auto *target=room.findPlayerByObjectName(prompt.at(1),true);if(!target)return;
    // showCard also emits viewCards for a subset! Only the actual recipient
    // payload is evidence; donor's whole-hand scan is not a disclosure grant.
    for(const auto &text:prompt.at(2).split('+',Qt::SkipEmptyParts)){
        bool ok=false;const int id=text.toInt(&ok);
        if(ok&&id>=0&&target->handCards().contains(id))owned[target->objectName()].insert(id);
    }
}

void clearAIViewCardsOriginalViews(Room &room)
{
    auto *child=room.findChild<QObject *>("isolated_original27_views",Qt::FindDirectChildrenOnly);
    if(auto *state=dynamic_cast<OriginalViews *>(child)){QMutexLocker lock(&state->mutex);state->observed.clear();}
}

// ---- original-hegemony-original-6 ----
#include "lua-wrapper.h"
#include "lua.hpp"
void projectAILegacyPrototypeExtraWorld(AIWorldView &world,Room &room,ServerPlayer *viewer) {
    if(!viewer||viewer->getRoom()!=&room||room.roomRuntime()->isClosing()||world.self.objectName!=viewer->objectName())return;
    QJsonObject globals;
    // Existence only, never execute the legacy policy/helper. Fixed temp-only
    // providers must not be invented when the runtime did not load them.
    {
        LuaRuntime::Binding binding(room.roomRuntime()->lua(),false);
        lua_State *L=room.roomRuntime()->lua().state();
        for(const char *name:{"GetQueueMembers","IsBigKingdomPlayer"}) {
            lua_getglobal(L,name);globals.insert(QString::fromLatin1(name),!lua_isnil(L,-1));lua_pop(L,1);
        }
    }
    QJsonObject prototypes;
    // Finite literal Card_Parse source spellings. Existence is public engine
    // metadata, not authority: missing Lua prototypes stay missing and are never
    // aliased to modern native H* products. Every temporary clone is destroyed.
    for (const char *name : {
        "#bf_nos_tiaodu",
        "#bf_qice",
        "#bf_xiongsuan",
        "#bf_yongjin",
        "#gd_zezhu",
        "#guandu_shicai",
        "#guandu_yuanlue",
        "#heg_chengliu",
        "#heg_duanxie",
        "#heg_duwu",
        "#heg_fk_bushi",
        "#heg_guikuang",
        "#heg_huaiyi",
        "#heg_huyuan",
        "#heg_jianglue",
        "#heg_jiantong",
        "#heg_jiaping_Attach",
        "#heg_jinfa",
        "#heg_jinghe",
        "#heg_keshou",
        "#heg_kurou",
        "#heg_lord_huibian",
        "#heg_lord_jiahe_Attach",
        "#heg_lord_lianzi",
        "#heg_lord_wendao",
        "#heg_mobile_paiyi",
        "#heg_mouduan",
        "#heg_nos_huaiyi",
        "#heg_ov_hongyuan",
        "#heg_ov_jiansu",
        "#heg_paiyi",
        "#heg_pojing",
        "#heg_qiangxi",
        "#heg_qingyin",
        "#heg_quanjin",
        "#heg_ruilve",
        "#heg_sanchen",
        "#heg_shensu",
        "#heg_shunfu",
        "#heg_six_swords",
        "#heg_tenyear_boyan",
        "#heg_tenyear_fenglue",
        "#heg_tenyear_mingfa",
        "#heg_tenyear_weimeng",
        "#heg_tianxiang",
        "#heg_transfer",
        "#heg_weidi",
        "#heg_xianqu",
        "#heg_xiejian",
        "#heg_xuanhuo",
        "#heg_yanxi",
        "#heg_yinyangyu",
        "#heg_zaoyun",
        "#heg_zhengbi",
        "#heg_zhuangrong",
        "#twyj_huzhu",
        "#twyj_qijia",
        "#twyj_rangyi",
        "#twyj_zhuchen"
    }) {
        const QString key=QString::fromLatin1(name);
        std::unique_ptr<LuaSkillCard> card(LuaSkillCard::Parse(key+":.:"));
        prototypes.insert(key,bool(card));
    }
    for (const char *name : {"ShensuCard","TenyearRendeCard","ZhihengCard"}) {
        std::unique_ptr<Card> card(Sanguosha->cloneSkillCard(QString::fromLatin1(name)));
        prototypes.insert(QString::fromLatin1(name),bool(card));
    }
    QJsonObject cloneNames;
    {
        // The literal source constructor key must resolve on its own. A modern
        // HThreatenEmperor template is not a replacement for a missing Lua card.
        std::unique_ptr<Card> card(Sanguosha->cloneCard("heg_threaten_emperor",Card::NoSuit,0));
        QJsonObject row{{"present",bool(card)}};
        if(card){row.insert("name",card->objectName());row.insert("class_name",card->getClassName());
            row.insert("suit",int(card->getSuit()));row.insert("number",card->getNumber());}
        cloneNames.insert("heg_threaten_emperor",row);
    }
    auto facts=world.strategyFacts.value("extra28").toObject();
    facts.insert("source_globals",globals);facts.insert("legacy_prototypes",prototypes);facts.insert("clone_names",cloneNames);
    world.strategyFacts.insert("extra28",facts);
}

// ---- original-hegemony-original-7 ----
void projectAIOriginalRetrialCandidates(const AIRequest &request, Room &room,
    ServerPlayer *viewer, const JudgeStruct *judge, QJsonObject &result,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || !judge || !judge->who || viewer->getRoom()!=&room
        || request.viewerObjectName!=viewer->objectName()
        || request.worldView.self.objectName!=viewer->objectName()
        || room.roomRuntime()->isClosing()) return;
    const auto facts=request.worldView.strategyFacts.value("hegemony").toObject();
    if (!facts.value("enabled").toBool() || !facts.value("original_overrides_loaded").toBool()) return;
    const auto players=facts.value("players").toObject();
    const auto who=players.value(judge->who->objectName()).toObject();
    if (!who.value("shown_skills").isObject()) return;
    const bool hongyan=who.value("shown_skills").toObject().contains("hongyan");
    // Match the existing judgment projector's privacy boundary. Zone/private
    // predicates for a foreign judged player are not public face predicates.
    bool publicPattern=!judge->pattern.contains('$');
    for (const auto &branch:judge->pattern.split('#')) {
        const auto factors=branch.split('|');
        if (factors.size()>=4 && factors.at(3)!=".") publicPattern=false;
    }
    QList<AICardView> disclosed=request.worldView.handCards;
    disclosed.append(request.worldView.self.equips);
    for (const auto &pile:request.worldView.self.piles)
        if (pile.handPile) disclosed.append(pile.cards);
    disclosed.append(request.choiceOptions.cards);
    QJsonObject rows; QSet<int> seen;
    for (const auto &view:disclosed) {
        const int id=view.effectiveId;
        if (id<0 || seen.contains(id) || view.virtualCard) continue;
        seen.insert(id);
        const Card *current=room.getCard(id);
        if (!current || current->isVirtualCard() || current->getEffectiveId()!=id
            || current->objectName()!=view.objectName || current->getClassName()!=view.className
            || int(current->getSuit())!=view.suit || current->getNumber()!=view.number) continue;
        const Card *candidate=Sanguosha->getEngineCard(id);
        if (!candidate) continue;
        std::unique_ptr<Card> clone;
        if (hongyan && candidate->getSuit()==Card::Spade) {
            clone.reset(Sanguosha->cloneCard(candidate->objectName(),Card::Heart,current->getNumber()));
            if (!clone) continue;
            candidate=clone.get();
        }
        auto face=publicCard(candidate);
        face.insert("evaluation_only",true);
        // No copied live private flags or authority. The printed/fresh face is
        // for source classification only; selection still returns original id.
        face.insert("flags_complete",false);
        face.insert("skill_names_complete",false);
        QJsonObject row{{"card",face},{"hongyan",hongyan && bool(clone)}};
        if (judge->who==viewer || publicPattern) row.insert("good",judge->isGood(candidate));
        rows.insert(QString::number(id),row);
    }
    result.insert("original_retrial_candidates",rows);
}

// ---- original-hegemony-original ----
namespace {
bool originalHegemonyOriginalAdmitted(const AIWorldView &world, const Room &room, const ServerPlayer *viewer)
{
    return viewer && viewer->getRoom() == &room && world.self.objectName == viewer->objectName()
        && HegemonyMode::enabled();
}
}
void projectAIStrategyKnownBothOriginalWorld(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!originalHegemonyOriginalAdmitted(world, room, viewer)) return;
    QJsonObject marks, visibleCounts;
    for (const auto *target : room.getAllPlayers(true)) {
        // Only this observer's AI "already viewed" marker. Never a target's
        // actual hidden general, another viewer's tag, or their personal marks.
        const QString key = "KnownBoth_" + viewer->objectName() + "_" + target->objectName();
        marks.insert(target->objectName(), target->getMark(key));
        QList<int> ids = target->handCards();
        ids.append(target->getPile("wooden_ox"));
        int visible = 0;
        const QString flag = "visible_" + viewer->objectName() + "_" + target->objectName();
        for (int id : ids) {
            const Card *card = room.getCard(id);
            if (card && (target == viewer || card->hasFlag("visible") || card->hasFlag(flag))) ++visible;
        }
        // Original common3037/4749 counts only hand+WoodenOx cards already
        // visible to this observer. Neither hidden IDs nor faces are exported.
        visibleCounts.insert(target->objectName(), visible);
    }
    world.strategyFacts.insert("original_known_both_marks", marks);
    world.strategyFacts.insert("original_known_both_visible_counts", visibleCounts);
}
void projectAIStrategyKnownBothOriginalRequest(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context)
{
    if (!originalHegemonyOriginalAdmitted(request.worldView, room, viewer) || request.viewerObjectName != viewer->objectName()
        || request.kind != AIRequest::Choice || request.choiceOptions.reason != "known_both") return;
    if (!data.canConvert<ServerPlayer *>()) return;
    const auto *target = data.value<ServerPlayer *>();
    if (target && target->getRoom() != &room) return;
    context.insert("original_known_both_target", target ? QJsonValue(target->objectName()) : QJsonValue(false));
}
void projectAIStrategyKnownBothOriginalEvent(AIWorldView &world, Room &room, ServerPlayer *viewer,
    const AIEventView &event)
{
    if (!originalHegemonyOriginalAdmitted(world, room, viewer) || event.triggerEvent != ChoiceMade || event.kind != "choice"
        || event.from != viewer->objectName() || event.reason != "known_both"
        || event.details.value("choice_kind") != "skillChoice"
        || !event.privateEvent || event.privateViewer != viewer->objectName()) return;
    // HKnownBoth::onEffect sets this exact flag around askForChoice. The actor
    // already knows the selected effect target, but has not yet seen its hidden
    // general: only a roster name is projected before native disclosure occurs.
    QJsonValue target(false);
    for (const auto *other : room.getOtherPlayers(viewer)) {
        if (other->hasFlag("KnownBothTarget")) { target = other->objectName(); break; }
    }
    world.decisionContext.insert("original_known_both_target", target);
}

// ---- original-hegemony-planning ----
namespace {
QStringList matchedKinds(const Card *card) {
    if(!card)return {};
    // Complete finite native override audit, not a catalogue cache:
    // Card.cpp169/180: Qt ancestor names + Original H aliases; QObject is
    // intentionally omitted by getKindOfNames and included here.
    // lua-wrapper.h: class_names + LuaCard + native ancestors; LuaTrickCard
    // additionally enumerates its current SingleTarget/Delayed/Global/AOE subclass.
    // wrapped-card.h285/291 delegates both getters to the same current inner card.
    // Only two fixed native overrides add names absent from that enumeration:
    // MeleeSlashJink (meleemode.cpp212): Slash/Jink; YanxiaoCard (bgm.h73): SkillCard.
    // Recheck native isKindOf rather than treating metadata entries as true.
    const QStringList enumerated=card->getKindOfNames();
    QSet<QString> names(enumerated.cbegin(),enumerated.cend());
    names.insert(QStringLiteral("QObject"));names.insert(QStringLiteral("Slash"));
    names.insert(QStringLiteral("Jink"));names.insert(QStringLiteral("SkillCard"));
    QStringList result;
    for(const QString &name:names){const QByteArray bytes=name.toUtf8();if(card->isKindOf(bytes.constData()))result.append(name);}
    result.sort();return result;
}
void addCounts(QJsonObject &counts,const QStringList &names) {
    QSet<QString> once(names.cbegin(),names.cend());
    for(const auto &name:once)counts.insert(name,counts.value(name).toInt()+1);
}
}
void projectAIOriginalCardCounts(AIWorldView &world,Room &room,const ServerPlayer *viewer) {
    if(!viewer || viewer->getRoom()!=&room || world.self.objectName!=viewer->objectName())return;
    const auto hegemony=world.strategyFacts.value("hegemony").toObject();
    if(!hegemony.value("enabled").toBool() || !hegemony.value("original_overrides_loaded").toBool())return;
    QList<const Card *> definitions;
    for(int id=0;id<Sanguosha->getCardCount();++id)definitions.append(Sanguosha->getEngineCard(id));
    QJsonObject engineCounts;bool missingEngineCard=false;
    // Public catalogue only: deliberately not getDeckCardIds/getRandomCards.
    for(const Card *card:definitions){if(!card)missingEngineCard=true;else addCounts(engineCounts,matchedKinds(card));}
    QJsonObject discards;
    for(int id:room.getDiscardPile()){
        const Card *printed=Sanguosha->getEngineCard(id);
        if(printed)discards.insert(QString::number(id),QJsonArray::fromStringList(matchedKinds(printed)));
        else discards.insert(QString::number(id),false);
    }
    QJsonObject knowledge;
    for(const ServerPlayer *player:room.getAllPlayers(true)){
        const QString visibleFlag=QStringLiteral("visible_%1_%2").arg(viewer->objectName(),player->objectName());
        const auto visible=[&](const Card *card){return card && (player==viewer || card->hasFlag("visible") || card->hasFlag(visibleFlag));};
        QJsonObject counts;int handCount=0,oxCount=0;
        const auto observe=[&](const Card *card){
            auto kinds=matchedKinds(card);
            const QString suit=card->getSuitString();
            if(suit=="club"||suit=="spade"||suit=="diamond"||suit=="heart")kinds.append(suit);
            if(card->isRed())kinds.append("red");if(card->isBlack())kinds.append("black");
            addCounts(counts,kinds);
        };
        for(const Card *card:player->getHandcards())if(visible(card)){++handCount;observe(card);}
        for(int id:player->getPile("wooden_ox")){
            const Card *card=Sanguosha->getCard(id);
            if(visible(card)){++oxCount;observe(card);}
        }
        knowledge.insert(player->objectName(),QJsonObject{{"complete",true},{"hand_visible_count",handCount},{"wooden_ox_visible_count",oxCount},{"counts",counts}});
    }
    world.strategyFacts.insert("original_card_counts",QJsonObject{
        {"complete",true},{"missing_engine_card",missingEngineCard},{"engine_size",definitions.size()},{"engine_counts",engineCounts},
        {"discard_engine_kinds",discards},{"ban_packages",QJsonArray::fromStringList(Sanguosha->getBanPackages())},
        {"knowledge",knowledge}});
}

// ---- original-hegemony-response ----
void projectAITrustCardDefault(AIRequest &r, Room &room, ServerPlayer *p)
{
    const auto h=r.worldView.strategyFacts.value("hegemony").toObject();
    if (!p || p->getRoom()!=&room || r.kind!=AIRequest::RespondCard
        || r.choiceOptions.question!="askForCard" || r.decisionId==0
        || r.viewerObjectName!=p->objectName() || r.worldView.self.objectName!=p->objectName()
        || r.stateRevision!=room.roomRuntime()->stateRevision()
        || h.value("enabled")!=QJsonValue(true)
        || h.value("original_overrides_loaded")!=QJsonValue(true))return;
    // Fixed ai.cpp TrustAI::askForCard: no AI callback, no policy fallback, no
    // conversion, no sorting or RNG. All inspected cards are the viewer's HE.
    QString match=r.pattern;
    if(match.endsWith(QLatin1Char('!')))match.chop(1);
    int selected=-1;
    for(const Card *card:p->getCards("he")) {
        if(p->isCardLimited(card,r.handlingMethod))continue;
        if(Sanguosha->matchPattern(match,p,card)){selected=card->getEffectiveId();break;}
    }
    // No visited-card list or new faces: the snapshot already has lawful owner HE.
    r.choiceOptions.context.insert("original_trust_card_default",QJsonObject{
        {"known",true},{"recipe","TrustAI.askForCard/owner-he/v1"},
        {"viewer",r.viewerObjectName},{"decision_id",r.getDecisionId()},
        {"state_revision",r.getStateRevision()},{"pattern",r.pattern},
        {"handling_method",int(r.handlingMethod)},{"selected_id",selected}});
}

// ---- original-response-wire ----
#include <QPointer>

bool aiOriginalResponseWireSyntax(const QString &wire, int *physicalId)
{
    if (physicalId) *physicalId = -1;
    if (wire == QStringLiteral(".")) return true;
    // ASCII decimal only, preserving spelling. No DummyCard '$', +/- sign,
    // whitespace, declaration, target suffix, or conversion prototype.
    if (wire.isEmpty() || wire.size() > 32) return false;
    for (const QChar c : wire) if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
    bool ok = false;
    const qlonglong id = wire.toLongLong(&ok, 10);
    if (!ok || id < 0 || id > std::numeric_limits<int>::max()) return false;
    if (physicalId) *physicalId = int(id);
    return true;
}

bool aiOriginalResponseWireRequest(const AIRequest &request)
{
    return aiOriginalHegemonyQuestion(request)
        && request.kind == AIRequest::RespondCard
        && request.choiceOptions.question == QStringLiteral("askForCard");
}

static bool disclosedOwnerPhysical(const AIRequest &r, int id)
{
    for (const auto &card : r.worldView.handCards) if (card.effectiveId == id) return true;
    for (const auto &card : r.worldView.self.equips) if (card.effectiveId == id) return true;
    for (const auto &pile : r.worldView.self.piles)
        for (const auto &card : pile.cards) if (card.effectiveId == id) return true;
    return false;
}

static bool currentOwnResponseZone(Room &room, ServerPlayer *p, int id)
{
    if (!p || p->getRoom() != &room || room.getCardOwner(id) != p) return false;
    if (room.getCardPlace(id) == Player::PlaceHand && p->handCards().contains(id)) return true;
    if (room.getCardPlace(id) == Player::PlaceEquip) {
        for (const Card *card : p->getEquips())
            if (card && card->getEffectiveId() == id) return true;
    }
    return p->getHandPile().contains(id);
}

AIOriginalResponseWireOutcome commitAIOriginalResponseWire(
    Room &room, ServerPlayer *viewer, const AIRequest &request, const AIResult &result,
    const std::function<bool()> &commitPriorStrategyEffects)
{
    AIOriginalResponseWireOutcome out;
    if (!result.hasOriginalResponseWire) return out;
    out.applicable = true;
    const auto reject = [&](const char *why) { out.error = QString::fromLatin1(why); return out; };
    int id = -1;
    if (!viewer || viewer->getRoom() != &room || viewer->objectName() != request.viewerObjectName
        || !aiOriginalResponseWireRequest(request) || !result.handled || !result.errorCode.isEmpty()
        || result.decisionId != request.decisionId || result.stateRevision != request.stateRevision
        || !room.roomRuntime() || request.stateRevision != room.roomRuntime()->stateRevision()
        || !aiOriginalResponseWireSyntax(result.originalResponseWire, &id))
        return reject("invalid current Original source-wire witness");
    // Only a physical/no-card result can enter this transition. An issued
    // conversion cannot be re-stamped or carried over the source tag revision.
    if (result.action.hasCardSpec || result.action.hasSkillActionContext
        || result.action.candidateId >= 0 || !result.action.randomCardChoice.isEmpty()
        || result.action.randomCardChoiceResolved
        || !result.action.selectedTargetNames.isEmpty() || !result.action.bottomCardIds.isEmpty()
        || !result.action.userString.isEmpty() || !result.action.legacyCardString.isEmpty()
        || result.action.useCardId >= 0)
        return reject("source-wire transition cannot carry a conversion or mixed action");
    const bool refusal = result.originalResponseWire == QStringLiteral(".");
    if (refusal) {
        if (result.kind != AIResult::Pass || !result.action.selectedCardIds.isEmpty())
            return reject("source refusal must carry no payment");
    } else {
        if (result.kind != AIResult::Answer || result.action.selectedCardIds != QList<int>{id}
            || !disclosedOwnerPhysical(request, id) || !currentOwnResponseZone(room, viewer, id))
            return reject("source physical wire was not an owned disclosed identity");
    }
    QPointer<ServerPlayer> player(viewer);
    if (!commitPriorStrategyEffects || !commitPriorStrategyEffects())
        return reject("source strategy prefix commit failed");
    if (!player || request.stateRevision != room.roomRuntime()->stateRevision())
        return reject("source strategy prefix unexpectedly changed gameplay revision");

    // Exact source order: onTagSet runs synchronously, even for the same string.
    // It may advance revision, move/replace the card, or alter restrictions.
    // Do not cache the Card pointer or any legality predicate before this call.
    room.setTag(QStringLiteral("AiResult"), result.originalResponseWire);
    out.committed = true;
    EngineRuntimeContextScope scope(*Sanguosha, &room);
    const Card *parsed = Card::Parse(result.originalResponseWire);
    if (refusal) {
        if (parsed) return reject("native refusal unexpectedly parsed a card");
        out.accepted = true;
        return out;
    }
    if (!player || !player->isAlive() || !currentOwnResponseZone(room, player, id)
        || !parsed || parsed->isVirtualCard() || parsed->getEffectiveId() != id)
        return reject("source physical product no longer belongs to the current actor");
    QString pattern = request.pattern;
    if (pattern.endsWith(QLatin1Char('!'))) pattern.chop(1);
    if (player->isCardLimited(parsed, request.handlingMethod)
        || (!pattern.isEmpty() && !Sanguosha->matchPattern(pattern, player, parsed)))
        return reject("source physical product is currently limited or mismatched");
    out.card = parsed;
    out.accepted = true;
    return out;
}

// ---- amazing-grace ----
#include "ai-strategy/owner.h"
#include <QVariant>

void projectAIAmazingGrace(AIRequest &request, Room &room,
                                 ServerPlayer *viewer, const Card *trick,
                                 ServerPlayer *from, ServerPlayer *to)
{
    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=room.roomRuntime()->stateRevision()
        || request.kind!=AIRequest::RespondCard
        || request.choiceOptions.question!=QStringLiteral("askForNullification")
        || request.choiceOptions.reason!=QStringLiteral("nullification")
        || request.pattern!=QStringLiteral("nullification")
        || request.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || request.handlingMethod!=Card::MethodUse || !trick
        || trick->getClassName()!=QStringLiteral("AmazingGrace")) return;
    auto &context=request.choiceOptions.context;
    auto common=context.value("smart_common").toObject();
    QJsonObject row{{"known",false}};
    const auto publish=[&](){common.insert("amazing_grace",row);context.insert("smart_common",common);};
    const auto players=room.getAllPlayers(true);
    if (!players.contains(viewer) || !to || !players.contains(to)
        || (from && !players.contains(from))) {publish();return;}
    // Match Room::getUseStruct's exact source key without operator[] insertion.
    // The current native question supplies trick identity; neither a Lua wire
    // nor a merely matching class can nominate another historical use.
    const QVariant history=room.getTag(QStringLiteral("UseHistory")+trick->toString());
    if (!history.canConvert<CardUseStruct>()) {publish();return;}
    const auto use=history.value<CardUseStruct>();
    if (use.card!=trick || use.from!=from || !use.to.contains(to)) {publish();return;}
    QJsonArray targets;
    for (auto *target:use.to) {
        if (!target || !players.contains(target)) {publish();return;}
        targets.append(target->objectName());
    }
    const QString identity=aiLegacyCardIdentity(room,viewer,trick);
    if (identity.isEmpty()) {publish();return;}
    row=QJsonObject{{"known",true},{"card_identity",identity},{"targets",targets},
        {"ids_complete",false}};
    // This exact unkeyed tag is the donor read. Standard AmazingGrace writes
    // AmazingGrace+toString instead; substituting that displayed pool changes
    // source policy. Missing/non-QVariantList values yield [] through fixed
    // SWIG QVariant::toIntList, and no unrelated tag is exposed.
    const QVariant value=room.getTag(QStringLiteral("AmazingGrace"));
    const QVariantList values=value.canConvert<QVariantList>()?value.toList():QVariantList();
    if (values.isEmpty()) {
        row.insert("ids_complete",true);
        row.insert("ids",QJsonArray());row.insert("cards",QJsonArray());
    }
    // Public membership in a different fillAG pool does not disclose which
    // subset/order a privately restored unkeyed tag holds. The only fixed
    // active unkeyed writer (scarletayuhuo6087) writes its exhausted empty list;
    // nonempty restored/unknown-writer values have no full-sequence witness.
    // Keep ids_complete=false with no IDs/faces, even if every face is public.
    auto declared=context.value("card").toObject();
    declared.insert("legacy_identity_token",identity);
    context.insert("card",declared);
    publish();
}
