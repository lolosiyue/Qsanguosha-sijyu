#include "ai-strategy/ol-ai.h"

// ---- olclan-actions ----
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>

bool aiOlClanActionConstruction(const AIRequest &request, const ServerPlayer *viewer,
                                const QJsonObject &spec, QString *error)
{
    const auto reject = [&](const char *why) { if (error) *error=QString::fromLatin1(why); return false; };
    if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != viewer->getRoom()->roomRuntime()->stateRevision()
        || request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
        return reject("OlClan construction requires the current owner play request");
    static const QSet<QString> fields{"source_key","name","skill","suit","number","subcards"};
    if (spec.size()!=fields.size()) return reject("OlClan constructor field set differs from source");
    for (auto it=spec.begin();it!=spec.end();++it)
        if (!fields.contains(it.key())) return reject("OlClan constructor contains an unaudited field");
    if (!spec.value("source_key").isString() || !spec.value("name").isString()
        || !spec.value("skill").isString() || !spec.value("suit").isDouble()
        || !spec.value("number").isDouble() || !spec.value("subcards").isArray()
        || spec.value("suit").toDouble()!=int(Card::NoSuit) || spec.value("number").toDouble()!=0)
        return reject("OlClan source constructor is not exact NoSuit zero");
    const QString key=spec.value("source_key").toString();
    QString name,skill;int count=-1;
    // b6025 ai/olClan-ai.lua:186,607,879,1818. Source cloneCard/dummyCard
    // defaults are NoSuit/0, followed by the listed raw skill and materials.
    if (key=="zuzhanding") { name="slash";skill=key;count=1; }
    else if (key=="kezuxumin") { name="amazing_grace";skill=key;count=1; }
    else if (key=="kezubolong") { name="thunder_slash";skill=key;count=0; }
    else if (key=="zutanfeng") { name="slash";count=0; }
    else return reject("OlClan constructor source is not audited");
    const QJsonArray ids=spec.value("subcards").toArray();
    if (spec.value("name").toString()!=name || spec.value("skill").toString()!=skill || ids.size()!=count
        || !viewer->hasSkill(key)) return reject("OlClan constructor product or owner source differs");
    for (const auto &value:ids) {
        const double n=value.toDouble(-1);
        if (!value.isDouble() || !std::isfinite(n) || n<0 || n>std::numeric_limits<int>::max() || n!=std::floor(n))
            return reject("OlClan source material ID is invalid");
        const int id=int(n);bool disclosed=false;
        for (const auto &card:request.worldView.handCards) if (card.effectiveId==id) disclosed=true;
        for (const auto &card:request.worldView.self.equips) if (card.effectiveId==id) disclosed=true;
        const auto place=viewer->getRoom()->getCardPlace(id);
        if (!disclosed || viewer->getRoom()->getCardOwner(id)!=viewer
            || (place!=Player::PlaceHand && place!=Player::PlaceEquip))
            return reject("OlClan source material is outside disclosed owner he");
    }
    if (error) error->clear();
    return true;
}

bool applyAIOlClanActionConstruction(const AIRequest &request, const ServerPlayer *viewer,
                                     const QJsonObject &spec, Card &card)
{
    if (!aiOlClanActionConstruction(request,viewer,spec) || card.getId()>=0
        || card.objectName()!=spec.value("name").toString()
        || card.getSkillName(false)!=spec.value("skill").toString()) return false;
    QList<int> ids;for (const auto &id:spec.value("subcards").toArray()) ids << id.toInt();
    if (card.getSubcards()!=ids) return false;
    // The source flag belongs to this original detached clone. It is retained
    // by the existing readonly recipe, not reconstructed from a Lua flag table.
    if (spec.value("source_key").toString()=="zutanfeng") card.setFlags("Qinggang");
    return true;
}

int aiOlClanActionPrototype(const QString &skill,const QString &prototype)
{
    // Skill::Skill strips the trailing '&' (skill.cpp:46-53); native objectName
    // and issued activation are zulianzhuvs, not the registration spelling.
    // Parent holder/attached activation: olClan.lua1003-1133. No forged parent
    // skill claim: caller's already-issued native source scope is required.
    if (skill=="zulianzhuvs" && prototype=="#zulianzhuCard") return 1;
    if ((skill=="kezubolong" && prototype=="#kezubolongCard")
        || (skill=="kezuxumin" && prototype=="#kezuxuminCard")
        || (skill=="zutanfeng" && prototype=="#zutanfengCard")) return 0;
    return -1;
}

// ---- olclan-observations ----
#include "ai-strategy/owner.h"
#include "structs.h"

void projectAIStrategyOlClanObservations(AIRequest &request, Room &room,
    ServerPlayer *viewer, const QVariant &data, QJsonObject &context,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.worldView.self.objectName!=viewer->objectName()) return;
    auto rows=context.value("olclan_observations").toObject();
    const QString reason=request.choiceOptions.reason;
    const auto question=[&](AIRequest::DecisionKind kind,const QString &key) {
        return request.kind==kind && (reason==key || reason==key+"$-1");
    };
    const auto roster=room.getAllPlayers(true);
    const auto playerTag=[&](const QString &key) {
        const QVariant value=viewer->getTag(key);
        const auto *p=value.value<ServerPlayer *>();
        QJsonObject row{{"known",!p || roster.contains(const_cast<ServerPlayer *>(p))},
            {"present",value.isValid()}};
        if (!p) row.insert("value",QJsonValue(QJsonValue::Null));
        else if (roster.contains(const_cast<ServerPlayer *>(p))) row.insert("value",p->objectName());
        rows.insert(key,row);
    };
    const auto useTag=[&](const QString &key,const QVariant &value) {
        QJsonObject row{{"known",true},{"present",value.isValid()}};
        const CardUseStruct use=value.value<CardUseStruct>();
        QJsonObject result;QJsonArray targets;
        // All three audited writers use this actor's current public CardUsed /
        // TargetSpecifying data. A foreign/stale use cannot borrow the question.
        if (use.card && (use.from!=viewer || !publicCard)) row.insert("known",false);
        else {
            bool valid=true;
            for (auto *p:use.to) {
                if (!p || !roster.contains(p)) {valid=false;break;}
                targets.append(p->objectName());
            }
            if (!valid) row.insert("known",false);
            else {
                result.insert("from",use.from ? QJsonValue(use.from->objectName()):QJsonValue(QJsonValue::Null));
                result.insert("to",targets);
                if (use.card) {
                    auto face=publicCard(use.card);
                    if (face.isEmpty()) row.insert("known",false);
                    else {
                        // E's only cost query is subcardsLength. Count and
                        // virtuality suffice; the public descriptor deliberately
                        // does not disclose underlying virtual payment IDs.
                        face.insert("virtual_card",use.card->isVirtualCard());
                        face.insert("subcard_count",use.card->subcardsLength());
                        result.insert("card",face);
                    }
                }
                row.insert("value",result);
            }
        }
        rows.insert(key,row);
    };
    if (question(AIRequest::PlayerChosen,"ny_sankuang"))
        useTag("ny_sankuang_use",viewer->getTag("ny_sankuang_use")); // olClan.lua1639–1641
    if (question(AIRequest::PlayersChosen,"zuheta"))
        useTag("zuheta_data",viewer->getTag("zuheta_data")); // 3308–3309
    if (question(AIRequest::SkillInvoke,"kezumingjie")) {
        // Only the actual 3641 string recipient. No newer/private tag substitute.
        const QString input=data.toString();
        if (input.startsWith("kezumingjie0:") && input.split(':').size()==2) {
            bool found=false;
            for (auto *p:roster) if (p->objectName()==input.section(':',1,1)) found=true;
            if (found) { rows.insert("invoke_string",QJsonObject{{"known",true},{"value",input}});
                useTag("kezumingjieData",room.getTag("kezumingjieData")); }
        }
    }
    if (question(AIRequest::SkillInvoke,"kezushangshen")) playerTag("fenchaiPlayer"); // own tag1959/1976
    if (question(AIRequest::Discard,"kezulianhe")) playerTag("kezulianheFrom"); // Exchange3103–3104
    if (question(AIRequest::PlayerChosen,"kezufangzhen"))
        rows.insert("TurnLengthCount",QJsonObject{{"known",true},{"value",room.getTag("TurnLengthCount").toInt()}});
    // Room::notifyMoveToPile writes each ForAI list and viewer-only disclosure.
    // Preserve its order and duplicates. A tag alone is NOT permission to resolve
    // Engine::getCard(id); missing disclosed face leaves the row unknown.
    const auto pile=[&](const QString &name) {
        const QVariant value=viewer->getTag(name+"ForAI");
        // Room stores the ledger under the fake-pile name (#reason),
        // while the owner tag remains reason+ForAI.
        const auto disclosed=readAIStrategyNotifiedPile(room,viewer,"#"+name).value("cards").toObject();
        QJsonArray faces;bool known=true;
        for (const QVariant &raw:value.toList()) {
            const QString id=QString::number(raw.toInt());
            if (!disclosed.value(id).isObject()) {known=false;break;}
            faces.append(disclosed.value(id));
        }
        rows.insert(name+"ForAI",QJsonObject{{"known",known},{"present",value.isValid()},{"value",faces}});
    };
    if (request.kind==AIRequest::UseCard && request.pattern=="@@zujieli") {pile("zujieli");pile("zujieliN");}
    if (request.kind==AIRequest::UseCard && request.pattern=="@@zugezhi") pile("zugezhi");
    if (!rows.isEmpty()) context.insert("olclan_observations",rows);
}

int aiOlClanObservationPrototype(const AIRequest &request, const ServerPlayer *viewer,
    const QString &skill, const QString &prototype)
{
    if (!viewer || !viewer->getRoom() || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=viewer->getRoom()->roomRuntime()->stateRevision()
        || request.kind!=AIRequest::UseCard
        || request.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || !viewer->hasSkill(skill)) return -1;
    // Actual trigger recipients own each view-as skill (olClan.lua710/6555).
    // Only these exact questions, not the native VS's broader prefix allowance.
    if (skill=="zujieli" && prototype=="#zujieliCard" && request.pattern=="@@zujieli") return 1;
    if (skill=="zugezhi" && prototype=="#zugezhiCard" && request.pattern=="@@zugezhi") return 1;
    return -1;
}

