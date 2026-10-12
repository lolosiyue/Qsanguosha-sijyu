#include "room-runtime.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round51-jijiao.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "card.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>

namespace {
bool scope(const AIRequest &r, const ServerPlayer *p, const std::function<bool()> &query = {}) {
    return p && p->getRoom() && Sanguosha && Sanguosha->currentRoom()==p->getRoom()
        && r.kind==AIRequest::Activate && r.reason==CardUseStruct::CARD_USE_REASON_PLAY
        && r.pattern.isEmpty() && r.viewerObjectName==p->objectName()
        && r.worldView.self.objectName==p->objectName()
        && p->getRoom()->getCurrent()==p && p->getPhase()==Player::Play
        && p->isAlive() && (query ? query() : p->hasSkill("jijiao"))
        && aiRound44RangeRequestCurrent(*p->getRoom(),r,p);
}
bool exactDiscard(const AICardView &v, const Card *c) {
    // Only called after CURRENT PUBLIC pile membership and mapping agree.
    return c && v.nativeIdentity && globalCardLifetimeManager().isLive(v.nativeIdentity)
        && v.nativeIdentity->address==c && v.cardId==c->getId()
        && v.effectiveId==c->getEffectiveId() && v.objectName==c->objectName()
        && v.className==c->getClassName() && v.suit==int(c->getSuit())
        && v.number==c->getNumber() && v.skillName==c->getSkillName(false)
        && v.subcardIds==c->getSubcards() && v.virtualCard==c->isVirtualCard();
}
}
void projectAIRound51Jijiao(AIRequest &r, Room &room, ServerPlayer *p) {
    const auto nativeHasJijiao = [&] { return ([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,p,gameLua));
        return p->hasSkill("jijiao"); }()); };
    auto facts=r.worldView.strategyFacts.value("tenyear_hc").toObject();
    facts.remove("JijiaoRecord"); facts.remove("jijiao_discard");
    r.worldView.strategyFacts.insert("tenyear_hc",facts);
    if(!scope(r,p,nativeHasJijiao) || p->getRoom()!=&room) return;
    // Exact SWIG QVariant::toIntList (qvariant.i145): append toInt()
    // without an ok filter. Native effect separately uses ListV2I, which
    // skips failed conversions; normal fixed writers store integers only.
    // Never sort/dedup or substitute the Room-global JijiaoRecord.
    QList<int> ids;
    const QVariant own=p->getTag("JijiaoRecord");
    if(own.canConvert<QVariantList>())
        for(const QVariant &v:own.toList()) ids.append(v.toInt());
    const auto discard=room.getDiscardPile();
    QJsonArray record; QJsonObject membership;
    for(int id:ids) {
        record.append(id);
        const QString key=QString::number(id);
        if(membership.contains(key)) continue;
        bool inSnapshot=false, exact=false;
        for(const auto &v:r.worldView.discardPile) if(v.effectiveId==id) {
            inSnapshot=true;
            if(discard.contains(id) && room.getCardPlace(id)==Player::DiscardPile)
                exact=exactDiscard(v,Sanguosha->getCard(id));
            break;
        }
        if(!discard.contains(id) && !inSnapshot) membership.insert(key,false);
        else if(exact) membership.insert(key,true);
        // Inconsistent/stale public pile, mapping, wrapper or face: omit the
        // membership row (typed unknown). Do not reveal any non-discard location.
    }
    if(!scope(r,p,nativeHasJijiao)) return;
    facts.insert("JijiaoRecord",record); facts.insert("jijiao_discard",membership);
    r.worldView.strategyFacts.insert("tenyear_hc",facts);
}
int aiRound51JijiaoPrototype(const AIRequest &r,const ServerPlayer *p,
                            const QString &skill,const QString &name) {
    if(skill!="jijiao" || name!="JijiaoCard" || !scope(r,p)
        || p->getMark("@jijiaoMark")<=0) return -1;
    // Obtained discard IDs are EFFECT cards, never selected subcard costs.
    return 0;
}
