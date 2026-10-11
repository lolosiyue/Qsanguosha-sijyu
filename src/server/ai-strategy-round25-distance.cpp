#include "ai-strategy-round25-distance.h"
#include "ai-strategy-range-query.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <limits>
thread_local AIRound25OwnerDistanceScope *AIRound25OwnerDistanceScope::s_current=nullptr;
AIRound25OwnerDistanceScope::AIRound25OwnerDistanceScope(const ServerPlayer *owner,int delta)
    :m_owner(owner),m_mark(owner,QStringLiteral("ExtraBfDistanceFrom-Clear"),
        owner->getMark("ExtraBfDistanceFrom-Clear")+delta),m_previous(s_current){s_current=this;}
AIRound25OwnerDistanceScope::~AIRound25OwnerDistanceScope(){s_current=m_previous;}
bool AIRound25OwnerDistanceScope::active(const ServerPlayer *owner)
{for(auto *s=s_current;s;s=s->m_previous)if(!owner||owner==s->m_owner)return true;return false;}
bool validateAIRound25OwnerDistance(Room &room,ServerPlayer *viewer,const AIRequest &r,const QString &source,int delta)
{
    if(!viewer||viewer->getRoom()!=&room||r.viewerObjectName!=viewer->objectName()
        ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.kind!=AIRequest::Activate
        ||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||source!="olqimou"||!viewer->hasSkill("olqimou")
        ||delta>=0||-qint64(delta)>viewer->getHp()||AIRound25OwnerDistanceScope::active())return false;
    const qint64 value=qint64(viewer->getMark("ExtraBfDistanceFrom-Clear"))+delta;
    return value>=std::numeric_limits<int>::min()&&value<=std::numeric_limits<int>::max();
}
QJsonObject projectAIRound25OwnerDistance(Room &room,ServerPlayer *viewer,const QString &source,int delta)
{
    auto row=projectAIStrategyOwnerRange(room,viewer,QStringLiteral("olqimou_distance"),source);
    QJsonObject distances,fixOne;
    for(auto *p:room.getAllPlayers(true)){
        distances.insert(p->objectName(),viewer->distanceTo(p));
        fixOne.insert(p->objectName(),viewer->distanceTo(p,1));
    }
    row.insert("distance_delta",delta);row.insert("distance_to",distances);row.insert("distance_fix_one",fixOne);
    return row;
}