// ---- olclan-plan ----
#include "engine.h"
#include <QHash>
#include <QPointer>
#include <QUuid>
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
bool aiOlClanConstruction(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s,QString*error){
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
bool aiOlClanRepeatedMaterials(const QJsonObject&s){return s.value("source_key")=="zuchengqi";}
bool aiOlClanForeignMaterial(const QJsonObject&s){return s.value("source_key")=="zujuetu";}
int aiOlClanExtraTarget(const QJsonObject&s){return s.value("source_key")=="kezuqiuxin"?99:0;}
int aiOlClanChengqiPrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){return current(r,p)&&play(r)&&p->hasSkill("zuchengqi")&&skill=="zuchengqi"&&prototype=="#zuchengqiCard"?1:-1;}
void projectAIOlClanPlanContext(AIRequest&r,Room&room,const ServerPlayer*p){
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
QString captureAIOlClanObservation(const AIRequest &r, ServerPlayer *p,
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
const Card *restoreAIOlClanObservation(const AIRequest &r, ServerPlayer *p,
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
void clearAIOlClanObservations(Room &room) { olClanObservations.remove(&room); }

// ---- olclan-planning-slots ----
#include "ai-strategy/lua-seal.h"
#include "ai-strategy/runtime.h"
#include "ai-strategy/inovation-ai.h"
#include "wrapped-card.h"
#include "card-lifetime-manager.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <memory>
#include <cstring>
#include "ai-strategy/lua-raw-inspect.h"
namespace {
struct Stack{lua_State*L;int n;explicit Stack(lua_State*s):L(s),n(lua_gettop(s)){}~Stack(){lua_settop(L,n);}};
struct Row {QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;int observedWrappedId=-1;};
class Ledger final:public QObject {public:explicit Ledger(Room&r):QObject(&r){} quint64 generation=0;QHash<const ServerPlayer*,QHash<QString,Row>>rows;};
Ledger*ledger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return create?new Ledger(r):nullptr;}
bool olclanPlanningSlotsCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool sourceScope(const AIRequest&q,const ServerPlayer*p){
 auto exact=[&](const AiSkillActionContext&a){return a.isValid()&&a.activationRef==a.sourceRef
 &&a.getActivationOwner()==p->objectName()&&a.getActivationSkillName()=="zuchengqi";};
 if(q.hasSkillActionContext&&exact(q.skillActionContext))return true;
 for(const auto&a:q.skillActions)if(exact(a))return true;return false;
}
bool scope(const AIRequest&q,ServerPlayer*p,const QString&slot,bool write){
 if(slot=="kezumingjieUse"||slot=="kezulilunUse"){
  if(write)return q.kind==AIRequest::AmazingGrace&&q.choiceOptions.reason==(slot=="kezumingjieUse"?"kezumingjie":"kezulilun");
  return q.kind==AIRequest::UseCard&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
   &&q.pattern==(slot=="kezumingjieUse"?"@@kezumingjiemark":"@@kezulilunuse");
 }
 if(slot!="kezufuxunUse"&&slot!="zuchengqiUse")return false;
 if(!write)return q.kind==AIRequest::UseCard&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  &&q.pattern==(slot=="kezufuxunUse"?"@@kezufuxunbasic":"@@zuchengqi");
 if(slot=="kezufuxunUse")return q.kind==AIRequest::AmazingGrace&&q.choiceOptions.reason=="kezufuxun"&&p->hasSkill("kezufuxun");
 if(aiOlClanPlanningQuestion(q,*p->getRoom(),p))return p->hasSkill("zuchengqi");
 return q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&sourceScope(q,p);
}
const Card*equippedMaterial(const AIRequest&q,ServerPlayer*p,int id){
 Room&r=*p->getRoom();const auto*owner=r.getCardOwner(id);
 if(!owner||r.getCardPlace(id)!=Player::PlaceEquip)return nullptr;
 const Card*outer=Sanguosha->getCard(id);if(!outer)return nullptr;
 const Card*inner=nullptr;for(const Card*c:owner->getEquips())if(c->getEffectiveId()==id)inner=c;
 if(!inner||outer->getRealCard()!=inner)return nullptr;
 auto players=q.worldView.players;players.prepend(q.worldView.self);
 for(const auto&v:players)if(v.objectName==owner->objectName())for(const auto&e:v.equips){
  if(e.effectiveId!=id||!e.nativeIdentity||!globalCardLifetimeManager().isLive(e.nativeIdentity)
   ||e.nativeIdentity->address!=inner)continue;
  if(owner==p)return outer; // Current owner knows its wrapped equipment face.
  // Foreign public inner equipment alone does not disclose a private transform.
  if(e.cardId==outer->getId()&&e.objectName==outer->objectName()&&e.className==outer->getClassName()
   &&e.suit==int(outer->getSuit())&&e.number==outer->getNumber()&&e.skillName==outer->getSkillName(false)
   &&e.subcardIds==outer->getSubcards()&&e.virtualCard==outer->isVirtualCard())return outer;
 }
 return nullptr;
}
bool olclanPlanningSlotsShown(const AIRequest&q,ServerPlayer*p,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact){
  if(p&&equippedMaterial(q,p,id))return true;
  exact=Sanguosha->getCard(id);
 }if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void field(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool self(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool absentNewIndex(lua_State*L,int t){Stack st(L);if(!lua_getmetatable(L,t))return true;field(L,-1,"__newindex");return lua_isnil(L,-1);}
bool plain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool env(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool code(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&env(L,f);}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool lookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));field(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  field(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool sourceReadBindings(Room&r,lua_State*L,const QString&slot){
 Stack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 field(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 field(L,sgs,"ai_skill_use");if(!plain(L,-1))return false;
 const char*pattern=slot=="kezufuxunUse"?"@@kezufuxunbasic":slot=="zuchengqiUse"?"@@zuchengqi":slot=="kezumingjieUse"?"@@kezumingjiemark":"@@kezulilunuse";
 const int first=slot=="kezufuxunUse"?985:slot=="zuchengqiUse"?1266:slot=="kezumingjieUse"?852:1078;
 const int last=slot=="kezufuxunUse"?994:slot=="zuchengqiUse"?1275:slot=="kezumingjieUse"?861:1087;
 field(L,-1,pattern);
 if(!code(r,L,-1,"lua/ai/olClan-ai.lua",first,last))return false;
 field(L,sgs,"list");if(!code(r,L,-1,"lua/utilities.lua",38,41))return false;
 field(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=code(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=code(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){field(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 field(L,globals,"table");if(!plain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){field(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Only this native thunk allocates game-VM values. LuaAI policy is never called.
struct Write {const AIRequest*q;ServerPlayer*owner;QByteArray key;const Card*card=nullptr;
 AIReadonlyCardLease lease;QList<ServerPlayer*>targets;int extra=0;};
int assign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"saved plan assignment stack unavailable");
 auto*w=static_cast<Write*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_absindex(L,-1);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAISlotCard(L,w->card))return luaL_error(L,"saved card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"saved target list unavailable");lua_setfield(L,d,"to");
 const int value=lua_absindex(L,-1);
 if(!olclanPlanningSlotsCurrent(*w->q,w->owner)||w->lease()!=w->card
  ||!self(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))return luaL_error(L,"saved plan actual receiver changed during allocation");lua_pop(L,1);
 field(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!absentNewIndex(L,1))return luaL_error(L,"saved plan assignment has dynamic __newindex");
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());
 return 0;
}
bool target(Room&r,lua_State*L,int index,ServerPlayer*&out){
 for(auto*p:r.getAllPlayers(true))if(matchAISlotPlayer(L,index,p)){
  if(!matchAINativeObjectMethods(r,L,index,"ServerPlayer",{"objectName"}))return false;out=p;return true;
 }return false;
}
}
bool registerAIOlClanPlanSlotSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/olClan-ai.lua",QByteArray("462bc693fba2c69567b179c6f617ae5b9d97c1d5472d4ed816408a45d95215da"),error)
  &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAIOlClanPlanSlots(Room&r){if(auto*l=ledger(r))delete l;}
QJsonObject queryAIOlClanPlanSlot(const AIRequest&q,ServerPlayer*p,const QString&slot,const QString&op,const QJsonObject&payload,AIReadonlyCardLease lease){
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","olClan.saved_plan"}};};
 if(op!="write"&&op!="read")return fail("unknown saved-plan operation");const bool writing=op=="write";
 if(!olclanPlanningSlotsCurrent(q,p)||!scope(q,p,slot,writing))return fail("saved plan requires exact current owner source inquiry");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("saved plan game runtime unavailable");Stack stack(L);
 if(!self(L,r,p))return fail("saved plan actual LuaAI self unavailable");const int receiver=lua_absindex(L,-1);
 const QByteArray key=slot.toUtf8();
 if(writing){
  Write w;w.q=&q;w.owner=p;w.key=key;
  if(payload.size()!=3||!payload.value("selector").isObject()||!payload.value("targets").isArray()||!payload.value("source_fields").isObject()||!lease)return fail("saved source plan shape malformed");
  const auto f=payload.value("source_fields").toObject();
  if(f.size()!=3||f.value("isDummy")!=QJsonValue(true)||f.value("current_targets_empty")!=QJsonValue(true)
   ||!f.value("extra_target").isDouble()||f.value("extra_target").toDouble()!=f.value("extra_target").toInt())return fail("saved dummy fields malformed");
   w.extra=f.value("extra_target").toInt();w.lease=std::move(lease);w.card=w.lease();if(!w.card)return fail("saved source card custody expired");
   if(!w.card->isVirtualCard()&&!olclanPlanningSlotsShown(q,p,w.card->getEffectiveId(),w.card))return fail("saved physical source card not currently disclosed");
   for(int id:w.card->getSubcards())if(!olclanPlanningSlotsShown(q,p,id))return fail("saved source material not currently disclosed");
   if(payload.value("targets").toArray().size()>128)return fail("saved target budget exceeded");
   for(const auto&n:payload.value("targets").toArray()){
    if(!n.isString())return fail("saved target is not a name");ServerPlayer*found=nullptr;
    for(auto*t:r.getAllPlayers(true))if(t->objectName()==n.toString())found=t;
    if(!found)return fail("saved target outside roster");w.targets<<found;
   }
  // Stack function and arguments do not allocate a captured C closure.
  lua_pushcfunction(L,assign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("exact native saved-plan assignment failed");
  // Reached assignment is not rolled back on errors.
  if(!olclanPlanningSlotsCurrent(q,p)||w.lease()!=w.card)return fail("saved-plan assignment changed request or custody");
  {auto*l=ledger(r,true);if(l->generation&&l->generation!=rt->lua().generation())return fail("saved plan game generation changed");
   const bool marked=slot=="kezumingjieUse"||slot=="kezulilunUse";
   const auto*wrapped=dynamic_cast<const WrappedCard*>(w.card);
   const int id=marked&&wrapped&&w.card->getId()>=0&&w.card==Sanguosha->getCard(w.card->getId())?w.card->getId():-1;
   l->generation=rt->lua().generation();l->rows[p][slot]=Row{p,std::move(w.lease),id};}
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("saved read accepts no descriptor or prior authority");
 if(!lookup(L,receiver,key.constData()))return fail("saved slot requires unproved dynamic getter");const int saved=lua_absindex(L,-1);
 if(lua_isnil(L,saved)||lua_isboolean(L,saved)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,saved)))return fail("saved primitive index metamethod unproved");
  return{{"source_error",QString("attempt to index a %1 value (saved source plan)").arg(lua_isnil(L,saved)?"nil":"boolean")}};
 }
 if(!lua_istable(L,saved)||!lookup(L,saved,"card"))return fail("saved card field unavailable");const int cardIndex=lua_absindex(L,-1);
 if(!lua_toboolean(L,cardIndex))return{{"status","pass"}};
 if(!lookup(L,saved,"to"))return fail("saved targets require unproved getter");const int list=lua_absindex(L,-1);
 if(!lua_toboolean(L,list))return{{"status","pass"}}; // Source short-circuits card AND to.
 if(!sourceReadBindings(r,L,slot))return fail("saved consumer serializer/iterator bindings changed");
 // Source iterates targets before calling card:toString. Preserve that order.
 QList<ServerPlayer*>targets;
 if(lua_istable(L,list)){
  if(!plain(L,list))return fail("saved plain target list has unproved metatable");
  for(int n=1;n<=129;++n){lua_rawgeti(L,list,n);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
   if(n==129)return fail("saved target budget exceeded");ServerPlayer*t=nullptr;
   if(!target(r,L,-1,t))return fail("saved table target identity or method changed");targets<<t;lua_pop(L,1);
  }
 }else if(!readAISlotTargets(L,list,targets))return fail("saved native target list custody or dispatch changed");
 auto*l=ledger(r);if(!l||l->generation!=rt->lua().generation())return fail("saved actual Card custody unavailable");
 const auto row=l->rows.value(p).value(slot);const Card*c=row.lease?row.lease():nullptr;
 if(!c||row.owner!=p||!matchAISlotCard(L,cardIndex,c)
  ||!matchAINativeObjectMethods(r,L,cardIndex,"Card",{"toString"}))return fail("saved actual Card identity or native serializer changed");
 // New AG readers only call WrappedCard::toString (wrapped-card.h195),
 // which reads its previously observed m_id and no current face/materials.
 // Preserve the same live engine wrapper and ID; do not disclose new faces.
 const bool knownPhysicalId=(slot=="kezumingjieUse"||slot=="kezulilunUse")&&row.observedWrappedId>=0
  &&dynamic_cast<const WrappedCard*>(c)&&c->getId()==row.observedWrappedId&&Sanguosha->getCard(row.observedWrappedId)==c;
 if(!knownPhysicalId){
  if(!c->isVirtualCard()&&!olclanPlanningSlotsShown(q,p,c->getEffectiveId(),c))return fail("saved physical face no longer disclosed");
  for(int id:c->getSubcards())if(!olclanPlanningSlotsShown(q,p,id))return fail("saved current material printing not lawfully observable");
 }
 QJsonArray names;const auto roster=r.getAllPlayers(true);for(auto*t:targets){if(!t||!roster.contains(t))return fail("saved target left roster");names.append(t->objectName());}
 const QString wire=c->toString();
 if(!olclanPlanningSlotsCurrent(q,p)||row.lease()!=c)return fail("saved observation changed request or custody");
 return{{"status","plan"},{"wire",wire},{"targets",names},{"evaluation_only",true}};
}

// ---- olclan-planning ----
bool aiOlClanPlanningQuestion(const AIRequest &request, Room &room,
                                    const ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || request.kind != AIRequest::SkillInvoke
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || !aiRangeRequestCurrent(room,request,viewer)) return false;
    // The Room service can normalize the authored '$-1' suffix before inquiry.
    const QString &question = request.choiceOptions.reason;
    if (question != QStringLiteral("zuheta") && question != QStringLiteral("zuheta$-1")) return false;
    return viewer->isAlive() && viewer->hasSkill(QStringLiteral("zuheta"))
        && viewer->getPhase() == Player::Play && !viewer->isChained();
}

QJsonObject aiOlClanPlanningContext(const AIRequest &request, Room &room,
                                          const ServerPlayer *viewer)
{
    if (!aiOlClanPlanningQuestion(request, room, viewer)) return {};
    return {{"known", true}, {"readonly", true}, {"source", "olClan.zuheta"},
        {"viewer", request.viewerObjectName}, {"decision_id", request.getDecisionId()},
        {"state_revision", request.getStateRevision()}, {"kind", "activate"},
        {"reason", int(CardUseStruct::CARD_USE_REASON_PLAY)}, {"pattern", ""},
        {"handling_method", int(Card::MethodUse)}};
}

// ---- olclan-qiuxin ----
bool aiQiuxinSource(const AIRequest &request, const ServerPlayer *viewer,
                          const Card *card, const QString &sourceKey, QString *error)
{
    const auto reject = [&](const char *why) {
        if (error) *error = QString::fromLatin1(why);
        return false;
    };
    if (sourceKey != "kezuqiuxin" || !card || card->getId() >= 0
        || card->getSkillName(false) != "_kezuqiuxin" || !card->getSubcards().isEmpty()
        || card->getSuit() != Card::NoSuit || card->getNumber() != 0)
        return reject("Qiuxin profile requires its original issued source clone");
    // Reuse the exact finite producer admission, including the current AG
    // question, current owner skill, offered public name and request revision.
    const QJsonObject spec{{"source_key", sourceKey}, {"name", card->objectName()},
        {"skill", card->getSkillName(false)}, {"suit", int(Card::NoSuit)},
        {"number", 0}, {"subcards", QJsonArray{}}};
    return aiOlClanConstruction(request, viewer, spec, error);
}

QJsonObject aiQiuxinTargetFilter(Room &room, ServerPlayer *viewer,
                                      const AIRequest &request, const Card *card,
                                      const QString &sourceKey, const QJsonObject &query)
{
    const auto reject = [](const QString &why) {
        return QJsonObject{{"error", why}, {"key", "qiuxin_target_query"}};
    };
    QString error;
    if (!viewer || viewer->getRoom() != &room
        || !aiQiuxinSource(request, viewer, card, sourceKey, &error))
        return reject(error.isEmpty() ? QStringLiteral("Qiuxin owner is absent") : error);
    if (query.size() != 2 || !query.value("prefix").isArray() || !query.value("target").isString())
        return reject("Qiuxin target query fields are malformed");
    const auto names = query.value("prefix").toArray();
    if (names.size() > room.getAlivePlayers().size()) return reject("Qiuxin prefix exceeds current roster");
    QList<const Player *> prefix;
    QSet<QString> seen;
    for (const auto &name : names) {
        if (!name.isString() || seen.contains(name.toString())) return reject("Qiuxin prefix is not a distinct roster list");
        const auto *target = room.findPlayerByObjectName(name.toString());
        if (!target || !target->isAlive()) return reject("Qiuxin prefix contains an absent player");
        seen.insert(name.toString()); prefix << target;
    }
    const auto *target = room.findPlayerByObjectName(query.value("target").toString());
    if (!target || !target->isAlive()) return reject("Qiuxin next target is absent");
    const auto revision = room.roomRuntime()->stateRevision();
    // Native targetFilter sees precisely the donor dummy(...,99) overlay;
    // this does not change the card, the live player, or an outer range flag.
    AITianyiQueryScope extra(viewer, card, 99, false);
    const bool accepted = card->targetFilter(prefix, target, viewer);
    if (room.roomRuntime()->stateRevision() != revision) return reject("Qiuxin target query changed state");
    return {{"accepted", accepted}};
}

// ---- olclan-scalar ----
#include "ai-strategy/diy.h"
namespace {
struct OlclanScalarStack{lua_State*L;int n;explicit OlclanScalarStack(lua_State*s):L(s),n(lua_gettop(s)){}~OlclanScalarStack(){lua_settop(L,n);}};
bool olclanScalarCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
void olclanScalarField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool olclanScalarLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));olclanScalarField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  olclanScalarField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
struct OlclanScalarWrite{const AIRequest*q;ServerPlayer*owner;QByteArray value;};
int olclanScalarAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"Zujiewu assignment stack unavailable");
 auto*w=static_cast<OlclanScalarWrite*>(lua_touserdata(L,2));
 lua_pushlstring(L,w->value.constData(),w->value.size());const int value=lua_absindex(L,-1);
 if(!olclanScalarCurrent(*w->q,w->owner)||!pushAIActualAISelf(*w->owner->getRoom(),w->owner,L)
  ||!lua_rawequal(L,1,-1))return luaL_error(L,"Zujiewu actual receiver changed during allocation");lua_pop(L,1);
 olclanScalarField(L,1,"zujiewusuit");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&lua_getmetatable(L,1)){
  olclanScalarField(L,-1,"__newindex");if(!lua_isnil(L,-1))return luaL_error(L,"Zujiewu assignment has dynamic __newindex");lua_pop(L,2);
 }
 lua_pushvalue(L,value);lua_setfield(L,1,"zujiewusuit");return 0;
}
}
void projectAIOlClanZujiewuSuit(const AIRequest&q,Room&r,const ServerPlayer*p,const QVariant&data,QJsonObject&context){
 if(!p||q.viewerObjectName!=p->objectName()||q.kind!=AIRequest::SkillInvoke||q.choiceOptions.reason!="zujiewu"
  ||!data.canConvert<CardUseStruct>())return;
 const auto use=data.value<CardUseStruct>();
 // Fixed TargetSpecified caller uses its owner's actual played Card. A raw tag,
 // public ID alone, foreign actor or pre-use-only frame is not this witness.
 if(!use.card||use.from!=p||!aiPlanningPublicCardWitness(q,r,p,use.card))return;
 context.insert("olclan_zujiewu_suit",use.card->getSuitString());
}
QJsonObject queryAIOlClanScalarSlot(const AIRequest&q,ServerPlayer*p,const QString&op,const QJsonObject&payload){
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","olClan.zujiewusuit"}};};
 if(op!="read"&&op!="write")return fail("unknown source scalar operation");const bool writing=op=="write";
 const bool invoke=q.kind==AIRequest::SkillInvoke&&q.choiceOptions.reason=="zujiewu";
 const bool chosen=q.kind==AIRequest::CardChosen&&q.choiceOptions.reason=="zujiewu";
 if(!olclanScalarCurrent(q,p)||!(invoke||(!writing&&chosen)))return fail("Zujiewu requires exact current own source inquiry");
 Room&r=*p->getRoom();LuaRuntime::Binding binding(r.roomRuntime()->lua(),false);auto*L=r.roomRuntime()->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Zujiewu game runtime unavailable");OlclanScalarStack stack(L);
 if(!pushAIActualAISelf(r,p,L))return fail("actual own LuaAI receiver unavailable");const int receiver=lua_absindex(L,-1);
 if(writing){
  const auto actual=q.choiceOptions.context.value("olclan_zujiewu_suit");
  if(payload.size()!=1||!payload.value("value").isString()||!actual.isString()||payload.value("value")!=actual)
   return fail("Zujiewu value is not the current public-use source suit");
  OlclanScalarWrite w{&q,p,actual.toString().toUtf8()};lua_pushcfunction(L,olclanScalarAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("actual Zujiewu scalar assignment failed");
  if(!olclanScalarCurrent(q,p))return fail("Zujiewu assignment changed request");
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("Zujiewu read cannot supply remembered suit");
 if(!olclanScalarLookup(L,receiver,"zujiewusuit"))return fail("Zujiewu slot has unproved getter");
 if(lua_isnil(L,-1))return{{"status","absent"}};
 if(lua_type(L,-1)==LUA_TSTRING){size_t size=0;const char*value=lua_tolstring(L,-1,&size);if(size>4096)return fail("source scalar budget exceeded");return{{"status","value"},{"value",QString::fromUtf8(value,int(size))}};}
 // Fixed source compares against a suit string. Primitive types are preserved,
 // never converted to strings. No hidden userdata/table is serialized.
 if(lua_type(L,-1)==LUA_TBOOLEAN)return{{"status","value"},{"value",bool(lua_toboolean(L,-1))}};
 if(lua_type(L,-1)==LUA_TNUMBER)return{{"status","value"},{"value",lua_tonumber(L,-1)}};
 return fail("source scalar is not an observable primitive");
}

// ---- olclan-values ----
namespace {
struct OlclanValuesStack{lua_State*L;int n;explicit OlclanValuesStack(lua_State*s):L(s),n(lua_gettop(s)){}~OlclanValuesStack(){lua_settop(L,n);}};
bool olclanValuesCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&!p->getRoom()->roomRuntime()->isClosing()&&q.viewerObjectName==p->objectName()
 &&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(*p->getRoom(),q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool active(const AIRequest&q,ServerPlayer*p){
 auto exact=[&](const AiSkillActionContext&a){return a.isValid()&&a.activationRef==a.sourceRef
 &&a.getActivationOwner()==p->objectName()&&a.getActivationSkillName()=="zulianzhuvs";};
 if(q.hasSkillActionContext&&exact(q.skillActionContext))return true;
 for(const auto&a:q.skillActions)if(exact(a))return true;return false;
}
bool olclanValuesScope(const AIRequest&q,ServerPlayer*p,const QString&slot,bool write){
 if(slot=="zujieliTo")return write?(q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="zujieli")
  :(q.kind==AIRequest::UseCard&&q.pattern=="@@zujieli"&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
 if(slot!="zulianzhuEp")return false;
 if(!write)return q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="zulianzhu";
 if(aiOlClanPlanningQuestion(q,*p->getRoom(),p))return p->hasSkill("zulianzhuvs");
 return q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY&&q.pattern.isEmpty()&&active(q,p);
}
void olclanValuesField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool olclanValuesLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));olclanValuesField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  olclanValuesField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
struct OlclanValuesWrite{const AIRequest*q;ServerPlayer*owner;ServerPlayer*target;QByteArray key;};
int olclanValuesAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"OlClan Player assignment stack unavailable");
 auto*w=static_cast<OlclanValuesWrite*>(lua_touserdata(L,2));
 if(!pushAISlotPlayer(L,w->target))return luaL_error(L,"OlClan Player wrapper unavailable");const int value=lua_absindex(L,-1);
 if(!olclanValuesCurrent(*w->q,w->owner)||!pushAIActualAISelf(*w->owner->getRoom(),w->owner,L)
  ||!lua_rawequal(L,1,-1))return luaL_error(L,"OlClan actual receiver changed during allocation");lua_pop(L,1);
 olclanValuesField(L,1,w->key.constData());const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&lua_getmetatable(L,1)){
  olclanValuesField(L,-1,"__newindex");if(!lua_isnil(L,-1))return luaL_error(L,"OlClan Player assignment has dynamic __newindex");lua_pop(L,2);
 }
 lua_pushvalue(L,value);lua_setfield(L,1,w->key.constData());return 0;
}
}
QJsonObject queryAIOlClanPlayerSlot(const AIRequest&q,ServerPlayer*p,const QString&slot,const QString&op,const QJsonObject&payload){
 auto fail=[](const QString&s){return QJsonObject{{"error",s},{"key","olClan.saved_player"}};};
 if(op!="read"&&op!="write")return fail("unknown source Player operation");const bool write=op=="write";
 if(!olclanValuesCurrent(q,p)||!olclanValuesScope(q,p,slot,write))return fail("Player slot requires exact current own source inquiry");
 Room&r=*p->getRoom();LuaRuntime::Binding binding(r.roomRuntime()->lua(),false);auto*L=r.roomRuntime()->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Player slot game runtime unavailable");OlclanValuesStack stack(L);
 if(!pushAIActualAISelf(r,p,L))return fail("actual own LuaAI receiver unavailable");const int receiver=lua_absindex(L,-1);
 const auto key=slot.toUtf8();
 if(write){
  if(payload.size()!=1||!payload.value("target").isString())return fail("source Player assignment shape malformed");
  const auto name=payload.value("target").toString();ServerPlayer*target=nullptr;
  for(auto*t:r.getAllPlayers(true))if(t->objectName()==name)target=t;
  bool shown=q.worldView.self.objectName==name;for(const auto&v:q.worldView.players)shown=shown||v.objectName==name;
  if(!target||!shown)return fail("source target is not a currently projected roster Player");
  if(slot=="zujieliTo"&&!q.choiceOptions.playerNames.contains(name))return fail("Zujieli source loop target not offered");
  OlclanValuesWrite w{&q,p,target,key};lua_pushcfunction(L,olclanValuesAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
  if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail("actual source Player assignment failed");
  if(!olclanValuesCurrent(q,p))return fail("source Player assignment changed request");
  return{{"written",true}};
 }
 if(!payload.isEmpty())return fail("source Player read cannot supply remembered target");
 if(!olclanValuesLookup(L,receiver,key.constData()))return fail("source Player slot has unproved getter");
 if(!lua_toboolean(L,-1))return{{"status","absent"}};
 ServerPlayer*target=nullptr;for(auto*t:r.getAllPlayers(true))if(matchAISlotPlayer(L,-1,t))target=t;
 if(!target)return fail("source Player slot is not an exact live roster Player");
 QStringList methods{"objectName"};if(slot=="zulianzhuEp")methods<<"canSlash";
 if(!matchAINativeObjectMethods(r,L,-1,"ServerPlayer",methods))return fail("source Player dispatch changed");
 if(!olclanValuesCurrent(q,p))return fail("source Player read changed request");
 return{{"status","player"},{"target",target->objectName()}};
}

// ---- olclan ----
namespace {
QString notifiedPile(const QString &pattern)
{
    // These exact donor callbacks evaluate already disclosed physical cards.
    // The two *Ids tags are written from the same ordered list immediately
    // before notifyMoveToPile, which also writes the corresponding ForAI tag.
    if (pattern == "@@zuguangu") return "zuguangu";
    if (pattern == "@@zukaiji") return "zukaiji";
    if (pattern == "@@zugaobian") return "zugaobian";
    if (pattern == "@@zugaoshi") return "zugaoshi";
    if (pattern == "@@kezuliuju") return "kezuliuju";
    if (pattern == "@@kezuhuanjia") return "kezuhuanjia";
    if (pattern == "@@zugezhi") return "zugezhi";
    return {};
}
bool physicalId(const QJsonValue &value, int *out)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > std::numeric_limits<int>::max()
        || std::floor(number) != number) return false;
    *out = int(number); return true;
}
}

bool authorizeAIOlClanNotifiedEvaluation(const AIRequest &request, int cardId,
    QJsonObject *disclosedCard, QString *error)
{
    if (disclosedCard) *disclosedCard = {};
    const auto reject = [error](const char *message) {
        if (error) *error = QString::fromLatin1(message);
        return false;
    };
    QString pile = notifiedPile(request.pattern);
    const bool jieli = request.pattern == "@@zujieli";
    if (jieli) {
        // Both source lists are notified to this exact actor. Select a list
        // by its disclosed ID only; the complete/current checks below remain.
        const auto piles = request.worldView.strategyFacts.value("notified_piles").toObject();
        for (const QString &name : {QStringLiteral("zujieli"), QStringLiteral("zujieliN")})
            if (piles.value('#' + name).toObject().value("card_ids").toArray().contains(cardId)) {
                if (!pile.isEmpty()) return reject("notified card occurs in both source lists");
                pile = name;
            }
    }
    if (request.kind != AIRequest::UseCard
        || request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || request.handlingMethod != (jieli ? Card::MethodNone : Card::MethodUse) || pile.isEmpty() || cardId < 0
        || request.decisionId == 0 || request.viewerObjectName.isEmpty()
        || request.viewerObjectName != request.worldView.self.objectName
        || request.stateRevision != request.worldView.revision)
        return reject("notified evaluation requires its exact active viewer request");
    const QJsonValue piles = request.worldView.strategyFacts.value("notified_piles");
    if (!piles.isObject()) return reject("notified pile disclosure is absent");
    const QJsonValue raw = piles.toObject().value('#' + pile);
    if (!raw.isObject()) return reject("notified pile disclosure is absent");
    const QJsonObject row = raw.toObject();
    if (row.value("complete") != QJsonValue(true)
        || row.value("tag_key") != QJsonValue(pile + "ForAI")
        || !row.value("card_ids").isArray() || !row.value("current_card_ids").isArray()
        || !row.value("cards").isArray())
        return reject("notified pile disclosure is incomplete");
    const QJsonArray ids = row.value("card_ids").toArray();
    const QJsonArray cards = row.value("cards").toArray();
    if (ids.size() != cards.size()) return reject("notified faces do not match their ordered ids");
    QSet<int> disclosedIds;
    QJsonObject selected;
    for (qsizetype i = 0; i < ids.size(); ++i) {
        int id = -1, faceId = -1, effectiveId = -1;
        if (!physicalId(ids.at(i), &id) || disclosedIds.contains(id) || !cards.at(i).isObject())
            return reject("notified ids are malformed or duplicated");
        const QJsonObject card = cards.at(i).toObject();
        if (!physicalId(card.value("id"), &faceId) || !physicalId(card.value("effective_id"), &effectiveId)
            || faceId != id || effectiveId != id || card.value("name").toString().isEmpty()
            || card.value("class_name").toString().isEmpty())
            return reject("notified card face is not the disclosed physical card");
        disclosedIds.insert(id);
        if (id == cardId) selected = card;
    }
    QSet<int> currentIds;
    int previous = -1;
    for (const QJsonValue &value : row.value("current_card_ids").toArray()) {
        int id = -1;
        if (!physicalId(value, &id) || !disclosedIds.contains(id) || currentIds.contains(id))
            return reject("current notified ids are not a disclosed subset");
        // Projection emits this subset in source tag order; do not authorize a
        // forged reorder as the source's first-card or stable-sort input.
        qsizetype index = 0;
        while (index < ids.size() && ids.at(index) != QJsonValue(id)) ++index;
        if (index <= previous) return reject("current notified ids changed source order");
        previous = int(index); currentIds.insert(id);
    }
    if (selected.isEmpty() || !currentIds.contains(cardId))
        return reject("notified card is remembered but not currently disclosed");
    if (disclosedCard) *disclosedCard = selected;
    return true;
}


bool authorizeAIOlClanMarkedPhysicalProduct(const AIRequest &request, int productId,
    QJsonObject *disclosedCard, bool *printedProduct, QString *error)
{
    if (disclosedCard) *disclosedCard = {};
    if (printedProduct) *printedProduct = false;
    const auto reject = [error](const char *message) {
        if (error) *error = QString::fromLatin1(message);
        return false;
    };
    QString skill, mark;
    bool printed = false;
    if (request.pattern == QStringLiteral("@@kezumingjiemark")) {
        skill = QStringLiteral("kezumingjiemark"); mark = QStringLiteral("kezumingjie-PlayClear"); printed = true;
    } else if (request.pattern == QStringLiteral("@@kezulilunuse")) {
        skill = QStringLiteral("kezulilunuse"); mark = QStringLiteral("kezulilunuse");
    } else return reject("marked physical product pattern is not audited");
    if (request.kind != AIRequest::UseCard || request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || request.handlingMethod != Card::MethodUse || request.decisionId == 0 || productId < 0
        || request.viewerObjectName.isEmpty() || request.viewerObjectName != request.worldView.self.objectName
        || request.stateRevision != request.worldView.revision)
        return reject("marked physical product requires its active owner request");
    const auto &marks = request.worldView.self.publicMarks;
    const auto selected = marks.constFind(mark);
    if (selected == marks.constEnd() || selected.value() != productId)
        return reject("marked physical product is not the disclosed owner selection");
    const auto matching = [&](const AiSkillActionContext &action) {
        return action.isValid() && action.getActivationOwner() == request.viewerObjectName
            && action.getActivationSkillName() == skill
            && action.activationQuotaAvailable && action.sourceQuotaAvailable;
    };
    bool scope = request.hasSkillActionContext && matching(request.skillActionContext);
    for (const auto &action : request.skillActions) if (matching(action)) { scope = true; break; }
    if (!scope) return reject("marked physical product has no authorized native skill scope");
    const AICardView *face = nullptr;
    const auto find = [&](const QList<AICardView> &cards) {
        for (const auto &card : cards) {
            if (card.cardId == productId && card.effectiveId == productId && !card.virtualCard
                && !card.objectName.isEmpty() && !card.className.isEmpty()) { face = &card; return; }
        }
    };
    // Lists below are already viewer-sanitized snapshot descriptors. A card ID
    // in a count-only/closed pile, a mark alone or old AG memory cannot pass.
    find(request.worldView.handCards);
    if (!face) find(request.worldView.discardPile);
    const auto findPlayer = [&](const AIPlayerView &player) {
        if (!face) find(player.equips);
        if (!face) find(player.judgingArea);
        if (!face) find(player.knownCards);
        for (const auto &pile : player.piles) if (!face) find(pile.cards);
    };
    findPlayer(request.worldView.self);
    for (const auto &player : request.worldView.players) if (!face) findPlayer(player);
    if (!face) return reject("marked physical product has no currently disclosed face");
    if (disclosedCard) *disclosedCard = QJsonObject{{"id", productId}, {"effective_id", productId},
        {"name", face->objectName}, {"class_name", face->className},
        {"suit", face->suit}, {"number", face->number}, {"virtual_card", false}};
    if (printedProduct) *printedProduct = printed;
    if (error) error->clear();
    return true;
}

// ---- olclan-jie-package-planning ----
namespace {
bool ownerRequest(const AIRequest &request, const ServerPlayer *viewer)
{
    return viewer && viewer->getRoom() && request.viewerObjectName == viewer->objectName()
        && request.stateRevision == viewer->getRoom()->roomRuntime()->stateRevision();
}
bool olclanJiePackagePlanningPlay(const AIRequest &request)
{
    return request.kind == AIRequest::Activate
        && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.pattern.isEmpty();
}
bool disclosed(const AIRequest &request, int id)
{
    for (const AICardView &card : request.worldView.handCards)
        if (card.effectiveId == id) return true;
    QList<AIPlayerView> disclosedPlayers = request.worldView.players;
    disclosedPlayers.prepend(request.worldView.self);
    for (const AIPlayerView &player : disclosedPlayers) {
        for (const AICardView &card : player.equips) if (card.effectiveId == id) return true;
        for (const AICardView &card : player.judgingArea) if (card.effectiveId == id) return true;
        for (const AICardView &card : player.knownCards) if (card.effectiveId == id) return true;
        for (const AICardPileView &pile : player.piles)
            for (const AICardView &card : pile.cards) if (card.effectiveId == id) return true;
    }
    return false;
}
}

bool aiOlClanJiePackagePlanningConstruction(const AIRequest &request, const ServerPlayer *viewer,
    const QJsonObject &spec, QString *error)
{
    const auto reject = [&](const char *why) { if (error) *error = QString::fromLatin1(why); return false; };
    if (!ownerRequest(request, viewer) || !olclanJiePackagePlanningPlay(request)) return reject("Round21 readonly constructor requires current owner Play");
    const QSet<QString> fields{"source_key", "name", "skill", "suit", "number", "subcards"};
    if (spec.size() != fields.size()) return reject("Round21 constructor field count differs");
    for (auto it = spec.begin(); it != spec.end(); ++it)
        if (!fields.contains(it.key())) return reject("Round21 constructor has extra field");
    if (!spec.value("source_key").isString() || !spec.value("name").isString()
        || !spec.value("skill").isString() || !spec.value("suit").isDouble()
        || !spec.value("number").isDouble() || !spec.value("subcards").isArray()
        || spec.value("suit").toDouble() != int(Card::NoSuit) || spec.value("number").toDouble() != 0)
        return reject("Round21 source requires exact NoSuit/0 constructor");
    const QString skill = spec.value("source_key").toString(), name = spec.value("name").toString();
    if (spec.value("skill").toString() != skill || !viewer->hasSkill(skill))
        return reject("Round21 raw printed skill or owner skill differs");
    const QJsonArray costs = spec.value("subcards").toArray();
    if (skill == "jinbingxin" || skill == "kezushengmo") {
        // Source jie_package185 / olClan1380: clone each PUBLIC pattern Basic
        // as a zero-material hypothesis, before choosing its declared seed.
        // Do not run canActivate here: hypotheses are not payment proposals.
        if (!costs.isEmpty() || !request.worldView.strategyFacts.value("pattern_names").toArray().contains(name))
            return reject("Round21 Basic hypothesis is not a zero-cost public pattern");
        bool basic = false;
        for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (card && card->objectName() == name && card->getTypeId() == Card::TypeBasic) { basic = true; break; }
        }
        if (!basic) return reject("Round21 declared hypothesis is not a public Basic definition");
    } else if (skill == "kezujiexuan") {
        // Source olClan722: every hypothesis keeps the NoSuit/0 constructor,
        // including the yin first-he RED branch which the native VS rejects.
        const int state = viewer->getChangeSkillState(skill);
        if (name != (state == 1 ? "snatch" : "dismantlement") || costs.size() != 1)
            return reject("Jiexuan source product or material count differs");
        const QJsonValue value = costs.first(); const double n = value.toDouble(-1);
        if (!value.isDouble() || !std::isfinite(n) || n != std::floor(n)
            || n < 0 || n > std::numeric_limits<int>::max()) return reject("Jiexuan material ID is malformed");
        const int id = int(n);
        bool sourceZone = viewer->getHandPile().contains(id);
        for (const Card *card : viewer->getCards("he")) if (card && card->getEffectiveId() == id) sourceZone = true;
        if (!sourceZone || !disclosed(request, id)) return reject("Jiexuan material is not disclosed in the source owner zones");
    } else return reject("Round21 constructor source is not audited");
    if (error) error->clear();
    return true;
}

int aiOlClanJiePackagePlanningPrototype(const AIRequest &request, const ServerPlayer *viewer,
    const QString &skill, const QString &prototype)
{
    if (!ownerRequest(request, viewer)) return -1;
    // Round21 A: fixed owner-material source constructors; live payment remains native.
    if (viewer->hasSkill(skill)) {
        if (olclanJiePackagePlanningPlay(request) && request.pattern == ""
            && skill == "kezulilun" && prototype == "#kezulilunCard") return 1;
        if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@kezuchenyacz"
            && skill == "kezuchenyacz" && prototype == "#kezuchenyaCard") return 1;
        if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@kezujianyuancz"
            && skill == "kezujianyuancz" && prototype == "#kezujianyuanCard") return 1;
        if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@zujuetu!"
            && skill == "zujuetu" && prototype == "#zujuetuCard") return 1;
        if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@zufuning"
            && skill == "zufuning" && prototype == "#zufuningCard") return 1;
        if (olclanJiePackagePlanningPlay(request) && request.pattern == ""
            && skill == "zumingdian" && prototype == "#zumingdianCard") return 1;
        if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@zumingdian"
            && skill == "zumingdian" && prototype == "#zumingdianCard") return 1;
    }
    if (olclanJiePackagePlanningPlay(request)) {
        if (skill == "jinbolan_skill" && prototype == "JinBolanSkillCard") return 0;
        if (skill == "jinbingxin" && prototype == "JinBingxinCard") return 2;
        if (skill == "kezushengmo" && prototype == "#kezushengmoCard") return 2;
        if (skill == "zukudu" && prototype == "#zukuduCard") return 1;
    }
    // Jinbingxin/ Shengmo also have actual V2 Basic-card response declarations.
    // No @@jinbingxin or @@zukudu writer exists in the fixed native producer.
    const bool response = (request.kind == AIRequest::UseCard || request.kind == AIRequest::RespondCard)
        && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    if (response && !request.pattern.startsWith("@@")) {
        if (skill == "jinbingxin" && prototype == "JinBingxinCard") return 2;
        if (skill == "kezushengmo" && prototype == "#kezushengmoCard") return 2;
    }
    return -1;
}

void projectAIJiePackagePlanningContext(AIRequest &request, Room &room, const ServerPlayer *viewer,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()) return;
    const QString reason = request.choiceOptions.reason;
    const bool canmou = request.kind == AIRequest::PlayerChosen && reason == "jincanmou";
    const bool congjian = request.kind == AIRequest::SkillInvoke && reason == "jincongjian";
    const bool guess = request.kind == AIRequest::SkillInvoke && reason == "jinxiongshu_guess";
    if (!canmou && !congjian && !guess) return;
    QJsonObject context = request.choiceOptions.context.value("jie_package").toObject();
    const auto roster = room.getAllPlayers(true);
    if (canmou || congjian) {
        const QString key = canmou ? "JincanmouData" : "JincongjianData";
        const QVariant tag = viewer->getTag(key);
        if (!tag.isValid() || tag.isNull()) context.insert(key, false);
        else if (tag.canConvert<CardUseStruct>()) {
            const CardUseStruct use = tag.value<CardUseStruct>();
            if (!use.card) context.insert(key, false);
            else if (publicCard && use.from && roster.contains(use.from)
                && !use.card->isVirtualCard() && disclosed(request, use.card->getEffectiveId())
                && use.card == Sanguosha->getCard(use.card->getEffectiveId())) {
                bool valid = true; QJsonArray targets;
                for (const ServerPlayer *p : use.to) {
                    if (!p || !roster.contains(const_cast<ServerPlayer *>(p))) { valid = false; break; }
                    targets.append(p->objectName());
                }
                const QJsonObject card = publicCard(use.card);
                if (valid && !card.isEmpty()) context.insert(key, QJsonObject{
                    {"from", use.from->objectName()}, {"to", targets}, {"card", card}});
            }
        }
    }
    if (guess) {
        // No writer remains in fixed olwenwu.cpp. A missing tag yields -1 and
        // donor nil-card failure. Never borrow the newer declared-name prompt.
        const ServerPlayer *target = room.getCurrent();
        if (target) {
            const QVariant tag = target->getTag("JinXiongshuShowCard_" + viewer->objectName());
            const int id = tag.toInt() - 1;
            if (!tag.isValid() || id < 0) context.insert("JinXiongshuShowCard", false);
            else if (disclosed(request, id) && publicCard) {
                const Card *card = Sanguosha->getCard(id);
                if (card) {
                    QJsonObject face = publicCard(card);
                    // This question historically reveals the card to this AI.
                    // Only an already disclosed card reaches the query, so no
                    // hidden ID, face or unobserved tag value is projected.
                    if (!face.isEmpty()) context.insert("JinXiongshuShowCard", face);
                }
            }
        }
    }
    request.choiceOptions.context.insert("jie_package", context);
}

// ---- olsp-response-2 ----

bool aiFuzhaiQuxiConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
    if (!p || !p->getRoom() || r.viewerObjectName!=p->objectName()
        || !aiRangeRequestCurrent(*p->getRoom(),r,p)
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
    return p->hasSkill(key) && aiRangeRequestCurrent(*p->getRoom(),r,p);
}

