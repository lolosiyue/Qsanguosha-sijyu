#include "ai-strategy-round22-olclan-plan.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include <QJsonArray>
#include <QSet>
#include <QHash>
#include <QPointer>
#include <QUuid>
#include <cmath>
#include <limits>
namespace {
bool current(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.viewerObjectName==p->objectName()&&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
bool play(const AIRequest&r){return r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty();}
bool use(const AIRequest&r,const QString&pattern){return r.kind==AIRequest::UseCard&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.pattern==pattern;}
bool choice(const AIRequest&r,AIRequest::DecisionKind kind,const QString&reason){return r.kind==kind&&r.choiceOptions.reason==reason;}
bool shown(const AIRequest&r,int id){
 for(const auto&c:r.worldView.handCards)if(c.effectiveId==id)return true;
 QList<AIPlayerView> players=r.worldView.players;players.prepend(r.worldView.self);
 for(const auto&p:players){for(const auto&c:p.equips)if(c.effectiveId==id)return true;for(const auto&c:p.knownCards)if(c.effectiveId==id)return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(c.effectiveId==id)return true;}
 return false;
}
bool offered(const AIRequest&r,const QString&name){for(int id:r.choiceOptions.cardIds){const Card*c=Sanguosha->getCard(id);if(c&&c->objectName()==name)return true;}return false;}
bool ownZone(const ServerPlayer*p,int id,bool handOnly){for(const Card*c:p->getCards(handOnly?"h":"he"))if(c->getEffectiveId()==id)return true;return false;}
}
bool aiRound22OlClanConstruction(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s,QString*error){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};
 if(!current(r,p))return fail("OlClan declaration query requires current owner request");
 const QSet<QString> fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return fail("OlClan constructor field count differs");
 for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return fail("OlClan constructor extra field");
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()||!s.value("subcards").isArray()||!s.value("suit").isDouble()||!s.value("number").isDouble()||s.value("suit").toDouble()!=int(Card::NoSuit)||s.value("number").toDouble()!=0)return fail("OlClan source constructor is exactly NoSuit/0");
 const QString key=s.value("source_key").toString(),name=s.value("name").toString(),raw=s.value("skill").toString();
 const auto ids=s.value("subcards").toArray(); bool handOnly=false,foreign=false,duplicates=false,valid=false;
 if(key=="ny_shenjun"){
  handOnly=true;
  valid=p->hasSkill(key)&&raw==key&&((choice(r,AIRequest::Choice,key)&&r.choiceOptions.choices.contains(name)&&name!="cancel")||(use(r,"@@ny_shenjun")&&name==p->property("ny_shenjun").toString()));
  // Source adds first card even for n<=0; readonly does not repair that bug.
  const int n=qMax(1,p->getMark("ny_shenjun"));valid=valid&&ids.size()==qMin(n,p->getHandcardNum());
 }else if(key=="kezufuxun")valid=p->hasSkill(key)&&raw=="_kezufuxun"&&choice(r,AIRequest::AmazingGrace,key)&&offered(r,name)&&ids.size()==1;
 else if(key=="kezuqiuxin")valid=p->hasSkill(key)&&raw=="_kezuqiuxin"&&choice(r,AIRequest::AmazingGrace,key)&&offered(r,name)&&ids.isEmpty();
 else if(key=="kezujianji")valid=raw==key&&use(r,"@@kezujianji")&&name=="slash"&&ids.isEmpty();
 else if(key=="zuchengqi"){
  handOnly=true;duplicates=true;
  const bool response=(r.kind==AIRequest::UseCard||r.kind==AIRequest::RespondCard)&&!r.pattern.startsWith("@@")&&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);
  valid=p->hasSkill(key)&&raw==key&&(play(r)||response)&&r.worldView.strategyFacts.value("pattern_names").toArray().contains(name)&&ids.size()>=2&&ids.size()<=2*p->getHandcardNum();
  bool ordinary=false;for(int i=0;i<Sanguosha->getCardCount();++i){const Card*c=Sanguosha->getEngineCard(i);if(c&&c->objectName()==name&&(c->getTypeId()==Card::TypeBasic||c->isNDTrick())){ordinary=true;break;}}valid=valid&&ordinary;
 }else if(key=="zujuetu"){
  foreign=true;valid=p->hasSkill(key)&&raw=="_zujuetu"&&use(r,"@@zujuetu1!")&&name=="dismantlement"&&ids.size()==1&&ids.first().isDouble()&&ids.first().toDouble()==p->getMark("zujuetuId");
 }else if(key=="zuyingxiang")valid=p->hasSkill(key)&&raw=="_zuyingxiang"&&use(r,"@@zuyingxiang!")&&name==p->property("zuyingxiangCn").toString()&&!name.isEmpty()&&ids.isEmpty();
 if(!valid)return fail("OlClan constructor does not match fixed source and current question");
 QSet<int>seen;for(const auto&v:ids){double n=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(n)||n<0||n>std::numeric_limits<int>::max()||n!=std::floor(n))return fail("OlClan material ID malformed");int id=int(n);if(!duplicates&&seen.contains(id))return fail("OlClan material repeated outside source mutation");seen.insert(id);if(!shown(r,id)||(!foreign&&!ownZone(p,id,handOnly)))return fail("OlClan material is not lawfully disclosed in exact source zone");}
 if(error)error->clear();return true;
}
bool aiRound22OlClanRepeatedMaterials(const QJsonObject&s){return s.value("source_key")=="zuchengqi";}
bool aiRound22OlClanForeignMaterial(const QJsonObject&s){return s.value("source_key")=="zujuetu";}
int aiRound22OlClanExtraTarget(const QJsonObject&s){return s.value("source_key")=="kezuqiuxin"?99:0;}
int aiRound22OlClanPrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){return current(r,p)&&play(r)&&p->hasSkill("zuchengqi")&&skill=="zuchengqi"&&prototype=="#zuchengqiCard"?1:-1;}
void projectAIRound22OlClanPlanContext(AIRequest&r,Room&room,const ServerPlayer*p){
 if(!p||p->getRoom()!=&room||r.viewerObjectName!=p->objectName())return;
 QJsonObject context=r.choiceOptions.context,row;
 if(use(r,"@@ny_shenjun"))row.insert("ny_shenjun",p->property("ny_shenjun").toString());
 if(use(r,"@@zuyingxiang!"))row.insert("zuyingxiangCn",p->property("zuyingxiangCn").toString());
 if(choice(r,AIRequest::AmazingGrace,"kezuqiuxin")){
  // extensions/olClan.lua4227 writes the already-public current target just
  // before this owner-only AG question; never serialize any associated card.
  const QVariant v=p->getTag("kezuqiuxinTo");const ServerPlayer*to=v.value<ServerPlayer*>();
  if(!v.isValid()||!to)row.insert("kezuqiuxinTo",false);
  else for(const Player*known:room.getAllPlayers(true))if(known==to){row.insert("kezuqiuxinTo",to->objectName());break;}
 }
 if(!row.isEmpty()){context.insert("olclan_plan",row);r.choiceOptions.context=context;}
}

