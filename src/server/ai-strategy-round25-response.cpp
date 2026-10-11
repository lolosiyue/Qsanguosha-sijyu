#include "ai-strategy-round25-response.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>
namespace {
bool disclosed(const AIRequest &r,int id) {
    const auto cards=[&](const QList<AICardView>&list){for(const auto &c:list)if(c.effectiveId==id)return true;return false;};
    for(const auto *list:{&r.worldView.handCards,&r.worldView.discardPile,&r.choiceOptions.cards})if(cards(*list))return true;
    const auto player=[&](const AIPlayerView&p){for(const auto *list:{&p.knownCards,&p.equips,&p.judgingArea})if(cards(*list))return true;for(const auto &pile:p.piles)if(pile.open&&cards(pile.cards))return true;return false;};
    if(player(r.worldView.self))return true;
    for(const auto &pile:r.worldView.self.piles)if(cards(pile.cards))return true; // owner-only lawful faces, even a closed hand pile
    for(const auto&p:r.worldView.players)if(player(p))return true;return false;
}
}
void projectAIStrategyRound25Response(AIRequest &request,Room &room,ServerPlayer *viewer,QJsonObject &context) {
    if(!viewer||viewer->getRoom()!=&room||request.viewerObjectName!=viewer->objectName()
       ||request.worldView.self.objectName!=viewer->objectName()||request.stateRevision!=room.roomRuntime()->stateRevision())return;
    const QString reason=request.choiceOptions.reason;
    QJsonObject out=context.value("ol_st_standard").toObject(),tags;
    // Owner-only raw tag contracts. Old tag absence is not repaired from the
    // newer V2 rule's differently named properties or SkillContext data.
    const auto strings=[&](const QString&key,bool list){const QVariant raw=viewer->getTag(key);QJsonObject row{{"known",true},{"present",raw.isValid()}};row.insert("value",list?QJsonValue(QJsonArray::fromStringList(raw.toStringList())):QJsonValue(raw.toString()));tags.insert(key,row);};
    if(reason=="olhuashen"&&request.kind==AIRequest::Choice) {
        strings("Huashens",true);QJsonObject generals;
        for(const QString&name:viewer->getTag("Huashens").toStringList()){
            const General*g=Sanguosha->getGeneral(name);QJsonObject row{{"exists",g!=nullptr}};QJsonArray skills;
            if(g)for(const Skill*s:g->getVisibleSkillList())skills.append(s->objectName());row.insert("skills",skills);generals.insert(name,row);
        }
        out.insert("huashen_generals",generals);
    }
    if(reason=="olbeige"&&request.kind==AIRequest::Discard)strings("OLbeigeTag",false);
    const auto ids=[&](const QString&key){const QVariant raw=viewer->getTag(key);QJsonArray list;for(int id:raw.value<QList<int>>())list.append(id);tags.insert(key,QJsonObject{{"known",true},{"present",raw.isValid()},{"value",list}});};
    if(request.kind==AIRequest::UseCard&&request.pattern=="@@secondolhanzhan")ids("secondolhanzhanForAI");
    if(request.kind==AIRequest::UseCard&&request.pattern=="@@olzongxuan")ids("olzongxuanForAI");
    if(reason=="olpiaoling"&&request.kind==AIRequest::PlayerChosen){
        const QVariant raw=viewer->getTag("olpiaoling");const Card*card=raw.value<const Card*>();
        QJsonObject row{{"known",true},{"present",card!=nullptr}};
        if(card){const int id=card->getEffectiveId();if(id>=0&&disclosed(request,id))row.insert("card_id",id);else row.insert("known",false);}
        tags.insert("olpiaoling",row);
    }
    if(reason=="olluanji"&&request.kind==AIRequest::PlayerChosen){
        const QVariant raw=viewer->getTag("olluanji_data");const CardUseStruct use=raw.value<CardUseStruct>();
        QJsonObject row{{"known",true},{"present",raw.isValid()}};
        if(raw.isValid()){
            QJsonObject data;QJsonArray to;for(const auto*p:use.to)to.append(p->objectName());data.insert("to",to);if(use.from)data.insert("from",use.from->objectName());
            if(use.card){const int id=use.card->getEffectiveId();if(!use.card->isVirtualCard()&&id>=0&&disclosed(request,id))data.insert("card_id",id);else row.insert("known",false);}
            row.insert("use",data);
        }
        tags.insert("olluanji_data",row);
    }
    if(reason=="olguhuo"&&request.kind==AIRequest::Choice)out.insert("guhuo_type",room.getTag("OLGuhuoType").toString());
    if(!tags.isEmpty())out.insert("tags",tags);
    if(!out.isEmpty())context.insert("ol_st_standard",out);
}