// ---- olsp-response-3 ----
bool aiMiuyanZonglveConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
    if (!p || !p->getRoom() || r.viewerObjectName!=p->objectName()
        || !aiRangeRequestCurrent(*p->getRoom(),r,p)
        || r.kind!=AIRequest::Activate || r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
        || s.size()!=6 || s.value("suit")!=QJsonValue(int(Card::NoSuit))
        || s.value("number")!=QJsonValue(0)
        || !s.value("subcards").isArray()) return false;
    const QString key=s.value("source_key").toString(),name=s.value("name").toString();
    if (s.value("skill")!=QJsonValue(key)
        || !(((key=="miuyan" || key=="huiyun") && name=="fire_attack") || (key=="zonglve" && name=="slash"))) return false;
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
    for(const Card *c:p->getCards("he")) {
        if(!c || c->getEffectiveId()!=id)continue;
        if(room.getCardPlace(id)==Player::PlaceHand) {
            for(const auto &v:r.worldView.handCards)if(same(v,c))material=c;
        } else if(room.getCardPlace(id)==Player::PlaceEquip) {
            for(const auto &v:r.worldView.self.equips)if(same(v,c))material=c;
        }
    }
    if(!material)return false;
    if(key=="miuyan" && !material->isBlack())return false;
    // Readonly source construction: do not consume quota or run viewFilter.
    // Source's ban-mark and availability reads retain their original order;
    // selected materialization independently invokes the current V1 provider.
    return p->hasSkill(key) && aiRangeRequestCurrent(*p->getRoom(),r,p);
}

// ---- olsp-response-4 ----
bool aiXixiangEvaluation(const AIRequest &r,const ServerPlayer *p,const QJsonObject &q)
{
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
  ||!aiRangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||q.size()!=4||q.value("kind")!="evaluate_skill_card"||q.value("skill")!="olspxixiang"
  ||(q.value("name")!="slash"&&q.value("name")!="duel")||!q.value("subcards").isArray())return false;
 const QJsonArray ids=q.value("subcards").toArray();if(ids.size()!=2)return false;
 Room &room=*p->getRoom();QSet<int> seen;
 for(const auto &v:ids){
  if(!v.isDouble())return false;const double d=v.toDouble();
  if(!std::isfinite(d)||d<0||d>std::numeric_limits<int>::max()||d!=std::floor(d)||seen.contains(int(d)))return false;
  const int id=int(d);seen.insert(id);if(room.getCardOwner(id)!=p)return false;
  const Card *material=nullptr;
  const auto same=[&](const AICardView &f,const Card *c){return f.effectiveId==id&&f.objectName==c->objectName()
   &&f.className==c->getClassName()&&f.suit==int(c->getSuit())&&f.number==c->getNumber()
   &&!f.legacyIdentityToken.isEmpty()&&f.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);};
  for(const Card *c:p->getCards("he"))if(c&&c->getEffectiveId()==id){
   if(room.getCardPlace(id)==Player::PlaceHand){for(const auto &f:r.worldView.handCards)if(same(f,c))material=c;}
   else if(room.getCardPlace(id)==Player::PlaceEquip){for(const auto &f:r.worldView.self.equips)if(same(f,c))material=c;}
  }
  if(!material)return false;
 }
 // Fixed source builds its two-cost hypothesis BEFORE mark/availability reads.
 // No canSelect/feasible/payment here; selected existing V2 owns that admission.
 return p->hasSkill("olspxixiang")&&aiRangeRequestCurrent(room,r,p);
}

// ---- olsp-response-5 ----
bool aiPanqinEvaluation(const AIRequest&r,const ServerPlayer*p,const QJsonObject&q)
{
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
  ||!aiRangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::SkillInvoke||r.choiceOptions.reason!="panqin"
  ||q.size()!=4||q.value("kind")!="evaluate_skill_card"||q.value("skill")!="panqin"
  ||q.value("name")!="SavageAssault"||!q.value("subcards").isArray())return false;
 const auto tag=r.choiceOptions.context.value("ol_sp").toObject().value("tags").toObject().value("PanqinRecord").toObject();
 if(tag.value("known")!=QJsonValue(true)||tag.value("owner")!="owner"||tag.value("kind")!="IntList"||!tag.value("value").isArray())return false;
 QJsonArray native;for(const QVariant &v:p->getTag("PanqinRecord").toList())native.append(v.toInt());
 const auto recorded=tag.value("value").toArray();if(recorded!=native)return false;
 QJsonArray expected;Room &room=*p->getRoom();
 for(const auto &v:recorded){
  if(!v.isDouble())return false;const double d=v.toDouble();
  if(!std::isfinite(d)||d<std::numeric_limits<int>::min()||d>std::numeric_limits<int>::max()||d!=std::floor(d))return false;
  const int id=int(d);const AICardView *face=nullptr;
  // Discard membership is already public in this exact snapshot. Never ask a
  // hidden current location to choose whether the proposal is acceptable.
  for(const auto &f:r.worldView.discardPile)if(f.effectiveId==id){face=&f;break;}
  if(!face)continue;
  if(room.getCardPlace(id)!=Player::DiscardPile)return false;
  const Card*c=Sanguosha->getCard(id);
  if(!c||face->objectName!=c->objectName()||face->className!=c->getClassName()
   ||face->suit!=int(c->getSuit())||face->number!=c->getNumber()
   ||face->legacyIdentityToken.isEmpty()||face->legacyIdentityToken!=aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c))return false;
  expected.append(id);
 }
 // Source addSubcard(int) preserves duplicates and recorded order. This is
 // readonly SavageAssault policy only, never a discard payment or receipt.
 return q.value("subcards").toArray()==expected&&p->hasSkill("panqin")&&aiRangeRequestCurrent(room,r,p);
}

// ---- olsp-response ----
bool aiChoulieEvaluation(const AIRequest &request, const ServerPlayer *viewer,
    const QJsonObject &proposal)
{
    if (!viewer || !viewer->getRoom() || !viewer->getRoom()->roomRuntime()
        || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != viewer->getRoom()->roomRuntime()->stateRevision()
        || proposal.size() != 4 || proposal.value("kind") != "evaluate_skill_card"
        || proposal.value("skill") != "choulie" || proposal.value("name") != "slash"
        || !proposal.value("subcards").isArray() || !proposal.value("subcards").toArray().isEmpty())
        return false;
    // Native Choulie::cost asks all other players. Source eligibility remains
    // in the complete AI callback; this grants only its unskilled dummy Slash.
    if (request.kind == AIRequest::PlayerChosen)
        return request.choiceOptions.reason == "choulie";
    // Choulie::effect passes this exact public target in the native question.
    // Do not infer an undisclosed target or admit choulie2 (it has no clone).
    const QString target = request.choiceOptions.context.value("player").toString();
    return request.kind == AIRequest::RespondCard
        && request.choiceOptions.question == "askForCard"
        && request.pattern == ".." && request.choiceOptions.reason == ".."
        && !target.isEmpty() && request.prompt == "choulie1:" + target;
}

bool configureAIChoulieEvaluation(Card &card)
{
    // Fixed addFunction dummyCard() is exactly cloneCard("slash",6,0).
    // Never configure a live physical card or substitute a class alias.
    if (card.objectName() != "slash" || !card.isKindOf("Slash")
        || !card.isVirtualCard() || !card.getSubcards().isEmpty()
        || card.getSuit() != Card::NoSuit || card.getNumber() != 0
        || !card.getSkillName(false).isEmpty())
        return false;
    card.setFlags("Qinggang");
    return true;
}

// ---- olsp ----
#include "skill.h"
namespace {
struct Frame {
 QPointer<Room> room; QPointer<ServerPlayer> owner;
 QString skill; SkillInstanceRef activation, source;
 Frame *previous=nullptr; bool valid=false;
};
thread_local Frame *olspCurrent=nullptr;
bool same(const SkillInstanceRef &a,const SkillInstanceRef &b) {
 return a.ownerObjectName==b.ownerObjectName && a.key.skillName==b.key.skillName
  && a.key.instanceID==b.key.instanceID;
}
bool live(Room &room,const ServerPlayer *p) {
 if(!olspCurrent||!olspCurrent->valid||olspCurrent->room!=&room||olspCurrent->owner!=p
  ||!p||p->getRoom()!=&room)return false;
 bool roster=false;for(auto *q:room.getAllPlayers(true))if(q==p)roster=true;
 return roster && p->hasSkillInstance(olspCurrent->activation.key.skillName,olspCurrent->activation.key.instanceID)
  && p->hasSkillInstance(olspCurrent->source.key.skillName,olspCurrent->source.key.instanceID);
}
}
struct AIOlspQuestionScope::Data {Frame frame;};
AIOlspQuestionScope::AIOlspQuestionScope(Room &room,const SkillContext &ctx,const QString &skill):d(new Data) {
 auto &f=d->frame;f.previous=olspCurrent;f.room=&room;f.owner=ctx.owner;
 f.skill=skill;f.activation=ctx.activationRef;f.source=ctx.sourceRef;
 f.valid=(skill=="ol2shanjia"||skill=="jiewan") && ctx.owner && ctx.owner->getRoom()==&room
  && f.activation.isValid() && f.source.isValid()
  && f.activation.ownerObjectName==ctx.owner->objectName()
  && f.source.ownerObjectName==ctx.owner->objectName()
  && f.activation.key.skillName==skill && f.source.key.skillName==skill;
 // Even an invalid nested scope shadows the outer inquiry.
 olspCurrent=&f;
}
AIOlspQuestionScope::~AIOlspQuestionScope(){olspCurrent=d->frame.previous;}
bool aiOlspInstance(Room &room,const ServerPlayer *p,const QString &skill,int instanceID) {
 return live(room,p)&&olspCurrent->skill==skill&&olspCurrent->activation.key.instanceID==instanceID;
}
bool aiOlspAction(Room &room,const ServerPlayer *p,const AiSkillActionContext &a) {
 return live(room,p)&&same(a.activationRef,olspCurrent->activation)&&same(a.sourceRef,olspCurrent->source);
}
bool aiOlspQuestion(const AIRequest &r,const ServerPlayer *p) {
 if(!p||!p->getRoom()||!live(*p->getRoom(),p)||r.decisionId==0
  ||r.viewerObjectName!=p->objectName()||!aiRangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::UseCard||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
  ||r.handlingMethod!=Card::MethodUse||r.pattern!="@@"+olspCurrent->skill
  ||r.prompt!=(olspCurrent->skill=="ol2shanjia"?"ol2shanjia1":"jiewan0"))return false;
 return !r.hasSkillActionContext||aiOlspAction(*p->getRoom(),p,r.skillActionContext);
}
bool aiOlspEvaluation(const AIRequest &r,const ServerPlayer *p,const QJsonObject &q) {
 if(!aiOlspQuestion(r,p)||q.size()!=4
  ||q.value("kind")!="evaluate_skill_card"||!q.value("subcards").isArray())return false;
 const QString raw=q.value("skill").toString();const auto ids=q.value("subcards").toArray();
 if(olspCurrent->skill=="ol2shanjia")return raw=="_ol2shanjia"&&q.value("name")=="slash"&&ids.isEmpty();
 if(raw!="jiewan"||q.value("name")!="snatch"||ids.size()!=1||!ids[0].isDouble())return false;
 const double value=ids[0].toDouble();
 if(!std::isfinite(value)||value<0||value>std::numeric_limits<int>::max()||value!=std::floor(value))return false;
 const int id=int(value);Room &room=*p->getRoom();
 if(room.getCardOwner(id)!=p||room.getCardPlace(id)!=Player::PlaceHand)return false;
 for(const Card *c:p->getHandcards())if(c&&c->getEffectiveId()==id)
  for(const auto &v:r.worldView.handCards)if(v.effectiveId==id&&v.cardId==c->getId()
   &&v.objectName==c->objectName()&&v.className==c->getClassName()
   &&v.suit==int(c->getSuit())&&v.number==c->getNumber()
   &&v.skillName==c->getSkillName(false)&&v.virtualCard==c->isVirtualCard()
   &&v.subcardIds==c->getSubcards()&&!v.legacyIdentityToken.isEmpty()
   &&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c))return true;
 return false;
}

