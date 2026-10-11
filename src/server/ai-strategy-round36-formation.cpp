#include "ai-strategy-round36-formation.h"
#include "ai.h"
#include "hegemony-mode.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
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
void projectAIRound36Formation(AIWorldView&world,Room&room,const ServerPlayer*viewer)
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
