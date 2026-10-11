#include "ai-strategy-round28-response.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <cmath>
#include <limits>
namespace {
bool integer(const QJsonValue &value,int &out)
{
    const double n=value.toDouble();
    if(!value.isDouble()||!std::isfinite(n)||std::floor(n)!=n
        ||n<std::numeric_limits<int>::min()||n>std::numeric_limits<int>::max())return false;
    out=int(n);return true;
}
bool disclosed(const AIRequest &r,int id)
{
    for(const auto &c:r.worldView.handCards)if(c.effectiveId==id)return true;
    for(const auto &c:r.worldView.self.knownCards)if(c.effectiveId==id)return true;
    for(const auto &c:r.worldView.self.equips)if(c.effectiveId==id)return true;
    return false;
}
}
bool aiRound28ResponseConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &spec)
{
    if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
        ||r.worldView.self.objectName!=p->objectName()
        ||r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision()
        ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY)return false;
    const QString source=spec.value("source_key").toString();
    if((source!="cihuai"&&source!="chixin")||!p->hasSkill(source)
        ||spec.value("name")!=QStringLiteral("slash")||!spec.value("subcards").isArray())return false;
    int suit=-1,number=-1;
    if(!integer(spec.value("suit"),suit)||!integer(spec.value("number"),number))return false;
    const QJsonArray costs=spec.value("subcards").toArray();
    // The source fill is unconditional. Active-instance/hand-count admission
    // belongs to the actual provider at final selection, not enumeration.
    if(source=="cihuai")return spec.value("skill")==QStringLiteral("_cihuai")
        &&costs.isEmpty()&&suit==Card::NoSuit&&number==0;
    if(spec.value("skill")!=source||costs.size()!=1)return false;
    int id=-1;if(!integer(costs.first(),id)||id<0||!disclosed(r,id))return false;
    const Room *room=p->getRoom();
    if(room->getCardOwner(id)!=p||(room->getCardPlace(id)!=Player::PlaceHand
        &&room->getCardPlace(id)!=Player::PlaceEquip))return false;
    const Card *card=Sanguosha->getCard(id);
    // Source fill does not check using: preserve hypothetical sorting, and let
    // the native final payment gate reject a selected unavailable material.
    return card&&!card->isVirtualCard()&&card->getSuit()==Card::Diamond
        &&suit==int(card->getSuit())&&number==card->getNumber();
}

bool aiRound28XdResponseConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &spec)
{
    if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
        ||r.worldView.self.objectName!=p->objectName()
        ||r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision())return false;
    const QString skill=spec.value("source_key").toString(),name=spec.value("name").toString();
    const bool turn=r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY;
    const bool prompted=skill=="zhifeng"&&r.kind==AIRequest::UseCard
        &&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern=="@@zhifeng"&&r.prompt=="zhifeng0";
    if((skill!="peiniang"&&skill!="zhifeng")||(!turn&&!prompted)||(turn&&!p->hasSkill(skill))
        ||spec.value("skill")!=skill||!spec.value("subcards").isArray())return false;
    int suit=-1,number=-1,id=-1;const auto costs=spec.value("subcards").toArray();
    if(!integer(spec.value("suit"),suit)||!integer(spec.value("number"),number)
        ||suit!=Card::NoSuit||number!=0||costs.size()!=1||!integer(costs.first(),id)||id<0||!disclosed(r,id))return false;
    const auto *room=p->getRoom();const auto place=room->getCardPlace(id);
    if(room->getCardOwner(id)!=p||(place!=Player::PlaceHand&&place!=Player::PlaceEquip))return false;
    const Card *card=Sanguosha->getCard(id);if(!card||card->isVirtualCard())return false;
    if(skill=="peiniang")return name=="analeptic"&&card->getSuitString()==p->property("yitongSuit").toString();
    // The fixed source counts he and uses n<1. The native V1 counts h and
    // uses n<0: preserve this real mismatch until selected native validation.
    const int sourceDifference=p->getCards("he").size()-p->getHp();
    return sourceDifference>0 ? name=="analeptic"&&card->isBlack() : name=="slash"&&card->isRed();
}