// ---- heqia-receipt ----
#include "ai-strategy/official.h"
#include <QWeakPointer>
namespace {
struct Receipt {
 QWeakPointer<AIConversionLedger> ledger;quint64 decision=0,revision=0;
 QString viewer,token,wire;bool first=false;QList<int> firstCosts;QStringList baseTargets;QStringList targets;AICardSpec spec;
 SkillInstanceRef activation,source;
};
class HeqiaReceiptLedger final:public QObject{public:explicit HeqiaReceiptLedger(Room&r):QObject(&r){}QList<Receipt>receipts;};
HeqiaReceiptLedger*heqiaReceiptLedger(Room*r,bool create=false){if(!r)return nullptr;for(auto*c:r->children())if(auto*l=dynamic_cast<HeqiaReceiptLedger*>(c))return l;return create?new HeqiaReceiptLedger(*r):nullptr;}
bool sameSpec(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number
 &&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
AICardSpec specOf(const Card *c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();
 s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
QJsonObject jsonSpec(const AICardSpec&s){QJsonArray costs;for(int id:s.subcardIds)costs<<id;
 return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",costs},{"conversion_id",s.conversionId}};}

bool heqiaReceiptCurrent(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.decisionId>0
 &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
 &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
}
namespace {
bool prepareFirst(const AIRequest&r,ServerPlayer*p,const QList<int>&costs,const QStringList&targets,
 const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use,QString&error){
 auto fail=[&](const char*m){error=QString::fromLatin1(m);return false;};
 if(!heqiaReceiptCurrent(r,p)||r.kind!=AIRequest::UseCard||r.pattern!="@@heqia1"
  ||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.handlingMethod!=Card::MethodNone)
  return fail("Heqia first selector requires exact native MethodNone inquiry");
 const auto*vs=Sanguosha->getViewAsSkill("heqia");
 if(!vs||dynamic_cast<const ViewAsSkillV2*>(vs)||!vs->isEnabledAtResponse(p,r.pattern))return fail("Heqia first native V1 inquiry unavailable");
 QList<const Card*>materials;QSet<int>seen;
 for(int id:costs){if(seen.contains(id)||p->getRoom()->getCardOwner(id)!=p
   ||(p->getRoom()->getCardPlace(id)!=Player::PlaceHand&&p->getRoom()->getCardPlace(id)!=Player::PlaceEquip))return fail("Heqia first material is not distinct current owner HE");
  seen.insert(id);const auto*c=Sanguosha->getCard(id);if(!c||c->hasFlag("using")||!vs->viewFilter(materials,c))return fail("Heqia first native material filter rejected");materials<<c;}
 const Card*product=vs->viewAs(materials);if(!product)return fail("Heqia first native constructor returned nil");
 std::unique_ptr<const Card>owned(product->isVirtualCard()?product:nullptr);
 if(!owned||product->getClassName()!="HeqiaCard"||product->getSubcards()!=costs)return fail("Heqia first native product mismatch");
 // Card::Parse schedules its virtual product. Only the native event-payload
 // lease is required; no redundant raw OwnedCardPtr survives Lua shutdown.
 const Card*parsed=Card::Parse(product->toString());if(!parsed)return fail("Heqia first source wire did not parse");
 use=CardUseStruct(parsed,p);for(const auto&name:targets){auto*t=p->getRoom()->findPlayerByObjectName(name,true);if(!t)return fail("Heqia first target unavailable");use.to<<t;}
 if(!admit(use))return fail("Heqia first original native provider rejected");
 return heqiaReceiptCurrent(r,p)||fail("Heqia first native admission changed current request");
}
}
AIFaqiSelection prepareAIHeqiaSelection(const AIRequest&r,ServerPlayer*p,
 const QString&token,const QStringList&selectedTargets,const std::function<bool(CardUseStruct&)> &admit){
 AIFaqiSelection out;AIHeqiaSlotStatus slotStatus=AIHeqiaSlotStatus::Unknown;
 const Card *saved=readAIHeqiaSlot(r,p,token,&out.targets,nullptr,&out.error,&slotStatus);
 if(slotStatus==AIHeqiaSlotStatus::SourcePass)out.outcome=AIFaqiOutcome::SourcePass;
 if(slotStatus==AIHeqiaSlotStatus::SourceError)out.outcome=AIFaqiOutcome::SourceError;
 if(!saved||!admit)return out;
 const QStringList baseTargets=out.targets;out.targets=selectedTargets;

 // Exact fixed source @@heqia serializes now, not at AG. Card::Parse intentionally
 // drops old request instance fields and current native attribution starts anew.
 out.sourceWire=saved->toString();
 const Card *parsed=Card::Parse(out.sourceWire);
 if(!parsed){out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved Heqia source wire has no native parsed product";return out;}
 out.use.from=p;
 // Numeric Card::Parse returns an Engine-owned wrapper even when filtered.
 // The wire form, not isVirtualCard(include_filter), determines parse custody.
 bool physicalWire=false;out.sourceWire.toInt(&physicalWire);
 // Card::Parse has already scheduled every virtual return with the lifetime
 // manager. Retain the normal event-payload lease without a second raw-pointer
 // OwnedCardPtr deleter: Lua-boxed CardUseStruct copies can outlive shutdown's
 // releaseEventPayloads/preclose drain. Their destruction must not dereference
 // an already retired parsed Card. Physical returns remain Engine-owned.
 out.use=CardUseStruct(parsed,p);
 for(const auto &name:out.targets){auto *target=p->getRoom()->findPlayerByObjectName(name,true);
  if(!target){out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved Heqia target no longer exists";return out;}out.use.to<<target;}
 const QString skill=parsed->getActivationSkillName();
 const auto *vs=Sanguosha->getViewAsSkill(skill);
 const auto *v2=dynamic_cast<const ViewAsSkillV2 *>(vs);
 // No post-revision check is advertised as a mutation guard. Only these fixed
 // compiled V2 paths are audited side-effect/inquiry-free during reconstruction.
 if(vs && (skill!="heqia" && (!v2 || skill!="limu"))){
  out.error="Heqia original provider is outside audited inquiry-free reconstruction";return out;
 }
 if(skill=="heqia"){
  // The fixed AI sends its real Basic, NOT the GUI HeqiaUseCard proxy.
  // Authenticate its actual native inquiry/material domain and preserve the
  // legacy original-provider resolution below, including no-instance refusal.
  if(!vs||v2||!vs->isEnabledAtResponse(p,"@@heqia2")||saved->getSkillName(false)!="_heqia"
    ||saved->getSubcards().size()!=1){
   out.outcome=AIFaqiOutcome::NativeRejected;out.error="Heqia actual source differs from current native declaration/material";return out;
  }
  const int id=saved->getSubcards().first();
  if(p->getRoom()->getCardOwner(id)!=p
    ||(p->getRoom()->getCardPlace(id)!=Player::PlaceHand&&p->getRoom()->getCardPlace(id)!=Player::PlaceEquip)||Sanguosha->getCard(id)->hasFlag("using")){
   out.outcome=AIFaqiOutcome::NativeRejected;out.error="Heqia source payment left current owner HE";return out;
  }
 }
 // Ordinary V1 declarations do not inquire. A solely attached multi-provider
 // V1 source would invoke native provider choice; never execute it in a query.
 if(skill=="heqia"){
  bool ordinary=false;int attached=0;
  for(const auto&i:p->getSkillInstances())if(i.skillName==skill){
   if(i.source==SourceAttached&&p->getSkillInstanceStateValue(skill,i.instanceID,"legacy_activation_lifecycle").toBool()){
    if(p->getValidSkillInstanceIds(skill).contains(i.instanceID))++attached;
   }else ordinary=true;
  }
  if(!ordinary&&attached>1){out.error="Heqia attached provider inquiry is outside this readonly selection";return out;}
 }
 bool privateAttachment=false;
 for(const auto &instance:p->getSkillInstances())if(instance.skillName==skill&&instance.source==SourceAttached
  &&p->getSkillInstanceStateValue(skill,instance.instanceID,"legacy_activation_lifecycle").toBool())privateAttachment=true;
 if(skill!="heqia" && !v2 && (privateAttachment || parsed->getActivationSkillInstanceId()!=0)){
  out.error="Heqia non-ViewAs attachment continuation is not audited";return out;
 }
 if(v2 && parsed->getActivationSkillInstanceId()==0){
  QList<int> ids;for(int id:p->getValidSkillInstanceIds(skill))
   if(p->getRoom()->canShowGeneralForSkill(SkillInstanceRef(p->objectName(),SkillInstanceKey(skill,id))))ids<<id;
  const int scoped=p->getMark(ViewAsSkillV2::borrowedActivationMarkName(skill));
  if(scoped>0){if(!ids.contains(scoped)){out.outcome=AIFaqiOutcome::NativeRejected;out.error="native borrowed provider is unavailable";return out;}ids={scoped};}
  if(ids.size()>1){out.error="Heqia current original provider needs native instance-choice continuation";return out;}
 }
 // Source getCard alternatives are own cards. A numeric parse never confers
 // authority over a foreign/discard card merely because it was observed.
 if(physicalWire && (!p->handCards().contains(parsed->getEffectiveId())
   &&!p->getEquipsId().contains(parsed->getEffectiveId())&&!p->getHandPile().contains(parsed->getEffectiveId()))){
  out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved physical replacement left original owner material domain";return out;
 }
 if(!v2 && !physicalWire && parsed->getTypeId()==Card::TypeSkill){
  out.error="Heqia non-ViewAs SkillCard native activation is not audited";return out;
 }
 if(!admit(out.use)){out.outcome=AIFaqiOutcome::NativeRejected;out.error="Heqia current original native provider rejected saved source";return out;}
 {
  QStringList actualTargets;QString why;const Card*actual=readAIHeqiaSlot(r,p,token,&actualTargets,nullptr,&why);
  if(!actual||actual!=saved||actualTargets!=baseTargets||actual->toString()!=out.sourceWire){out.error=why.isEmpty()?"Heqia actual source changed during admission":why;return out;}
 }
 if(!heqiaReceiptCurrent(r,p)){out.error="Heqia reconstruction changed current request state";return out;}
 out.outcome=AIFaqiOutcome::Ready;out.error.clear();return out;
}

QJsonObject issueAIHeqiaReceipt(const AIRequest&r,ServerPlayer*p,const QString&token,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit){
 auto selected=prepareAIHeqiaSelection(r,p,token,targets,admit);
 if(selected.outcome==AIFaqiOutcome::SourcePass)return{{"source_pass",true}};
 if(selected.outcome==AIFaqiOutcome::SourceError)return{{"source_error",selected.error}};
 if(selected.outcome!=AIFaqiOutcome::Ready)return{{"error",selected.error},{"native_rejected",selected.outcome==AIFaqiOutcome::NativeRejected}};
 if(!r.conversionLedger)return{{"error","Heqia request ledger unavailable"}};
 const int id=r.cardConversions.size()+r.conversionLedger->tickets.size()+1;
 Receipt receipt;receipt.ledger=r.conversionLedger;receipt.decision=r.decisionId;receipt.revision=r.stateRevision;
 receipt.viewer=p->objectName();receipt.token=token;receipt.targets=selected.targets;receipt.wire=selected.sourceWire;
 receipt.spec=specOf(selected.use.card,id);receipt.activation=selected.use.activationRef;receipt.source=selected.use.sourceRef;
 AICardConversionView reservation;reservation.conversionId=id;reservation.policyClass="@heqia_saved_reconstruction";
 reservation.name=receipt.spec.name;reservation.available=false; // Never generic buildSpec authority.
 r.conversionLedger->tickets<<reservation;
 auto *l=heqiaReceiptLedger(p->getRoom(),true);
 for(auto it=l->receipts.begin();it!=l->receipts.end();)if(it->ledger.isNull())it=l->receipts.erase(it);else ++it;
 l->receipts<<receipt;
 return{{"card_spec",jsonSpec(receipt.spec)},{"targets",QJsonArray::fromStringList(receipt.targets)}};
}
QJsonObject issueAIHeqiaFirst(const AIRequest&r,ServerPlayer*p,const QList<int>&costs,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit){
 CardUseStruct use;QString error;
 if(!prepareFirst(r,p,costs,targets,admit,use,error))return{{"error",error},{"native_rejected",true}};
 if(!r.conversionLedger)return{{"error","Heqia first request ledger unavailable"}};
 const int id=r.cardConversions.size()+r.conversionLedger->tickets.size()+1;
 Receipt receipt;receipt.ledger=r.conversionLedger;receipt.decision=r.decisionId;receipt.revision=r.stateRevision;
 receipt.viewer=p->objectName();receipt.first=true;receipt.firstCosts=costs;receipt.targets=targets;receipt.wire=use.card->toString();
 receipt.spec=specOf(use.card,id);receipt.activation=use.activationRef;receipt.source=use.sourceRef;
 AICardConversionView reservation;reservation.conversionId=id;reservation.policyClass="@heqia_saved_reconstruction";reservation.name=receipt.spec.name;reservation.available=false;
 r.conversionLedger->tickets<<reservation;heqiaReceiptLedger(p->getRoom(),true)->receipts<<receipt;
 return{{"card_spec",jsonSpec(receipt.spec)},{"targets",QJsonArray::fromStringList(targets)}};
}
bool hasAIHeqiaReceipt(const AIRequest&r,int id){
 // A marker only; final lookup additionally authenticates the Room-owned proof.
 if(!r.conversionLedger)return false;
 for(const auto &ticket:r.conversionLedger->tickets)
  if(ticket.conversionId==id&&ticket.policyClass=="@heqia_saved_reconstruction")return true;
 return false;
}
bool applyAIHeqiaReceipt(const AIRequest&r,ServerPlayer*p,const AICardSpec&spec,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!heqiaReceiptCurrent(r,p)||!hasAIHeqiaReceipt(r,spec.conversionId))return false;
 auto *l=heqiaReceiptLedger(p->getRoom());if(!l)return false;
 const Receipt *found=nullptr;
 for(const auto &entry:l->receipts)if(entry.spec.conversionId==spec.conversionId
  &&entry.ledger.toStrongRef()==r.conversionLedger&&entry.decision==r.decisionId
  &&entry.revision==r.stateRevision&&entry.viewer==p->objectName()){found=&entry;break;}
 if(!found)return false;const Receipt receipt=*found;
 if(!sameSpec(spec,receipt.spec)||targets!=receipt.targets)return false;
 if(receipt.first){
  CardUseStruct selected;QString error;
  if(!prepareFirst(r,p,receipt.firstCosts,targets,admit,selected,error)
    ||!sameSpec(specOf(selected.card,spec.conversionId),receipt.spec)
    ||selected.activationRef!=receipt.activation||selected.sourceRef!=receipt.source)return false;
  use=selected;return true;
 }
 auto selected=prepareAIHeqiaSelection(r,p,receipt.token,targets,admit);
 if(selected.outcome!=AIFaqiOutcome::Ready||selected.sourceWire!=receipt.wire
  ||!sameSpec(specOf(selected.use.card,spec.conversionId),receipt.spec)
  ||selected.use.activationRef!=receipt.activation||selected.use.sourceRef!=receipt.source)return false;
 use=selected.use;return true;
}

// ---- heqia ----
namespace {
struct HeqiaStack{lua_State*L;int n;explicit HeqiaStack(lua_State*s):L(s),n(lua_gettop(s)){}~HeqiaStack(){lua_settop(L,n);}};
struct HeqiaRow{
 QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;QString token;
 int offered=-1;quint64 producerDecision=0,producerRevision=0;
 QList<int>materials;int physical=-1;
};
class HeqiaLedger final:public QObject{
public:explicit HeqiaLedger(Room&r):QObject(&r){}lua_State*L=nullptr;quint64 generation=0;
 QHash<const ServerPlayer*,HeqiaRow>rows;
};
HeqiaLedger*heqiaLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<HeqiaLedger*>(c))return l;return create?new HeqiaLedger(r):nullptr;}
bool heqiaCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&q.stateRevision==p->getRoom()->roomRuntime()->stateRevision()&&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool heqiaShown(const AIRequest&q,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact)exact=Sanguosha->getCard(id);if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void heqiaField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool heqiaSelf(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool heqiaAbsentNewIndex(lua_State*L,int t){HeqiaStack st(L);if(!lua_getmetatable(L,t))return true;heqiaField(L,-1,"__newindex");return lua_isnil(L,-1);}
bool heqiaPlain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool heqiaEnv(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool heqiaCode(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&heqiaEnv(L,f);}
bool sourceCallback(Room&r,lua_State*L){
 HeqiaStack stack(L);AILuaInspect::globals(L);heqiaField(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 heqiaField(L,-1,"ai_skill_use");if(!heqiaPlain(L,-1))return false;heqiaField(L,-1,"@@heqia2");
 return heqiaCode(r,L,-1,"lua/ai/ol_sp-ai.lua",229,247);
}
bool sourceProducer(Room&r,lua_State*L){
 HeqiaStack stack(L);AILuaInspect::globals(L);heqiaField(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 heqiaField(L,-1,"ai_skill_askforag");if(!heqiaPlain(L,-1))return false;heqiaField(L,-1,"heqia");
 return heqiaCode(r,L,-1,"lua/ai/ol_sp-ai.lua",214,227);
}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool heqiaLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));heqiaField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  heqiaField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool heqiaSourceReadBindings(Room&r,lua_State*L){
 HeqiaStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 heqiaField(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 heqiaField(L,sgs,"ai_skill_use");if(!heqiaPlain(L,-1))return false;heqiaField(L,-1,"@@heqia2");
 if(!heqiaCode(r,L,-1,"lua/ai/ol_sp-ai.lua",229,247))return false;
 heqiaField(L,sgs,"list");if(!heqiaCode(r,L,-1,"lua/utilities.lua",38,41))return false;
 heqiaField(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=heqiaCode(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=heqiaCode(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){heqiaField(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 heqiaField(L,globals,"table");if(!heqiaPlain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){heqiaField(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Executes only our fixed native assignment thunk. No legacy AI strategy call.
// The caller proved source-equivalent __newindex behavior before entering it.
struct HeqiaWrite {const AIRequest*request;ServerPlayer*owner;AIReadonlyCardLease lease;const Card*card;QList<ServerPlayer*>targets;QString cardState,targetState;int extra=0;};
int heqiaAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"Heqia assignment inspection stack unavailable");
 auto*w=static_cast<HeqiaWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_gettop(L);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");
 lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(w->cardState=="card"){if(!pushAISlotCard(L,w->card))return luaL_error(L,"Heqia native Card wrapper unavailable");lua_setfield(L,d,"card");}
 else if(w->cardState=="false"){lua_pushboolean(L,0);lua_setfield(L,d,"card");}
 if(w->targetState=="list"){if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"Heqia native target list unavailable");lua_setfield(L,d,"to");}
 else if(w->targetState=="false"){lua_pushboolean(L,0);lua_setfield(L,d,"to");}
 if(!heqiaCurrent(*w->request,w->owner)||(w->lease?w->lease():nullptr)!=w->card||!heqiaSelf(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))
  return luaL_error(L,"Heqia actual owner/request changed during allocation");
 lua_pop(L,1);
 // Preserve source assignment (not rawset). If GC changed the exact receiver's
 // absent-key metatable after preflight, reject before dispatching that hook.
 heqiaField(L,1,"heqia_use");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!heqiaAbsentNewIndex(L,1))return luaL_error(L,"Heqia source __newindex changed during allocation");
 lua_pushvalue(L,d);lua_setfield(L,1,"heqia_use");
 lua_pushvalue(L,d);return 1;
}
}
void clearAIHeqiaSlots(Room&r){if(auto*l=heqiaLedger(r))delete l;}
bool hasAIHeqiaSlot(Room&r,ServerPlayer*p,const QString&token){auto*l=heqiaLedger(r);if(!l||!p||token.isEmpty())return false;auto i=l->rows.constFind(p);return i!=l->rows.cend()&&i->owner==p&&i->token==token;}
QString writeAIHeqiaSlot(const AIRequest&q,ServerPlayer*p,int offered,AIReadonlyCardLease lease,const QStringList&names,const QJsonObject&fields,QString*error){
 auto fail=[&](const QString&s){if(error)*error=s;return QString();};
 if(!heqiaCurrent(q,p)||q.kind!=AIRequest::AmazingGrace||q.choiceOptions.reason!="heqia"||!q.choiceOptions.cardIds.contains(offered))return fail("Heqia source assignment requires actual current offered choice");
 if(fields.size()!=5||fields.value("isDummy")!=QJsonValue(true)||!fields.value("extra_target").isDouble()
  ||fields.value("extra_target").toDouble()!=fields.value("extra_target").toInt()||fields.value("current_targets_empty")!=QJsonValue(true))return fail("Heqia full dummy field envelope unsupported");
 const QString cardState=fields.value("card_state").toString(), targetState=fields.value("target_state").toString();
 if(cardState!="card"&&cardState!="nil"&&cardState!="false")return fail("Heqia card state malformed");
 if(targetState!="list"&&targetState!="nil"&&targetState!="false")return fail("Heqia target state malformed");
 const Card*card=lease?lease():nullptr;if((cardState=="card")!=bool(card))return fail("Heqia exact producer custody/state mismatch");
 const int physical=!card||card->isVirtualCard()?-1:card->getEffectiveId();if(physical>=0&&!heqiaShown(q,physical,card))return fail("Heqia source physical card undisclosed");
 const auto costs=card?card->getSubcards():QList<int>();for(int id:costs)if(!heqiaShown(q,id))return fail("Heqia source material undisclosed");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Heqia game runtime unavailable");HeqiaStack stack(L);
 if(!sourceProducer(r,L))return fail("Heqia actual producer callback changed");
 if(!heqiaSelf(L,r,p))return fail("Heqia actual LuaAI self authentication failed");const int receiver=lua_absindex(L,-1);
 heqiaField(L,receiver,"heqia_use");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!heqiaAbsentNewIndex(L,receiver))return fail("Heqia source __newindex is not the direct assignment domain");
 HeqiaWrite w;w.request=&q;w.owner=p;w.lease=lease;w.card=card;w.extra=fields.value("extra_target").toInt();w.cardState=cardState;w.targetState=targetState;
 const auto roster=r.getAllPlayers(true);for(const auto&name:names){ServerPlayer*target=nullptr;for(auto*t:roster)if(t&&t->objectName()==name){target=t;break;}if(!target)return fail("Heqia source target is outside current roster");w.targets<<target;}
 lua_pushcfunction(L,heqiaAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,2,1,0)!=LUA_OK)return fail("Heqia native source assignment failed before completion");
 if(!heqiaCurrent(q,p)||(lease&&lease()!=card))return fail("Heqia source assignment changed current request or custody");
 heqiaField(L,receiver,"heqia_use");const bool exact=lua_rawequal(L,-1,-2);lua_pop(L,1);if(!exact||!heqiaPlain(L,-1))return fail("Heqia assignment no longer names its actual source table");
 auto*l=heqiaLedger(r,true);if(l->L&&l->generation!=rt->lua().generation())return fail("Heqia synchronized runtime generation changed");l->L=L;l->generation=rt->lua().generation();
 HeqiaRow row;row.owner=p;row.lease=std::move(lease);row.token="heqia-actual-slot:"+QUuid::createUuid().toString(QUuid::WithoutBraces);row.offered=offered;row.materials=costs;row.physical=physical;row.producerDecision=q.decisionId;row.producerRevision=q.stateRevision;
 // Actual self owns the table/list wrappers. No extra Lua registry allocation
 // or old table identity is needed; the native lease retains only Card custody.
 l->rows[p]=row;
 if(!heqiaCurrent(q,p)||(row.lease&&row.lease()!=card))return fail("Heqia source assignment publication changed current request/custody");
 if(error)error->clear();return row.token;
}
const Card*readAIHeqiaSlot(const AIRequest&q,ServerPlayer*p,const QString&token,QStringList*targets,int*offered,QString*error,AIHeqiaSlotStatus*status){
 if(status)*status=AIHeqiaSlotStatus::Unknown;
 auto fail=[&](const QString&s)->const Card*{if(error)*error=s;return nullptr;};
 if(!heqiaCurrent(q,p)||q.kind!=AIRequest::UseCard||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.pattern!="@@heqia2"||q.handlingMethod!=Card::MethodUse)return fail("Heqia actual source read requires current response-use question");Room&r=*p->getRoom();auto*l=heqiaLedger(r);auto*rt=r.roomRuntime();
 const bool retained=l&&l->generation==rt->lua().generation()&&hasAIHeqiaSlot(r,p,token);
 const HeqiaRow row=retained?l->rows.value(p):HeqiaRow();
 LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||(retained&&L!=l->L)||!lua_checkstack(L,4096))return fail("Heqia actual source runtime unavailable");HeqiaStack stack(L);
 if(!sourceCallback(r,L))return fail("Heqia actual source callback changed");
 if(!heqiaSelf(L,r,p))return fail("Heqia actual LuaAI self changed");if(!heqiaLookup(L,-1,"heqia_use"))return fail("Heqia own slot lookup requires unproved dynamic getter");
 // Fixed source indexes dummy.card without testing dummy itself.
 if(lua_isnil(L,-1)||lua_isboolean(L,-1)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,-1)))return fail("Heqia primitive plan index metamethod unproved");
  if(status)*status=AIHeqiaSlotStatus::SourceError;return fail(QString("attempt to index a %1 value (local 'dummy')").arg(lua_isnil(L,-1)?"nil":"boolean"));
 }
 if(!lua_istable(L,-1))return fail("Heqia source dummy is outside table/nil/boolean observation");const int d=lua_absindex(L,-1);
 if(!heqiaLookup(L,d,"card"))return fail("Heqia card field requires unproved dynamic getter");const int value=lua_absindex(L,-1);
 const auto pass=[&]()->const Card*{if(!heqiaCurrent(q,p))return fail("Heqia source decline changed current request");if(offered)*offered=row.offered;if(targets)targets->clear();if(error)error->clear();if(status)*status=AIHeqiaSlotStatus::SourcePass;return nullptr;};
 if(!lua_toboolean(L,value))return pass(); // do not read dummy.to on this branch
 if(!heqiaLookup(L,d,"to"))return fail("Heqia target field requires unproved dynamic getter");const int list=lua_absindex(L,-1);
 // `dummy.card and dummy.to` short-circuits before list/card methods.
 if(!lua_toboolean(L,list))return pass();
 if(!heqiaSourceReadBindings(r,L))return fail("Heqia actual source serializer/list bindings changed");
 if(!retained)return fail("Heqia actual truthy plan has no retained producer custody");
 // A new plain source table is lawful if both actually consumed fields retain
 // independently authenticated custody. An old table identity is NOT authority.
 if(!heqiaPlain(L,d))return fail("Heqia truthy source fields require a plain observed plan");
 const Card*card=row.lease?row.lease():nullptr;if(!card)return fail("Heqia actual source Card custody expired");
 if(!matchAISlotCard(L,value,card)||!matchAINativeObjectMethods(r,L,value,"Card",{"toString","isKindOf"}))return fail("Heqia actual source Card identity or native serializer changed");
 QList<ServerPlayer*>actual;if(!readAISlotTargets(L,list,actual))return fail("Heqia actual source target list custody changed");
 if(!heqiaSourceReadBindings(r,L))return fail("Heqia actual source serializer/list bindings changed");
 // Only now does source consume a Card face/wire. Printed IDs cannot grant it.
 if(row.physical>=0&&!heqiaShown(q,row.physical,card))return fail("Heqia actual source physical observation expired");
 for(int id:row.materials)if(!heqiaShown(q,id))return fail("Heqia actual source material observation expired");
 if(card->getSubcards()!=row.materials)return fail("Heqia actual source material provenance changed");
 const auto roster=r.getAllPlayers(true);QStringList names;for(auto*t:actual){if(!t||!roster.contains(t))return fail("Heqia actual source target is not a live roster identity");names<<t->objectName();}
 if(!heqiaCurrent(q,p)||row.lease()!=card)return fail("Heqia actual source read changed current request/custody");
 if(targets)*targets=names;if(offered)*offered=row.offered;if(error)error->clear();if(status)*status=AIHeqiaSlotStatus::Card;return card;
}

bool registerAIHeqiaSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/ol_sp-ai.lua",QByteArray("d8cc0c6d06b58c3d7306e5d742e44247e88b2050f94df886600f840e68b4205a"),error)
 &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
std::unique_ptr<Card> constructAIHeqia(const AIRequest&q,ServerPlayer*p,int offered,int material,QString*error){
 auto fail=[&](const QString&s)->std::unique_ptr<Card>{if(error)*error=s;return {};};
 if(!heqiaCurrent(q,p)||q.kind!=AIRequest::AmazingGrace||q.choiceOptions.reason!="heqia"||!q.choiceOptions.cardIds.contains(offered))return fail("Heqia constructor requires an actual offered basic card");
 const AICardView*shownOffer=nullptr;for(const auto&v:q.choiceOptions.cards)if(v.effectiveId==offered){shownOffer=&v;break;}
 if(!shownOffer)return fail("Heqia offered source face unavailable");
 const auto hand=p->handCards();if(hand.isEmpty()||hand.first()!=material||!heqiaShown(q,material))return fail("Heqia source first hand material changed or is undisclosed");
 std::unique_ptr<Card>c(Sanguosha->cloneCard(shownOffer->objectName,Card::NoSuit,0));
 if(!c){if(error)error->clear();return {};}
 c->addSubcard(material);c->setSkillName("_heqia");if(error)error->clear();return c;
}

// ---- leiluan-receipt ----
namespace {
struct LeiluanReceiptReceipt {
 QWeakPointer<AIConversionLedger> ledger;quint64 decision=0,revision=0;
 QString viewer,token,wire;QStringList targets;AICardSpec spec;
 SkillInstanceRef activation,source;
};
class LeiluanReceiptLedger final:public QObject{public:explicit LeiluanReceiptLedger(Room&r):QObject(&r){}QList<LeiluanReceiptReceipt>receipts;};
LeiluanReceiptLedger*leiluanReceiptLedger(Room*r,bool create=false){if(!r)return nullptr;for(auto*c:r->children())if(auto*l=dynamic_cast<LeiluanReceiptLedger*>(c))return l;return create?new LeiluanReceiptLedger(*r):nullptr;}
bool leiluanReceiptSameSpec(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number
 &&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
AICardSpec leiluanReceiptSpecOf(const Card *c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();
 s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
QJsonObject leiluanReceiptJsonSpec(const AICardSpec&s){QJsonArray costs;for(int id:s.subcardIds)costs<<id;
 return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",costs},{"conversion_id",s.conversionId}};}

bool leiluanReceiptCurrent(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.decisionId>0
 &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
 &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
}
AIFaqiSelection prepareAILeiluanSelection(const AIRequest&r,ServerPlayer*p,
 const QString&token,const std::function<bool(CardUseStruct&)> &admit){
 AIFaqiSelection out;AILeiluanSlotStatus slotStatus=AILeiluanSlotStatus::Unknown;
 const Card *saved=readAILeiluanSlot(r,p,token,&out.targets,nullptr,&out.error,&slotStatus);
 if(slotStatus==AILeiluanSlotStatus::SourcePass)out.outcome=AIFaqiOutcome::SourcePass;
 if(slotStatus==AILeiluanSlotStatus::SourceError)out.outcome=AIFaqiOutcome::SourceError;
 if(!saved||!admit)return out;

 // Exact fixed source @@leiluan serializes now, not at AG. Card::Parse intentionally
 // drops old request instance fields and current native attribution starts anew.
 out.sourceWire=saved->toString();
 const Card *parsed=Card::Parse(out.sourceWire);
 if(!parsed){out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved Leiluan source wire has no native parsed product";return out;}
 out.use.from=p;
 // Numeric Card::Parse returns an Engine-owned wrapper even when filtered.
 // The wire form, not isVirtualCard(include_filter), determines parse custody.
 bool physicalWire=false;out.sourceWire.toInt(&physicalWire);
 // Card::Parse has already scheduled every virtual return with the lifetime
 // manager. Retain the normal event-payload lease without a second raw-pointer
 // OwnedCardPtr deleter: Lua-boxed CardUseStruct copies can outlive shutdown's
 // releaseEventPayloads/preclose drain. Their destruction must not dereference
 // an already retired parsed Card. Physical returns remain Engine-owned.
 out.use=CardUseStruct(parsed,p);
 for(const auto &name:out.targets){auto *target=p->getRoom()->findPlayerByObjectName(name,true);
  if(!target){out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved Leiluan target no longer exists";return out;}out.use.to<<target;}
 const QString skill=parsed->getActivationSkillName();
 const auto *vs=Sanguosha->getViewAsSkill(skill);
 const auto *v2=dynamic_cast<const ViewAsSkillV2 *>(vs);
 // No post-revision check is advertised as a mutation guard. Only these fixed
 // compiled V2 paths are audited side-effect/inquiry-free during reconstruction.
 if(vs && (skill!="leiluan" && (!v2 || skill!="limu"))){
  out.error="Leiluan original provider is outside audited inquiry-free reconstruction";return out;
 }
 if(skill=="leiluan"){
  if(!vs||v2||!vs->isEnabledAtResponse(p,"@@leiluan")){out.outcome=AIFaqiOutcome::NativeRejected;out.error="Leiluan native response is disabled";return out;}
  const Card*product=vs->viewAs(QList<const Card*>());
  std::unique_ptr<const Card> owned(product&&product->isVirtualCard()?product:nullptr);
  if(!product||!product->isVirtualCard()||product->toString()!=out.sourceWire){out.outcome=AIFaqiOutcome::NativeRejected;out.error="Leiluan current native zero-cost product differs from saved source";return out;}
 }
 // Ordinary V1 declarations do not inquire. A solely attached multi-provider
 // V1 source would invoke native provider choice; never execute it in a query.
 if(skill=="leiluan"){
  bool ordinary=false;int attached=0;
  for(const auto&i:p->getSkillInstances())if(i.skillName==skill){
   if(i.source==SourceAttached&&p->getSkillInstanceStateValue(skill,i.instanceID,"legacy_activation_lifecycle").toBool()){
    if(p->getValidSkillInstanceIds(skill).contains(i.instanceID))++attached;
   }else ordinary=true;
  }
  if(!ordinary&&attached>1){out.error="Leiluan attached provider inquiry is outside this readonly selection";return out;}
 }
 bool privateAttachment=false;
 for(const auto &instance:p->getSkillInstances())if(instance.skillName==skill&&instance.source==SourceAttached
  &&p->getSkillInstanceStateValue(skill,instance.instanceID,"legacy_activation_lifecycle").toBool())privateAttachment=true;
 if(skill!="leiluan" && !v2 && (privateAttachment || parsed->getActivationSkillInstanceId()!=0)){
  out.error="Leiluan non-ViewAs attachment continuation is not audited";return out;
 }
 if(v2 && parsed->getActivationSkillInstanceId()==0){
  QList<int> ids;for(int id:p->getValidSkillInstanceIds(skill))
   if(p->getRoom()->canShowGeneralForSkill(SkillInstanceRef(p->objectName(),SkillInstanceKey(skill,id))))ids<<id;
  const int scoped=p->getMark(ViewAsSkillV2::borrowedActivationMarkName(skill));
  if(scoped>0){if(!ids.contains(scoped)){out.outcome=AIFaqiOutcome::NativeRejected;out.error="native borrowed provider is unavailable";return out;}ids={scoped};}
  if(ids.size()>1){out.error="Leiluan current original provider needs native instance-choice continuation";return out;}
 }
 // Source getCard alternatives are own cards. A numeric parse never confers
 // authority over a foreign/discard card merely because it was observed.
 if(physicalWire && (!p->handCards().contains(parsed->getEffectiveId())
   &&!p->getEquipsId().contains(parsed->getEffectiveId())&&!p->getHandPile().contains(parsed->getEffectiveId()))){
  out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved physical replacement left original owner material domain";return out;
 }
 if(!v2 && !physicalWire && parsed->getTypeId()==Card::TypeSkill){
  out.error="Leiluan non-ViewAs SkillCard native activation is not audited";return out;
 }
 if(!admit(out.use)){out.outcome=AIFaqiOutcome::NativeRejected;out.error="Leiluan current original native provider rejected saved source";return out;}
 {
  QStringList actualTargets;QString why;const Card*actual=readAILeiluanSlot(r,p,token,&actualTargets,nullptr,&why);
  if(!actual||actual!=saved||actualTargets!=out.targets||actual->toString()!=out.sourceWire){out.error=why.isEmpty()?"Leiluan actual source changed during admission":why;return out;}
 }
 if(!leiluanReceiptCurrent(r,p)){out.error="Leiluan reconstruction changed current request state";return out;}
 out.outcome=AIFaqiOutcome::Ready;out.error.clear();return out;
}

QJsonObject issueAILeiluanReceipt(const AIRequest&r,ServerPlayer*p,const QString&token,
 const std::function<bool(CardUseStruct&)>&admit){
 auto selected=prepareAILeiluanSelection(r,p,token,admit);
 if(selected.outcome==AIFaqiOutcome::SourcePass)return{{"source_pass",true}};
 if(selected.outcome==AIFaqiOutcome::SourceError)return{{"source_error",selected.error}};
 if(selected.outcome!=AIFaqiOutcome::Ready)return{{"error",selected.error},{"native_rejected",selected.outcome==AIFaqiOutcome::NativeRejected}};
 if(!r.conversionLedger)return{{"error","Leiluan request ledger unavailable"}};
 const int id=r.cardConversions.size()+r.conversionLedger->tickets.size()+1;
 LeiluanReceiptReceipt receipt;receipt.ledger=r.conversionLedger;receipt.decision=r.decisionId;receipt.revision=r.stateRevision;
 receipt.viewer=p->objectName();receipt.token=token;receipt.targets=selected.targets;receipt.wire=selected.sourceWire;
 receipt.spec=leiluanReceiptSpecOf(selected.use.card,id);receipt.activation=selected.use.activationRef;receipt.source=selected.use.sourceRef;
 AICardConversionView reservation;reservation.conversionId=id;reservation.policyClass="@leiluan_saved_reconstruction";
 reservation.name=receipt.spec.name;reservation.available=false; // Never generic buildSpec authority.
 r.conversionLedger->tickets<<reservation;
 auto *l=leiluanReceiptLedger(p->getRoom(),true);
 for(auto it=l->receipts.begin();it!=l->receipts.end();)if(it->ledger.isNull())it=l->receipts.erase(it);else ++it;
 l->receipts<<receipt;
 return{{"card_spec",leiluanReceiptJsonSpec(receipt.spec)},{"targets",QJsonArray::fromStringList(receipt.targets)}};
}
bool hasAILeiluanReceipt(const AIRequest&r,int id){
 // A marker only; final lookup additionally authenticates the Room-owned proof.
 if(!r.conversionLedger)return false;
 for(const auto &ticket:r.conversionLedger->tickets)
  if(ticket.conversionId==id&&ticket.policyClass=="@leiluan_saved_reconstruction")return true;
 return false;
}
bool applyAILeiluanReceipt(const AIRequest&r,ServerPlayer*p,const AICardSpec&spec,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!leiluanReceiptCurrent(r,p)||!hasAILeiluanReceipt(r,spec.conversionId))return false;
 auto *l=leiluanReceiptLedger(p->getRoom());if(!l)return false;
 const LeiluanReceiptReceipt *found=nullptr;
 for(const auto &entry:l->receipts)if(entry.spec.conversionId==spec.conversionId
  &&entry.ledger.toStrongRef()==r.conversionLedger&&entry.decision==r.decisionId
  &&entry.revision==r.stateRevision&&entry.viewer==p->objectName()){found=&entry;break;}
 if(!found)return false;const LeiluanReceiptReceipt receipt=*found;
 if(!leiluanReceiptSameSpec(spec,receipt.spec)||targets!=receipt.targets)return false;
 auto selected=prepareAILeiluanSelection(r,p,receipt.token,admit);
 if(selected.outcome!=AIFaqiOutcome::Ready||selected.sourceWire!=receipt.wire
  ||!leiluanReceiptSameSpec(leiluanReceiptSpecOf(selected.use.card,spec.conversionId),receipt.spec)
  ||selected.use.activationRef!=receipt.activation||selected.use.sourceRef!=receipt.source)return false;
 use=selected.use;return true;
}

// ---- leiluan ----
namespace {
struct LeiluanStack{lua_State*L;int n;explicit LeiluanStack(lua_State*s):L(s),n(lua_gettop(s)){}~LeiluanStack(){lua_settop(L,n);}};
struct LeiluanRow{
 QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;QString token;
 QString offered;quint64 producerDecision=0,producerRevision=0;
 QList<int>materials;int physical=-1;
};
class LeiluanLedger final:public QObject{
public:explicit LeiluanLedger(Room&r):QObject(&r){}lua_State*L=nullptr;quint64 generation=0;
 QHash<const ServerPlayer*,LeiluanRow>rows;
};
LeiluanLedger*leiluanLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<LeiluanLedger*>(c))return l;return create?new LeiluanLedger(r):nullptr;}
bool leiluanCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&q.stateRevision==p->getRoom()->roomRuntime()->stateRevision()&&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool leiluanShown(const AIRequest&q,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact)exact=Sanguosha->getCard(id);if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void leiluanField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool leiluanSelf(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool leiluanAbsentNewIndex(lua_State*L,int t){LeiluanStack st(L);if(!lua_getmetatable(L,t))return true;leiluanField(L,-1,"__newindex");return lua_isnil(L,-1);}
bool leiluanPlain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool leiluanEnv(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool leiluanCode(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&leiluanEnv(L,f);}
bool leiluanSourceCallback(Room&r,lua_State*L){
 LeiluanStack stack(L);AILuaInspect::globals(L);leiluanField(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 leiluanField(L,-1,"ai_skill_use");if(!leiluanPlain(L,-1))return false;leiluanField(L,-1,"@@leiluan");
 return leiluanCode(r,L,-1,"lua/ai/ol_sp-ai.lua",2765,2774);
}
bool leiluanSourceProducer(Room&r,lua_State*L){
 LeiluanStack stack(L);AILuaInspect::globals(L);leiluanField(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 leiluanField(L,-1,"ai_skill_choice");if(!leiluanPlain(L,-1))return false;leiluanField(L,-1,"leiluan");
 return leiluanCode(r,L,-1,"lua/ai/ol_sp-ai.lua",2748,2763);
}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool leiluanLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));leiluanField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  leiluanField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool leiluanSourceReadBindings(Room&r,lua_State*L){
 LeiluanStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 leiluanField(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 leiluanField(L,sgs,"ai_skill_use");if(!leiluanPlain(L,-1))return false;leiluanField(L,-1,"@@leiluan");
 if(!leiluanCode(r,L,-1,"lua/ai/ol_sp-ai.lua",2765,2774))return false;
 leiluanField(L,sgs,"list");if(!leiluanCode(r,L,-1,"lua/utilities.lua",38,41))return false;
 leiluanField(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=leiluanCode(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=leiluanCode(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){leiluanField(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 leiluanField(L,globals,"table");if(!leiluanPlain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){leiluanField(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Executes only our fixed native assignment thunk. No legacy AI strategy call.
// The caller proved source-equivalent __newindex behavior before entering it.
struct LeiluanWrite {const AIRequest*request;ServerPlayer*owner;AIReadonlyCardLease lease;const Card*card;QList<ServerPlayer*>targets;int extra=0;};
int leiluanAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"Leiluan assignment inspection stack unavailable");
 auto*w=static_cast<LeiluanWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_gettop(L);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");
 lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAISlotCard(L,w->card))return luaL_error(L,"Leiluan native Card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"Leiluan native target list unavailable");lua_setfield(L,d,"to");
 if(!leiluanCurrent(*w->request,w->owner)||w->lease()!=w->card||!leiluanSelf(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))
  return luaL_error(L,"Leiluan actual owner/request changed during allocation");
 lua_pop(L,1);
 // Preserve source assignment (not rawset). If GC changed the exact receiver's
 // absent-key metatable after preflight, reject before dispatching that hook.
 leiluanField(L,1,"leiluanUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!leiluanAbsentNewIndex(L,1))return luaL_error(L,"Leiluan source __newindex changed during allocation");
 lua_pushvalue(L,d);lua_setfield(L,1,"leiluanUse");
 lua_pushvalue(L,d);return 1;
}
}
void clearAILeiluanSlots(Room&r){if(auto*l=leiluanLedger(r))delete l;}
bool hasAILeiluanSlot(Room&r,ServerPlayer*p,const QString&token){auto*l=leiluanLedger(r);if(!l||!p||token.isEmpty())return false;auto i=l->rows.constFind(p);return i!=l->rows.cend()&&i->owner==p&&i->token==token;}
QString writeAILeiluanSlot(const AIRequest&q,ServerPlayer*p,const QString&offered,AIReadonlyCardLease lease,const QStringList&names,const QJsonObject&fields,QString*error){
 auto fail=[&](const QString&s){if(error)*error=s;return QString();};
 if(!leiluanCurrent(q,p)||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="leiluan"||!q.choiceOptions.choices.contains(offered)||!lease)return fail("Leiluan source assignment requires actual current offered choice");
 if(fields.size()!=3||fields.value("isDummy")!=QJsonValue(true)||!fields.value("extra_target").isDouble()
  ||fields.value("extra_target").toDouble()!=fields.value("extra_target").toInt()||fields.value("current_targets_empty")!=QJsonValue(true))return fail("Leiluan full dummy field envelope unsupported");
 const Card*card=lease();if(!card)return fail("Leiluan exact producer custody expired");
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();if(physical>=0&&!leiluanShown(q,physical,card))return fail("Leiluan source physical card undisclosed");
 const auto costs=card->getSubcards();for(int id:costs)if(!leiluanShown(q,id))return fail("Leiluan source material undisclosed");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Leiluan game runtime unavailable");LeiluanStack stack(L);
 if(!leiluanSourceProducer(r,L))return fail("Leiluan actual producer callback changed");
 if(!leiluanSelf(L,r,p))return fail("Leiluan actual LuaAI self authentication failed");const int receiver=lua_absindex(L,-1);
 leiluanField(L,receiver,"leiluanUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!leiluanAbsentNewIndex(L,receiver))return fail("Leiluan source __newindex is not the direct assignment domain");
 LeiluanWrite w;w.request=&q;w.owner=p;w.lease=lease;w.card=card;w.extra=fields.value("extra_target").toInt();
 const auto roster=r.getAllPlayers(true);for(const auto&name:names){ServerPlayer*target=nullptr;for(auto*t:roster)if(t&&t->objectName()==name){target=t;break;}if(!target)return fail("Leiluan source target is outside current roster");w.targets<<target;}
 lua_pushcfunction(L,leiluanAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,2,1,0)!=LUA_OK)return fail("Leiluan native source assignment failed before completion");
 if(!leiluanCurrent(q,p)||!lease())return fail("Leiluan source assignment changed current request or custody");
 leiluanField(L,receiver,"leiluanUse");const bool exact=lua_rawequal(L,-1,-2);lua_pop(L,1);if(!exact||!leiluanPlain(L,-1))return fail("Leiluan assignment no longer names its actual source table");
 auto*l=leiluanLedger(r,true);if(l->L&&l->generation!=rt->lua().generation())return fail("Leiluan synchronized runtime generation changed");l->L=L;l->generation=rt->lua().generation();
 LeiluanRow row;row.owner=p;row.lease=std::move(lease);row.token="leiluan-actual-slot:"+QUuid::createUuid().toString(QUuid::WithoutBraces);row.offered=offered;row.materials=costs;row.physical=physical;row.producerDecision=q.decisionId;row.producerRevision=q.stateRevision;
 // Actual self owns the table/list wrappers. No extra Lua registry allocation
 // or old table identity is needed; the native lease retains only Card custody.
 l->rows[p]=row;
 if(!leiluanCurrent(q,p)||!row.lease())return fail("Leiluan source assignment publication changed current request/custody");
 if(error)error->clear();return row.token;
}
const Card*readAILeiluanSlot(const AIRequest&q,ServerPlayer*p,const QString&token,QStringList*targets,QString*offered,QString*error,AILeiluanSlotStatus*status){
 if(status)*status=AILeiluanSlotStatus::Unknown;
 auto fail=[&](const QString&s)->const Card*{if(error)*error=s;return nullptr;};
 if(!leiluanCurrent(q,p)||q.kind!=AIRequest::UseCard||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.pattern!="@@leiluan"||q.handlingMethod!=Card::MethodUse)return fail("Leiluan actual source read requires current response-use question");Room&r=*p->getRoom();auto*l=leiluanLedger(r);auto*rt=r.roomRuntime();
 const bool retained=l&&l->generation==rt->lua().generation()&&hasAILeiluanSlot(r,p,token);
 const LeiluanRow row=retained?l->rows.value(p):LeiluanRow();
 LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||(retained&&L!=l->L)||!lua_checkstack(L,4096))return fail("Leiluan actual source runtime unavailable");LeiluanStack stack(L);
 if(!leiluanSourceCallback(r,L))return fail("Leiluan actual source callback changed");
 if(!leiluanSelf(L,r,p))return fail("Leiluan actual LuaAI self changed");if(!leiluanLookup(L,-1,"leiluanUse"))return fail("Leiluan own slot lookup requires unproved dynamic getter");
 // Fixed Leiluan indexes d.card without a preceding d test.
 if(lua_isnil(L,-1)||lua_isboolean(L,-1)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,-1)))return fail("Leiluan primitive plan index metamethod is unproved");
  if(status)*status=AILeiluanSlotStatus::SourceError;return fail("attempt to index a nil or boolean value (local 'd')");
 }
 if(!lua_istable(L,-1))return fail("Leiluan source dummy is outside table/nil/boolean observation");const int d=lua_absindex(L,-1);
 if(!leiluanLookup(L,d,"card"))return fail("Leiluan card field requires unproved dynamic getter");const int value=lua_absindex(L,-1);
 const auto pass=[&]()->const Card*{if(!leiluanCurrent(q,p))return fail("Leiluan source decline changed current request");if(offered)*offered=row.offered;if(targets)targets->clear();if(error)error->clear();if(status)*status=AILeiluanSlotStatus::SourcePass;return nullptr;};
 if(!lua_toboolean(L,value))return pass(); // do not read dummy.to on this branch
 if(!leiluanLookup(L,d,"to"))return fail("Leiluan target field requires unproved dynamic getter");const int list=lua_absindex(L,-1);
 if(!leiluanSourceReadBindings(r,L))return fail("Leiluan actual source serializer/list bindings changed");
 // Source has no `and d.to` guard. sgs.list(nil/false) fails at qlist.length.
 if(!lua_toboolean(L,list)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,list)))return fail("Leiluan target primitive metatable is unproved");
  if(status)*status=AILeiluanSlotStatus::SourceError;return fail("sgs.qlist: received nil list");
 }
 if(!retained)return fail("Leiluan actual truthy plan has no retained producer custody");
 // A new plain source table is lawful if both actually consumed fields retain
 // independently authenticated custody. An old table identity is NOT authority.
 if(!leiluanPlain(L,d))return fail("Leiluan truthy source fields require a plain observed plan");
 const Card*card=row.lease?row.lease():nullptr;if(!card)return fail("Leiluan actual source Card custody expired");
 if(!matchAISlotCard(L,value,card)||!matchAINativeObjectMethods(r,L,value,"Card",{"toString"}))return fail("Leiluan actual source Card identity or native serializer changed");
 QList<ServerPlayer*>actual;if(!readAISlotTargets(L,list,actual))return fail("Leiluan actual source target list custody changed");
 if(!leiluanSourceReadBindings(r,L))return fail("Leiluan actual source serializer/list bindings changed");
 // Only now does source consume a Card face/wire. Printed IDs cannot grant it.
 if(row.physical>=0&&!leiluanShown(q,row.physical,card))return fail("Leiluan actual source physical observation expired");
 for(int id:row.materials)if(!leiluanShown(q,id))return fail("Leiluan actual source material observation expired");
 if(card->getSubcards()!=row.materials)return fail("Leiluan actual source material provenance changed");
 const auto roster=r.getAllPlayers(true);QStringList names;for(auto*t:actual){if(!t||!roster.contains(t))return fail("Leiluan actual source target is not a live roster identity");names<<t->objectName();}
 if(!leiluanCurrent(q,p)||row.lease()!=card)return fail("Leiluan actual source read changed current request/custody");
 if(targets)*targets=names;if(offered)*offered=row.offered;if(error)error->clear();if(status)*status=AILeiluanSlotStatus::Card;return card;
}

bool registerAILeiluanSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/ol_sp-ai.lua",QByteArray("d8cc0c6d06b58c3d7306e5d742e44247e88b2050f94df886600f840e68b4205a"),error)
 &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
std::unique_ptr<Card> constructAILeiluan(const AIRequest&q,ServerPlayer*p,const QString&name,QString*error){
 auto fail=[&](const QString&s)->std::unique_ptr<Card>{if(error)*error=s;return {};};
 if(!leiluanCurrent(q,p)||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="leiluan"||!q.choiceOptions.choices.contains(name))return fail("Leiluan constructor requires an actual offered declaration");
 // Actual native RoundEnd builds offered names from recorded discarded NDTricks.
 // The current native choice discloses the exact names; no history/tag oracle.
 std::unique_ptr<Card>c(Sanguosha->cloneCard(name,Card::NoSuit,0));
 if(!c){if(error)error->clear();return {};}
 c->setSkillName("leiluan");if(error)error->clear();return c;
}

// ---- jianman-receipt ----
namespace {
struct JianmanReceiptReceipt {
 QWeakPointer<AIConversionLedger> ledger;quint64 decision=0,revision=0;
 QString viewer,token,wire;QStringList targets;AICardSpec spec;
 SkillInstanceRef activation,source;
};
class JianmanReceiptLedger final:public QObject{public:explicit JianmanReceiptLedger(Room&r):QObject(&r){}QList<JianmanReceiptReceipt>receipts;};
JianmanReceiptLedger*jianmanReceiptLedger(Room*r,bool create=false){if(!r)return nullptr;for(auto*c:r->children())if(auto*l=dynamic_cast<JianmanReceiptLedger*>(c))return l;return create?new JianmanReceiptLedger(*r):nullptr;}
bool jianmanReceiptSameSpec(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number
 &&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
AICardSpec jianmanReceiptSpecOf(const Card *c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();
 s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
QJsonObject jianmanReceiptJsonSpec(const AICardSpec&s){QJsonArray costs;for(int id:s.subcardIds)costs<<id;
 return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",costs},{"conversion_id",s.conversionId}};}

bool jianmanReceiptCurrent(const AIRequest&r,const ServerPlayer*p){return p&&p->getRoom()&&r.decisionId>0
 &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
 &&r.stateRevision==p->getRoom()->roomRuntime()->stateRevision();}
}
AIFaqiSelection prepareAIJianmanSelection(const AIRequest&r,ServerPlayer*p,
 const QString&token,const std::function<bool(CardUseStruct&)> &admit){
 AIFaqiSelection out;AIJianmanSlotStatus slotStatus=AIJianmanSlotStatus::Unknown;
 const Card *saved=readAIJianmanSlot(r,p,token,&out.targets,nullptr,&out.error,&slotStatus);
 if(slotStatus==AIJianmanSlotStatus::SourcePass)out.outcome=AIFaqiOutcome::SourcePass;
 if(slotStatus==AIJianmanSlotStatus::SourceError)out.outcome=AIFaqiOutcome::SourceError;
 if(!saved||!admit)return out;

 // Exact fixed source @@jianman serializes now, not at AG. Card::Parse intentionally
 // drops old request instance fields and current native attribution starts anew.
 out.sourceWire=saved->toString();
 const Card *parsed=Card::Parse(out.sourceWire);
 if(!parsed){out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved Jianman source wire has no native parsed product";return out;}
 out.use.from=p;
 // Numeric Card::Parse returns an Engine-owned wrapper even when filtered.
 // The wire form, not isVirtualCard(include_filter), determines parse custody.
 bool physicalWire=false;out.sourceWire.toInt(&physicalWire);
 // Card::Parse has already scheduled every virtual return with the lifetime
 // manager. Retain the normal event-payload lease without a second raw-pointer
 // OwnedCardPtr deleter: Lua-boxed CardUseStruct copies can outlive shutdown's
 // releaseEventPayloads/preclose drain. Their destruction must not dereference
 // an already retired parsed Card. Physical returns remain Engine-owned.
 out.use=CardUseStruct(parsed,p);
 for(const auto &name:out.targets){auto *target=p->getRoom()->findPlayerByObjectName(name,true);
  if(!target){out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved Jianman target no longer exists";return out;}out.use.to<<target;}
 const QString skill=parsed->getActivationSkillName();
 const auto *vs=Sanguosha->getViewAsSkill(skill);
 const auto *v2=dynamic_cast<const ViewAsSkillV2 *>(vs);
 // No post-revision check is advertised as a mutation guard. Only these fixed
 // compiled V2 paths are audited side-effect/inquiry-free during reconstruction.
 if(vs && (skill!="jianman" && (!v2 || skill!="limu"))){
  out.error="Jianman original provider is outside audited inquiry-free reconstruction";return out;
 }
 if(skill=="jianman"){
  if(!vs||v2||!vs->isEnabledAtResponse(p,"@@jianman")){out.outcome=AIFaqiOutcome::NativeRejected;out.error="Jianman native response is disabled";return out;}
  const Card*product=vs->viewAs(QList<const Card*>());
  std::unique_ptr<const Card> owned(product&&product->isVirtualCard()?product:nullptr);
  if(!product||!product->isVirtualCard()||product->toString()!=out.sourceWire){out.outcome=AIFaqiOutcome::NativeRejected;out.error="Jianman current native zero-cost product differs from saved source";return out;}
 }
 // Ordinary V1 declarations do not inquire. A solely attached multi-provider
 // V1 source would invoke native provider choice; never execute it in a query.
 if(skill=="jianman"){
  bool ordinary=false;int attached=0;
  for(const auto&i:p->getSkillInstances())if(i.skillName==skill){
   if(i.source==SourceAttached&&p->getSkillInstanceStateValue(skill,i.instanceID,"legacy_activation_lifecycle").toBool()){
    if(p->getValidSkillInstanceIds(skill).contains(i.instanceID))++attached;
   }else ordinary=true;
  }
  if(!ordinary&&attached>1){out.error="Jianman attached provider inquiry is outside this readonly selection";return out;}
 }
 bool privateAttachment=false;
 for(const auto &instance:p->getSkillInstances())if(instance.skillName==skill&&instance.source==SourceAttached
  &&p->getSkillInstanceStateValue(skill,instance.instanceID,"legacy_activation_lifecycle").toBool())privateAttachment=true;
 if(skill!="jianman" && !v2 && (privateAttachment || parsed->getActivationSkillInstanceId()!=0)){
  out.error="Jianman non-ViewAs attachment continuation is not audited";return out;
 }
 if(v2 && parsed->getActivationSkillInstanceId()==0){
  QList<int> ids;for(int id:p->getValidSkillInstanceIds(skill))
   if(p->getRoom()->canShowGeneralForSkill(SkillInstanceRef(p->objectName(),SkillInstanceKey(skill,id))))ids<<id;
  const int scoped=p->getMark(ViewAsSkillV2::borrowedActivationMarkName(skill));
  if(scoped>0){if(!ids.contains(scoped)){out.outcome=AIFaqiOutcome::NativeRejected;out.error="native borrowed provider is unavailable";return out;}ids={scoped};}
  if(ids.size()>1){out.error="Jianman current original provider needs native instance-choice continuation";return out;}
 }
 // Source getCard alternatives are own cards. A numeric parse never confers
 // authority over a foreign/discard card merely because it was observed.
 if(physicalWire && (!p->handCards().contains(parsed->getEffectiveId())
   &&!p->getEquipsId().contains(parsed->getEffectiveId())&&!p->getHandPile().contains(parsed->getEffectiveId()))){
  out.outcome=AIFaqiOutcome::NativeRejected;out.error="saved physical replacement left original owner material domain";return out;
 }
 if(!v2 && !physicalWire && parsed->getTypeId()==Card::TypeSkill){
  out.error="Jianman non-ViewAs SkillCard native activation is not audited";return out;
 }
 if(!admit(out.use)){out.outcome=AIFaqiOutcome::NativeRejected;out.error="Jianman current original native provider rejected saved source";return out;}
 {
  QStringList actualTargets;QString why;const Card*actual=readAIJianmanSlot(r,p,token,&actualTargets,nullptr,&why);
  if(!actual||actual!=saved||actualTargets!=out.targets||actual->toString()!=out.sourceWire){out.error=why.isEmpty()?"Jianman actual source changed during admission":why;return out;}
 }
 if(!jianmanReceiptCurrent(r,p)){out.error="Jianman reconstruction changed current request state";return out;}
 out.outcome=AIFaqiOutcome::Ready;out.error.clear();return out;
}

QJsonObject issueAIJianmanReceipt(const AIRequest&r,ServerPlayer*p,const QString&token,
 const std::function<bool(CardUseStruct&)>&admit){
 auto selected=prepareAIJianmanSelection(r,p,token,admit);
 if(selected.outcome==AIFaqiOutcome::SourcePass)return{{"source_pass",true}};
 if(selected.outcome==AIFaqiOutcome::SourceError)return{{"source_error",selected.error}};
 if(selected.outcome!=AIFaqiOutcome::Ready)return{{"error",selected.error},{"native_rejected",selected.outcome==AIFaqiOutcome::NativeRejected}};
 if(!r.conversionLedger)return{{"error","Jianman request ledger unavailable"}};
 const int id=r.cardConversions.size()+r.conversionLedger->tickets.size()+1;
 JianmanReceiptReceipt receipt;receipt.ledger=r.conversionLedger;receipt.decision=r.decisionId;receipt.revision=r.stateRevision;
 receipt.viewer=p->objectName();receipt.token=token;receipt.targets=selected.targets;receipt.wire=selected.sourceWire;
 receipt.spec=jianmanReceiptSpecOf(selected.use.card,id);receipt.activation=selected.use.activationRef;receipt.source=selected.use.sourceRef;
 AICardConversionView reservation;reservation.conversionId=id;reservation.policyClass="@jianman_saved_reconstruction";
 reservation.name=receipt.spec.name;reservation.available=false; // Never generic buildSpec authority.
 r.conversionLedger->tickets<<reservation;
 auto *l=jianmanReceiptLedger(p->getRoom(),true);
 for(auto it=l->receipts.begin();it!=l->receipts.end();)if(it->ledger.isNull())it=l->receipts.erase(it);else ++it;
 l->receipts<<receipt;
 return{{"card_spec",jianmanReceiptJsonSpec(receipt.spec)},{"targets",QJsonArray::fromStringList(receipt.targets)}};
}
bool hasAIJianmanReceipt(const AIRequest&r,int id){
 // A marker only; final lookup additionally authenticates the Room-owned proof.
 if(!r.conversionLedger)return false;
 for(const auto &ticket:r.conversionLedger->tickets)
  if(ticket.conversionId==id&&ticket.policyClass=="@jianman_saved_reconstruction")return true;
 return false;
}
bool applyAIJianmanReceipt(const AIRequest&r,ServerPlayer*p,const AICardSpec&spec,
 const QStringList&targets,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!jianmanReceiptCurrent(r,p)||!hasAIJianmanReceipt(r,spec.conversionId))return false;
 auto *l=jianmanReceiptLedger(p->getRoom());if(!l)return false;
 const JianmanReceiptReceipt *found=nullptr;
 for(const auto &entry:l->receipts)if(entry.spec.conversionId==spec.conversionId
  &&entry.ledger.toStrongRef()==r.conversionLedger&&entry.decision==r.decisionId
  &&entry.revision==r.stateRevision&&entry.viewer==p->objectName()){found=&entry;break;}
 if(!found)return false;const JianmanReceiptReceipt receipt=*found;
 if(!jianmanReceiptSameSpec(spec,receipt.spec)||targets!=receipt.targets)return false;
 auto selected=prepareAIJianmanSelection(r,p,receipt.token,admit);
 if(selected.outcome!=AIFaqiOutcome::Ready||selected.sourceWire!=receipt.wire
  ||!jianmanReceiptSameSpec(jianmanReceiptSpecOf(selected.use.card,spec.conversionId),receipt.spec)
  ||selected.use.activationRef!=receipt.activation||selected.use.sourceRef!=receipt.source)return false;
 use=selected.use;return true;
}

// ---- jianman ----
namespace {
struct JianmanStack{lua_State*L;int n;explicit JianmanStack(lua_State*s):L(s),n(lua_gettop(s)){}~JianmanStack(){lua_settop(L,n);}};
struct JianmanRow{
 QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;QString token;
 QString offered;quint64 producerDecision=0,producerRevision=0;
 QList<int>materials;int physical=-1;
};
class JianmanLedger final:public QObject{
public:explicit JianmanLedger(Room&r):QObject(&r){}lua_State*L=nullptr;quint64 generation=0;
 QHash<const ServerPlayer*,JianmanRow>rows;
};
JianmanLedger*jianmanLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<JianmanLedger*>(c))return l;return create?new JianmanLedger(r):nullptr;}
bool jianmanCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&q.stateRevision==p->getRoom()->roomRuntime()->stateRevision()&&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool jianmanShown(const AIRequest&q,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 if(!exact)exact=Sanguosha->getCard(id);if(!exact)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity
   ||!globalCardLifetimeManager().isLive(v.nativeIdentity)||v.nativeIdentity->address!=exact)return false;
  return v.cardId==exact->getId()&&v.effectiveId==exact->getEffectiveId()
   &&v.objectName==exact->objectName()&&v.className==exact->getClassName()
   &&v.suit==int(exact->getSuit())&&v.number==exact->getNumber()
   &&v.skillName==exact->getSkillName(false)&&v.subcardIds==exact->getSubcards()
   &&v.virtualCard==exact->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void jianmanField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool jianmanSelf(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool jianmanAbsentNewIndex(lua_State*L,int t){JianmanStack st(L);if(!lua_getmetatable(L,t))return true;jianmanField(L,-1,"__newindex");return lua_isnil(L,-1);}
bool jianmanPlain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool jianmanEnv(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool jianmanCode(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&jianmanEnv(L,f);}
bool jianmanSourceCallback(Room&r,lua_State*L){
 JianmanStack stack(L);AILuaInspect::globals(L);jianmanField(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 jianmanField(L,-1,"ai_skill_use");if(!jianmanPlain(L,-1))return false;jianmanField(L,-1,"@@jianman");
 return jianmanCode(r,L,-1,"lua/ai/ol_sp-ai.lua",1881,1890);
}
bool jianmanSourceProducer(Room&r,lua_State*L){
 JianmanStack stack(L);AILuaInspect::globals(L);jianmanField(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 jianmanField(L,-1,"ai_skill_choice");if(!jianmanPlain(L,-1))return false;jianmanField(L,-1,"jianman");
 return jianmanCode(r,L,-1,"lua/ai/ol_sp-ai.lua",1866,1879);
}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool jianmanLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));jianmanField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  jianmanField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool jianmanSourceReadBindings(Room&r,lua_State*L){
 JianmanStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 jianmanField(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 jianmanField(L,sgs,"ai_skill_use");if(!jianmanPlain(L,-1))return false;jianmanField(L,-1,"@@jianman");
 if(!jianmanCode(r,L,-1,"lua/ai/ol_sp-ai.lua",1881,1890))return false;
 jianmanField(L,sgs,"list");if(!jianmanCode(r,L,-1,"lua/utilities.lua",38,41))return false;
 jianmanField(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=jianmanCode(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=jianmanCode(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){jianmanField(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 jianmanField(L,globals,"table");if(!jianmanPlain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){jianmanField(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Executes only our fixed native assignment thunk. No legacy AI strategy call.
// The caller proved source-equivalent __newindex behavior before entering it.
struct JianmanWrite {const AIRequest*request;ServerPlayer*owner;AIReadonlyCardLease lease;const Card*card;QList<ServerPlayer*>targets;int extra=0;};
int jianmanAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"Jianman assignment inspection stack unavailable");
 auto*w=static_cast<JianmanWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_gettop(L);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");
 lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAISlotCard(L,w->card))return luaL_error(L,"Jianman native Card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"Jianman native target list unavailable");lua_setfield(L,d,"to");
 if(!jianmanCurrent(*w->request,w->owner)||w->lease()!=w->card||!jianmanSelf(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))
  return luaL_error(L,"Jianman actual owner/request changed during allocation");
 lua_pop(L,1);
 // Preserve source assignment (not rawset). If GC changed the exact receiver's
 // absent-key metatable after preflight, reject before dispatching that hook.
 jianmanField(L,1,"jianmanUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!jianmanAbsentNewIndex(L,1))return luaL_error(L,"Jianman source __newindex changed during allocation");
 lua_pushvalue(L,d);lua_setfield(L,1,"jianmanUse");
 lua_pushvalue(L,d);return 1;
}
}
void clearAIJianmanSlots(Room&r){if(auto*l=jianmanLedger(r))delete l;}
bool hasAIJianmanSlot(Room&r,ServerPlayer*p,const QString&token){auto*l=jianmanLedger(r);if(!l||!p||token.isEmpty())return false;auto i=l->rows.constFind(p);return i!=l->rows.cend()&&i->owner==p&&i->token==token;}
QString writeAIJianmanSlot(const AIRequest&q,ServerPlayer*p,const QString&offered,AIReadonlyCardLease lease,const QStringList&names,const QJsonObject&fields,QString*error){
 auto fail=[&](const QString&s){if(error)*error=s;return QString();};
 if(!jianmanCurrent(q,p)||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="jianman"||!q.choiceOptions.choices.contains(offered)||!lease)return fail("Jianman source assignment requires actual current offered choice");
 if(fields.size()!=3||fields.value("isDummy")!=QJsonValue(true)||!fields.value("extra_target").isDouble()
  ||fields.value("extra_target").toDouble()!=fields.value("extra_target").toInt()||fields.value("current_targets_empty")!=QJsonValue(true))return fail("Jianman full dummy field envelope unsupported");
 const Card*card=lease();if(!card)return fail("Jianman exact producer custody expired");
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();if(physical>=0&&!jianmanShown(q,physical,card))return fail("Jianman source physical card undisclosed");
 const auto costs=card->getSubcards();for(int id:costs)if(!jianmanShown(q,id))return fail("Jianman source material undisclosed");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Jianman game runtime unavailable");JianmanStack stack(L);
 if(!jianmanSourceProducer(r,L))return fail("Jianman actual producer callback changed");
 if(!jianmanSelf(L,r,p))return fail("Jianman actual LuaAI self authentication failed");const int receiver=lua_absindex(L,-1);
 jianmanField(L,receiver,"jianmanUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!jianmanAbsentNewIndex(L,receiver))return fail("Jianman source __newindex is not the direct assignment domain");
 JianmanWrite w;w.request=&q;w.owner=p;w.lease=lease;w.card=card;w.extra=fields.value("extra_target").toInt();
 const auto roster=r.getAllPlayers(true);for(const auto&name:names){ServerPlayer*target=nullptr;for(auto*t:roster)if(t&&t->objectName()==name){target=t;break;}if(!target)return fail("Jianman source target is outside current roster");w.targets<<target;}
 lua_pushcfunction(L,jianmanAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,2,1,0)!=LUA_OK)return fail("Jianman native source assignment failed before completion");
 if(!jianmanCurrent(q,p)||!lease())return fail("Jianman source assignment changed current request or custody");
 jianmanField(L,receiver,"jianmanUse");const bool exact=lua_rawequal(L,-1,-2);lua_pop(L,1);if(!exact||!jianmanPlain(L,-1))return fail("Jianman assignment no longer names its actual source table");
 auto*l=jianmanLedger(r,true);if(l->L&&l->generation!=rt->lua().generation())return fail("Jianman synchronized runtime generation changed");l->L=L;l->generation=rt->lua().generation();
 JianmanRow row;row.owner=p;row.lease=std::move(lease);row.token="jianman-actual-slot:"+QUuid::createUuid().toString(QUuid::WithoutBraces);row.offered=offered;row.materials=costs;row.physical=physical;row.producerDecision=q.decisionId;row.producerRevision=q.stateRevision;
 // Actual self owns the table/list wrappers. No extra Lua registry allocation
 // or old table identity is needed; the native lease retains only Card custody.
 l->rows[p]=row;
 if(!jianmanCurrent(q,p)||!row.lease())return fail("Jianman source assignment publication changed current request/custody");
 if(error)error->clear();return row.token;
}
const Card*readAIJianmanSlot(const AIRequest&q,ServerPlayer*p,const QString&token,QStringList*targets,QString*offered,QString*error,AIJianmanSlotStatus*status){
 if(status)*status=AIJianmanSlotStatus::Unknown;
 auto fail=[&](const QString&s)->const Card*{if(error)*error=s;return nullptr;};
 if(!jianmanCurrent(q,p)||q.kind!=AIRequest::UseCard||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.pattern!="@@jianman"||q.handlingMethod!=Card::MethodUse)return fail("Jianman actual source read requires current response-use question");Room&r=*p->getRoom();auto*l=jianmanLedger(r);auto*rt=r.roomRuntime();
 const bool retained=l&&l->generation==rt->lua().generation()&&hasAIJianmanSlot(r,p,token);
 const JianmanRow row=retained?l->rows.value(p):JianmanRow();
 LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();if(!L||(retained&&L!=l->L)||!lua_checkstack(L,4096))return fail("Jianman actual source runtime unavailable");JianmanStack stack(L);
 if(!jianmanSourceCallback(r,L))return fail("Jianman actual source callback changed");
 if(!jianmanSelf(L,r,p))return fail("Jianman actual LuaAI self changed");if(!jianmanLookup(L,-1,"jianmanUse"))return fail("Jianman own slot lookup requires unproved dynamic getter");
 // Fixed Jianman source tests d before d.card: nil/false decline without indexing.
 if(!lua_toboolean(L,-1)){if(status)*status=AIJianmanSlotStatus::SourcePass;if(error)error->clear();return nullptr;}
 if(lua_isboolean(L,-1)){
  if(AILuaInspect::primitiveMetatable(L,LUA_TBOOLEAN))return fail("Jianman true plan index metamethod is unproved");
  if(status)*status=AIJianmanSlotStatus::SourceError;return fail("attempt to index a boolean value (local 'd')");
 }
 if(!lua_istable(L,-1))return fail("Jianman source dummy is outside table/nil/boolean observation");const int d=lua_absindex(L,-1);
 if(!jianmanLookup(L,d,"card"))return fail("Jianman card field requires unproved dynamic getter");const int value=lua_absindex(L,-1);
 const auto pass=[&]()->const Card*{if(!jianmanCurrent(q,p))return fail("Jianman source decline changed current request");if(offered)*offered=row.offered;if(targets)targets->clear();if(error)error->clear();if(status)*status=AIJianmanSlotStatus::SourcePass;return nullptr;};
 if(!lua_toboolean(L,value))return pass(); // do not read dummy.to on this branch
 if(!jianmanLookup(L,d,"to"))return fail("Jianman target field requires unproved dynamic getter");const int list=lua_absindex(L,-1);
 if(!jianmanSourceReadBindings(r,L))return fail("Jianman actual source serializer/list bindings changed");
 // Source has no `and d.to` guard. sgs.list(nil/false) fails at qlist.length.
 if(!lua_toboolean(L,list)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,list)))return fail("Jianman target primitive metatable is unproved");
  if(status)*status=AIJianmanSlotStatus::SourceError;return fail("sgs.qlist: received nil list");
 }
 if(!retained)return fail("Jianman actual truthy plan has no retained producer custody");
 // A new plain source table is lawful if both actually consumed fields retain
 // independently authenticated custody. An old table identity is NOT authority.
 if(!jianmanPlain(L,d))return fail("Jianman truthy source fields require a plain observed plan");
 const Card*card=row.lease?row.lease():nullptr;if(!card)return fail("Jianman actual source Card custody expired");
 if(!matchAISlotCard(L,value,card)||!matchAINativeObjectMethods(r,L,value,"Card",{"toString"}))return fail("Jianman actual source Card identity or native serializer changed");
 QList<ServerPlayer*>actual;if(!readAISlotTargets(L,list,actual))return fail("Jianman actual source target list custody changed");
 if(!jianmanSourceReadBindings(r,L))return fail("Jianman actual source serializer/list bindings changed");
 // Only now does source consume a Card face/wire. Printed IDs cannot grant it.
 if(row.physical>=0&&!jianmanShown(q,row.physical,card))return fail("Jianman actual source physical observation expired");
 for(int id:row.materials)if(!jianmanShown(q,id))return fail("Jianman actual source material observation expired");
 if(card->getSubcards()!=row.materials)return fail("Jianman actual source material provenance changed");
 const auto roster=r.getAllPlayers(true);QStringList names;for(auto*t:actual){if(!t||!roster.contains(t))return fail("Jianman actual source target is not a live roster identity");names<<t->objectName();}
 if(!jianmanCurrent(q,p)||row.lease()!=card)return fail("Jianman actual source read changed current request/custody");
 if(targets)*targets=names;if(offered)*offered=row.offered;if(error)error->clear();if(status)*status=AIJianmanSlotStatus::Card;return card;
}

bool registerAIJianmanSources(Room&r,lua_State*L,QString*error){
 return registerAIPinnedSource(r,L,"lua/ai/ol_sp-ai.lua",QByteArray("d8cc0c6d06b58c3d7306e5d742e44247e88b2050f94df886600f840e68b4205a"),error)
 &&registerAIPinnedSource(r,L,"lua/utilities.lua",QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
std::unique_ptr<Card> constructAIJianman(const AIRequest&q,ServerPlayer*p,const QString&name,QString*error){
 auto fail=[&](const QString&s)->std::unique_ptr<Card>{if(error)*error=s;return {};};
 if(!jianmanCurrent(q,p)||q.kind!=AIRequest::Choice||q.choiceOptions.reason!="jianman"||!q.choiceOptions.choices.contains(name))return fail("Jianman constructor requires an actual offered declaration");
 // Native Jianman builds choices only from the two recorded BasicCard names.
 // No history/tag read is needed: the current native choice is the disclosure.
 std::unique_ptr<Card>c(Sanguosha->cloneCard(name,Card::NoSuit,0));
 if(!c){if(error)error->clear();return {};}
 c->setSkillName("_jianman");if(error)error->clear();return c;
}

// ---- xuanzhu-response ----
#include "ai-runtime.h"
#include "ol.h"
#include <typeinfo>
namespace {
struct XuanzhuResponseFrame{
 Room*room=nullptr;QPointer<ServerPlayer>owner;QPointer<const Card>card;
 std::shared_ptr<const CardLifetimeToken>token;CardLifetimeLease lease;
 QString chosen;bool claimed=false;XuanzhuResponseFrame*previous=nullptr;
 const Card*liveCard()const{
  // Identity custody for the exact accepted native outer SkillCard. This is
  // not a readonly observation and grants no access to its tags or graph.
  if(card.isNull()||!token||!token->live||!lease.isValid())return nullptr;
  return globalCardLifetimeManager().liveToken(card.data())==token?card.data():nullptr;
 }
};
thread_local XuanzhuResponseFrame*xuanzhuResponseActive=nullptr;
struct Inquiry{XuanzhuResponseFrame*origin=nullptr;Inquiry*previous=nullptr;quint64 decision=0,revision=0;
 QWeakPointer<AIConversionLedger>ledger;bool bound=false;};
thread_local Inquiry*inquiry=nullptr;
bool xuanzhuResponseCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&p->getRoom()->getAllPlayers(true).contains(p)&&!p->getRoom()->roomRuntime()->isClosing()
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&aiRangeRequestCurrent(*p->getRoom(),q,p);}
bool xuanzhuResponsePlay(const AIRequest&q,ServerPlayer*p){if(!xuanzhuResponseCurrent(q,p)||q.kind!=AIRequest::Activate
 ||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!q.pattern.isEmpty()||p->getPhase()!=Player::Play)return false;
 // Presence is enough for a source readonly constructor. No extra hasSkill
 // invalidity/activation test is added to the reached source callback.
 for(const auto&i:p->getSkillInstances())if(i.skillName=="xuanzhu")return true;return false;}
bool xuanzhuResponseDisclosed(const AIRequest&q,const Card*c){if(!c)return false;
 auto match=[&](const AICardView&v){return v.nativeIdentity&&globalCardLifetimeManager().isLive(v.nativeIdentity)
  &&v.nativeIdentity->address==c&&v.cardId==c->getId()&&v.effectiveId==c->getEffectiveId()
  &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())
  &&v.number==c->getNumber()&&v.skillName==c->getSkillName(false)&&v.subcardIds==c->getSubcards()
  &&v.virtualCard==c->isVirtualCard();};
 for(const auto&v:q.worldView.handCards)if(match(v))return true;
 for(const auto&v:q.worldView.self.equips)if(match(v))return true;
 for(const auto&pile:q.worldView.self.piles)for(const auto&v:pile.cards)if(match(v))return true;
 return false;}
bool material(const AIRequest&q,ServerPlayer*p,int id,bool pile){
 // Source getCards('he') returns inner equipment objects. Authenticate that
 // exact disclosed native object, never substitute outer wrapper metadata.
 for(const auto*c:p->getCards("he"))if(c&&c->getEffectiveId()==id&&xuanzhuResponseDisclosed(q,c))return true;
 return pile&&p->getPile("xuanzhu").contains(id)&&xuanzhuResponseDisclosed(q,Sanguosha->getCard(id));
}
// The exact fixed ordinary V1 raw-wire path returns before GUI viewAs. Refuse
// private attachment resolution before it could select/inquire on a provider.
bool ordinaryRaw(ServerPlayer*p,const Card*c,QString&error){
 if(!c||c->getActivationSkillInstanceId()!=0){error="Xuanzhu raw source unexpectedly carries an instance";return false;}
 const QString name=c->getActivationSkillName();const auto*vs=Sanguosha->getViewAsSkill(name);
 if(dynamic_cast<const ViewAsSkillV2*>(vs)){error="Xuanzhu selected replacement reaches a V2 provider outside this fixed raw branch";return false;}
 bool ordinary=false,attached=false;
 for(const auto&i:p->getSkillInstances())if(i.skillName==name){
  if(i.source==SourceAttached&&p->getSkillInstanceStateValue(name,i.instanceID,"legacy_activation_lifecycle").toBool())attached=true;
  else ordinary=true;
 }
 if(attached&&!ordinary){error="Xuanzhu solely attached provider requires separate native continuation";return false;}
 return true;
}
struct XuanzhuResponseReceipt{QWeakPointer<AIConversionLedger>ledger;QPointer<ServerPlayer>owner;quint64 decision=0,revision=0;
 bool first=false;int material=-1;QPointer<const Card>source;QString wire;QStringList targets;AICardSpec spec;SkillInstanceRef activation,root;};
class XuanzhuResponseLedger final:public QObject{public:explicit XuanzhuResponseLedger(Room&r):QObject(&r){}QList<XuanzhuResponseReceipt>rows;};
XuanzhuResponseLedger*xuanzhuResponseLedger(Room*r,bool create=false){if(!r)return nullptr;for(auto*c:r->children())if(auto*l=dynamic_cast<XuanzhuResponseLedger*>(c))return l;return create?new XuanzhuResponseLedger(*r):nullptr;}
AICardSpec xuanzhuResponseSpecOf(const Card*c,int id){AICardSpec s;s.name=c->objectName();s.suit=c->getSuit();s.number=c->getNumber();s.skillName=c->getSkillName(false);s.subcardIds=c->getSubcards();s.conversionId=id;return s;}
bool xuanzhuResponseSame(const AICardSpec&a,const AICardSpec&b){return a.name==b.name&&a.suit==b.suit&&a.number==b.number&&a.skillName==b.skillName&&a.subcardIds==b.subcardIds&&a.conversionId==b.conversionId;}
QJsonObject json(const AICardSpec&s){QJsonArray a;for(int id:s.subcardIds)a<<id;return{{"name",s.name},{"suit",s.suit},{"number",s.number},{"skill",s.skillName},{"subcards",a},{"conversion_id",s.conversionId}};}
bool prepare(const AIRequest&q,ServerPlayer*p,bool first,const AIReadonlyCardLease&lease,const QStringList&targets,
 int cost,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use,QString&wire,QString&error){
 auto fail=[&](const char*s){error=QString::fromLatin1(s);return false;};
 if(!xuanzhuResponseCurrent(q,p)||!admit||!lease)return fail("Xuanzhu selected request or custody expired");
 const Card*saved=lease();if(!saved)return fail("Xuanzhu exact selected source expired");
 if(first){
  if(!xuanzhuResponsePlay(q,p)||typeid(*saved)!=typeid(Xuanzhu2Card)||saved->getSkillName(false)!="xuanzhu2"||!targets.isEmpty()||!saved->getSubcards().isEmpty())return fail("Xuanzhu initial source differs from exact zero-cost SkillCard");
  wire=saved->toString();
 }else{
  if(!aiXuanzhuQuestion(q,p))return fail("Xuanzhu selection outside exact native inquiry");
  auto actual=readAIXuanzhuSlot(q,p,true);
  if(actual.kind!=AIXuanzhuSlotRead::Card||!actual.lease||actual.lease()!=saved||actual.targets!=targets)return fail("Xuanzhu actual source card or ordered targets changed");
  if(!material(q,p,cost,false))return fail("Xuanzhu selected source cost is not current disclosed owner HE");
  // Original response uses its saved Card name, NOT the independently chosen AG
  // mark used only by GUI viewAs. Ordinary raw V1 validation preserves that wire.
  wire="@XuanzhuCard="+QString::number(cost)+":"+actual.name;
 }
 // Parse schedules virtual products before returning. Protect that interval
 // until the following CardUseStruct acquires its event-payload native lease;
 // otherwise final native reconstruction can retire the product inside Parse.
 CardLifetimeScope parseCustody(globalCardLifetimeManager());
 const Card*parsed=Card::Parse(wire);if(!parsed)return fail("Xuanzhu exact source wire did not parse");
 // Parse already scheduled virtual returns; normal event-payload custody only.
 use=CardUseStruct(parsed,p);
 for(const auto&name:targets){auto*t=p->getRoom()->findPlayerByObjectName(name,true);if(!t)return fail("Xuanzhu ordered source target left roster");use.to<<t;}
 if(!ordinaryRaw(p,parsed,error)||!admit(use))return fail(error.isEmpty()?"Xuanzhu original native raw provider rejected":error.toUtf8().constData());
 if(!xuanzhuResponseCurrent(q,p)||lease()!=saved||(first&&saved->toString()!=wire))return fail("Xuanzhu source changed during raw reconstruction");
 if(!first){auto actual=readAIXuanzhuSlot(q,p,true);if(actual.kind!=AIXuanzhuSlotRead::Card||!actual.lease||actual.lease()!=saved||actual.targets!=targets||"@XuanzhuCard="+QString::number(cost)+":"+actual.name!=wire)return fail("Xuanzhu actual slot changed during reconstruction");}
 return true;
}
QJsonObject issue(const AIRequest&q,ServerPlayer*p,bool first,AIReadonlyCardLease source,const QStringList&targets,int cost,const std::function<bool(CardUseStruct&)>&admit){
 CardUseStruct use;QString wire,error;if(!prepare(q,p,first,source,targets,cost,admit,use,wire,error))return{{"error",error}};
 if(!q.conversionLedger)return{{"error","Xuanzhu current conversion ledger unavailable"}};
 const int id=q.cardConversions.size()+q.conversionLedger->tickets.size()+1;
 XuanzhuResponseReceipt receipt;receipt.ledger=q.conversionLedger;receipt.owner=p;receipt.decision=q.decisionId;receipt.revision=q.stateRevision;
 receipt.first=first;receipt.material=cost;receipt.source=source();q.conversionLedger->observedProducts.insert(id,std::move(source));receipt.wire=wire;receipt.targets=targets;receipt.spec=xuanzhuResponseSpecOf(use.card,id);receipt.activation=use.activationRef;receipt.root=use.sourceRef;
 AICardConversionView marker;marker.conversionId=id;marker.policyClass="@xuanzhu_raw_source";marker.name=receipt.spec.name;marker.available=false;
 q.conversionLedger->tickets<<marker;p->getRoom()->roomRuntime()->ai().trackObservationLedger(q.conversionLedger);auto*l=xuanzhuResponseLedger(p->getRoom(),true);
 for(auto i=l->rows.begin();i!=l->rows.end();)if(i->ledger.isNull())i=l->rows.erase(i);else ++i;l->rows<<receipt;
 return{{"card_spec",json(receipt.spec)},{"targets",QJsonArray::fromStringList(targets)}};
}
}
struct AIXuanzhuQuestionScope::State{XuanzhuResponseFrame f;};
AIXuanzhuQuestionScope::AIXuanzhuQuestionScope(Room&r,ServerPlayer*p,const Card*c,const QString&chosen):d(new State){
 d->f.previous=xuanzhuResponseActive;xuanzhuResponseActive=&d->f; // Invalid/nested entries shadow the outer scope.
 if(p&&p->getRoom()==&r&&c&&typeid(*c)==typeid(Xuanzhu2Card)&&!chosen.isEmpty()){
  d->f.room=&r;d->f.owner=p;d->f.card=c;d->f.chosen=chosen;
  // This compiled on-use scope borrows its actual live `this` object. The
  // native factory need not have registered it yet: observeCard registers
  // lifetime/destruction tracking without taking ownership or disclosing face.
  d->f.token=globalCardLifetimeManager().observeCard(const_cast<Card*>(c));
  d->f.lease=CardLifetimeLease(globalCardLifetimeManager(),d->f.token);
 }
}
AIXuanzhuQuestionScope::~AIXuanzhuQuestionScope(){xuanzhuResponseActive=d->f.previous;}
struct AIXuanzhuInquiryScope::State{Inquiry i;};
AIXuanzhuInquiryScope::AIXuanzhuInquiryScope(Room&r,ServerPlayer*p,const QString&pattern,const QString&prompt,int method):d(new State){
 d->i.previous=inquiry;inquiry=&d->i; // Every native inquiry shadows the outer one.
 if(!xuanzhuResponseActive||xuanzhuResponseActive->claimed)return;
 xuanzhuResponseActive->claimed=true; // The compiled origin may authorize only its immediate first inquiry.
 if(xuanzhuResponseActive->room==&r&&xuanzhuResponseActive->owner==p&&xuanzhuResponseActive->liveCard()
  &&pattern=="@@xuanzhu"&&prompt=="xuanzhu0:"+xuanzhuResponseActive->chosen&&method==int(Card::MethodUse))d->i.origin=xuanzhuResponseActive;
}
AIXuanzhuInquiryScope::~AIXuanzhuInquiryScope(){inquiry=d->i.previous;}
namespace {
bool questionShape(const AIRequest&q,ServerPlayer*p){return xuanzhuResponseCurrent(q,p)&&inquiry&&inquiry->origin
 &&inquiry->origin==xuanzhuResponseActive&&xuanzhuResponseActive->room==p->getRoom()&&xuanzhuResponseActive->owner==p&&xuanzhuResponseActive->liveCard()
 &&q.kind==AIRequest::UseCard&&q.pattern=="@@xuanzhu"&&q.prompt=="xuanzhu0:"+xuanzhuResponseActive->chosen
 &&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&q.handlingMethod==Card::MethodUse;}
}
void bindAIXuanzhuQuestion(const AIRequest&q,ServerPlayer*p){
 if(!questionShape(q,p)||inquiry->bound||!q.conversionLedger)return;
 inquiry->bound=true;inquiry->decision=q.decisionId;inquiry->revision=q.stateRevision;inquiry->ledger=q.conversionLedger;
}
bool aiXuanzhuQuestion(const AIRequest&q,ServerPlayer*p){return questionShape(q,p)&&inquiry->bound
 &&inquiry->decision==q.decisionId&&inquiry->revision==q.stateRevision
 &&q.conversionLedger&&inquiry->ledger.toStrongRef()==q.conversionLedger;}
std::unique_ptr<Card> constructAIXuanzhuFirst(const AIRequest&q,ServerPlayer*p,QString*error){
 if(!xuanzhuResponsePlay(q,p)){if(error)*error="Xuanzhu first source requires current owner Play";return{};}
 std::unique_ptr<Card>c(new Xuanzhu2Card);c->setSkillName("xuanzhu2");if(error)error->clear();return c;
}
QJsonObject issueAIXuanzhuFirst(const AIRequest&q,ServerPlayer*p,AIReadonlyCardLease c,const std::function<bool(CardUseStruct&)>&f){return issue(q,p,true,std::move(c),{},-1,f);}
QJsonObject issueAIXuanzhuSaved(const AIRequest&q,ServerPlayer*p,int materialId,const QString&name,const QStringList&t,const std::function<bool(CardUseStruct&)>&f){
 auto actual=readAIXuanzhuSlot(q,p,true);
 if(actual.kind!=AIXuanzhuSlotRead::Card||actual.name!=name||actual.targets!=t)return{{"error","Xuanzhu selected source changed after policy read"}};
 return issue(q,p,false,std::move(actual.lease),t,materialId,f);
}
bool hasAIXuanzhuReceipt(const AIRequest&q,int id){if(!q.conversionLedger)return false;for(const auto&t:q.conversionLedger->tickets)if(t.conversionId==id&&t.policyClass=="@xuanzhu_raw_source")return true;return false;}
bool applyAIXuanzhuReceipt(const AIRequest&q,ServerPlayer*p,const AICardSpec&spec,const QStringList&t,const std::function<bool(CardUseStruct&)>&admit,CardUseStruct&use){
 if(!xuanzhuResponseCurrent(q,p)||!hasAIXuanzhuReceipt(q,spec.conversionId))return false;auto*l=xuanzhuResponseLedger(p->getRoom());if(!l)return false;
 for(const auto&e:l->rows)if(e.ledger.toStrongRef()==q.conversionLedger&&e.owner==p&&e.decision==q.decisionId&&e.revision==q.stateRevision&&xuanzhuResponseSame(e.spec,spec)&&e.targets==t){
  const XuanzhuResponseReceipt copy=e;QString wire,error;CardUseStruct actual;
  const auto source=q.conversionLedger->observedProducts.value(spec.conversionId);
  if(!source||!copy.source||source()!=copy.source.data())return false;
  if(!prepare(q,p,copy.first,source,t,copy.material,admit,actual,wire,error)||wire!=copy.wire||!xuanzhuResponseSame(xuanzhuResponseSpecOf(actual.card,spec.conversionId),spec)||actual.activationRef!=copy.activation||actual.sourceRef!=copy.root)return false;
  actual.m_validateTargets=true;use=actual;return true;
 }return false;
}

// ---- xuanzhu-slot ----
namespace {
struct XuanzhuSlotStack{lua_State*L;int n;explicit XuanzhuSlotStack(lua_State*s):L(s),n(lua_gettop(s)){}~XuanzhuSlotStack(){lua_settop(L,n);}};
struct XuanzhuSlotRow{
 QPointer<ServerPlayer>owner;AIReadonlyCardLease lease;QString token;
 int offered=-1;quint64 producerDecision=0,producerRevision=0;
 QList<int>materials;int physical=-1;
};
class XuanzhuSlotLedger final:public QObject{
public:explicit XuanzhuSlotLedger(Room&r):QObject(&r){}lua_State*L=nullptr;quint64 generation=0;
 QHash<const ServerPlayer*,XuanzhuSlotRow>rows;
};
XuanzhuSlotLedger*xuanzhuSlotLedger(Room&r,bool create=false){for(auto*c:r.children())if(auto*l=dynamic_cast<XuanzhuSlotLedger*>(c))return l;return create?new XuanzhuSlotLedger(r):nullptr;}
bool xuanzhuSlotCurrent(const AIRequest&q,ServerPlayer*p){return p&&p->getRoom()&&q.decisionId>0
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
 &&aiRangeRequestCurrent(*p->getRoom(),q,p)&&Sanguosha&&Sanguosha->currentRoom()==p->getRoom();}
bool xuanzhuSlotShown(const AIRequest&q,int id,const Card*exact=nullptr){
 // IDs in printed notification DTOs are not evidence of current wrapped face.
 // Authenticate the actually projected object BEFORE consulting its fields.
 const Card*outer=Sanguosha->getCard(id);if(!outer)return false;
 auto matches=[&](const AICardView&v){if(v.effectiveId!=id||!v.nativeIdentity||!globalCardLifetimeManager().isLive(v.nativeIdentity))return false;
  const Card*c=exact;if(!c){if(v.nativeIdentity->address==outer)c=outer;else if(v.nativeIdentity->address==outer->getRealCard())c=outer->getRealCard();}
  if(!c||v.nativeIdentity->address!=c)return false;
  return v.cardId==c->getId()&&v.effectiveId==c->getEffectiveId()&&v.objectName==c->objectName()
   &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
   &&v.skillName==c->getSkillName(false)&&v.subcardIds==c->getSubcards()&&v.virtualCard==c->isVirtualCard();
 };
 for(const auto&c:q.worldView.handCards)if(matches(c))return true;
 for(const auto&c:q.worldView.discardPile)if(matches(c))return true;
 for(const auto&c:q.choiceOptions.cards)if(matches(c))return true;
 auto ps=q.worldView.players;ps.prepend(q.worldView.self);
 for(const auto&p:ps){for(const auto&c:p.knownCards)if(matches(c))return true;for(const auto&c:p.equips)if(matches(c))return true;
  for(const auto&c:p.judgingArea)if(matches(c))return true;for(const auto&pile:p.piles)for(const auto&c:pile.cards)if(matches(c))return true;}
 return false;
}
void xuanzhuSlotField(lua_State*L,int i,const char*k){AILuaInspect::field(L,i,k);}
bool xuanzhuSlotSelf(lua_State*L,Room&r,ServerPlayer*p){return pushAIActualAISelf(r,p,L);}
bool xuanzhuSlotAbsentNewIndex(lua_State*L,int t){XuanzhuSlotStack st(L);if(!lua_getmetatable(L,t))return true;xuanzhuSlotField(L,-1,"__newindex");return lua_isnil(L,-1);}
bool xuanzhuSlotPlain(lua_State*L,int n){auto*t=AILuaInspect::table(L,n);return t&&!t->metatable;}
bool xuanzhuSlotEnv(lua_State*L,int f){f=lua_absindex(L,f);for(int n=1;;++n){const char*k=lua_getupvalue(L,f,n);if(!k)break;bool ok=false;if(!std::strcmp(k,"_ENV")){AILuaInspect::globals(L);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}lua_pop(L,1);if(!ok)return false;}return true;}
bool xuanzhuSlotCode(Room&r,lua_State*L,int f,const char*path,int first,int last){return matchAIPinnedFunction(r,L,f,path,first,last)&&xuanzhuSlotEnv(L,f);}
bool xuanzhuSlotSourceCallback(Room&r,lua_State*L,QString*why){
 auto fail=[&](const char*text){if(why)*why=QString::fromLatin1(text);return false;};
 XuanzhuSlotStack stack(L);AILuaInspect::globals(L);xuanzhuSlotField(L,-1,"sgs");if(!lua_istable(L,-1))return fail("Xuanzhu source sgs table unavailable");
 xuanzhuSlotField(L,-1,"ai_skill_use");if(!xuanzhuSlotPlain(L,-1))return fail("Xuanzhu source use registry is not plain");xuanzhuSlotField(L,-1,"@@xuanzhu");
 if(!matchAIPinnedFunction(r,L,-1,"lua/ai/ol_sp-ai.lua",2455,2471,why))return false;
 if(!xuanzhuSlotEnv(L,-1))return fail("Xuanzhu pinned source reader environment changed");
 if(why)why->clear();return true;
}
bool xuanzhuSlotSourceProducer(Room&r,lua_State*L){
 XuanzhuSlotStack st(L);AILuaInspect::globals(L);xuanzhuSlotField(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 xuanzhuSlotField(L,-1,"ai_skills");if(!xuanzhuSlotPlain(L,-1))return false;const int rows=lua_absindex(L,-1);int matches=0;
 lua_pushnil(L);while(AILuaInspect::next(L,rows)){
  if(lua_istable(L,-1)){const int row=lua_absindex(L,-1);xuanzhuSlotField(L,row,"name");const bool named=lua_type(L,-1)==LUA_TSTRING&&QString::fromUtf8(lua_tostring(L,-1))=="xuanzhu";lua_pop(L,1);
   if(named){if(!xuanzhuSlotPlain(L,row))return false;xuanzhuSlotField(L,row,"getTurnUseCard");if(!xuanzhuSlotCode(r,L,-1,"lua/ai/ol_sp-ai.lua",2420,2446))return false;lua_pop(L,1);++matches;}}
  lua_pop(L,1);
 }return matches==1;
}
// Exact table-valued __index lookup. Never execute authored getter functions.
bool xuanzhuSlotLookup(lua_State*L,int value,const char*key){
 value=lua_absindex(L,value);QSet<const void*>seen;
 for(int depth=0;depth<16;++depth){if(!lua_istable(L,value)||seen.contains(lua_topointer(L,value)))return false;
  seen.insert(lua_topointer(L,value));xuanzhuSlotField(L,value,key);if(!lua_isnil(L,-1))return true;lua_pop(L,1);
  if(!lua_getmetatable(L,value)){lua_pushnil(L);return true;}
  xuanzhuSlotField(L,-1,"__index");if(lua_isnil(L,-1))return true;if(!lua_istable(L,-1))return false;value=lua_absindex(L,-1);
 }return false;
}
bool xuanzhuSlotSourceReadBindings(Room&r,lua_State*L){
 XuanzhuSlotStack stack(L);AILuaInspect::globals(L);const int globals=lua_absindex(L,-1);
 xuanzhuSlotField(L,globals,"sgs");if(!lua_istable(L,-1))return false;const int sgs=lua_absindex(L,-1);
 xuanzhuSlotField(L,sgs,"ai_skill_use");if(!xuanzhuSlotPlain(L,-1))return false;xuanzhuSlotField(L,-1,"@@xuanzhu");
 if(!xuanzhuSlotCode(r,L,-1,"lua/ai/ol_sp-ai.lua",2455,2471))return false;
 xuanzhuSlotField(L,sgs,"list");if(!xuanzhuSlotCode(r,L,-1,"lua/utilities.lua",38,41))return false;
 xuanzhuSlotField(L,sgs,"qlist");const int qlist=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,qlist,"lua/utilities.lua",19,35))return false;
 for(int n=1;;++n){const char*k=lua_getupvalue(L,qlist,n);if(!k)break;bool ok=false;
  if(!std::strcmp(k,"_ENV")){lua_pushvalue(L,globals);ok=lua_rawequal(L,-1,-2);lua_pop(L,1);}
  else if(!std::strcmp(k,"qlist_userdata_iterator"))ok=xuanzhuSlotCode(r,L,-1,"lua/utilities.lua",7,11);
  else if(!std::strcmp(k,"qlist_table_iterator"))ok=xuanzhuSlotCode(r,L,-1,"lua/utilities.lua",13,17);
  lua_pop(L,1);if(!ok)return false;
 }
 for(const char*k:{"type","ipairs"}){xuanzhuSlotField(L,globals,k);if(!matchAINativeBinding(r,L,-1,k))return false;lua_pop(L,1);}
 xuanzhuSlotField(L,globals,"table");if(!xuanzhuSlotPlain(L,-1))return false;const int table=lua_absindex(L,-1);
 for(const char*k:{"insert","concat"}){xuanzhuSlotField(L,table,k);if(!matchAINativeBinding(r,L,-1,QString("table.")+k))return false;lua_pop(L,1);}
 return matchAINativeClassMethod(r,L,"SPlayerList","length")&&matchAINativeClassMethod(r,L,"SPlayerList","at")
  &&matchAINativeDerivedMethods(r,L,"ServerPlayer",{"objectName"});
}
// Executes only our fixed native assignment thunk. No legacy AI strategy call.
// The caller proved source-equivalent __newindex behavior before entering it.
struct XuanzhuSlotWrite {const AIRequest*request;ServerPlayer*owner;AIReadonlyCardLease lease;const Card*card;QList<ServerPlayer*>targets;int extra=0;};
int xuanzhuSlotAssign(lua_State*L){
 // This is a new protected C call frame; the outer host reserve is not inherited.
 if(!lua_checkstack(L,4096))return luaL_error(L,"Xuanzhu assignment frame capacity unavailable");
 auto*w=static_cast<XuanzhuSlotWrite*>(lua_touserdata(L,2));
 lua_newtable(L);const int d=lua_gettop(L);
 lua_pushboolean(L,1);lua_setfield(L,d,"isDummy");
 lua_pushinteger(L,w->extra);lua_setfield(L,d,"extra_target");
 lua_newtable(L);lua_setfield(L,d,"current_targets");
 if(!pushAISlotCard(L,w->card))return luaL_error(L,"Xuanzhu native Card wrapper unavailable");lua_setfield(L,d,"card");
 if(!pushAISlotTargets(L,w->targets))return luaL_error(L,"Xuanzhu native target list unavailable");lua_setfield(L,d,"to");
 if(!xuanzhuSlotCurrent(*w->request,w->owner)||w->lease()!=w->card||!xuanzhuSlotSelf(L,*w->owner->getRoom(),w->owner)||!lua_rawequal(L,1,-1))
  return luaL_error(L,"Xuanzhu actual owner/request changed during allocation");
 lua_pop(L,1);
 // Preserve source assignment (not rawset). If GC changed the exact receiver's
 // absent-key metatable after preflight, reject before dispatching that hook.
 xuanzhuSlotField(L,1,"xuanzhuUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!xuanzhuSlotAbsentNewIndex(L,1))return luaL_error(L,"Xuanzhu source __newindex changed during allocation");
 lua_pushvalue(L,d);lua_setfield(L,1,"xuanzhuUse");
 lua_pushvalue(L,d);return 1;
}
struct ReadTargets {const AIRequest*q;ServerPlayer*owner;QList<ServerPlayer*>*out;const char*error=nullptr;};
int readTargets(lua_State*L){
 auto*w=static_cast<ReadTargets*>(lua_touserdata(L,2));
 auto fail=[&](const char*s){w->error=s;return luaL_error(L,"%s",s);};
 if(!lua_checkstack(L,4096))return fail("saved native target reader stack unavailable");
 Room&r=*w->owner->getRoom();const int list=1;
 auto certified=[&](){QList<ServerPlayer*>tracked;
  if(readAISlotTargets(L,list,tracked))return matchAINativeClassMethod(r,L,"SPlayerList","length")
   &&matchAINativeClassMethod(r,L,"SPlayerList","at");
  return matchAINativeObjectMethods(r,L,list,"SPlayerList",{"length","at"});
 };
 for(int i=0;;++i){
  if(!xuanzhuSlotCurrent(*w->q,w->owner)||!certified())return fail("saved native target dispatch changed during iteration");
  lua_getfield(L,list,"length");lua_pushvalue(L,list);lua_call(L,1,1);
  if(lua_type(L,-1)!=LUA_TNUMBER)return fail("saved native target length ABI changed");
  const double n=lua_tonumber(L,-1);lua_pop(L,1);
  if(!std::isfinite(n)||n<0||n>128||n!=std::floor(n))return fail("saved target budget exceeded");
  if(i>=int(n))break;
  lua_getfield(L,list,"at");lua_pushvalue(L,list);lua_pushinteger(L,i);lua_call(L,2,1);
  ServerPlayer*p=nullptr;
  for(auto*t:r.getAllPlayers(true))if(t&&matchAISlotPlayer(L,-1,t)){p=t;break;}
  if(!p||!matchAINativeObjectMethods(r,L,-1,"ServerPlayer",{"objectName"}))return fail("saved native target identity or method changed");
  auto players=w->q->worldView.players;players.prepend(w->q->worldView.self);
  bool disclosed=false;for(const auto&v:players)if(v.objectName==p->objectName()){disclosed=true;break;}
  if(!disclosed)return fail("saved native target absent from current projected roster");
  w->out->append(p);lua_pop(L,1);
 }
 if(!xuanzhuSlotCurrent(*w->q,w->owner))return fail("saved target iteration changed request");
 return 0;
}
}
bool registerAIXuanzhuSlotSources(Room&r,lua_State*L,QString*error){
 // Independently fixed source bytes, not a hash derived from the installed
 // effective callback. The common seal verifies runtime generation and compiles
 // metadata without executing any package policy. Never reseal on a reader.
 if(!registerAIPinnedSource(r,L,"lua/ai/ol_sp-ai.lua",
     QByteArray("d8cc0c6d06b58c3d7306e5d742e44247e88b2050f94df886600f840e68b4205a"),error))return false;
 return registerAIPinnedSource(r,L,"lua/utilities.lua",
     QByteArray("b76cdc44c0fec46f1e10d918c6e0676c5e301ed92cd5b0e35da882606a7d4d3a"),error);
}
void clearAIXuanzhuSlots(Room&r){if(auto*l=xuanzhuSlotLedger(r))delete l;}
QString writeAIXuanzhuSlot(const AIRequest&q,ServerPlayer*p,AIReadonlyCardLease lease,const QStringList&names,const QJsonObject&fields,QString*error){
 auto fail=[&](const QString&s){if(error)*error=s;return QString();};
 if(!xuanzhuSlotCurrent(q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||p->getPhase()!=Player::Play||!lease)
  return fail("Xuanzhu source assignment requires current owner Play producer");
 if(!xuanzhuSlotCurrent(q,p))return fail("Xuanzhu skill lookup invalidated producer request");
 if(fields.size()!=3||fields.value("isDummy")!=QJsonValue(true)||!fields.value("extra_target").isDouble()
  ||fields.value("extra_target").toDouble()!=fields.value("extra_target").toInt()||fields.value("current_targets_empty")!=QJsonValue(true))return fail("Xuanzhu full dummy field envelope unsupported");
 const Card*card=lease();if(!card)return fail("Xuanzhu exact producer custody expired");
 const int physical=card->isVirtualCard()?-1:card->getEffectiveId();if(physical>=0&&!xuanzhuSlotShown(q,physical,card))return fail("Xuanzhu selected physical replacement undisclosed");
 const auto costs=card->getSubcards();for(int id:costs)if(!xuanzhuSlotShown(q,id))return fail("Xuanzhu selected source material undisclosed");
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Xuanzhu game runtime unavailable");XuanzhuSlotStack stack(L);
 if(!xuanzhuSlotSourceProducer(r,L))return fail("Xuanzhu actual complete source producer changed");
 if(!xuanzhuSlotSelf(L,r,p))return fail("Xuanzhu actual LuaAI self authentication failed");const int receiver=lua_absindex(L,-1);
 xuanzhuSlotField(L,receiver,"xuanzhuUse");const bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&!xuanzhuSlotAbsentNewIndex(L,receiver))return fail("Xuanzhu source __newindex is not direct assignment");
 XuanzhuSlotWrite w;w.request=&q;w.owner=p;w.lease=lease;w.card=card;w.extra=fields.value("extra_target").toInt();
 const auto roster=r.getAllPlayers(true);for(const auto&name:names){ServerPlayer*target=nullptr;for(auto*t:roster)if(t&&t->objectName()==name){target=t;break;}if(!target)return fail("Xuanzhu source target outside roster");w.targets<<target;}
 lua_pushcfunction(L,xuanzhuSlotAssign);lua_pushvalue(L,receiver);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,2,1,0)!=LUA_OK)return fail("Xuanzhu native source assignment did not complete");
 if(!xuanzhuSlotCurrent(q,p)||!lease())return fail("Xuanzhu assignment changed current request or custody");
 xuanzhuSlotField(L,receiver,"xuanzhuUse");const bool exact=lua_rawequal(L,-1,-2);lua_pop(L,1);if(!exact||!xuanzhuSlotPlain(L,-1))return fail("Xuanzhu source assignment was replaced");
 auto*l=xuanzhuSlotLedger(r,true);if(l->L&&l->generation!=rt->lua().generation())return fail("Xuanzhu source runtime generation changed");l->L=L;l->generation=rt->lua().generation();
 XuanzhuSlotRow row;row.owner=p;row.lease=std::move(lease);row.token="xuanzhu-actual-slot:"+QUuid::createUuid().toString(QUuid::WithoutBraces);row.materials=costs;row.physical=physical;row.producerDecision=q.decisionId;row.producerRevision=q.stateRevision;
 l->rows[p]=row; // Source writes before its following first-card Parse; no rollback.
 if(!xuanzhuSlotCurrent(q,p)||!row.lease())return fail("Xuanzhu source publication changed request or custody");
 if(error)error->clear();return row.token;
}
AIXuanzhuSlotRead readAIXuanzhuSlot(const AIRequest&q,ServerPlayer*p,bool includeCard){
 AIXuanzhuSlotRead out;auto fail=[&](const QString&s){out=AIXuanzhuSlotRead();out.error=s;return out;};
 if(!xuanzhuSlotCurrent(q,p)||q.kind!=AIRequest::UseCard||q.pattern!="@@xuanzhu"||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||q.handlingMethod!=Card::MethodUse)return fail("Xuanzhu source read requires actual current response");
 auto finish=[&](){if(!xuanzhuSlotCurrent(q,p)){out=AIXuanzhuSlotRead();out.error="Xuanzhu source read invalidated current request";}return out;};
 Room&r=*p->getRoom();auto*rt=r.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,4096))return fail("Xuanzhu actual source runtime unavailable");XuanzhuSlotStack stack(L);QString why;
 if(!xuanzhuSlotSourceCallback(r,L,&why))return fail("Xuanzhu source reader: "+why);
 if(!xuanzhuSlotSelf(L,r,p)||!xuanzhuSlotLookup(L,-1,"xuanzhuUse"))return fail("Xuanzhu actual slot receiver/getter unproved");
 if(lua_isnil(L,-1)||lua_isboolean(L,-1)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,-1)))return fail("Xuanzhu primitive indexing metatable changed");
  out.kind=lua_isnil(L,-1)?AIXuanzhuSlotRead::NilPlan:AIXuanzhuSlotRead::BooleanPlan;return finish();
 }
 if(!lua_istable(L,-1))return fail("Xuanzhu source plan is outside table domain");const int d=lua_absindex(L,-1);
 // Source enumerates d.to BEFORE it ever reads d.card. No hand means it never reads card.
 if(!xuanzhuSlotLookup(L,d,"to"))return fail("Xuanzhu source target getter unproved");
 if(!xuanzhuSlotSourceReadBindings(r,L))return fail("Xuanzhu reached source list bindings changed");
 if(lua_isnil(L,-1)||lua_isboolean(L,-1)){out.kind=AIXuanzhuSlotRead::TargetsError;out.targetsNil=lua_isnil(L,-1);out.targetsBoolean=lua_isboolean(L,-1);return finish();}
 QList<ServerPlayer*>actual;ReadTargets read{&q,p,&actual};
 const int sourceList=lua_absindex(L,-1);
 lua_pushcfunction(L,readTargets);lua_pushvalue(L,sourceList);lua_pushlightuserdata(L,&read);
 if(LuaRuntime::protectedCall(L,2,0,0)!=LUA_OK)return fail(read.error?QString::fromLatin1(read.error):QStringLiteral("Xuanzhu actual target iteration failed"));
 lua_pop(L,1);
 const auto roster=r.getAllPlayers(true);for(auto*t:actual){if(!t||!roster.contains(t))return fail("Xuanzhu actual target left roster");out.targets<<t->objectName();}
 out.kind=AIXuanzhuSlotRead::Targets;
 if(!includeCard)return finish();
 if(p->getCards("he").isEmpty())return fail("Xuanzhu source has no material and does not read card");
 if(!xuanzhuSlotLookup(L,d,"card"))return fail("Xuanzhu source card getter unproved");
 if(lua_isnil(L,-1)||lua_isboolean(L,-1)){
  if(AILuaInspect::primitiveMetatable(L,lua_type(L,-1)))return fail("Xuanzhu primitive Card indexing changed");
  out.kind=AIXuanzhuSlotRead::CardError;out.cardNil=lua_isnil(L,-1);return finish();
 }
 auto*l=xuanzhuSlotLedger(r);if(!l||l->L!=L||l->generation!=rt->lua().generation())return fail("Xuanzhu actual Card has no retained producer custody");
 const auto it=l->rows.constFind(p);if(it==l->rows.cend()||it->owner!=p)return fail("Xuanzhu current owner has no retained producer");const XuanzhuSlotRow row=*it;
 const Card*card=row.lease?row.lease():nullptr;if(!card||!matchAISlotCard(L,-1,card)||!matchAINativeObjectMethods(r,L,-1,"Card",{"objectName"}))return fail("Xuanzhu actual Card identity/objectName changed");
 if(row.physical>=0&&!xuanzhuSlotShown(q,row.physical,card))return fail("Xuanzhu physical current face not disclosed");
 for(int id:row.materials)if(!xuanzhuSlotShown(q,id))return fail("Xuanzhu material observation expired");
 if(card->getSubcards()!=row.materials)return fail("Xuanzhu retained material provenance changed");
 out.name=card->objectName();out.lease=row.lease;out.kind=AIXuanzhuSlotRead::Card;
 if(!xuanzhuSlotCurrent(q,p)||row.lease()!=card)return fail("Xuanzhu source read changed custody/request");return finish();
}
std::unique_ptr<Card> constructAIXuanzhuPolicy(const AIRequest&q,ServerPlayer*p,const QString&name,QString*error){
 auto fail=[&](const char*s)->std::unique_ptr<Card>{if(error)*error=QString::fromLatin1(s);return{};};
 if(!xuanzhuSlotCurrent(q,p)||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||p->getPhase()!=Player::Play)return fail("Xuanzhu dummy requires current owner Play");
 bool own=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="xuanzhu")own=true;if(!own)return fail("Xuanzhu source skill is not owned");
 const auto names=q.worldView.strategyFacts.value("pattern_names").toArray();if(!names.contains(name))return fail("Xuanzhu source name is outside issued patterns");
 std::unique_ptr<Card>c(Sanguosha->cloneCard(name,Card::NoSuit,0));if(error)error->clear();return c;
}

// ---- jijiang-choice ----
#include "general.h"
#include <QScopeGuard>
namespace {
class JijiangChoiceLedger final:public QObject {
public:explicit JijiangChoiceLedger(Room&r):QObject(&r){}
 bool captured=false,entered=false,sourceKnown=false,targetsKnown=false,sourcePresent=false,targetsPresent=false;
 QString viewer,source,clearedEvent;QStringList targets;
};
JijiangChoiceLedger*state(Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<JijiangChoiceLedger*>(c))return s;return new JijiangChoiceLedger(r);}
void forget(JijiangChoiceLedger*s){s->sourceKnown=false;s->targetsKnown=false;s->source.clear();s->targets.clear();s->clearedEvent.clear();}
QJsonObject unknown(const QString&reason){return{{"kind","ol_jijiang_state"},{"known",false},{"reason",reason}};}
bool jijiangChoiceCurrent(Room&r,const AIRequest&q,const ServerPlayer*p){return p&&p->getRoom()==&r&&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&q.stateRevision==r.roomRuntime()->stateRevision();}
bool tableRaw(lua_State*L,const char*key){lua_pushstring(L,key);lua_rawget(L,-2);return lua_istable(L,-1);}
}
void projectAIOLGeneralContext(Room &room,ServerPlayer *viewer,const QStringList &offered,QJsonObject &ctx){
 if(!viewer||viewer->getRoom()!=&room)return;
 QJsonObject catalog=ctx.value("general_catalog").toObject();
 for(const auto&name:offered){auto row=catalog.value(name).toObject();if(const General*g=Sanguosha->getGeneral(name))row.insert("max_hp",g->getMaxHp());catalog.insert(name,row);}ctx.insert("general_catalog",catalog);
 LuaRuntime::Binding binding(room.roomRuntime()->lua(),false);lua_State*L=room.roomRuntime()->lua().state();if(!L)return;
 int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});
 lua_pushglobaltable(L);if(!tableRaw(L,"sgs")||!tableRaw(L,"ai_general_choice"))return;
 const int registry=lua_absindex(L,-1);QJsonObject rows;
 for(const auto&key:QStringList{"ov_lusu","ov_zhaoe","heg_qingcheng","mobilemou_caoren","mobilemou_gaoshun","mobilemou_lvmeng","mobilemou_machao","sp_machao","tw_machao","xizhicai","liubei","nos_huanggai","nos_liubei","nos_xiahoudun","cenhun"}){
  const auto k=key.toUtf8();lua_pushlstring(L,k.constData(),k.size());lua_rawget(L,registry);
  QJsonObject row{{"known",true},{"present",!lua_isnil(L,-1)},{"value_type",QString::fromLatin1(lua_typename(L,lua_type(L,-1)))}};
  if(lua_isfunction(L,-1)){lua_Debug ar{};lua_pushvalue(L,-1);if(lua_getinfo(L,">S",&ar)){row.insert("source",QString::fromUtf8(ar.source?ar.source:""));row.insert("line",ar.linedefined);row.insert("last_line",ar.lastlinedefined);}}
  rows.insert(key,row);lua_pop(L,1);
 }ctx.insert("ol_general_sources",rows);
}
void captureAIOLJijiang(Room&r){
 auto*s=state(r);if(s->captured)return;s->captured=true;if(s->entered)return;
 LuaRuntime::Binding binding(r.roomRuntime()->lua(),false);lua_State*L=r.roomRuntime()->lua().state();if(!L)return;
 int top=lua_gettop(L);const auto restore=qScopeGuard([&]{lua_settop(L,top);});lua_pushglobaltable(L);if(!tableRaw(L,"sgs"))return;
 // Only proven initial nil is exported. No SWIG player/global value is read or
 // decoded; startup owner/foreign populated globals remain unknown.
 lua_pushliteral(L,"oljijiangsource");lua_rawget(L,-2);s->sourceKnown=lua_isnil(L,-1);s->sourcePresent=false;lua_pop(L,1);
 lua_pushliteral(L,"oljijiangtarget");lua_rawget(L,-2);s->targetsKnown=lua_isnil(L,-1);s->targetsPresent=false;
}
void beginAIOLJijiang(Room&r,const QString&viewer){auto*s=state(r);s->entered=true;if(viewer.isEmpty()||(!s->viewer.isEmpty()&&s->viewer!=viewer))forget(s);s->viewer=viewer;}
void invalidateAIOLJijiang(Room&r){auto*s=state(r);s->entered=true;forget(s);}
QJsonObject aiOLJijiangEvent(const AIEventView&e){
 const auto prompt=e.details.value("prompt_list").toArray();const auto kind=e.details.value("choice_kind").toString();
 if(e.kind=="card_use"&&(e.cardName=="OLJijiangCard"||e.cardClass=="OLJijiangCard"))
  return{{"writer","card_use"},{"actor",e.details.value("player")},{"sequence",QString::number(e.sequence)}};
 if(e.kind=="choice"&&kind=="skillInvoke"&&e.reason=="oljijiang"&&!prompt.isEmpty()&&prompt.last()=="yes")
  return{{"writer","invoke"},{"actor",e.from},{"sequence",QString::number(e.sequence)}};
 if(e.kind=="choice"&&kind=="cardResponded"&&prompt.size()>=3&&prompt.at(2)=="@oljijiang-slash")
  return{{"writer","respond"},{"actor",e.from},{"sequence",QString::number(e.sequence)},{"answer_nonempty",prompt.last().isString()&&!prompt.last().toString().isEmpty()}};
 return {};
}
QJsonObject queryAIOLJijiang(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QString&value){
 // Actual shared namespace only. The old viewer-local shadow is not a
 // cross-viewer writer witness and cannot substitute for a current source.
 return queryAIJijiang(r,q,p,op,value);
}

// ---- jijiang ----
namespace {
// Strategy memory stays in the isolated observer VM. This operation storage
// also retains weak constructor origins for one fixed, publicly declared list.
struct ConstructedSkillNames {std::weak_ptr<const CardLifetimeToken>token;QPointer<const Card>card;};
struct State final:QObject {
 explicit State(Room&r):QObject(&r),room(r){}
 Room&room;quint64 next=0;QHash<const Card*,ConstructedSkillNames>constructedSkills;
};
State*jijiangState(Room&r,bool create=false){
 for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;
 return create?new State(r):nullptr;
}
thread_local AIJijiangScope::Data*jijiangActive=nullptr;
thread_local AIJijiangLiege::Data*liegeFrame=nullptr;
thread_local AIJijiangEvent::Data*eventFrame=nullptr;
QJsonObject no(const char*why){return{{"kind","ol_jijiang_state"},{"known",false},{"reason",why}};}
QJsonObject absent(){return{{"kind","ol_jijiang_state"},{"known",true},{"present",false}};}
}
// Sole callers are native OLJijiangVS createCard and its paid replacement.
void recordAIFreshOLJijiangSkills(Room&r,const Card*c){
 if(!c||!c->isVirtualCard()||!c->isKindOf("Slash")||c->getSkillName(false)!="oljijiang"
  ||c->getSkillNames()!=QStringList{QStringLiteral("oljijiang")})return;
 const auto token=globalCardLifetimeManager().observeCard(const_cast<Card*>(c));
 if(!token||!token->live)return;auto&entries=jijiangState(r,true)->constructedSkills;
 for(auto i=entries.begin();i!=entries.end();){const auto t=i->token.lock();
  if(!t||!t->live||i->card.isNull())i=entries.erase(i);else ++i;}
 if(!entries.contains(c))entries.insert(c,{token,c});
}
QJsonObject readAIOLJijiangConstructedSkills(Room&r,ServerPlayer*viewer,const Card*c){
 if(!viewer||viewer->getRoom()!=&r||!c)return{};auto*s=jijiangState(r);if(!s)return{};
 const auto i=s->constructedSkills.constFind(c);
 if(i==s->constructedSkills.cend()||i->card.isNull()||i->card.data()!=c)return{};
 const auto token=i->token.lock();
 if(!token||!token->live||globalCardLifetimeManager().liveToken(c)!=token
  ||c->getSkillNames()!=QStringList{QStringLiteral("oljijiang")})return{};
 return{{"skill_names",QJsonArray{QStringLiteral("oljijiang")}},{"skill_names_complete",true}};
}
struct AIJijiangScope::Data {
 QPointer<State>s;QPointer<ServerPlayer>lord;const SkillContext*context=nullptr;
 SkillInstanceRef activation,source;quint64 operation=0;bool cleared=false;
 Data*previous=nullptr;
};
struct AIJijiangLiege::Data {
 AIJijiangScope::Data*operation=nullptr;QPointer<ServerPlayer>liege;
 Data*previous=nullptr;
};
struct AIJijiangEvent::Data {
 AIJijiangScope::Data*operation=nullptr;QPointer<ServerPlayer>actor;
 QString payload;bool read=false,cleared=false;Data*previous=nullptr;
};
namespace {
bool jijiangLive(AIJijiangScope::Data*d,Room&r){
 return d&&d==jijiangActive&&d->s && &d->s->room==&r&&d->operation&&d->lord
  &&d->lord->getRoom()==&r&&r.roomRuntime()&&!r.roomRuntime()->isClosing()
  &&d->context&&!d->context->is_canceled&&d->context->invoker==d->lord
  &&d->context->activationRef==d->activation&&d->context->sourceRef==d->source;
}
void begin(AIJijiangScope::Data*d,Room&r,const SkillContext&ctx){
 if(!r.roomRuntime()||r.roomRuntime()->isClosing()||ctx.is_canceled
  ||!ctx.invoker||ctx.invoker->getRoom()!=&r||!r.getAllPlayers(true).contains(ctx.invoker)
  ||ctx.activationRef.key.skillName!="oljijiang"||!ctx.activationRef.isValid()||!ctx.sourceRef.isValid())return;
 auto*s=jijiangState(r,true);if(s->next==std::numeric_limits<quint64>::max())return;
 d->s=s;d->lord=ctx.invoker;d->context=&ctx;d->activation=ctx.activationRef;d->source=ctx.sourceRef;
 d->operation=++s->next;
}
bool answering(Room&r,ServerPlayer*p){
 return jijiangLive(jijiangActive,r)&&liegeFrame&&liegeFrame->operation==jijiangActive&&liegeFrame->liege==p;
}
QJsonObject source(AIJijiangScope::Data*d){
 auto out=absent();out.insert("operation",QString::number(d->operation));
 if(!d->cleared){out.insert("present",true);out.insert("player",d->lord->objectName());}
 return out;
}
}
AIJijiangScope::AIJijiangScope(Room&r,const SkillContext&ctx):d(new Data){
 d->previous=jijiangActive;jijiangActive=d;begin(d,r,ctx);
}
AIJijiangScope::AIJijiangScope(Room&r,const SkillContext&ctx,int event,bool effect):d(new Data){
 d->previous=jijiangActive;jijiangActive=d;
 // Cost-time invoke observation is not yet a live liege question. A cancelled
 // cost has nothing transferable; effect obtains its own native operation.
 if(effect&&event==CardAsked&&(ctx.current_event==EventSkillEffect||ctx.current_event==CardAsked))begin(d,r,ctx);
}
AIJijiangScope::~AIJijiangScope(){jijiangActive=d->previous;delete d;}
AIJijiangLiege::AIJijiangLiege(Room&r,ServerPlayer*p):d(new Data){
 d->previous=liegeFrame;liegeFrame=d;
 if(jijiangLive(jijiangActive,r)&&p&&p->getRoom()==&r&&r.getLieges("shu",jijiangActive->lord).contains(p)){
  d->operation=jijiangActive;d->liege=p;
 }
}
AIJijiangLiege::~AIJijiangLiege(){liegeFrame=d->previous;delete d;}
AIJijiangEvent::AIJijiangEvent(Room&r,int event,ServerPlayer*p,const QVariant&data):d(new Data){
 d->previous=eventFrame;eventFrame=d;
 if(event!=ChoiceMade||!answering(r,p))return;
 const auto fields=data.toString().split(':');
 if(fields.size()<5||fields.at(0)!="cardResponded"||fields.at(1)!="slash"
  ||fields.at(2)!="@oljijiang-slash"||fields.at(3)!=jijiangActive->lord->objectName())return;
 d->operation=jijiangActive;d->actor=p;d->payload=data.toString();
}
AIJijiangEvent::~AIJijiangEvent(){eventFrame=d->previous;delete d;}

QJsonObject queryAIJijiang(Room&r,const AIRequest&q,ServerPlayer*p,const QString&op,const QString&value){
 if(!r.roomRuntime()||!p||p->getRoom()!=&r||!value.isEmpty()
  ||(op!="source_read"&&op!="source_clear"))return no("OLJijiang source operation unavailable");
 auto&runtime=r.roomRuntime()->ai();
 const bool responseEvent=answering(r,p)&&eventFrame&&eventFrame->operation==jijiangActive
  &&eventFrame->actor==p&&runtime.currentOLJijiangResponseEvent(q,p,eventFrame->payload);
 if(responseEvent){
  if(op=="source_read"){eventFrame->read=true;return source(jijiangActive);}
  const auto fields=eventFrame->payload.split(':');
  // Preserve the donor's literal nonempty test: '.' is not the empty answer.
  bool clears=!fields.last().isEmpty();
  if(!clears&&!jijiangActive->cleared){
   const auto lieges=r.getLieges("shu",jijiangActive->lord);
   clears=!lieges.isEmpty()&&lieges.last()==p;
  }
  if(!eventFrame->read||eventFrame->cleared||!clears)
   return no("OLJijiang clear is not the reached response continuation");
  jijiangActive->cleared=true;eventFrame->cleared=true;return source(jijiangActive);
 }
 if(op!="source_read"||!runtime.currentOLJijiangPlanningRequest(q,p))
  return no("OLJijiang source has no active observer request");
 if(q.kind!=AIRequest::RespondCard||!q.prompt.startsWith("@oljijiang-slash:"))return absent();
 if(!answering(r,p)||q.pattern!="slash"||q.handlingMethod!=Card::MethodResponse
  ||q.prompt!="@oljijiang-slash:"+jijiangActive->lord->objectName())
  return no("OLJijiang response does not match the native askForCard operation");
 return source(jijiangActive);
}

bool initializeAIJijiang(Room&r,lua_State*){return r.roomRuntime()&&!r.roomRuntime()->isClosing();}
void clearAIJijiang(Room&r){delete jijiangState(r);}
// Ordinary events and GameVM calls cannot retire observer memory. The lexical
// pay/effect and liege scopes alone delimit the source's current-use eligibility.
void enterAIJijiangEvent(Room&,int,ServerPlayer*){}
void enterAIJijiangFilter(Room&,int,ServerPlayer*,const QVariant&){}
void observeAIJijiangEntry(Room&,lua_State*,int)noexcept{}
bool acceptAIJijiangEntry(Room&,lua_State*,int)noexcept{return false;}
bool allowAIJijiangWrite(Room&,lua_State*,const lua_TableObservation&)noexcept{return false;}

// ---- shebian ----
#include "settings.h"
namespace {
struct ShebianStack{lua_State*L;int top;explicit ShebianStack(lua_State*s):L(s),top(lua_gettop(s)){}~ShebianStack(){lua_settop(L,top);}};
struct ShebianState;
void observe(lua_State*,const lua_TableObservation*,void*)noexcept;
struct ShebianState final:QObject {
 explicit ShebianState(Room&r):QObject(&r),room(r){}
 ~ShebianState()override{for(auto id:{globals,space,players,cards,invokes})if(id)lua_unobservetableidentity(L,id,observe,this);}
 Room&room;lua_State*L=nullptr;quint64 generation=0,epoch=1;bool valid=false,exhausted=false;
 const void*globals=nullptr,*space=nullptr,*players=nullptr,*cards=nullptr,*invokes=nullptr,*producer=nullptr;
 QPointer<ServerPlayer>owner;const SkillContext*context=nullptr;SkillInstanceRef activation,source;
 bool known[3]={false,false,false};int pending=-1;unsigned observed=0;
 void revoke()noexcept{for(bool&k:known)k=false;pending=-1;if(epoch==std::numeric_limits<quint64>::max())exhausted=true;else++epoch;}
};
ShebianState*shebianState(Room&r,bool create=false){for(auto*c:r.children())if(auto*s=dynamic_cast<ShebianState*>(c))return s;return create?new ShebianState(r):nullptr;}
const char*keys[]={"olshebian_from","olshebian","olshebian_to"};
bool key(const lua_TableObservation&e,const char*s){return e.string_key&&e.string_key_length==std::strlen(s)&&!std::memcmp(e.string_key,s,e.string_key_length);}
int shebianAssign(lua_State*);
void observe(lua_State*L,const lua_TableObservation*event,void*ptr)noexcept{
 auto*s=static_cast<ShebianState*>(ptr);const auto&e=*event;if(!L||!s->L||G(L)!=G(s->L))return;
 const bool structural=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE;
 if(structural){s->valid=false;s->revoke();if(e.event==LUA_TABLE_OBSERVER_FREE){for(auto*id:{&s->globals,&s->space,&s->players,&s->cards,&s->invokes})if(*id==e.table_identity)*id=nullptr;}return;}
 if(e.event!=LUA_TABLE_OBSERVER_WRITE)return;
 if((e.table_identity==s->globals&&key(e,"sgs"))||(e.table_identity==s->space&&(key(e,"ai_skill_playerchosen")||key(e,"ai_skill_cardchosen")||key(e,"ai_skill_invoke")))
    ||(e.table_identity==s->invokes&&key(e,"olshebian"))){s->valid=false;s->revoke();return;}
 for(int n=0;n<3;++n)if(e.table_identity==(n==1?s->cards:s->players)&&key(e,keys[n])){
  const TValue*f=s2v(L->ci->func.p);
  if(s->pending==n&&ttislcf(f)&&fvalue(f)==shebianAssign&&s->observed==0){++s->observed;s->known[n]=false;}
  else s->revoke();
 }
}
bool shebianLive(ShebianState*s){auto*rt=s?s->room.roomRuntime():nullptr;return rt&&!rt->isClosing()&&s->valid&&!s->exhausted
 &&rt->lua().isCurrentThreadOwner()&&rt->lua().lifecycle()==LuaRuntime::Lifecycle::Running
 &&rt->lua().rawState()==s->L&&rt->lua().generation()==s->generation&&!Config.value("EnableHegemony",false).toBool();}
bool roots(ShebianState*s){ShebianStack st(s->L);auto*L=s->L;AILuaInspect::globals(L);if(lua_topointer(L,-1)!=s->globals)return false;
 AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1)||lua_topointer(L,-1)!=s->space)return false;int ns=lua_absindex(L,-1);
 const char*names[]={"ai_skill_playerchosen","ai_skill_cardchosen","ai_skill_invoke"};const void*ids[]={s->players,s->cards,s->invokes};
 for(int n=0;n<3;++n){AILuaInspect::field(L,ns,names[n]);auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable||lua_topointer(L,-1)!=ids[n])return false;lua_pop(L,1);}
 AILuaInspect::field(L,ns,"ai_skill_invoke");AILuaInspect::field(L,-1,"olshebian");return lua_topointer(L,-1)==s->producer;
}
bool request(Room&r,ServerPlayer*p,const AIRequest&q){return p&&r.getAllPlayers(true).contains(p)&&p->getRoom()==&r&&q.decisionId
 &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(r,q,p)
 &&Sanguosha&&Sanguosha->currentRoom()==&r;}
QJsonObject shebianNo(const char*why){return{{"known",false},{"reason",QString::fromLatin1(why)}};}
thread_local AIShebianScope::Data*shebianActive=nullptr;
}
struct AIShebianScope::Data {ShebianState*s=nullptr;const SkillContext*ctx=nullptr;quint64 epoch=0;bool effect=false,invokeEntered=false;Data*previous=nullptr;};
namespace {
bool frame(ShebianState*s,ServerPlayer*p,bool effect){return shebianActive&&shebianActive->s==s&&shebianActive->epoch==s->epoch&&shebianActive->effect==effect
 &&s->owner==p&&s->context==shebianActive->ctx&&shebianLive(s);}
struct Call{ShebianState*s;Room*r;ServerPlayer*p;const AIRequest*q;int key=-1;bool write=false,consume=false;QPointer<ServerPlayer>target;int id=-1;QJsonObject out;const char*error=nullptr;quint64 epoch=0;};
int fail(lua_State*L,Call*w,const char*why){if(w)w->error=why;return luaL_error(L,"%s",why);}
bool shebianCurrent(const Call&w){return request(*w.r,w.p,*w.q)&&frame(w.s,w.p,!w.write)&&w.epoch==w.s->epoch&&roots(w.s);}
int shebianAssign(lua_State*L){
 if(!lua_checkstack(L,4096))return luaL_error(L,"Shebian source frame capacity unavailable");
 auto*w=static_cast<Call*>(lua_touserdata(L,1));if(!w||!shebianCurrent(*w))return fail(L,w,"Shebian native operation changed");
 auto*s=w->s;AILuaInspect::globals(L);AILuaInspect::field(L,-1,"sgs");AILuaInspect::field(L,-1,w->key==1?"ai_skill_cardchosen":"ai_skill_playerchosen");int table=lua_absindex(L,-1);
 if(w->write){
  lua_pushstring(L,keys[w->key]);
  if(w->key==1)lua_pushinteger(L,w->id);
  else if(!w->target||!pushAISlotPlayer(L,w->target))return fail(L,w,"Shebian source Player binding unavailable");
 }else{
  if(!s->known[w->key])return fail(L,w,"Shebian actual registry writer is not this operation");
  AILuaInspect::field(L,table,keys[w->key]);
  if(lua_isnil(L,-1))w->out={{"known",true},{"present",false}};
  else if(w->key==1){
   if(lua_type(L,-1)!=LUA_TNUMBER)return fail(L,w,"Shebian actual card slot changed type");
   double n=lua_tonumber(L,-1);if(n<0||n>2147483647.0||n!=int(n))return fail(L,w,"Shebian actual card slot is not integral");
   // This is the same known own prefix, not a fresh foreign-card lookup.
   w->out={{"known",true},{"present",true},{"value",n}};
  }else{
   ServerPlayer*actual=nullptr;for(auto*p:w->r->getAllPlayers(true))if(matchAISlotPlayer(L,-1,p)){actual=p;break;}
   if(!actual)return fail(L,w,"Shebian actual target is not a current roster Player");
   w->out={{"known",true},{"present",true},{"value",actual->objectName()}};
  }
  if(!w->consume||w->out.value("present")!=true){if(!shebianCurrent(*w)||!s->known[w->key])return fail(L,w,"Shebian reader changed during decoding");return 0;}
  lua_pushstring(L,keys[w->key]);lua_pushnil(L);
 }
 if(!shebianCurrent(*w)||(w->target&&!w->r->getAllPlayers(true).contains(w->target)))return fail(L,w,"Shebian source changed before assignment");
 s->pending=w->key;s->observed=0;lua_settable(L,table);const bool observed=s->observed==1&&s->pending==w->key;s->pending=-1;
 if(!observed||!shebianCurrent(*w))return fail(L,w,"Shebian assignment was interrupted");
 s->known[w->key]=true;if(w->write)w->out={{"known",true},{"recorded",true}};return 0;
}
}
bool initializeAIShebian(Room&r,lua_State*L){
 auto*rt=r.roomRuntime();if(!rt||!L||L!=rt->lua().rawState()||!lua_checkstack(L,4096))return false;
 if(auto*s=shebianState(r))return shebianLive(s)&&roots(s);
 if(Config.value("EnableHegemony",false).toBool())return false;
 ShebianStack st(L);
 if(!registerAIPinnedSource(r,L,"lua/ai/ol_st_standard-ai.lua","cd1ff325355f4d7847522a73ce281f2044dee731eecf19be9ff9539059263dd7"))return false;
 AILuaInspect::globals(L);const void*g=lua_topointer(L,-1);AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return false;int ns=lua_absindex(L,-1);const void*space=lua_topointer(L,ns);
 const char*names[]={"ai_skill_playerchosen","ai_skill_cardchosen","ai_skill_invoke"};const void*ids[3];
 for(int n=0;n<3;++n){AILuaInspect::field(L,ns,names[n]);auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable)return false;ids[n]=lua_topointer(L,-1);lua_pop(L,1);}
 AILuaInspect::field(L,ns,"ai_skill_invoke");AILuaInspect::field(L,-1,"olshebian");int fn=lua_absindex(L,-1);
 if(!matchAIPinnedFunction(r,L,fn,"lua/ai/ol_st_standard-ai.lua",662,671))return false;
 const char*up=lua_getupvalue(L,fn,1);if(!up||std::strcmp(up,"_ENV")||lua_topointer(L,-1)!=g)return false;lua_pop(L,1);if(lua_getupvalue(L,fn,2))return false;
 auto*s=shebianState(r,true);s->L=L;s->generation=rt->lua().generation();s->producer=lua_topointer(L,fn);
 const void**dest[]={&s->globals,&s->space,&s->players,&s->cards,&s->invokes};const void*all[]={g,space,ids[0],ids[1],ids[2]};
 for(int n=0;n<5;++n)if(lua_observetableidentity(L,all[n],observe,s))*dest[n]=all[n];
 s->valid=s->globals&&s->space&&s->players&&s->cards&&s->invokes;return s->valid;
}
AIShebianScope::AIShebianScope(Room&r,const SkillContext&ctx,bool effect):d(new Data){
 d->previous=shebianActive;shebianActive=d;d->ctx=&ctx;d->effect=effect;
 auto*s=shebianState(r);if(!s||!shebianLive(s))return;d->s=s;
 if(!effect){
  s->revoke();s->owner=ctx.invoker;s->context=&ctx;s->activation=ctx.activationRef;s->source=ctx.sourceRef;
 }else if(s->owner!=ctx.invoker||s->context!=&ctx||s->activation!=ctx.activationRef||s->source!=ctx.sourceRef){s->revoke();return;}
 if(!ctx.invoker||ctx.owner!=ctx.invoker||ctx.activationRef.key.skillName!="olshebian")return;
 d->epoch=s->epoch;
}
AIShebianScope::~AIShebianScope(){if(d->effect&&d->s)d->s->revoke();shebianActive=d->previous;delete d;}
void invalidateAIShebian(Room&r){if(auto*s=shebianState(r))s->revoke();}
void clearAIShebian(Room&r){delete shebianState(r);}
void enterAIShebianInquiry(Room&r,ServerPlayer*p,const QString&kind,const QString&reason){
 auto*s=shebianState(r);if(!s)return;
 if((kind=="invoke"&&reason=="olshebian")||(kind=="player_chosen"&&(reason=="olshebian_from"||reason=="olshebian_to"))||(kind=="card_chosen"&&reason=="olshebian")){
  if(!shebianActive||shebianActive->s!=s||s->owner!=p||shebianActive->epoch!=s->epoch
     ||(kind=="invoke"?shebianActive->effect:!shebianActive->effect)){s->revoke();return;}
  // The compiled cost issues one invoke inquiry. A recursive same-owner
  // inquiry (including a direct legacy fallback) is not that source entry.
  if(kind=="invoke"){if(shebianActive->invokeEntered){s->revoke();return;}shebianActive->invokeEntered=true;}
 }
}
QJsonObject queryAIShebian(Room&r,ServerPlayer*p,const AIRequest&q,const QJsonObject&input){
 auto*s=shebianState(r);if(!shebianLive(s)||!request(r,p,q))return shebianNo("Shebian source operation unavailable");
 const QString key=input.value("key").toString(),op=input.value("op").toString();int n=-1;for(int i=0;i<3;++i)if(key==keys[i])n=i;
 if(n<0)return shebianNo("Shebian key outside fixed family");const bool write=op=="record",consume=op=="consume";
 if(write){if(q.kind!=AIRequest::SkillInvoke||q.choiceOptions.reason!="olshebian"||input.size()!=3||!input.value("row").isObject())return shebianNo("Shebian writer question differs");}
 else if((op!="read"&&!consume)||input.size()!=2||(consume!=(n==1))||q.choiceOptions.reason!=key||(n==1?q.kind!=AIRequest::CardChosen:q.kind!=AIRequest::PlayerChosen))return shebianNo("Shebian reader question differs");
 if(!frame(s,p,!write))return shebianNo("Shebian current compiled cost/effect scope unavailable");
 Call w{s,&r,p,&q,n,write,consume};w.epoch=s->epoch;
 if(write){const auto v=input.value("row").toObject();if(v.size()!=3||v.value("known")!=true||v.value("present")!=true)return shebianNo("Shebian writer shape differs");
  if(n==1){if(!v.value("value").isDouble())return shebianNo("Shebian ID shape differs");const double x=v.value("value").toDouble();w.id=v.value("value").toInt(-1);if(w.id<0||x!=w.id)return shebianNo("Shebian ID is not integral");
   auto*holder=r.getCardOwner(w.id);if(!holder||!r.getAllPlayers(true).contains(holder)||r.getCardPlace(w.id)!=Player::PlaceEquip)return shebianNo("Shebian source is not current equipment");
   const Card*c=nullptr;for(auto*e:holder->getEquips())if(e->getEffectiveId()==w.id)c=e;bool seen=false;
   auto inspect=[&](const AIPlayerView&v){if(v.objectName==holder->objectName())for(const auto&e:v.equips)if(e.effectiveId==w.id&&e.nativeIdentity&&globalCardLifetimeManager().isLive(e.nativeIdentity)&&e.nativeIdentity->address==c)seen=true;};
   inspect(q.worldView.self);for(const auto&v:q.worldView.players)inspect(v);if(!c||!seen)return shebianNo("Shebian source equipment lacks public current identity");
  }else{if(!v.value("value").isString())return shebianNo("Shebian Player shape differs");w.target=r.findPlayerByObjectName(v.value("value").toString(),true);if(!w.target)return shebianNo("Shebian Player outside room");}
 }
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding game(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return shebianNo("Shebian game stack unavailable");ShebianStack st(L);
 lua_pushcfunction(L,shebianAssign);lua_pushlightuserdata(L,&w);const int status=LuaRuntime::protectedCall(L,1,0,0);s->pending=-1;
 if(status!=LUA_OK)return shebianNo(w.error?w.error:"Shebian protected source operation failed");return w.out;
}

// smart-ai.lua:3431 actual Self prefix, not a copied local observation.
namespace {
struct OrdinaryDisabledWrite{Room*r;ServerPlayer*p;const AIRequest*q;QJsonArray ids;const char*error=nullptr;};
int ordinaryDisabledFail(lua_State*L,OrdinaryDisabledWrite*w,const char*why){w->error=why;return luaL_error(L,"%s",why);}
int assignOrdinaryDisabled(lua_State*L){
 auto*w=static_cast<OrdinaryDisabledWrite*>(lua_touserdata(L,1));
 if(!lua_checkstack(L,4096))return ordinaryDisabledFail(L,w,"ordinary disabled list frame capacity unavailable");
 // SWIG QVariant::toIntList is exactly toList()/toInt() in source order.
 // Construct the same native list type; no gameplay userdata leaves this VM.
 AILuaInspect::globals(L);AILuaInspect::field(L,-1,"sgs");AILuaInspect::field(L,-1,"IntList");
 if(!matchAINativeScratchConstructor(*w->r,L,-1,"IntList"))return ordinaryDisabledFail(L,w,"ordinary disabled IntList constructor changed");lua_call(L,0,1);int value=lua_absindex(L,-1);
 if(!matchAINativeObjectMethods(*w->r,L,value,"IntList",{"append","contains"}))return ordinaryDisabledFail(L,w,"ordinary disabled IntList methods changed");
 for(const auto&v:w->ids){lua_getfield(L,value,"append");lua_pushvalue(L,value);lua_pushinteger(L,v.toInt());lua_call(L,2,0);}
 if(!request(*w->r,w->p,*w->q)||!pushAIActualAISelf(*w->r,w->p,L))return ordinaryDisabledFail(L,w,"ordinary disabled actual Self unavailable");int self=lua_absindex(L,-1);
 AILuaInspect::field(L,self,"disabled_ids");bool present=!lua_isnil(L,-1);lua_pop(L,1);
 if(!present&&lua_getmetatable(L,self)){AILuaInspect::field(L,-1,"__newindex");bool absent=lua_isnil(L,-1);lua_pop(L,2);if(!absent)return ordinaryDisabledFail(L,w,"ordinary disabled assignment handler changed");}
 if(!request(*w->r,w->p,*w->q))return ordinaryDisabledFail(L,w,"ordinary disabled request changed before assignment");
 lua_pushliteral(L,"disabled_ids");lua_pushvalue(L,value);lua_settable(L,self);
 if(!request(*w->r,w->p,*w->q)||!pushAIActualAISelf(*w->r,w->p,L)||!lua_rawequal(L,self,-1))return ordinaryDisabledFail(L,w,"ordinary disabled actual Self changed after assignment");
 AILuaInspect::field(L,self,"disabled_ids");if(!lua_rawequal(L,value,-1))return ordinaryDisabledFail(L,w,"ordinary disabled assignment changed");return 0;
}
}
QJsonObject refreshAIOrdinaryCardChoice(Room&r,ServerPlayer*p,const AIRequest&q){
 const auto bad=[](const char*why){return QJsonObject{{"error",why},{"key","askForCardChosen.disabled_ids"}};};
 if(!r.roomRuntime()||r.roomRuntime()->isClosing()||!request(r,p,q))return bad("ordinary card-choice request unavailable");
 auto&vm=r.roomRuntime()->lua();LuaRuntime::Binding binding(vm,false);auto*L=vm.state();if(!L||!lua_checkstack(L,4096))return bad("ordinary card-choice gameplay VM unavailable");ShebianStack st(L);
 const auto result=refreshAIStrategyDiscardPreferences(r,p);if(result.contains("error"))return result;
 OrdinaryDisabledWrite w{&r,p,&q,result.value("disabled_ids").toArray()};
 lua_pushcfunction(L,assignOrdinaryDisabled);lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,1,0,0)!=LUA_OK)return bad(w.error?w.error:"ordinary disabled protected assignment failed");
 return result;
}

// ---- ol-st-standard ----
namespace {
bool olStStandardDisclosed(const AIRequest &r,int id) {
    const auto cards=[&](const QList<AICardView>&list){for(const auto &c:list)if(c.effectiveId==id)return true;return false;};
    for(const auto *list:{&r.worldView.handCards,&r.worldView.discardPile,&r.choiceOptions.cards})if(cards(*list))return true;
    const auto player=[&](const AIPlayerView&p){for(const auto *list:{&p.knownCards,&p.equips,&p.judgingArea})if(cards(*list))return true;for(const auto &pile:p.piles)if(pile.open&&cards(pile.cards))return true;return false;};
    if(player(r.worldView.self))return true;
    for(const auto &pile:r.worldView.self.piles)if(cards(pile.cards))return true; // owner-only lawful faces, even a closed hand pile
    for(const auto&p:r.worldView.players)if(player(p))return true;return false;
}
}
void projectAIStrategyOlStandardResponse(AIRequest &request,Room &room,ServerPlayer *viewer,QJsonObject &context) {
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
        if(card){const int id=card->getEffectiveId();if(id>=0&&olStStandardDisclosed(request,id))row.insert("card_id",id);else row.insert("known",false);}
        tags.insert("olpiaoling",row);
    }
    if(reason=="olluanji"&&request.kind==AIRequest::PlayerChosen){
        const QVariant raw=viewer->getTag("olluanji_data");const CardUseStruct use=raw.value<CardUseStruct>();
        QJsonObject row{{"known",true},{"present",raw.isValid()}};
        if(raw.isValid()){
            QJsonObject data;QJsonArray to;for(const auto*p:use.to)to.append(p->objectName());data.insert("to",to);if(use.from)data.insert("from",use.from->objectName());
            if(use.card){const int id=use.card->getEffectiveId();if(!use.card->isVirtualCard()&&id>=0&&olStStandardDisclosed(request,id))data.insert("card_id",id);else row.insert("known",false);}
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
int aiOlStandardResponsePrototype(const AIRequest &r,const ServerPlayer *viewer,
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

bool aiOlStandardResponseConstruction(const AIRequest &r,const ServerPlayer *viewer,const QJsonObject &spec)
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
        const int id=int(n);if(seen.contains(id)||room.getCardOwner(id)!=viewer||!olStStandardDisclosed(r,id))return false;seen.insert(id);
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
