#include "ai-strategy-round27-response.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>
namespace {
bool scope(const AIRequest &r,const ServerPlayer *p)
{
    return p&&p->getRoom()&&r.viewerObjectName==p->objectName()
        &&r.worldView.self.objectName==p->objectName()
        &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();
}
bool play(const AIRequest &r)
{
    return r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;
}
bool disclosed(const AIRequest &r,int id)
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
bool aiRound27ResponseConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &spec)
{
    if(!scope(r,p))return false;
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
        int id=-1;if(!integer(value,id)||id<0||unique.contains(id)||!disclosed(r,id))return false;
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
bool aiRound27ZhangwuGeneratedBranch(const AIRequest &r,const ServerPlayer *p,const QString &skill)
{
    return scope(r,p)&&play(r)&&skill=="lxtx_zhangwu"&&p->hasSkill(skill)
        &&p->getMark("lxtx_jiangchi_recast-Clear")<=0;
}
bool aiRound27ZhangwuGeneratedPayment(const AIRequest &r,const ServerPlayer *p,const QString &skill,const QList<int> &costs)
{
    if(!aiRound27ZhangwuGeneratedBranch(r,p,skill)||costs.isEmpty())return false;
    QList<int> generated;
    for(const Card *c:p->getHandcards()){
        if(!c||!disclosed(r,c->getEffectiveId()))return false;
        if(p->getMark("lxtx_jiangchi_draw-Clear")<=0
            ||(c->isKindOf("BasicCard")&&!c->isKindOf("Peach")&&!c->isKindOf("Analeptic"))||c->isKindOf("EquipCard"))generated<<c->getEffectiveId();
    }
    return generated==costs;
}
