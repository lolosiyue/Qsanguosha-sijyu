#include "ai-strategy-round49-response.h"
#include "ai.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <cmath>
#include <limits>

bool aiRound49ResponseConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
    if (!p || !p->getRoom() || r.viewerObjectName!=p->objectName()
        || !aiRound44RangeRequestCurrent(*p->getRoom(),r,p)
        || r.kind!=AIRequest::Activate || r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
        || s.size()!=7 || s.value("suit")!=QJsonValue(int(Card::NoSuit))
        || s.value("number")!=QJsonValue(0) || s.value("can_recast")!=QJsonValue(false)
        || !s.value("subcards").isArray()) return false;
    const QString key=s.value("source_key").toString(),name=s.value("name").toString();
    if (s.value("skill")!=QJsonValue(key)
        || !((key=="fuzhai" && (name=="ex_nihilo" || name=="collateral"))
            || (key=="olquxi" && (name=="dismantlement" || name=="snatch")))) return false;
    const QJsonArray ids=s.value("subcards").toArray();
    if(ids.size()!=1 || !ids[0].isDouble())return false;
    const double value=ids[0].toDouble();
    if(!std::isfinite(value)||value<0||value>std::numeric_limits<int>::max()||value!=std::floor(value))return false;
    const int id=int(value);
    Room &room=*p->getRoom();
    if(room.getCardOwner(id)!=p)return false;
    const Card *material=nullptr;
    // Source getCards("he") returns current hand wrappers but realEquip inner
    // objects in the equipment branch. Authenticate that exact source identity.
    const auto same=[&](const AICardView &v,const Card *c){
        return c && v.effectiveId==id && v.objectName==c->objectName()
            && v.className==c->getClassName() && v.suit==int(c->getSuit())
            && v.number==c->getNumber() && !v.legacyIdentityToken.isEmpty()
            && v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
    };
    for(const Card *c:p->getCards(key=="fuzhai"?"he":"h")) {
        if(!c || c->getEffectiveId()!=id)continue;
        if(room.getCardPlace(id)==Player::PlaceHand) {
            for(const auto &v:r.worldView.handCards)if(same(v,c))material=c;
        } else if(key=="fuzhai" && room.getCardPlace(id)==Player::PlaceEquip) {
            for(const auto &v:r.worldView.self.equips)if(same(v,c))material=c;
        }
    }
    if(!material)return false;
    if(key=="fuzhai" ? !material->isKindOf("EquipCard")
        : !(name=="dismantlement"?material->isKindOf("Jink"):material->isKindOf("Peach")))return false;
    // Readonly source construction: do not consume quota or run viewFilter.
    // Source's ban-mark and availability reads retain their original order;
    // selected materialization independently invokes the current V1 provider.
    return p->hasSkill(key) && aiRound44RangeRequestCurrent(*p->getRoom(),r,p);
}