namespace {
bool observationShown(const AIRequest &r,int id) {
 if(shown(r,id))return true;
 for(const auto &c:r.worldView.discardPile)if(c.effectiveId==id)return true;
 for(const auto &c:r.choiceOptions.cards)if(c.effectiveId==id)return true;
 QList<AIPlayerView> players=r.worldView.players;players.prepend(r.worldView.self);
 for(const auto &p:players)for(const auto &c:p.judgingArea)if(c.effectiveId==id)return true;
 return false;
}
struct OlClanObservation {
 QPointer<ServerPlayer> viewer;
 AIReadonlyCardLease lease;
 QString token;
 QList<int> materials;
 int physicalId=-1;
};
// Room shutdown explicitly clears this table BEFORE Lua/lifetime shutdown.
// QObject destruction also clears it as a defensive non-runtime exit path.
QHash<Room *, QHash<QString, QHash<QString, OlClanObservation>>> olClanObservations;
QSet<Room *> observedRooms;
bool observationSlot(const QString &slot) {
 return slot == "kezufuxunUse" || slot == "zuchengqiUse";
}
bool captureQuestion(const AIRequest &r, const QString &slot) {
 return slot == "kezufuxunUse" ? choice(r,AIRequest::AmazingGrace,"kezufuxun")
                                : play(r);
}
bool restoreQuestion(const AIRequest &r, const QString &slot) {
 return use(r,slot == "kezufuxunUse" ? "@@kezufuxunbasic" : "@@zuchengqi");
}
}
QString captureAIRound23OlClanObservation(const AIRequest &r, ServerPlayer *p,
 const QString &slot, AIReadonlyCardLease lease, QString *error) {
 auto fail=[&](const char *m){if(error)*error=QString::fromLatin1(m);return QString();};
 if(!current(r,p)||!observationSlot(slot)||!captureQuestion(r,slot))
  return fail("OlClan saved observation requires exact current source question");
 if(!lease)return fail("OlClan saved observation requires native readonly custody");
 const Card *card=lease();
 if(!card)return fail("OlClan saved readonly graph expired");
 const int physicalId=card->isVirtualCard()?-1:card->getEffectiveId();
 if(!card->isVirtualCard()&&(physicalId<0||!observationShown(r,physicalId)))return fail("OlClan saved physical card is not currently disclosed");
 // Only ID membership is read before current disclosure has been established;
 // getSuit/getNumber/toString may dereference mutable material wrappers.
 const QList<int> materials=card->getSubcards();
 for(int id:materials)if(!observationShown(r,id))return fail("OlClan saved material is not currently disclosed");
 Room *room=p->getRoom();
 if(!observedRooms.contains(room)){
  observedRooms.insert(room);
  QObject::connect(room,&QObject::destroyed,[room](QObject *){
   olClanObservations.remove(room);observedRooms.remove(room);
  });
 }
 OlClanObservation entry;entry.viewer=p;entry.lease=std::move(lease);entry.materials=materials;entry.physicalId=physicalId;
 entry.token="olclan-observation:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
 olClanObservations[room][p->objectName()].insert(slot,entry);
 if(error)error->clear();return entry.token;
}
const Card *restoreAIRound23OlClanObservation(const AIRequest &r, ServerPlayer *p,
 const QString &slot, const QString &token, QString *error) {
 auto fail=[&](const char *m)->const Card *{if(error)*error=QString::fromLatin1(m);return nullptr;};
 if(!current(r,p)||!observationSlot(slot)||!restoreQuestion(r,slot))
  return fail("OlClan saved observation requires exact current continuation");
 const auto room=olClanObservations.constFind(p->getRoom());
 if(room==olClanObservations.cend())return fail("OlClan saved observation absent");
 const auto viewer=room->constFind(p->objectName());
 if(viewer==room->cend())return fail("OlClan saved observation viewer absent");
 const auto found=viewer->constFind(slot);
 if(found==viewer->cend()||found->viewer!=p||token.isEmpty()||found->token!=token)
  return fail("OlClan saved observation capability does not match viewer and slot");
 // Validate visibility before reading ANY dynamic material-derived face. The
 // saved observation does not grant continuing access to hidden transformations.
 if(found->physicalId>=0&&!observationShown(r,found->physicalId))return fail("OlClan saved physical card is no longer currently disclosed");
 for(int id:found->materials)if(!observationShown(r,id))return fail("OlClan saved material is no longer currently disclosed");
 const Card *card=found->lease();
 if(!card)return fail("OlClan saved readonly graph expired");
 if(card->getSubcards()!=found->materials)return fail("OlClan saved graph material list unexpectedly changed");
 if(error)error->clear();return card;
}
void clearAIRound23OlClanObservations(Room &room) { olClanObservations.remove(&room); }