// This is a constructor vocabulary, not a payment grant. The issued activation
// instance and native canSelect/cardSelectionFeasible remain authoritative.
int aiRound25ResponsePrototype(const AIRequest &r,const ServerPlayer *viewer,
    const QString &skill,const QString &prototype)
{
    if(!viewer||!viewer->getRoom()||r.viewerObjectName!=viewer->objectName()
       ||r.stateRevision!=viewer->getRoom()->roomRuntime()->stateRevision()||!viewer->hasSkill(skill))return -1;
    static const QMap<QString,QString> prototypes{
        {"oljijiang","OLJijiangCard"},{"olhuangtian_attach","OLHuangtianCard"},{"olguhuo","OLGuhuoCard"},
        {"olqimou","OLQimouCard"},{"olzhiba","OLZhibaCard"},{"olzhiba_pindian","OLZhibaPindianCard"},
        {"olchangbiao","OLChangbiaoCard"},{"oltiaoxin","OLTiaoxinCard"},{"olqiaobian","OLQiaobianCard"},
        {"olqiangxi","OLQiangxiCard"},{"oljianmie","OLJianmieCard"},{"olmieji","OLMiejiCard"},{"olzhijian","OLZhijianCard"}};
    if(prototypes.value(skill)!=prototype||!prototypes.contains(skill))return -1;
    const bool play=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;
    const bool response=(r.kind==AIRequest::RespondCard||r.kind==AIRequest::UseCard)
        &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
    if(skill=="olqiaobian")return response&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern=="@@olqiaobian" ? 0:-1;
    if(skill=="oljijiang")return play||(response&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&(r.pattern.contains("slash",Qt::CaseInsensitive)||r.pattern=="@jijiang")) ? 0:-1;
    if(skill=="olguhuo")return play||(response&&!r.pattern.isEmpty()&&!r.pattern.startsWith('@')&&!r.pattern.startsWith('.')) ? 3:-1;
    if(!play)return -1;
    static const QSet<QString> paid{"olhuangtian_attach","olchangbiao","olqiangxi","olmieji","olzhijian"};
    return paid.contains(skill)?1:0;
}

bool aiRound25ResponseConstruction(const AIRequest &r,const ServerPlayer *viewer,const QJsonObject &spec)
{
    if(!viewer||!viewer->getRoom()||r.viewerObjectName!=viewer->objectName()
        ||r.worldView.self.objectName!=viewer->objectName()
        ||r.stateRevision!=viewer->getRoom()->roomRuntime()->stateRevision()
        ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY)return false;
    const QString skill=spec.value("source_key").toString(),name=spec.value("name").toString();
    static const QMap<QString,QString> names{{"olhuoji","fire_attack"},{"ollianhuan","iron_chain"},
        {"oljiuchi","analeptic"},{"olduanliang","supply_shortage"},{"olluanji","archery_attack"},{"oljiushi","analeptic"}};
    if((!names.contains(skill)&&skill!="ollongdan")||!viewer->hasSkill(skill)
        ||spec.value("skill")!=skill||!spec.value("subcards").isArray())return false;
    if(skill!="ollongdan"&&names.value(skill)!=name)return false;
    const QJsonArray costs=spec.value("subcards").toArray();
    if(costs.size()!=(skill=="olluanji"?2:skill=="oljiushi"?0:1))return false;
    if(skill=="oljiushi")return spec.value("suit")==int(Card::NoSuit)&&spec.value("number")==0;
    QList<const Card*> cards;QSet<int> seen;const Room &room=*viewer->getRoom();
    for(const QJsonValue &v:costs){
        const double n=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(n)||n<0||std::floor(n)!=n||n>std::numeric_limits<int>::max())return false;
        const int id=int(n);if(seen.contains(id)||room.getCardOwner(id)!=viewer||!disclosed(r,id))return false;seen.insert(id);
        const auto place=room.getCardPlace(id);
        const bool hand=place==Player::PlaceHand, equip=place==Player::PlaceEquip, pile=viewer->getHandPile().contains(id);
        // Source policies enumerate hand for these three constructions, he for
        // Huoji/Lianhuan/Duanliang. Do not broaden to arbitrary owner piles.
        if(skill=="ollongdan"||skill=="olluanji"||skill=="oljiuchi") {if(!hand&&!pile)return false;}
        else if(!hand&&!equip&&!pile)return false;
        const Card*c=Sanguosha->getCard(id);if(!c||c->isVirtualCard())return false;cards<<c;
    }
    const Card *c=cards.first();
    if(skill=="ollongdan"){
        const QString expected=c->isKindOf("Jink")?QStringLiteral("slash"):c->isKindOf("Peach")?QStringLiteral("analeptic"):
            c->isKindOf("Analeptic")?QStringLiteral("peach"):QString();
        if(expected.isEmpty()||name!=expected)return false;
    }else if(skill=="olhuoji"&&!c->isRed())return false;
    else if(skill=="ollianhuan"&&c->getSuit()!=Card::Club)return false;
    else if(skill=="oljiuchi"&&c->getSuit()!=Card::Spade)return false;
    else if(skill=="olduanliang"&&(!c->isBlack()||(!c->isKindOf("BasicCard")&&!c->isKindOf("EquipCard"))))return false;
    else if(skill=="olluanji")return cards[0]->getSuit()==cards[1]->getSuit()
        &&spec.value("suit")==int(Card::NoSuit)&&spec.value("number")==0;
    return spec.value("suit")==int(c->getSuit())&&spec.value("number")==c->getNumber();
}
