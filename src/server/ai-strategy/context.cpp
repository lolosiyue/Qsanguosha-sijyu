#include "ai-strategy/context.h"

// ---- choice-context ----
#include "ai.h"
#include "ai-strategy/owner.h"
#include "wrapped-card.h"
#include <QObject>
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"
#include <QJsonArray>
namespace {
bool roster(Room &room, const Player *p)
{ return !p || room.findPlayerByObjectName(p->objectName(), true) == p; }
QObject *agHistory(Room &room, bool create)
{
    auto *ledger=room.findChild<QObject *>(QStringLiteral("isolated-ai-choice-ag-history"),Qt::FindDirectChildrenOnly);
    if (!ledger && create) { ledger=new QObject(&room); ledger->setObjectName(QStringLiteral("isolated-ai-choice-ag-history")); }
    return ledger;
}
QJsonObject agFace(const Card *card, int id)
{
    // Exactly the face the AG notification disclosed. No foreign card flags,
    // private skill chain, source IDs, limitations or action authority.
    return {{"id",id},{"effective_id",id},{"name",card->objectName()},
        {"class_name",card->getClassName()},{"engine_class_name",QString::fromLatin1(card->metaObject()->className())},
        {"kind_of",QJsonArray::fromStringList(card->getKindOfNames())},{"subtype",card->getSubtype()},
        {"suit",int(card->getSuit())},{"number",card->getNumber()},{"type_id",int(card->getTypeId())},
        {"red",card->isRed()},{"black",card->isBlack()},{"virtual_card",false},{"subcards",QJsonArray()},
        {"legacy_string_key",QString::number(id)},{"legacy_sort_key",QString::number(id)}};
}
QJsonObject face(const AICardView &c)
{
    QJsonArray sub; for (int id : c.subcardIds) sub.append(id);
    return {{"id",c.cardId},{"effective_id",c.effectiveId},{"name",c.objectName},
        {"class_name",c.className},{"engine_class_name",c.engineClassName},
        {"kind_of",QJsonArray::fromStringList(c.kindOfNames)},{"suit",c.suit},{"number",c.number},
        {"type_id",c.typeId},{"equip_slot",c.equipSlot},{"virtual_card",c.virtualCard},
        {"subcards",sub},{"red",c.red},{"black",c.black},{"skill_name",c.skillName},
        {"legacy_string_key",c.legacyStringKey},{"legacy_sort_key",c.legacySortKey},
        {"known_flags",c.knownFlags},{"flags_complete",c.flagsComplete}};
}
QJsonObject disclosed(const AIRequest &request, Room &room, ServerPlayer *viewer, int id)
{
    if (id < 0) return {};
    const auto scan = [id](const QList<AICardView> &cards) {
        for (const auto &card : cards) if (card.effectiveId == id) return face(card);
        return QJsonObject();
    };
    for (const auto *cards : {&request.worldView.handCards, &request.worldView.discardPile, &request.choiceOptions.cards}) {
        auto row = scan(*cards); if (!row.isEmpty()) return row;
    }
    const auto player = [&](const AIPlayerView &p) {
        for (const auto *cards : {&p.knownCards,&p.equips,&p.judgingArea}) { auto row=scan(*cards); if (!row.isEmpty()) return row; }
        for (const auto &pile:p.piles) if (pile.open) { auto row=scan(pile.cards); if (!row.isEmpty()) return row; }
        return QJsonObject();
    };
    auto row=player(request.worldView.self); if (!row.isEmpty()) return row;
    for (const auto &p:request.worldView.players) { row=player(p); if (!row.isEmpty()) return row; }
    const auto witness=readAIStrategyPublicDisclosure(room,viewer,id);
    return witness.value("known").toBool() ? witness.value("card").toObject() : QJsonObject();
}
}
void recordAIStrategyChoiceAGDisclosure(Room &room, ServerPlayer *recipient, const QList<int> &ids)
{
    if (!recipient || recipient->getRoom()!=&room || !roster(room,recipient)) return;
    auto *ledger=agHistory(room,true);
    auto viewers=ledger->property("viewers").toJsonObject();
    auto rows=viewers.value(recipient->objectName()).toObject();
    for (int id:ids) {
        if (id<0) continue;
        const Card *card=room.getCard(id);
        if (!card || card->isVirtualCard() || card->getEffectiveId()!=id) continue;
        const auto *owner=room.getCardOwner(id);
        rows.insert(QString::number(id),QJsonObject{{"card",agFace(card,id)},
            {"place",int(room.getCardPlace(id))},{"owner",owner?owner->objectName():QString()},
            {"identity",aiLegacyCardIdentity(room,recipient,card)}});
        // Direct invalidation also prevents identity A->B->A from resurrecting
        // a stale witness. Simple suit/number changes are checked at read time.
        auto watched=ledger->property("watched").toJsonObject();
        const auto key=QString::number(id);
        if (!watched.contains(key)) {
            if (const auto *wrapped=qobject_cast<const WrappedCard *>(card)) {
                QObject::connect(wrapped,&WrappedCard::identityChanged,ledger,
                    [&room,id](){invalidateAIStrategyChoiceAGDisclosure(room,id);},Qt::DirectConnection);
                watched.insert(key,true);ledger->setProperty("watched",watched);
            }
        }
    }
    viewers.insert(recipient->objectName(),rows);ledger->setProperty("viewers",viewers);
}
void invalidateAIStrategyChoiceAGDisclosure(Room &room, int id)
{
    auto *ledger=agHistory(room,false);if (!ledger) return;
    auto viewers=ledger->property("viewers").toJsonObject();
    for (auto it=viewers.begin();it!=viewers.end();++it) {
        auto rows=it.value().toObject();rows.remove(QString::number(id));it.value()=rows;
    }
    ledger->setProperty("viewers",viewers);
}
QJsonObject readAIStrategyChoiceAGDisclosure(Room &room, ServerPlayer *viewer, int id)
{
    const QJsonObject unknown{{"known",false}};
    if (!viewer || viewer->getRoom()!=&room || !roster(room,viewer) || id<0) return unknown;
    auto *ledger=agHistory(room,false);if (!ledger) return unknown;
    const auto row=ledger->property("viewers").toJsonObject().value(viewer->objectName()).toObject().value(QString::number(id)).toObject();
    if (row.isEmpty()) return unknown; // Never inspect a hidden face before proof.
    const auto *card=room.getCard(id);const auto *owner=room.getCardOwner(id);
    if (!card || card->isVirtualCard() || card->getEffectiveId()!=id
        || row.value("place").toInt()!=int(room.getCardPlace(id))
        || row.value("owner").toString()!=(owner?owner->objectName():QString())
        || row.value("identity").toString()!=aiLegacyCardIdentity(room,viewer,card)
        || row.value("card").toObject()!=agFace(card,id)) {
        invalidateAIStrategyChoiceAGDisclosure(room,id);return unknown;
    }
    return {{"known",true},{"card",row.value("card")}};
}
void projectAIStrategyChoiceContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context, const std::function<QJsonObject(const Card *)> &publicCard)
{
    Q_UNUSED(publicCard);
    if (!viewer || request.viewerObjectName != viewer->objectName() || !roster(room,viewer)) return;
    // Pindian helper may be called while planning any skill. This is the
    // viewer's own finite tag namespace, never another player's private tag.
    QJsonObject tags;
    const auto ownerTags=viewer->getAllTags();
    const QString prefix=QStringLiteral("targetsPindian_");
    for (auto it=ownerTags.cbegin();it!=ownerTags.cend();++it)
        if (it.key().startsWith(prefix)) tags.insert(it.key().mid(prefix.size()),it.value().toString());
    auto pindian=request.worldView.strategyFacts.value("pindian").toObject();
    pindian.insert("owner_target_tags",tags);
    pindian.insert("owner_target_tags_complete",true);
    request.worldView.strategyFacts.insert("pindian",pindian);
    auto competition=request.worldView.strategyFacts.value("mcompetition").toObject();
    competition.insert("qishibusiyutushou_equips",viewer->property("qishibusiyutushouEquips").toString());
    request.worldView.strategyFacts.insert("mcompetition",competition);
    auto body=context.value("choice_body").toObject();
    const auto reason=request.choiceOptions.reason;
    const bool invoke=request.kind==AIRequest::SkillInvoke;
    const bool choice=request.kind==AIRequest::Choice;
    const bool chosen=request.kind==AIRequest::PlayerChosen;
    if ((invoke && (reason=="y_huaiju" || reason=="y_youfang" || reason=="y_zhixi"))
        || (choice && reason=="y_youfang")) {
        // Only the public endpoints are needed, never unrelated move card IDs.
        const auto move=data.value<CardsMoveOneTimeStruct>();
        if (roster(room,move.from) && roster(room,move.to))
            body.insert("shadow_move",QJsonObject{{"from",move.from?move.from->objectName():QString()},
                {"to",move.to?move.to->objectName():QString()}});
    }
    const bool shouju=(invoke && reason=="y_shouju") || (chosen && reason=="y_shouju")
        || (request.kind==AIRequest::RespondCard && request.choiceOptions.question=="askForCard"
            && request.prompt.section(':',0,0)=="y_shouju");
    if (shouju) {
        // The rule asks while this equipment/judge has been publicly discarded.
        const int id=(chosen?room.getTag("y_shouju"):data).toInt();
        auto row=disclosed(request,room,viewer,id);
        if (!row.isEmpty()) body.insert("y_shouju_card",row);
    }
    if (chosen && reason=="y_huaiju") {
        const int id=viewer->getMark("y_huaiju");
        auto row=disclosed(request,room,viewer,id);
        if (!row.isEmpty()) body.insert("y_huaiju_card",row);
    }
    if (chosen && reason=="y_xiaoyi") {
        // Rule 2555 publicly fills then clears AG before this owner tag read.
        // Require surviving lawful disclosed face; a tag alone is not visibility.
        const auto *card=viewer->getTag("y_xiaoyi").value<const Card *>();
        if (!card) body.insert("y_xiaoyi_card",false);
        else {
            auto row=disclosed(request,room,viewer,card->getEffectiveId());
            if (row.isEmpty()) {
                const auto seen=readAIStrategyChoiceAGDisclosure(room,viewer,card->getEffectiveId());
                if (seen.value("known").toBool()) row=seen.value("card").toObject();
            }
            if (!row.isEmpty()) body.insert("y_xiaoyi_card",row);
        }
    }
    if ((chosen || request.kind==AIRequest::CardChosen) && reason=="y_fanjin")
        body.insert("y_fanjin_property",viewer->property("y_fanjin").toString());
    if (choice && reason=="shenyi") body.insert("shenyi_phases",data.toString());
    if (invoke && reason=="caoduo") {
        const auto fields=data.toString().split(':');
        const auto *target=fields.size()>3?room.findPlayerByObjectName(fields.at(3)):nullptr;
        body.insert("caoduo_target",target?QJsonValue(target->objectName()):QJsonValue(false));
    }
    if (invoke && reason=="huanxing") body.insert("huanxing_used",viewer->getTag("huanxing").toBool());
    if (invoke && reason=="xiaode" && data.metaType().id()==QMetaType::QStringList) {
        const auto names=data.toStringList(); body.insert("xiaode_skills",QJsonArray::fromStringList(names));
        QJsonObject skills;
        for (const auto &name:names) {
            const Skill *skill=Sanguosha->getSkill(name); const ViewAsSkill *viewAs=Sanguosha->getViewAsSkill(name);
            const auto *trigger=dynamic_cast<const TriggerSkill *>(skill);
            QJsonObject row{{"exists",skill!=nullptr},{"view_as",viewAs!=nullptr},{"trigger",trigger!=nullptr},
                {"has_cards_move",trigger && trigger->getTriggerEvents().contains(CardsMoveOneTime)}};
            if (skill) row.insert("description",skill->getDescription());
            if (viewAs) row.insert("enabled_at_play",viewAs->isEnabledAtPlay(viewer));
            skills.insert(name,row);
        }
        context.insert("huashen_skills",skills);
    }
    if (request.kind==AIRequest::RespondCard && request.choiceOptions.question=="askForNullification") {
        const auto card=context.value("card").toObject();
        const auto heg=request.worldView.strategyFacts.value("hegemony").toObject();
        if (heg.value("enabled").toBool() && heg.value("original_overrides_loaded").toBool()
            && QSet<QString>{"HAwaitExhausted","HRuleTheWorld","HConquering","HConsolidateCountry","HChaos"}.contains(card.value("class_name").toString())) {
            const auto key=card.value("legacy_string_key").toString();
            if (!key.isEmpty()) {
                QJsonArray targets; bool valid=true;
                for (const auto &item:room.getTag("targets"+key).toList()) {
                    const auto *p=item.value<ServerPlayer *>();
                    if (!p || !roster(room,p)) { valid=false; break; }
                    targets.append(p->objectName());
                }
                if (valid) {
                    if (card.value("class_name") == "HAwaitExhausted")
                        body.insert("await_exhausted",QJsonObject{{"targets",targets},
                            {"nullifying_times",room.getTag("NullifyingTimes").toInt()},
                            {"nullification_type",room.getTag("NullificatonType").toBool()}});
                    else context.insert("original26_nullification",QJsonObject{{"targets",targets},
                        {"times",room.getTag("NullifyingTimes").toInt()},
                        {"type",room.getTag("NullificatonType").toBool()}});
                }
            }
        }
    }
    if (!body.isEmpty()) context.insert("choice_body",body);
}

// ---- choice-continuity ----
#include "ai-strategy/source-slots.h"
#include "ai-strategy/ol-ai.h"
#include "ai-strategy/xufa.h"
#include "ai-strategy/tenyear-ai.h"
#include "ai-strategy/overseas.h"
#include "ai-strategy/inovation-ai.h"
#include "ai-strategy/lua-seal.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QPointer>
#include <QHash>
#include <cmath>
#include <cstring>
#include <limits>
#include "ai-strategy/lua-raw-inspect.h"
namespace {
struct ChoiceContinuityRow {QPointer<ServerPlayer>writer;QJsonObject value;bool known=false,diverged=false;quint64 epoch=0;quint64 xufaTransportEpoch=0,xufaWriteSerial=0,xufaGeneration=0;};
struct Continuity:QObject {
 explicit Continuity(Room&r):QObject(&r),room(r){}
 Room&room;
 quint64 epoch=1;bool exhausted=false;
 quint64 serial=0;bool legacyReady=false,isolatedReady=false,entered=false;
 QString lastViewer;QHash<QString,ChoiceContinuityRow>rows;
 bool*xufaWriteGuard=nullptr;quint64 nextXufaWriteSerial=1,xufaRootGeneration=0;
};
Continuity*ledger(Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<Continuity*>(c))return s;return new Continuity(r);}
const QStringList keys{"xuhe","olxuhe","xufa","olshebian_from","olshebian_to","olshebian"};
QString registry(const QString&k){return k=="olshebian"?"ai_skill_cardchosen":k.startsWith("olshebian_")?"ai_skill_playerchosen":"ai_skill_choice";}
int domain(const QString&k){return k=="olshebian"?4:k.startsWith("olshebian_")?3:2;}
void forget(ChoiceContinuityRow&r){r.known=false;r.value={};r.writer=nullptr;r.diverged=false;}
void hard(Continuity*s){retireAIXufaChoiceRegistry(s->room);if(s->xufaWriteGuard)*s->xufaWriteGuard=true;for(auto i=s->rows.begin();i!=s->rows.end();++i)forget(*i);}
thread_local AIChoiceContinuityInquiry*active=nullptr;
QJsonObject unknown(){return{{"known",false}};}
QString questionKind(const AIRequest&r){switch(r.kind){case AIRequest::SkillInvoke:return"invoke";case AIRequest::Choice:return"choice";case AIRequest::Activate:return"activate";case AIRequest::PlayerChosen:return"player_chosen";case AIRequest::CardChosen:return"card_chosen";default:return{};}}
bool eq(const lua_TableObservation&e,const char*k){return e.string_key&&e.string_key_length==strlen(k)&&!memcmp(e.string_key,k,e.string_key_length);}
bool admittedWrite(const AIRequest&r,const ServerPlayer*p,const QString&key){
 if(key=="xuhe"||key=="olxuhe")return r.kind==AIRequest::SkillInvoke
  &&(r.choiceOptions.reason==key||(key=="xuhe"&&r.choiceOptions.reason=="olxuhe"))
  &&p->hasSkill(r.choiceOptions.reason);
 if(key=="xufa")return r.kind==AIRequest::Activate&&p->hasSkill("xufa");
 return r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason=="olshebian"&&p->hasSkill("olshebian");
}
bool admittedRead(const AIRequest&r,const QString&key){
 if(key=="xuhe"&&(r.kind==AIRequest::SkillInvoke)&&(r.choiceOptions.reason=="xuhe"||r.choiceOptions.reason=="olxuhe"))return true;
 if(key=="olshebian")return r.kind==AIRequest::CardChosen&&r.choiceOptions.reason==key;
 if(key.startsWith("olshebian_"))return r.kind==AIRequest::PlayerChosen&&r.choiceOptions.reason==key;
 return r.kind==AIRequest::Choice&&r.choiceOptions.reason==key;
}
// Bookkeeping may run with the isolated VM bound. Do not call state() here:
// it deliberately rejects a noncurrent game VM. No Lua stack is accessed.
bool liveXufaGame(Room&r){
 auto*rt=r.roomRuntime();if(!rt||rt->isClosing())return false;
 const auto&vm=rt->lua();return vm.isCurrentThreadOwner()&&vm.lifecycle()==LuaRuntime::Lifecycle::Running&&vm.rawState();
}
QJsonObject read(Continuity*s,ServerPlayer*p,const QString&key){
 auto i=s->rows.find(key);if(i==s->rows.end())return unknown();auto&r=*i;
 if(s->exhausted||r.epoch!=s->epoch||!r.known||(r.writer&&r.writer!=p))return unknown();
 if(key=="xufa"){
  if(!p||!p->getRoom()||!s->nextXufaWriteSerial
     ||(r.value.value("present")==true&&!r.xufaWriteSerial))return unknown();
  auto*rt=p->getRoom()->roomRuntime();
  if(!liveXufaGame(*p->getRoom())||!r.xufaGeneration
     ||r.xufaGeneration!=s->xufaRootGeneration||r.xufaGeneration!=rt->lua().generation()
     ||!r.xufaTransportEpoch||r.xufaTransportEpoch!=rt->xufaTransportEpoch())return unknown();
 }
 return r.value;
}
bool validValue(Room&room,const AIRequest&r,ServerPlayer*p,const QString&key,const QJsonObject&v){
 if(v.value("known")!=true||!v.value("present").isBool())return false;
 if(!v.value("present").toBool())return false; // fixed reached writers never assign nil; copy/consume do so internally
 if(v.size()!=3)return false;
 auto value=v.value("value");
 if(key=="xuhe"||key=="olxuhe")return value.isString()&&(value=="draw"||value=="discard");
 if(key=="xufa"){
  if(!value.isString())return false;QList<int>ids=p->handCards();for(auto*c:p->getEquips())ids<<c->getEffectiveId();ids<<p->getPile("xufa");
  for(int id:ids){auto*c=room.getCard(id);if(c&&c->isNDTrick()&&c->objectName()==value.toString())return true;}return false;
 }
 if(key!="olshebian")return value.isString()&&room.findPlayerByObjectName(value.toString(),true);
 if(!value.isDouble()||!std::isfinite(value.toDouble())||value.toDouble()!=value.toInt(-1)||value.toInt(-1)<0)return false;
 const int id=value.toInt();
 // MoveField(nil,"e") supplies an actual public equip, never a hidden face.
 for(auto*owner:room.getAllPlayers(true))for(auto*c:owner->getEquips())if(c->getEffectiveId()==id)return true;
 return false;
}
// R46: the two fixed Xufa producer statements assign the actual game registry.
// This is independent of whether later native entries have a nonwriter proof.
// In particular, a successful write is NOT a lease through an unknown entry.
struct XufaGlobalWrite {
 Continuity*s;Room*room;ServerPlayer*owner;const AIRequest*request;
 const void*globals=nullptr;const void*sgs=nullptr;const void*registry=nullptr;
 bool interrupted=false,armed=false;quint64 transportEpoch=0,generation=0;lua_State*state=nullptr;
};
bool xufaCurrent(const XufaGlobalWrite&w){
 return w.owner&&w.owner->getRoom()==w.room&&w.request->kind==AIRequest::Activate
  &&w.request->viewerObjectName==w.owner->objectName()
  &&w.request->worldView.self.objectName==w.owner->objectName()
  &&w.request->stateRevision==w.room->roomRuntime()->stateRevision()
  &&!w.room->roomRuntime()->isClosing()&&w.s->legacyReady&&w.s->isolatedReady
  &&w.transportEpoch&&w.transportEpoch==w.room->roomRuntime()->xufaTransportEpoch()
  &&w.state&&w.state==w.room->roomRuntime()->lua().state()
  &&w.generation&&w.generation==w.s->xufaRootGeneration
  &&w.generation==w.room->roomRuntime()->lua().generation();
}
bool xufaRoots(lua_State*L,const XufaGlobalWrite&w){
 const int top=lua_gettop(L);
 AILuaInspect::globals(L);const bool globals=lua_topointer(L,-1)==w.globals;
 AILuaInspect::field(L,-1,"sgs");const bool ns=lua_istable(L,-1)&&lua_topointer(L,-1)==w.sgs;
 AILuaInspect::field(L,-1,"ai_skill_choice");auto*t=AILuaInspect::table(L,-1);
 const bool registry=t&&!t->metatable&&lua_topointer(L,-1)==w.registry;
 lua_settop(L,top);return globals&&ns&&registry;
}
int assignXufaGlobal(lua_State*L){
 if(!lua_checkstack(L,256))return luaL_error(L,"Xufa scalar assignment stack unavailable");
 auto*w=static_cast<XufaGlobalWrite*>(lua_touserdata(L,3));
 if(!w||lua_type(L,2)!=LUA_TSTRING||!xufaCurrent(*w)||!xufaRoots(L,*w)
  ||lua_topointer(L,1)!=w->registry)return luaL_error(L,"Xufa scalar assignment context changed");
 // The outer protected-entry observer has already invalidated old observations.
 // Arm only now. Any reentry/async/structural failure during the write prevents
 // publishing a new known row, even if its final scalar happens to equal ours.
 w->armed=true;w->s->xufaWriteGuard=&w->interrupted;
 lua_pushvalue(L,2);lua_setfield(L,1,"xufa"); // source setfield, not a shadow/raw override
 return 0;
}
bool writeActualXufaGlobal(Room&room,ServerPlayer*p,const AIRequest&r,Continuity*s,const QString&value,quint64&transportStamp,quint64&generationStamp){
 transportStamp=0;generationStamp=0;
 auto*rt=room.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,512))return false;
 const int top=lua_gettop(L);struct Restore {lua_State*L;int top;Continuity*s;bool*previous;
  ~Restore(){s->xufaWriteGuard=previous;lua_settop(L,top);}} restore{L,top,s,s->xufaWriteGuard};
 XufaGlobalWrite w;w.s=s;w.room=&room;w.owner=p;w.request=&r;w.transportEpoch=rt->xufaTransportEpoch();w.generation=rt->lua().generation();w.state=L;
 AILuaInspect::globals(L);w.globals=lua_topointer(L,-1);
 AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return false;w.sgs=lua_topointer(L,-1);
 AILuaInspect::field(L,-1,"ai_skill_choice");auto*t=AILuaInspect::table(L,-1);
 if(!t||t->metatable)return false;w.registry=lua_topointer(L,-1);const int registryIndex=lua_absindex(L,-1);
 lua_pushcfunction(L,assignXufaGlobal);lua_pushvalue(L,registryIndex);
 const auto bytes=value.toUtf8();lua_pushlstring(L,bytes.constData(),size_t(bytes.size()));lua_pushlightuserdata(L,&w);
 if(LuaRuntime::protectedCall(L,3,0,0)!=LUA_OK)return false;
 if(!w.armed||w.interrupted||!xufaCurrent(w)||!xufaRoots(L,w))return false;
 AILuaInspect::field(L,registryIndex,"xufa");size_t n=0;const char*actual=lua_type(L,-1)==LUA_TSTRING?lua_tolstring(L,-1,&n):nullptr;
 if(!actual||n!=size_t(bytes.size())||std::memcmp(actual,bytes.constData(),n)||!xufaCurrent(w))return false;
 transportStamp=w.transportEpoch;generationStamp=w.generation;return true;
}
bool actualXufaGlobalMatches(Room&room,const QJsonObject&row){
 if(row.value("known")!=true||row.value("present")!=true||!row.value("value").isString())return false;
 auto*rt=room.roomRuntime();LuaRuntime::Binding binding(rt->lua(),false);auto*L=rt->lua().state();
 if(!L||!lua_checkstack(L,64))return false;const int top=lua_gettop(L);
 struct Restore{lua_State*L;int top;~Restore(){lua_settop(L,top);}}restore{L,top};
 AILuaInspect::globals(L);AILuaInspect::field(L,-1,"sgs");if(!lua_istable(L,-1))return false;
 AILuaInspect::field(L,-1,"ai_skill_choice");auto*t=AILuaInspect::table(L,-1);if(!t||t->metatable)return false;
 AILuaInspect::field(L,-1,"xufa");size_t n=0;const char*actual=lua_type(L,-1)==LUA_TSTRING?lua_tolstring(L,-1,&n):nullptr;
 const auto expected=row.value("value").toString().toUtf8();return actual&&n==size_t(expected.size())&&!std::memcmp(actual,expected.constData(),n);
}
}
AIChoiceContinuityInquiry::AIChoiceContinuityInquiry(Room&r,const QString&k,ServerPlayer*p,const QString&why)
 :room(&r),kind(k),reason(why),viewer(p),previous(active){enterAIXufaChoiceRegistryPolicy(r,p,active!=nullptr);enterAIShebianInquiry(r,p,k,why);serial=++ledger(r)->serial;active=this;}
AIChoiceContinuityInquiry::~AIChoiceContinuityInquiry(){active=previous;}
void invalidateAIChoiceContinuity(Room&r){auto*s=ledger(r);s->entered=true;hard(s);}

ServerPlayer*currentAIXufaContinuityWriter(Room&r) noexcept {
 // Runtime's Xufa producer/consumer use R53's actual observed registry. The
 // earlier copied row is not the producer and cannot authorize this scope.
 return currentAIXufaChoiceWriter(r);
}
quint64 currentAIXufaWriteSerial(Room&r) noexcept {
 return currentAIXufaChoiceSerial(r);
}
void interruptAIXufaContinuity(Room&r) {
 retireAIXufaChoiceRegistry(r);
 for(auto*c:r.children())if(auto*s=dynamic_cast<Continuity*>(c)){
  if(s->xufaWriteGuard)*s->xufaWriteGuard=true;
  auto i=s->rows.find(QStringLiteral("xufa"));if(i!=s->rows.end())forget(*i);return;
 }
}
void interruptAIChoiceContinuityExceptXufa(Room&r){invalidateAIGoodatwep(r);
 auto*s=ledger(r);s->entered=true;if(s->xufaWriteGuard)*s->xufaWriteGuard=true;
 for(auto i=s->rows.begin();i!=s->rows.end();++i)if(i.key()!=QStringLiteral("xufa"))forget(*i);
}

void interruptAIChoiceContinuity(Room&r){invalidateAIGoodatwep(r);auto*s=ledger(r);s->entered=true;hard(s);}
void interruptAIChoiceContinuityAsync(Room&r)noexcept{
 retireAIXufaChoiceRegistry(r);
 invalidateAIGoodatwep(r);
 // Lookup only. No QObject creation, container detach, value/lease destruction,
 // Lua access or notification. A fresh observed write receives the new epoch;
 // other owners' old rows cannot be resurrected by that write.
 for(auto*c:r.children())if(auto*s=dynamic_cast<Continuity*>(c)){
  s->entered=true;
  if(s->xufaWriteGuard)*s->xufaWriteGuard=true;
  if(s->epoch==std::numeric_limits<quint64>::max())s->exhausted=true;
  else ++s->epoch;
  return;
 }
}
void beginAIChoiceContinuityPolicy(Room&r,const QString&viewer){
 enterAIXufaChoiceRegistryPolicy(r,r.findPlayerByObjectName(viewer,true),false);
 enterAIGoodatwep(r,viewer);
 auto*s=ledger(r);s->entered=true;
 if(pendingAIXufaEvent(r)){interruptAIChoiceContinuityExceptXufa(r);return;}
 // Both isolated decisions and events enter here before executing policy.
 // A foreign event must not reveal its conditional registry access pattern.
 if(viewer.isEmpty()||(!s->lastViewer.isEmpty()&&s->lastViewer!=viewer))hard(s);
 s->lastViewer=viewer;
}
void observeAIChoiceContinuityEvent(Room&r,const QString&viewer,const QString&actor){
 invalidateAIGoodatwep(r); // fixed source events have no tag-write authority
 if(pendingAIXufaEvent(r)){interruptAIChoiceContinuityExceptXufa(r);return;}
 beginAIChoiceContinuityPolicy(r,viewer);
 if(viewer.isEmpty()||actor!=viewer)interruptAIChoiceContinuity(r);
}
void initializeAIChoiceContinuityRoots(Room&r,bool isolated,bool valid,const QJsonObject&initialNil,quint64 xufaTransportStamp,quint64 xufaGenerationStamp){
 auto*s=ledger(r);if(isolated){s->isolatedReady=valid;return;}
 if(s->legacyReady)return;s->legacyReady=valid;
 s->xufaRootGeneration=valid&&xufaGenerationStamp&&liveXufaGame(r)
   &&xufaGenerationStamp==r.roomRuntime()->lua().generation()?xufaGenerationStamp:0;
 if(!valid||s->entered)return;
 for(const auto&k:keys)if(initialNil.value(k)==true){
  s->rows[k]={nullptr,{{"known",true},{"present",false}},true,false,s->epoch};
  if(k=="xufa"){
   s->rows[k].xufaTransportEpoch=xufaTransportStamp;
   s->rows[k].xufaGeneration=xufaGenerationStamp;
   if(!s->xufaRootGeneration||!xufaTransportStamp||xufaTransportStamp!=r.roomRuntime()->xufaTransportEpoch())forget(s->rows[k]);
  }
 }
}
void observeAIChoiceContinuityTable(Room&r,int d,bool isolated,const lua_TableObservation&e)noexcept{
 try{
  auto*s=ledger(r);
  if(d<2){
   const bool affected=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE
    ||(e.event==LUA_TABLE_OBSERVER_WRITE&&(d==0?eq(e,"sgs"):(eq(e,"ai_skill_choice")||eq(e,"ai_skill_playerchosen")||eq(e,"ai_skill_cardchosen"))));
   if(affected){if(isolated)s->isolatedReady=false;else s->legacyReady=false;hard(s);}return;
  }
  const bool structural=e.event==LUA_TABLE_OBSERVER_FREE||e.event==LUA_TABLE_OBSERVER_METATABLE;
  if(structural){if(isolated)s->isolatedReady=false;else s->legacyReady=false;}
  for(const auto&k:keys){if(domain(k)!=d)continue;const auto bytes=k.toUtf8();
   const bool matched=eq(e,bytes.constData());auto i=s->rows.find(k);if(i==s->rows.end())continue;
   if(structural||(e.event==LUA_TABLE_OBSERVER_WRITE&&matched)
    ||(!isolated&&i->diverged&&((e.event==LUA_TABLE_OBSERVER_READ&&matched)||e.event==LUA_TABLE_OBSERVER_ENUMERATE)))forget(*i);
  }
 }catch(...){/* No exception crosses Lua's observer boundary. */try{auto*s=ledger(r);s->legacyReady=false;s->isolatedReady=false;hard(s);}catch(...){}}
}
void observeAIChoiceContinuityDecision(Room&r,const AIRequest&q){auto*s=ledger(r);s->entered=true;
 enterAIXufaChoiceRegistryPolicy(r,r.findPlayerByObjectName(q.viewerObjectName,true),pendingAIXufaEvent(r));
 if(pendingAIXufaEvent(r))hard(s); // Reentrant decision never inherits event custody.
 if(!s->lastViewer.isEmpty()&&s->lastViewer!=q.viewerObjectName)hard(s);s->lastViewer=q.viewerObjectName;
 if(!active||active->room!=&r||!active->viewer||q.viewerObjectName!=active->viewer->objectName()
  ||questionKind(q)!=active->kind||(active->kind!="activate"&&q.choiceOptions.reason!=active->reason))hard(s);
}
QJsonObject queryAIChoiceContinuity(Room&room,ServerPlayer*p,const AIRequest&r,const QJsonObject&q){
 if(q.value("key")=="olshebian"||q.value("key")=="olshebian_from"||q.value("key")=="olshebian_to")return queryAIShebian(room,p,r,q);
 if(q.value("op")=="pingjian_catalog")return queryAIPingjianCatalog(room,p,r,q);
 if(q.value("op")=="source_global")return writeAITenyearHCGlobal(room,p,r,q);
 if(q.value("op")=="source_priority")return copyAISourceUsePriority(room,p,r,q);
 if(q.value("op")=="source_priority_read")return readAIUsePriority(room,p,r,q);
 if(q.value("op")=="source_priority_increment")return incrementAISourceUsePriority(room,p,r,q);
 // Immediate named source effects have their own exact current-question gate.
 // They neither consume nor grant a retained continuity observation.
 if(q.value("op")=="source_prefix")return writeAIOverseasPrefix(room,p,r,q);
 auto*s=ledger(room);
 if(pendingAIXufaEvent(room))return unknown(); // No event-time read/write grant.
 if(!validateAIChoiceRegistryProxies(room))return unknown();
 if(!p||p->getRoom()!=&room||room.roomRuntime()->isClosing()||r.viewerObjectName!=p->objectName()
  ||r.worldView.self.objectName!=p->objectName()||r.stateRevision!=room.roomRuntime()->stateRevision()
  ||!active||active->room!=&room||active->viewer!=p||active->serial!=s->serial
  ||questionKind(r)!=active->kind||(active->kind!="activate"&&r.choiceOptions.reason!=active->reason)
  ||s->exhausted||!s->legacyReady||!s->isolatedReady)return unknown();
 const QString op=q.value("op").toString(),key=q.value("key").toString();if(!keys.contains(key))return unknown();
 if(op=="read"||op=="consume"){
  if(q.size()!=2||!admittedRead(r,key)||(op=="consume"&&key!="olshebian"))return unknown();
  auto out=read(s,p,key);
  if(key=="xufa"){
   if(out.value("present")==true&&!actualXufaGlobalMatches(room,out))return unknown();
   // The retained stamp was captured BEFORE the source write, never refreshed
   // by a read. A concurrent disconnect cannot be blessed by a later load.
   if(read(s,p,key).value("known")!=true)return unknown();
  }
  if(op=="consume"&&out.value("known")==true&&out.value("present")==true)
   s->rows[key]={p,{{"known",true},{"present",false}},true,true,s->epoch};
  return out;
 }
 if(op=="copy"){
  if(q.size()!=3||key!="olxuhe"||q.value("from")!="xuhe"||!admittedWrite(r,p,key))return unknown();
  auto value=read(s,p,"xuhe");if(value.value("known")!=true)return unknown();
  s->rows[key]={p,value,true,true,s->epoch};return{{"known",true},{"recorded",true}};
 }
 if(op!="record"||q.size()!=3||!q.value("row").isObject()||!admittedWrite(r,p,key))return unknown();
 const auto value=q.value("row").toObject();if(!validValue(room,r,p,key,value))return unknown();
 if(key=="xufa"){
  quint64 transportStamp=0,generationStamp=0;
  if(!writeActualXufaGlobal(room,p,r,s,value.value("value").toString(),transportStamp,generationStamp))return unknown();
  // Zero is an exhausted tombstone. Even the same owner/scalar after a clear
  // is a NEW reached write and must not revive an older native scope.
  if(!s->nextXufaWriteSerial)return unknown();
  const quint64 writeSerial=s->nextXufaWriteSerial++;
  if(!s->nextXufaWriteSerial){forget(s->rows[key]);return unknown();}
  s->rows[key]={p,value,true,false,s->epoch,transportStamp,writeSerial,generationStamp};
  if(read(s,p,key).value("known")!=true)return unknown();
 }else s->rows[key]={p,value,true,true,s->epoch};
 return{{"known",true},{"recorded",true}};
}

// ---- construction-context ----
#include "card.h"
#include "structs.h"

void projectAIConstructionContext(AIRequest &request, Room &room, const ServerPlayer *viewer,
    const std::function<QJsonObject(const Card *)> &describeCard)
{
    if (!viewer || viewer->getRoom() != &room
        || request.viewerObjectName != viewer->objectName()) return;
    const bool lingfa = request.kind == AIRequest::Discard
        && request.choiceOptions.reason == "MR_lingfa";
    const bool gongxin = request.kind == AIRequest::AmazingGrace
        && request.choiceOptions.reason == "MR_gongxin";
    if (!lingfa && !gongxin) return;
    QJsonObject context = request.choiceOptions.context.value("MRmou").toObject();
    const auto roster = room.getAllPlayers(true);
    if (lingfa) {
        // Fixed MR.lua1048-1050: the already public CardUse is installed only
        // around askForExchange(use.from, MR_lingfa). Never borrow other tags.
        const QVariant tag = room.getTag("MR_lingfa");
        if (!tag.isValid() || tag.isNull()) context.insert("lingfa_use", false);
        else if (tag.canConvert<CardUseStruct>()) {
            const CardUseStruct use = tag.value<CardUseStruct>();
            if (use.from == viewer && use.card && describeCard) {
                QJsonArray targets;
                bool valid = true;
                for (const ServerPlayer *target : use.to) {
                    if (!target || !roster.contains(const_cast<ServerPlayer *>(target))) {
                        valid = false;
                        break;
                    }
                    targets.append(target->objectName());
                }
                const QJsonObject card = describeCard(use.card);
                if (valid && !card.isEmpty()) context.insert("lingfa_use", QJsonObject{
                    {"card", card}, {"from", viewer->objectName()}, {"to", targets}});
            }
        }
        // Invalid/mismatched present data stays missing (unknown), not false.
    }
    if (gongxin) {
        // Source MRmou-ai.lua reads the OWNER tag MR_gongxin. Fixed MR.lua
        // calls doGongxin without its skill_name argument, hence normally asks
        // reason gongxin and writes the different owner tag gongxin. Do not
        // repair that incompatibility by reading the Room tag or aliasing it.
        const QVariant tag = viewer->getTag("MR_gongxin");
        const ServerPlayer *target = tag.value<ServerPlayer *>();
        if (!target) context.insert("gongxin_target", false);
        else if (roster.contains(const_cast<ServerPlayer *>(target)))
            context.insert("gongxin_target", target->objectName());
        QJsonObject printed;
        for (const AICardView &offered : request.choiceOptions.cards) {
            if (offered.effectiveId < 0) continue;
            const Card *card = Sanguosha->getEngineCard(offered.effectiveId);
            if (!card) continue;
            // Immutable public definition of this already disclosed offer;
            // no target hand enumeration, private flags, or conversion tags.
            printed.insert(QString::number(offered.effectiveId), QJsonObject{
                {"id", offered.effectiveId}, {"effective_id", offered.effectiveId},
                {"name", card->objectName()}, {"class_name", card->getClassName()},
                {"subtype", card->getSubtype()},
                {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
                {"suit", int(card->getSuit())}, {"number", card->getNumber()},
                {"type_id", int(card->getTypeId())}, {"red", card->isRed()},
                {"black", card->isBlack()},
                {"legacy_string_key", QString::number(offered.effectiveId)},
                {"legacy_sort_key", QString::number(offered.effectiveId)},
                {"virtual_card", false}, {"subcards", QJsonArray()}});
        }
        context.insert("gongxin_engine_cards", printed);
    }
    request.choiceOptions.context.insert("MRmou", context);
}

// ---- construction-query ----
#include "ai-strategy/runtime.h"
#include <QSet>

namespace {
struct Constructor {
    const char *source;
    const char *product;
    const char *printedSkill;
    AIRequest::DecisionKind kind;
    const char *question;
};
// Fixed b6025acc ai/feiDIY-ai.lua197/311/350/728 and meizl-ai.lua5513.
// Feilangduo deliberately prints feihujue; Feiyanjun prints feiyanjunCard.
// None of these source clones carries material, declaration or recast mutation.
const Constructor constructors[] = {
    {"feilianying", "fire_attack", "feilianying", AIRequest::PlayerChosen, "feilianyingCard"},
    {"feihujue", "duel", "feihujue", AIRequest::Activate, ""},
    {"feilangduo", "duel", "feihujue", AIRequest::Activate, ""},
    {"feiyanjun", "duel", "feiyanjunCard", AIRequest::Activate, ""},
    {"meizl_o_yuxiang", "savage_assault", "meizl_o_yuxiang", AIRequest::SkillInvoke, "meizl_o_yuxiang"}
};
}

bool aiAuditedConstructionQuery(const AIRequest &request, const ServerPlayer *viewer,
                               const QJsonObject &spec, QString *error)
{
    const auto reject = [&](const char *reason) {
        if (error) *error = QString::fromLatin1(reason);
        return false;
    };
    if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
        || !aiRangeRequestCurrent(*viewer->getRoom(),request,viewer))
        return reject("construction query request owner or revision mismatch");
    static const QSet<QString> fields{"source_key", "skill", "name", "suit", "number", "subcards"};
    if (spec.size() != fields.size()) return reject("construction query field set mismatch");
    for (auto it = spec.begin(); it != spec.end(); ++it)
        if (!fields.contains(it.key())) return reject("construction query has unaudited field");
    if (!spec.value("source_key").isString() || !spec.value("skill").isString()
        || !spec.value("name").isString() || !spec.value("suit").isDouble()
        || !spec.value("number").isDouble() || !spec.value("subcards").isArray())
        return reject("construction query field type mismatch");
    const QString key = spec.value("source_key").toString();
    // The named recipient need not own Mouxianchou (newgenerals.lua16176).
    // This is its exact askForCard policy probe, never a payable conversion.
    if (key == "mouxianchou") {
        const bool ok = request.kind == AIRequest::RespondCard && request.pattern == ".."
            && request.prompt == "@mouxianchou-slash"
            && spec.value("name").toString() == "slash"
            && spec.value("skill").toString() == "_mouxianchou"
            && spec.value("suit").toDouble() == int(Card::NoSuit)
            && spec.value("number").toDouble() == 0 && spec.value("subcards").toArray().isEmpty();
        if (!ok) return reject("Mouxianchou constructor differs from the named source question");
        if (error) error->clear();
        return true;
    }
    if (key == "keyaotaiping" || key == "kejieyaotaiping") {
        if (request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || (!viewer->hasSkill(key) && !viewer->hasLordSkill(key))
            || spec.value("skill").toString() != key
            || spec.value("suit").toDouble() != int(Card::NoSuit)
            || spec.value("number").toDouble() != 0 || spec.value("subcards").toArray().size() != 2)
            return reject("Taiping constructor scope or face differs from source");
        QList<int> ids;
        Card::Suit materialSuit = Card::NoSuit;
        for (const QJsonValue &cost : spec.value("subcards").toArray()) {
            const double n = cost.toDouble(-1);
            if (!cost.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < 0
                || n > std::numeric_limits<int>::max() || ids.contains(int(n)))
                return reject("Taiping requires two ordered distinct physical materials");
            const int id = int(n);
            bool disclosed = false;
            if (viewer->handCards().contains(id))
                for (const AICardView &card : request.worldView.handCards)
                    if (card.effectiveId == id) disclosed = true;
            if (viewer->getPile("wooden_ox").contains(id))
                for (const AICardPileView &pile : request.worldView.self.piles)
                    if (pile.name == "wooden_ox")
                        for (const AICardView &card : pile.cards)
                            if (card.effectiveId == id) disclosed = true;
            const Card *card = disclosed ? Sanguosha->getCard(id) : nullptr;
            if (!card || viewer->getRoom()->getCardOwner(id) != viewer
                || (!ids.isEmpty() && card->getSuit() != materialSuit))
                return reject("Taiping materials are not disclosed same-suit owner hand or wooden_ox");
            materialSuit = card->getSuit();
            ids.append(id);
        }
        const QString product = materialSuit == Card::Heart ? "archery_attack"
            : materialSuit == Card::Spade ? "savage_assault"
            : materialSuit == Card::Diamond ? "god_salvation"
            : materialSuit == Card::Club ? "amazing_grace" : QString();
        if (product.isEmpty() || product != spec.value("name").toString())
            return reject("Taiping product does not match the source material suit");
        if (error) error->clear();
        return true;
    }
    for (const auto &row : constructors) {
        if (key != QLatin1String(row.source)) continue;
        if (request.kind != row.kind
            || (row.kind == AIRequest::Activate && request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            || (row.kind != AIRequest::Activate && request.choiceOptions.reason != QLatin1String(row.question)))
            return reject("construction query is outside its fixed source question");
        if (!viewer->hasSkill(key) && !viewer->hasLordSkill(key))
            return reject("construction query source is not currently owned");
        if (spec.value("name").toString() != QLatin1String(row.product)
            || spec.value("skill").toString() != QLatin1String(row.printedSkill)
            || spec.value("suit").toDouble() != int(Card::NoSuit)
            || spec.value("number").toDouble() != 0
            || !spec.value("subcards").toArray().isEmpty())
            return reject("construction query differs from fixed source clone");
        if (error) error->clear();
        return true;
    }
    return reject("construction query source is outside audited finite catalog");
}

void projectAIConstructionFacts(AIWorldView &world)
{
    QJsonObject values = world.strategyFacts.value("MRmou").toObject();
    // MRmou AI tests Sanguosha:getSkill('olwansha'), not any player's ownership.
    values.insert("has_olwansha_definition", Sanguosha->getSkill("olwansha") != nullptr);
    world.strategyFacts.insert("MRmou", values);
}

// mountainrumour.lua1698-1851: the dynamic factory emits these prototypes
// only in Play with an empty pattern. Response products are different cards.
int aiAuditedConstructionSourcePrototype(const AIRequest &request, const ServerPlayer *viewer,
                                        const QString &skill, const QString &prototype)
{
    if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
        || !aiRangeRequestCurrent(*viewer->getRoom(),request,viewer)
        || (!viewer->hasSkill(skill) && !viewer->hasLordSkill(skill))) return -1;
    struct Action { const char *skill; const char *prototype; int flags; };
    // Exact fixed-source/provider pairs. These only select the prototype:
    // active scope, ordered costs, canSelect and createCard remain authoritative.
    static const Action actions[] = {
        {"MR_huishi", "#MR_huishi", 0}, // extensions/MR.lua:149
        {"MR_jilve", "#MR_jilve", 0}, // extensions/MR.lua:223
        {"MR_fanjian", "#MR_fanjian", 1}, // extensions/MR.lua:764
        {"MR_quhu", "#MR_quhu", 0}, // extensions/MR.lua:896
        {"MR_gongxin", "#MR_gongxin", 0}, // extensions/MR.lua:1223
        {"keyaoquwu", "#keyaoquwuCard", 0}, // extensions/keyaobao.lua:368
        {"kejieyaoquwu", "#kejieyaoquwuCard", 0}, // extensions/keyaobao.lua:1285
        {"keyaoshidu", "#keyaoshiduCard", 1}, // extensions/keyaobao.lua:606
        {"kejieyaoshidu", "#kejieyaoshiduCard", 1}, // extensions/keyaobao.lua:1546
        {"feilianying", "#feilianyingCard", 0}, // extensions/feiDIY.lua:91
        {"feiqianxun", "#feiqianxuncard", 0}, // extensions/feiDIY.lua:157
        {"feihujue", "#feihujueCard", 0}, // extensions/feiDIY.lua:498
        {"feilangduo", "#feilangduoCard", 0}, // extensions/feiDIY.lua:584
        {"feizuijiao", "#feizuijiaoCard", 0}, // extensions/feiDIY.lua:1060
        {"feilianhuan", "#feilianhuanCard", 0}, // extensions/feiDIY.lua:1354
        {"feisheji", "#feishejiCard", 1}, // extensions/feiDIY.lua:758
        {"feiyanjun", "#feiyanjunCard", 1}, // extensions/feiDIY.lua:1236
    };
    const bool play = request.kind == AIRequest::Activate
        && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.pattern.isEmpty();
    const bool response = request.kind == AIRequest::UseCard || request.kind == AIRequest::RespondCard;
    for (const auto &row : actions) {
        if (skill != QLatin1String(row.skill) || prototype != QLatin1String(row.prototype)) continue;
        if (skill == "feilianying") {
            return response && request.pattern == "@@feilianying"
                && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                    || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) ? row.flags : -1;
        }
        if (skill == "feisheji" && response && request.pattern == "slash"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return row.flags;
        return play ? row.flags : -1;
    }
    if (!play) return -1;
    for (int i = 1; i <= 10; ++i) {
        if (skill != "shisheng" + QString::number(i)) continue;
        if (prototype != "#shisheng_sc" + QString::number(i)
            || viewer->getMark("@wins") < i) return -1;
        return i == 10 ? 0 : 1;
    }
    return -1;
}

// Fixed b6025acc MRmou-ai.lua738-764 / MR.lua1389-1413. This is an
// admission predicate, never permission to skip native canSelect/createCard.
bool aiAuditedNonconsumingSelection(const AIRequest &request, const ServerPlayer *viewer,
    const QString &skill, const QJsonObject &product, const QJsonArray &selectedMaterials,
    QString *error)
{
    const auto reject = [&](const char *reason) {
        if (error) *error = QString::fromLatin1(reason);
        return false;
    };
    if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
        || !aiRangeRequestCurrent(*viewer->getRoom(),request,viewer)
        || request.kind != AIRequest::Activate
        || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || !request.pattern.isEmpty()
        || skill != "MR_zhizhe" || !viewer->hasSkill(skill))
        return reject("nonconsuming selection is outside the fixed owner Play source");
    static const QSet<QString> fields{"name", "skill", "suit", "number", "subcards"};
    if (product.size() != fields.size()) return reject("nonconsuming product field set mismatch");
    for (auto it = product.begin(); it != product.end(); ++it)
        if (!fields.contains(it.key())) return reject("nonconsuming product has unaudited field");
    if (!product.value("name").isString() || !product.value("skill").isString()
        || product.value("skill").toString() != skill
        || !product.value("suit").isDouble() || product.value("suit").toDouble() != int(Card::NoSuit)
        || !product.value("number").isDouble() || product.value("number").toDouble() != 0
        || !product.value("subcards").isArray() || !product.value("subcards").toArray().isEmpty()
        || selectedMaterials.size() != 1)
        return reject("nonconsuming selection requires one material and the exact zero-cost face");
    const QJsonValue value = selectedMaterials.first();
    const double n = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < 0
        || n > std::numeric_limits<int>::max())
        return reject("nonconsuming material must be a physical card ID");
    const int id = int(n);
    bool disclosed = false;
    for (const AICardView &card : request.worldView.handCards)
        if (card.effectiveId == id) disclosed = true;
    if (!disclosed || !viewer->handCards().contains(id)
        || viewer->getRoom()->getCardOwner(id) != viewer)
        return reject("nonconsuming material is not current disclosed owner hand");
    const Card *material = Sanguosha->getCard(id);
    if (!material || material->objectName() != product.value("name").toString()
        || (!material->isKindOf("BasicCard") && !material->isNDTrick())
        || material->isKindOf("Jink") || material->isKindOf("Nullification")
        || viewer->getMark(material->objectName() + "+MR_zhizhe-Clear") >= 1)
        return reject("nonconsuming material or product differs from the fixed provider");
    if (error) error->clear();
    return true;
}

// ---- history ----
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QMap>
#include <QSaveFile>
#include <mutex>

namespace {
constexpr qint64 MaximumBytes = 8 * 1024 * 1024;
constexpr qint64 MaximumArchiveBytes = 32 * 1024 * 1024;
constexpr int MaximumKeys = 4096;
const QStringList Statistics = {QStringLiteral("drawData"), QStringLiteral("convertData"),
    QStringLiteral("damageData"), QStringLiteral("throwData")};
const QStringList Mappings = {QStringLiteral("aiResponse"), QStringLiteral("card_damage_nature")};

bool fail(QString *error, const char *reason)
{
    if (error) *error = QString::fromLatin1(reason);
    return false;
}
QString digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
bool nameValid(const QString &name)
{
    return !name.isEmpty() && name.size() <= 256 && !name.contains(QChar::Null);
}
QString checkedDirectory(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.exists() || !info.isDir() || info.isSymLink()) {
        fail(error, "AI_HISTORY_INVALID_DIRECTORY"); return {};
    }
    const QString canonical = info.canonicalFilePath();
    // Refuse symlink traversal as well as a symlink final component. Configuration
    // can pass the resolved location explicitly instead.
    if (canonical.isEmpty() || QDir::cleanPath(info.absoluteFilePath()) != canonical) {
        fail(error, "AI_HISTORY_DIRECTORY_ALIAS"); return {};
    }
    return canonical;
}
bool readFile(const QString &path, QByteArray *bytes, bool *exists, QString *error,
              qint64 maximum = MaximumBytes)
{
    const QFileInfo info(path);
    *exists = info.exists();
    if (info.isSymLink() || (*exists && !info.isFile()))
        return fail(error, "AI_HISTORY_UNSAFE_FILE");
    if (!*exists) { bytes->clear(); return true; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, "AI_HISTORY_READ_FAILED");
    *bytes = file.read(maximum + 1);
    if (file.error() != QFileDevice::NoError) return fail(error, "AI_HISTORY_READ_FAILED");
    if (bytes->size() > maximum) return fail(error, "AI_HISTORY_TOO_LARGE");
    return true;
}

QJsonObject emptyData()
{
    QJsonObject result;
    for (const QString &key : Statistics + Mappings) result.insert(key, QJsonObject{});
    return result;
}

// There is deliberately no recursive/free-form JSON carrier in the format.
// allowlists == nullptr is only used to validate an on-disk partition or a
// quarantined legacy archive, never to authorize a policy write/projection.
bool normalizeData(const QJsonObject &source, QJsonObject *result, bool legacy,
                   const QSet<QString> *skills, const QSet<QString> *cards,
                   QString *error, bool allowEmptySampleObject = false)
{
    *result = emptyData();
    if (source.size() > Statistics.size() + Mappings.size())
        return fail(error, "AI_HISTORY_UNKNOWN_CATEGORY");
    for (auto category = source.constBegin(); category != source.constEnd(); ++category) {
        const bool statistic = Statistics.contains(category.key());
        if (!statistic && !Mappings.contains(category.key()))
            return fail(error, "AI_HISTORY_UNKNOWN_CATEGORY");
        if (!category.value().isObject()) return fail(error, "AI_HISTORY_INVALID_CATEGORY");
        const QJsonObject input = category.value().toObject();
        if (input.size() > MaximumKeys) return fail(error, "AI_HISTORY_TOO_MANY_KEYS");
        QJsonObject output;
        for (auto row = input.constBegin(); row != input.constEnd(); ++row) {
            if (!nameValid(row.key())) return fail(error, "AI_HISTORY_INVALID_KEY");
            if ((statistic && skills && !skills->contains(row.key()))
                || (!statistic && cards && !cards->contains(row.key())))
                return fail(error, "AI_HISTORY_UNDISCLOSED_KEY");
            if (statistic) {
                // The bounded Lua bridge cannot distinguish an empty array
                // from an empty object. This exception is only for policy
                // input at this exact schema position; disk format stays strict.
                if (!row.value().isArray() && !(allowEmptySampleObject
                    && row.value().isObject() && row.value().toObject().isEmpty()))
                    return fail(error, "AI_HISTORY_INVALID_SAMPLES");
                QJsonArray numbers;
                const QJsonArray samples = row.value().toArray();
                if (samples.size() > 99) return fail(error, "AI_HISTORY_TOO_MANY_SAMPLES");
                for (const QJsonValue &sample : samples) {
                    bool converted = false;
                    const double value = sample.isDouble() ? sample.toDouble()
                        : legacy && sample.isString() ? sample.toString().toDouble(&converted) : 0;
                    if ((!sample.isDouble() && !converted) || !std::isfinite(value))
                        return fail(error, "AI_HISTORY_NONNUMERIC_SAMPLE");
                    numbers.append(value);
                }
                output.insert(row.key(), numbers);
            } else if (category.key() == QLatin1String("aiResponse")) {
                if (!row.value().isString() || !nameValid(row.value().toString()))
                    return fail(error, "AI_HISTORY_INVALID_RESPONSE");
                if (cards && !cards->contains(row.value().toString()))
                    return fail(error, "AI_HISTORY_UNDISCLOSED_RESPONSE");
                output.insert(row.key(), row.value());
            } else {
                const double nature = row.value().toDouble(-1);
                // DamageStruct::Nature is a stable serialized enum: Normal,
                // Fire, Thunder, Ice, God, Poison. Never accept arbitrary JSON here.
                if (!row.value().isDouble() || nature != std::floor(nature) || nature < 0 || nature > 5)
                    return fail(error, "AI_HISTORY_INVALID_NATURE");
                output.insert(row.key(), row.value());
            }
        }
        result->insert(category.key(), output);
    }
    return true;
}

QJsonObject projected(const QJsonObject &source, const QSet<QString> &skills,
                      const QSet<QString> &cards)
{
    QJsonObject result = emptyData();
    for (const QString &category : Statistics + Mappings) {
        QJsonObject rows;
        const QJsonObject input = source.value(category).toObject();
        const QSet<QString> &allowed = Statistics.contains(category) ? skills : cards;
        for (auto row = input.constBegin(); row != input.constEnd(); ++row) {
            if (!allowed.contains(row.key())) continue;
            if (category == QLatin1String("aiResponse") && !cards.contains(row.value().toString())) continue;
            rows.insert(row.key(), row.value());
        }
        result.insert(category, rows);
    }
    return result;
}

struct Loaded {
    QJsonObject data = emptyData();
    QString revision = QStringLiteral("absent");
    quint64 generation = 0;
};
bool load(const QString &path, const QString &scope, Loaded *loaded, QString *error)
{
    QByteArray bytes; bool exists;
    if (!readFile(path, &bytes, &exists, error)) return false;
    if (!exists) return true;
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail(error, "AI_HISTORY_CORRUPT_JSON");
    const QJsonObject envelope = doc.object();
    if (envelope.size() != 5 || envelope.value("format") != QLatin1String("qsanguosha.isolated-ai-history")
        || envelope.value("version") != 1 || envelope.value("scope") != scope
        || !envelope.value("generation").isString() || !envelope.value("data").isObject())
        return fail(error, "AI_HISTORY_INVALID_ENVELOPE");
    bool ok = false;
    loaded->generation = envelope.value("generation").toString().toULongLong(&ok);
    if (!ok || loaded->generation == 0 || QString::number(loaded->generation) != envelope.value("generation").toString())
        return fail(error, "AI_HISTORY_INVALID_GENERATION");
    if (!normalizeData(envelope.value("data").toObject(), &loaded->data, false, nullptr, nullptr, error)) return false;
    loaded->revision = digest(bytes);
    return true;
}

std::shared_ptr<std::mutex> mutexFor(const QString &path)
{
    static QMutex mapMutex;
    static QHash<QString, std::weak_ptr<std::mutex>> mutexes;
    QMutexLocker locker(&mapMutex);
    // Do not accumulate one permanent entry for every retired Room identity.
    for (auto it = mutexes.begin(); it != mutexes.end();) {
        if (it.value().expired()) it = mutexes.erase(it); else ++it;
    }
    auto result = mutexes.value(path).lock();
    if (!result) { result = std::make_shared<std::mutex>(); mutexes.insert(path, result); }
    return result;
}
}

struct AIStrategyHistoryWrite::Impl {
    std::shared_ptr<std::mutex> mutex;
    std::unique_lock<std::mutex> threadLock;
    std::unique_ptr<QLockFile> processLock;
    std::unique_ptr<QSaveFile> file;
    QString revision;
    bool committed = false;

    bool lock(const QString &path, QString *error)
    {
        mutex = mutexFor(path);
        threadLock = std::unique_lock<std::mutex>(*mutex, std::try_to_lock);
        if (!threadLock.owns_lock()) return fail(error, "AI_HISTORY_BUSY");
        const QFileInfo lockInfo(path + QStringLiteral(".lock"));
        if (lockInfo.isSymLink()) return fail(error, "AI_HISTORY_UNSAFE_LOCK");
        processLock = std::make_unique<QLockFile>(lockInfo.absoluteFilePath());
        processLock->setStaleLockTime(30000);
        if (!processLock->tryLock(0)) return fail(error, "AI_HISTORY_BUSY");
        return true;
    }
    bool stage(const QString &path, const QByteArray &bytes, QString *error,
               qint64 maximum = MaximumBytes)
    {
        if (bytes.size() > maximum) return fail(error, "AI_HISTORY_TOO_LARGE");
        const QFileInfo info(path);
        if (info.isSymLink() || (info.exists() && !info.isFile()))
            return fail(error, "AI_HISTORY_UNSAFE_FILE");
        file = std::make_unique<QSaveFile>(path);
        file->setDirectWriteFallback(false);
        if (!file->open(QIODevice::WriteOnly)
            || !file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || file->write(bytes) != bytes.size()) return fail(error, "AI_HISTORY_STAGE_FAILED");
        revision = digest(bytes);
        return true;
    }
};

AIStrategyHistoryWrite::AIStrategyHistoryWrite(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
AIStrategyHistoryWrite::~AIStrategyHistoryWrite() = default;
QString AIStrategyHistoryWrite::revision() const { return m_impl->revision; }
bool AIStrategyHistoryWrite::commit(QString *error)
{
    if (error) error->clear();
    if (m_impl->committed) return fail(error, "AI_HISTORY_ALREADY_COMMITTED");
    if (!m_impl->file || !m_impl->file->commit()) return fail(error, "AI_HISTORY_COMMIT_FAILED");
    m_impl->committed = true;
    m_impl->file.reset();
    m_impl->processLock.reset();
    if (m_impl->threadLock.owns_lock()) m_impl->threadLock.unlock();
    return true;
}

AIStrategyHistoryStore::AIStrategyHistoryStore(const QString &rootDirectory, const QString &trustedScope)
{
    m_root = checkedDirectory(rootDirectory, &m_error);
    if (!nameValid(trustedScope)) { fail(&m_error, "AI_HISTORY_INVALID_SCOPE"); return; }
    m_scopeHash = digest(QByteArrayLiteral("qsanguosha-isolated-history-v1:") + trustedScope.toUtf8());
}
AIStrategyHistorySnapshot AIStrategyHistoryStore::read(const QSet<QString> &skills, const QSet<QString> &cards) const
{
    AIStrategyHistorySnapshot result;
    if (!m_error.isEmpty()) { result.error = m_error; return result; }
    Loaded loaded;
    if (!load(QDir(m_root).filePath(m_scopeHash + QStringLiteral(".json")), m_scopeHash, &loaded, &result.error)) return result;
    result.known = true;
    result.data = projected(loaded.data, skills, cards);
    result.revision = loaded.revision;
    return result;
}
std::unique_ptr<AIStrategyHistoryWrite> AIStrategyHistoryStore::prepare(
    const QString &expectedRevision, const QJsonObject &replacement,
    const QSet<QString> &skills, const QSet<QString> &cards, QString *error) const
{
    if (error) error->clear();
    if (!m_error.isEmpty()) { if (error) *error = m_error; return {}; }
    QJsonObject valid;
    if (!normalizeData(replacement, &valid, false, &skills, &cards, error, true)) return {};
    const QString path = QDir(m_root).filePath(m_scopeHash + QStringLiteral(".json"));
    auto state = std::make_unique<AIStrategyHistoryWrite::Impl>();
    if (!state->lock(path, error)) return {};
    Loaded loaded;
    if (!load(path, m_scopeHash, &loaded, error)) return {};
    if (loaded.revision != expectedRevision) { fail(error, "AI_HISTORY_STALE"); return {}; }
    if (loaded.generation == std::numeric_limits<quint64>::max()) { fail(error, "AI_HISTORY_GENERATION_OVERFLOW"); return {}; }
    QJsonObject merged = loaded.data;
    for (const QString &category : Statistics + Mappings) {
        QJsonObject rows = merged.value(category).toObject();
        // Only erase keys the viewer actually received, including both sides
        // of aiResponse. Hidden older entries remain opaque and recoverable.
        const QJsonObject visible = projected(loaded.data, skills, cards).value(category).toObject();
        for (auto row = visible.constBegin(); row != visible.constEnd(); ++row) rows.remove(row.key());
        const QJsonObject input = valid.value(category).toObject();
        for (auto row = input.constBegin(); row != input.constEnd(); ++row) rows.insert(row.key(), row.value());
        merged.insert(category, rows);
    }
    QJsonObject revalidated;
    if (!normalizeData(merged, &revalidated, false, nullptr, nullptr, error)) return {};
    const QJsonObject envelope{{"format", "qsanguosha.isolated-ai-history"}, {"version", 1},
        {"scope", m_scopeHash}, {"generation", QString::number(loaded.generation + 1)}, {"data", revalidated}};
    if (!state->stage(path, QJsonDocument(envelope).toJson(QJsonDocument::Compact), error)) return {};
    return std::unique_ptr<AIStrategyHistoryWrite>(new AIStrategyHistoryWrite(std::move(state)));
}

namespace {
struct LegacyFiles {
    QString root;
    QMap<QString, QByteArray> files;
    QString error;
};
LegacyFiles legacyFiles(const QString &directory, const QString &modernAiDataPath)
{
    LegacyFiles result;
    result.root = checkedDirectory(directory, &result.error);
    if (result.root.isEmpty()) return result;
    qint64 total = 0;
    for (const QString &name : QStringList{QStringLiteral("AiData")} + Statistics) {
        QByteArray bytes; bool exists;
        QString path = QDir(result.root).filePath(name);
        if (name == QLatin1String("AiData") && !modernAiDataPath.isEmpty()) {
            const QFileInfo modern(modernAiDataPath);
            if (!modern.isAbsolute() || modern.fileName() != QLatin1String("AiData")
                || checkedDirectory(modern.absolutePath(), &result.error).isEmpty()) {
                if (result.error.isEmpty()) fail(&result.error, "AI_HISTORY_INVALID_LEGACY_PATH");
                return result;
            }
            path = modern.absoluteFilePath();
        }
        if (!readFile(path, &bytes, &exists, &result.error)) return result;
        if (!exists) continue;
        total += bytes.size();
        // Bound total input independently of the recovery envelope, which also
        // holds base64 originals and parsed samples. Never truncate input.
        if (total > MaximumBytes) { fail(&result.error, "AI_HISTORY_LEGACY_TOO_LARGE"); return result; }
        result.files.insert(name, bytes);
    }
    return result;
}
AIStrategyLegacyHistory inspectLegacy(const LegacyFiles &files)
{
    AIStrategyLegacyHistory result;
    result.sourceFiles = files.files.keys();
    if (!files.error.isEmpty()) { result.error = files.error; return result; }
    QJsonObject raw;
    const QByteArray json = files.files.value(QStringLiteral("AiData"));
    bool useOldFiles = json.isEmpty();
    if (!json.isEmpty()) {
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(json, &parse);
        if (parse.error != QJsonParseError::NoError || doc.isNull()) {
            result.warnings.append(QStringLiteral("AI_HISTORY_LEGACY_CORRUPT_JSON"));
            useOldFiles = true; // Engine::getAiData exposes empty string here.
        } else if (!doc.isObject()) {
            fail(&result.error, "AI_HISTORY_LEGACY_INVALID_OBJECT"); return result;
        } else {
            raw = doc.object();
        }
    }
    if (useOldFiles) {
        for (const QString &category : Statistics) {
            QJsonObject rows;
            const QByteArray bytes = files.files.value(category);
            const QString text = QString::fromUtf8(bytes);
            if (text.toUtf8() != bytes) { fail(&result.error, "AI_HISTORY_LEGACY_ENCODING"); return result; }
            for (const QString &line : text.split('\n')) {
                if (line.isEmpty()) continue;
                const QStringList columns = line.split(':');
                if (columns.size() < 2) continue; // donor ignores lines without colon
                QJsonArray samples;
                for (const QString &sample : columns.at(1).split(','))
                    if (!sample.isEmpty()) samples.append(sample);
                rows.insert(columns.at(0), samples); // donor last definition wins
            }
            raw.insert(category, rows);
        }
    }
    result.valid = normalizeData(raw, &result.data, true, nullptr, nullptr, &result.error);
    return result;
}
}
AIStrategyLegacyHistory inspectAIStrategyLegacyHistory(const QString &legacyDirectory,
                                                      const QString &modernAiDataPath)
{
    return inspectLegacy(legacyFiles(legacyDirectory, modernAiDataPath));
}
std::unique_ptr<AIStrategyHistoryWrite> prepareAIStrategyLegacyArchive(
    const QString &archiveDirectory, const QString &legacyDirectory, QString *error,
    const QString &modernAiDataPath)
{
    if (error) error->clear();
    const QString root = checkedDirectory(archiveDirectory, error);
    if (root.isEmpty()) return {};
    const LegacyFiles files = legacyFiles(legacyDirectory, modernAiDataPath);
    if (!files.error.isEmpty()) { if (error) *error = files.error; return {}; }
    if (files.files.isEmpty()) { fail(error, "AI_HISTORY_NO_LEGACY_FILES"); return {}; }
    const AIStrategyLegacyHistory parsed = inspectLegacy(files);
    QJsonObject originals;
    for (auto it = files.files.constBegin(); it != files.files.constEnd(); ++it)
        originals.insert(it.key(), QJsonObject{{"encoding", "base64"},
            {"bytes", QString::fromLatin1(it.value().toBase64())}, {"sha256", digest(it.value())}});
    const QJsonObject archive{{"format", "qsanguosha.isolated-ai-history-quarantine"}, {"version", 1},
        {"originals", originals}, {"parsed_valid", parsed.valid}, {"parsed", parsed.data},
        {"parse_error", parsed.error}, {"warnings", QJsonArray::fromStringList(parsed.warnings)},
        {"provenance", "legacy-unpartitioned-unattributed"}};
    const QByteArray bytes = QJsonDocument(archive).toJson(QJsonDocument::Compact);
    const QString path = QDir(root).filePath(QStringLiteral("legacy-") + digest(bytes) + QStringLiteral(".json"));
    auto state = std::make_unique<AIStrategyHistoryWrite::Impl>();
    if (!state->lock(path, error)) return {};
    QByteArray existing; bool exists;
    if (!readFile(path, &existing, &exists, error, MaximumArchiveBytes)) return {};
    if (exists && existing != bytes) { fail(error, "AI_HISTORY_ARCHIVE_CONFLICT"); return {}; }
    if (!state->stage(path, bytes, error, MaximumArchiveBytes)) return {};
    return std::unique_ptr<AIStrategyHistoryWrite>(new AIStrategyHistoryWrite(std::move(state)));
}

// ---- history-context ----
#include "settings.h"
#include "runtime-paths.h"

namespace {
class HistoryKnowledge final : public QObject {
public:
    explicit HistoryKnowledge(Room &room) : QObject(&room) { setObjectName("isolated-strategy-history-knowledge"); }
    QHash<QString, QSet<QString>> skills;
};
HistoryKnowledge &knowledge(Room &room) {
    for (QObject *child : room.children())
        if (auto *found = dynamic_cast<HistoryKnowledge *>(child)) return *found;
    return *new HistoryKnowledge(room);
}
QString profile(const AIWorldView &world)
{
    // Seat names alone never imply continuity. An administrator must explicitly
    // map this Room's seat to a stable profile before cross-Room writes exist.
    return Config.value("AiHistoryProfiles").toMap().value(world.self.objectName).toString();
}
QString root()
{
    return QSanRuntimePaths::userDataPath(QStringLiteral("lua/ai/data/isolated"));
}
QSet<QString> skills(const AIWorldView &world, Room &room)
{
    QSet<QString> &result = knowledge(room).skills[world.self.objectName];
    const auto add = [&](const AIPlayerView &player) {
        for (const AISkillView &skill : player.skills)
            if (Sanguosha->getSkill(skill.skillName)) result.insert(skill.skillName);
    };
    add(world.self);
    for (const auto &player : world.players) add(player);
    return result;
}
QSet<QString> cardNames()
{
    QSet<QString> result;
    for (const QString &name : Sanguosha->getCardNames()) {
        result.insert(name);
        const std::unique_ptr<Card> card(Sanguosha->cloneCard(name));
        if (card) result.insert(card->getClassName());
    }
    return result;
}
}
void observeAIStrategyHistory(AIWorldView &world, Room &room, const AIEventView &event)
{
    auto &known = knowledge(room).skills[world.self.objectName];
    for (const QString &name : {event.reason, event.cardSkill,
        event.details.value("move").toObject().value("skill").toString()})
        if (!name.isEmpty() && Sanguosha->getSkill(name)) known.insert(name);
    projectAIStrategyHistory(world, room, room.findPlayerByObjectName(world.self.objectName, true));
}

void projectAIStrategyHistory(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    world.strategyFacts.remove("ai_data");
    world.strategyFacts.remove("ai_data_revision");
    world.strategyFacts.remove("ai_data_scope");
    QJsonArray allowed;
    for (const QString &name : skills(world, room)) allowed.append(name);
    world.strategyFacts.insert("ai_data_allowed_skills", allowed);
    world.strategyFacts.insert("ai_data_known", true);
    world.strategyFacts.insert("ai_data_persistent", false);
    if (!viewer || viewer->getRoom() != &room || viewer->objectName() != world.self.objectName) return;
    const QString identity = profile(world);
    if (identity.isEmpty()) return;
    for (const ServerPlayer *other : room.getAllPlayers(true)) {
        if (other != viewer && Config.value("AiHistoryProfiles").toMap().value(other->objectName()).toString() == identity) {
            world.strategyFacts.insert("ai_data_known", false);
            world.strategyFacts.insert("ai_data_error", "AI_HISTORY_PROFILE_COLLISION");
            return;
        }
    }
    if (!QDir().mkpath(root())) {
        world.strategyFacts.insert("ai_data_known", false);
        world.strategyFacts.insert("ai_data_error", "AI_HISTORY_ROOT_UNAVAILABLE");
        return;
    }
    AIStrategyHistoryStore store(root(), identity);
    const auto snapshot = store.read(skills(world, room), cardNames());
    world.strategyFacts.insert("ai_data_known", snapshot.known);
    if (!snapshot.known) {
        world.strategyFacts.insert("ai_data_error", snapshot.error);
        return;
    }
    world.strategyFacts.insert("ai_data_scope", store.scopeId());
    world.strategyFacts.insert("ai_data_persistent", true);
    world.strategyFacts.insert("ai_data_revision", snapshot.revision);
    world.strategyFacts.insert("ai_data", snapshot.data);
}
std::unique_ptr<AIStrategyHistoryWrite> prepareAIStrategyHistory(
    const AIWorldView &world, Room &room, const QString &revision, const QJsonObject &data, QString *error)
{
    ServerPlayer *viewer = room.findPlayerByObjectName(world.self.objectName, true);
    const QString identity = profile(world);
    if (!viewer || viewer->getRoom() != &room || identity.isEmpty()
        || !world.strategyFacts.value("ai_data_persistent").toBool()
        || world.strategyFacts.value("ai_data_revision").toString() != revision) {
        if (error) *error = "AI_HISTORY_UNAUTHORIZED";
        return nullptr;
    }
    AIStrategyHistoryStore store(root(), identity);
    if (world.strategyFacts.value("ai_data_scope").toString() != store.scopeId()) {
        if (error) *error = "AI_HISTORY_SCOPE_CHANGED";
        return nullptr;
    }
    for (const ServerPlayer *other : room.getAllPlayers(true)) {
        if (other != viewer && Config.value("AiHistoryProfiles").toMap().value(other->objectName()).toString() == identity) {
            if (error) *error = "AI_HISTORY_PROFILE_COLLISION";
            return nullptr;
        }
    }
    return store.prepare(revision, data, skills(world, room), cardNames(), error);
}

// ---- judge-context ----
#include "qt-collection-utils.h"
#include "ai-strategy/original-hegemony.h"
#include "util.h"
QJsonObject projectAIStrategyJudgeContext(const AIRequest &request, Room &room,
    ServerPlayer *viewer, const JudgeStruct *judge,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || !judge || !judge->who) return {};
        const auto knownNoFilter = [&](const ServerPlayer *player) {
            const AIPlayerView *visible = nullptr;
            if (player->objectName() == request.worldView.self.objectName) visible = &request.worldView.self;
            else for (const AIPlayerView &candidate : request.worldView.players)
                if (candidate.objectName == player->objectName()) { visible = &candidate; break; }
            if (!visible || !visible->skillsComplete) return false;
            for (const AISkillView &skill : visible->skills) {
                // A visible skill's public class may be inspected; never enumerate
                // the foreign player's hidden live SkillInstances to infer absence.
                if (!skill.invalid && dynamic_cast<const FilterSkill *>(Sanguosha->getViewAsSkill(skill.skillName)))
                    return false;
            }
            return true;
        };
        // Owner filter queries use an audited detached wrapper/mapping scope.
        // Another player's private filters remain outside this viewer's authority.
        bool complete = judge->who == viewer || knownNoFilter(judge->who);
        int lightningHolders = 0;
        bool lightningReserveComplete = true;
        if (judge->reason != QStringLiteral("lightning")) {
            for (ServerPlayer *holder : room.getAllPlayers()) {
                if (!holder->containsTrick(QStringLiteral("lightning"))) continue;
                ++lightningHolders;
                if (!knownNoFilter(holder)) lightningReserveComplete = false;
            }
        }
        QList<int> owned = viewer->handCards();
        for (const Card *equip : viewer->getEquips())
            if (equip) owned << equip->getEffectiveId();
        // Response candidates already carry the authority's pattern/method gates.
        // Invocation/choice outcomes describe owned cards, not skill-cost legality.
        const QSet<int> ownedSet = qsanToSet(owned);
        const QList<int> candidates = request.kind == AIRequest::RespondCard
            ? request.choiceOptions.cardIds : owned;
        QJsonObject outcomes, filteredSuits, filteredNumbers, unfilteredOutcomes, filterRepairs;
        bool publicPattern = !judge->pattern.contains('$');
        for (const QString &branch : judge->pattern.split('#')) {
            const QStringList factors = branch.split('|');
            if (factors.size() >= 4 && factors.at(3) != ".") publicPattern = false;
        }
        QJsonObject lightningCandidates;
        for (int id : candidates) {
            if (!ownedSet.contains(id)) continue;
            const Card *card = Sanguosha->getCard(id);
            if (!card) {
                complete = false;
                lightningReserveComplete = false;
                break;
            }
            if (viewer->isCardLimited(card, request.handlingMethod)) continue;
            // Name/suit/number-only predicates depend solely on this disclosed
            // owned card. Do not run foreign availability or zone predicates.
            if (publicPattern) unfilteredOutcomes.insert(QString::number(id), judge->isGood(card));
            if (judge->who == viewer) {
                const QJsonObject filtered = projectAIOwnerFilteredCard(room, viewer, id, judge);
                const QJsonObject printed = filtered.value("card").toObject();
                if (filtered.contains("donor_error_repair"))
                    filterRepairs.insert(QString::number(id), filtered.value("donor_error_repair"));
                if (!filtered.value("complete").toBool() || printed.isEmpty()
                    || !filtered.value("judge_outcome").isBool()) complete = false;
                else {
                    outcomes.insert(QString::number(id), filtered.value("judge_outcome"));
                    filteredSuits.insert(QString::number(id), printed.value("suit"));
                    filteredNumbers.insert(QString::number(id), printed.value("number"));
                }
            } else if (complete) {
                outcomes.insert(QString::number(id), judge->isGood(card));
                filteredSuits.insert(QString::number(id), int(card->getSuit()));
                filteredNumbers.insert(QString::number(id), card->getNumber());
            }
            if (lightningHolders > 0 && lightningReserveComplete
                && card->getSuit() == Card::Spade && card->getNumber() >= 2
                && card->getNumber() <= 9)
                lightningCandidates.insert(QString::number(id), true);
        }
        if (!complete) outcomes = QJsonObject();
        if (!lightningReserveComplete) lightningCandidates = QJsonObject();
        // Lua first applies its retrial policy, then reserves one eligible card
        // from the end per holder. Reserving here would use the wrong candidate order.
        QJsonObject result{{"who", judge->who->objectName()},
            {"reason", judge->reason}, {"pattern", judge->pattern}, {"good", judge->isGood()},
            {"predicate_good", judge->good}, {"negative", judge->negative}, {"card", publicCard(judge->card)},
            {"outcome_by_id", outcomes}, {"outcomes_complete", complete},
            {"unfiltered_outcome_by_id", unfilteredOutcomes}, {"unfiltered_outcomes_complete", publicPattern},
            {"repair_by_id", filterRepairs},
            {"filtered_suit_by_id", filteredSuits}, {"filtered_number_by_id", filteredNumbers},
            {"filtered_complete", complete},
            {"lightning_candidate_ids", lightningCandidates},
            {"lightning_holder_count", lightningHolders},
            {"lightning_reserve_complete", lightningReserveComplete}};
        projectAIOriginalRetrialCandidates(request, room, viewer, judge, result, publicCard);
        return result;
}

// ---- response-context ----
#include "ai-strategy/native-registry.h"
#include "ai-strategy/anime.h"
#include "ai-strategy/diy.h"
#include "lua-wrapper.h"
#include <memory>

namespace {
using PublicCard = std::function<QJsonObject(const Card *)>;
QJsonValue name(const ServerPlayer *p) { return p ? QJsonValue(p->objectName()) : QJsonValue(QJsonValue::Null); }
QJsonObject useRow(const CardUseStruct &use, const PublicCard &card)
{
    QJsonArray tos;
    for (const ServerPlayer *p : use.to) if (p) tos.append(p->objectName());
    QJsonObject row{{"from", name(use.from)}, {"to", tos}, {"who", name(use.who)}};
    if (use.card) row.insert("card", card(use.card));
    if (use.whocard) row.insert("whocard", card(use.whocard));
    return row;
}
QJsonObject damageRow(const DamageStruct &d, const PublicCard &card)
{
    QJsonObject row{{"from", name(d.from)}, {"to", name(d.to)}, {"damage", d.damage},
        {"nature", int(d.nature)}, {"chain", d.chain}, {"transfer", d.transfer},
        {"by_user", d.by_user}, {"reason", d.reason}};
    if (d.card) row.insert("card", card(d.card));
    return row;
}
QJsonArray ids(const QVariant &value)
{
    QJsonArray result;
    // toIntList in the donor converts a QVariantList, preserving order/duplicates.
    for (const QVariant &id : value.toList()) result.append(id.toInt());
    return result;
}
bool question(const AIRequest &r, AIRequest::DecisionKind kind, const char *key)
{
    return r.kind == kind && r.choiceOptions.reason == QLatin1String(key);
}
bool response(const AIRequest &r, const char *pattern)
{
    return r.kind == AIRequest::UseCard && r.pattern == QLatin1String(pattern);
}
void insertTag(QJsonObject &tags, const char *key, const char *kind, const QVariant &v,
               const PublicCard &card, bool roomTag = false)
{
    QJsonObject row{{"known", true}, {"present", v.isValid()},
        {"owner", roomTag ? "room" : "owner"}, {"kind", kind}};
    const QString type = QLatin1String(kind);
    if (type == "CardUse") row.insert("value", useRow(v.value<CardUseStruct>(), card));
    else if (type == "Damage") row.insert("value", damageRow(v.value<DamageStruct>(), card));
    else if (type == "Player") row.insert("value", name(v.value<ServerPlayer *>()));
    else if (type == "IntList") row.insert("value", ids(v));
    else if (type == "String") row.insert("value", v.toString());
    tags.insert(QLatin1String(key), row);
}
}


namespace {
void projectResponseProperties(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    auto properties = [&](const char *package, std::initializer_list<const char *> keys) {
        QJsonObject facts = request.worldView.strategyFacts.value(package).toObject();
        QJsonObject values = facts.value("properties").toObject();
        for (const char *key : keys) values.insert(key, viewer->property(key).toString());
        facts.insert("properties", values); request.worldView.strategyFacts.insert(package, facts);
    };
    // Finite owner scalars, preserving QVariant::toString (including unset/false).
    // Writers: NyarzFirst.lua5642/7108, NyarzSecond.lua3477,
    // tenyear.cpp3825/6170; OL package scoped declaration properties.
    properties("NyarzFirst", {"dwzhizheshow", "jfjifengsuit"});
    properties("NyarzSecond", {"ny_second_juanxia_card"});
    properties("tenyear_xd", {"yitongSuit", "shimouPN"});
    properties("ol_qifu", {"allowed_guhuo_dialog_buttons", "mozhi", "olrende"});
    auto scalar = [&](const char *package, const char *key, const QJsonValue &value) {
        auto facts = request.worldView.strategyFacts.value(package).toObject();
        facts.insert(key, value); request.worldView.strategyFacts.insert(package, facts);
    };
    scalar("Loong", "rende_property", viewer->property("lxtx_rende").toString());
    scalar("rushB", "diy_zuguangu", viewer->property("diy_zuguangu").toString());
    scalar("sk", "diezhang_point", viewer->getTag("diezhang_point").toInt());
    scalar("NyarzFirst", "swap_pile", room.getTag("SwapPile").toInt());
    scalar("rushB", "draw_count", room.getDrawPile().size());
    scalar("NyarzSecond", "draw_count", room.getDrawPile().size());
    auto loong = request.worldView.strategyFacts.value("Loong").toObject();
    QJsonObject publicFlags;
    // Both literals are broadcast through setPlayerFlag in Loong.lua608/623/2011.
    for (const char *key : {"LxtxFeiLongDuoFeng", "xianzhenTarget"}) {
        QJsonObject flags;
        for (ServerPlayer *p : room.getAllPlayers(true)) {
            if (QString::fromLatin1(key) == "LxtxFeiLongDuoFeng") {
                const auto observed=readAIStrategyFeilongPublicFlag(room,viewer,p,QString::fromLatin1(key));
                if(observed.value("known").toBool()) flags.insert(p->objectName(), observed.value("value"));
            } else flags.insert(p->objectName(), p->hasFlag(key));
        }
        publicFlags.insert(key, flags);
    }
    loong.insert("public_flags", publicFlags); request.worldView.strategyFacts.insert("Loong", loong);
    // Modern OL Anxu uses a temporary private flag, but the exact recipient
    // is also explicitly named to this giver in the actual question prompt.
    if (question(request, AIRequest::Discard, "olanxu")) {
        const auto parts = request.prompt.split(":");
        if (parts.size() >= 3 && parts.first() == "@olanxu") {
            const QString recipient = parts.at(2);
            if (room.findPlayerByObjectName(recipient, true)) {
                QJsonObject flags;
                for (const ServerPlayer *p : room.getAllPlayers(true)) flags.insert(p->objectName(), p->objectName() == recipient);
                scalar("ol_qifu", "olanxu_target", flags);
            }
        }
    }
    // Exact fixed literals whose writes/removals are broadcast setPlayerFlag.
    // Source paths and every fixed writer are inventoried in Round19 evidence.
    auto publicFlagsFor = [&](const char *package, std::initializer_list<const char *> keys) {
        auto facts = request.worldView.strategyFacts.value(package).toObject();
        auto all = facts.value("public_flags").toObject();
        for (const char *key : keys) {
            QJsonObject values;
            for (const ServerPlayer *p : room.getAllPlayers(true)) values.insert(p->objectName(), p->hasFlag(key));
            all.insert(key, values);
        }
        facts.insert("public_flags", all); request.worldView.strategyFacts.insert(package, facts);
    };
    publicFlagsFor("ol_sp", {"xiongmang_target", "olqingshiTo"});
    publicFlagsFor("sk", {"sk_pudu_target", "kuangao_target", "lirang_Target"});
    publicFlagsFor("NyarzFirst", {"jitongxindraw", "jitongxinda", "spjinjintarget", "hflanggutarget", "drangjiejud", "dwqingshidrawall", "dwqingshiupcan", "dmzhanmenggood", "old_yifuta", "shejifrom", "shejito", "xrxiaoruitarget"});
    publicFlagsFor("NyarzSecond", {"jxjuxiangtarget", "basuran", "mjtongguantarget", "gsgushetarget", "ny_second_juanxia_target", "nyarz_wangxi_target", "nyarz_liuzhuan_target", "nyarz_pojun_target", "nyarz_juece_target"});
    auto ol = request.worldView.strategyFacts.value("ol_sp").toObject();
    auto rows = ol.value("properties").toObject();
    auto own = rows.value(viewer->objectName()).toObject();
    for (const char *key : {"xiongmang", "KouchaoCNS", "xianyingName", "quanyuBf"})
        own.insert(key, viewer->property(key).toString());
    rows.insert(viewer->objectName(), own);
    // Both Caiyi histories are broadcast by room.setPlayerProperty immediately
    // on each choice (tenyear2.cpp28870/28926); no hidden recipient fields.
    for (const ServerPlayer *p : room.getAllPlayers(true)) {
        auto row = rows.value(p->objectName()).toObject();
        for (const char *key : {"SkillDescriptionRecord_caiyi", "SkillDescriptionChoiceRecord1_caiyi"})
            row.insert(key, p->property(key).toString());
        rows.insert(p->objectName(), row);
    }
    ol.insert("properties", rows); request.worldView.strategyFacts.insert("ol_sp", ol);
}

void projectAdditionalResponseTags(AIRequest &request, Room &room, ServerPlayer *viewer,
                                  QJsonObject &context, const PublicCard &card)
{
    // Each row is tied to the actual writer's immediate question, not merely
    // possession of a skill. A tag pointer never confers foreign hand access.
    struct Row { const char *package; AIRequest::DecisionKind kind; const char *question;
                 const char *key; const char *type; bool roomTag; const char *pile; };
    const Row rows[] = {
        {"NyarzFirst", AIRequest::SkillInvoke, "Godjieying", "jieyingtarget", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::UseCard, "@@Godpoxi", "godpoxi", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::SkillInvoke, "exrenxin", "renxintarget", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::SkillInvoke, "exjueqing", "exjueqingta", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::SkillInvoke, "dxibin", "dxibinta", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::SkillInvoke, "spduorui", "duoruita", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::SkillInvoke, "Gposhi", "Gposhi", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::UseCard, "@@godgongxin", "godgongxin", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::UseCard, "@@spyuqi", "spyuqita", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::Choice, "spyinghun", "spyinghun", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::UseCard, "@@exchengxiang", "chengxiangcards", "IntList", false, "#exchengxiang"},
        {"NyarzFirst", AIRequest::UseCard, "@@spyuqi", "spyuqicc", "IntList", false, "#spyuqi"},
        {"NyarzFirst", AIRequest::UseCard, "@@spjiufa", "spjiufalist", "IntList", false, "#spjiufa"},
        {"NyarzFirst", AIRequest::UseCard, "@@drangjie", "drangjiecards", "IntList", false, "#drangjie"},
        {"NyarzFirst", AIRequest::UseCard, "@@jfjifeng", "jfjifengai", "IntList", false, "#jfjifeng"},
        {"NyarzFirst", AIRequest::Choice, "dwqingshi", "dwqingshiuseai", "CardUse", false, nullptr},
        {"NyarzSecond", AIRequest::Choice, "jxyangjie", "jxyangjieto", "Player", false, nullptr},
        {"NyarzSecond", AIRequest::Discard, "ny_qingwang_diy", "ny_qingwang_target", "Player", true, nullptr},
        {"NyarzSecond", AIRequest::SkillInvoke, "nyarz_diaodu", "nyarz_diaodu", "Player", true, nullptr},
        {"NyarzSecond", AIRequest::SkillInvoke, "nyarz_shangyu", "nyarz_shangyu", "Player", false, nullptr},
        {"NyarzSecond", AIRequest::PlayerChosen, "nyarz_shangyu", "nyarz_shangyu", "Player", false, nullptr},
        {"NyarzSecond", AIRequest::UseCard, "@@mjtongguan", "mjtongguancards", "IntList", false, "#mjtongguan"},
        {"NyarzSecond", AIRequest::UseCard, "@@nyarz_yingmou_mou", "nyarz_yingmou_mou_card_ids", "IntList", false, "#nyarz_yingmou_mou"},
        {"NyarzSecond", AIRequest::SkillInvoke, "nyarz_renwang", "nyarz_renwang", "Damage", true, nullptr},
        {"NyarzSecond", AIRequest::SkillInvoke, "nyarz_yijve", "nyarz_yijve", "Dying", true, nullptr},
        {"NyarzSecond", AIRequest::Discard, "nyarz_luanzheng_put", "nyarz_luanzheng_cards", "String", true, nullptr},
        {"NyarzSecond", AIRequest::Discard, "nyarz_luanzheng", "nyarz_luanzheng", "CardUse", true, nullptr},
        {"NyarzSecond", AIRequest::SkillInvoke, "nyarz_caixia", "nyarz_caixia", "Card", false, nullptr},
        {"tenyear_xd", AIRequest::UseCard, "@@qiaodui", "qiaodui_data", "CardUse", false, nullptr},
        {"tenyear_xd", AIRequest::PlayerChosen, "chiguo0", "chiguo_data", "CardUse", false, nullptr},
        {"tenyear_xd", AIRequest::PlayerChosen, "juce", "juce_data", "CardUse", false, nullptr},
        {"Loong", AIRequest::SkillInvoke, "lxtx_taoxi", "taoxi_carduse", "CardUse", false, nullptr},
        {"Loong", AIRequest::UseCard, "@@lxtx_shichou", "lxtx_shichou_data", "CardUse", false, nullptr},
        {"rushB", AIRequest::PlayersChosen, "diy_k_fenwei", "fenwei", "CardUse", false, nullptr},
        {"sk", AIRequest::PlayerChosen, "yaoming_second", "yaomingTarget", "Player", true, nullptr},
    };
    for (const auto &row : rows) {
        if (!(row.kind == AIRequest::UseCard ? response(request, row.question)
                                             : question(request, row.kind, row.question))) continue;
        const QVariant value = row.roomTag ? room.getTag(row.key) : viewer->getTag(row.key);
        const QString type(row.type), package(row.package);
        QJsonObject out = context.value(package).toObject();
        QJsonValue result;
        if (type == "Player") result = name(value.value<ServerPlayer *>());
        else if (type == "CardUse") result = useRow(value.value<CardUseStruct>(), card);
        else if (type == "Damage") result = damageRow(value.value<DamageStruct>(), card);
        else if (type == "Dying") {
            const auto dying = value.value<DyingStruct>();
            QJsonObject d{{"who", name(dying.who)}};
            if (dying.damage) d.insert("damage", damageRow(*dying.damage, card));
            result = d;
        } else if (type == "String") result = value.toString();
        else if (type == "Card") {
            const Card *c = value.value<const Card *>();
            result = c ? QJsonValue(card(c)) : QJsonValue(QJsonValue::Null);
        } else if (type == "IntList") {
            const auto list = ids(value);
            const auto piles = request.worldView.strategyFacts.value("notified_piles").toObject();
            const auto disclosed = piles.value(row.pile).toObject().value("card_ids").toArray();
            bool visible = true;
            for (const auto &id : list) if (!disclosed.contains(id)) visible = false;
            if (!visible) continue; // stale/unobserved owner tag cannot disclose hidden IDs
            result = list;
        }
        if (package == "NyarzFirst" || package == "NyarzSecond") {
            if (type == "Player") {
                auto tags = out.value("player_tags").toObject();
                tags.insert(row.key, QJsonObject{{"known", true}, {"present", !result.isNull()},
                    {"owner", row.roomTag ? QString("@room") : viewer->objectName()}, {"value", result}});
                out.insert("player_tags", tags);
            } else if (type == "IntList") {
                auto tags = out.value("id_tags").toObject(); tags.insert(row.key, result); out.insert("id_tags", tags);
            } else if (package == "NyarzSecond") {
                auto tags = out.value("values").toObject();
                tags.insert(row.key, QJsonObject{{"known", true}, {"present", value.isValid()}, {"kind", type}, {"value", result}});
                out.insert("values", tags);
            } else out.insert(row.key, result);
        } else if (package == "sk") { context.insert("sk_yaoming_target", result); context.insert("sk_yaoming_known", true); }
        else out.insert(row.key, result);
        context.insert(package, out);
    }
    // Reuse the exact normal judge producer, including owner filters and
    // honest foreign completeness. Each source writer immediately precedes its question.
    const bool firstJudge = question(request, AIRequest::SkillInvoke, "spjilve")
        || question(request, AIRequest::Choice, "spjilve");
    const bool secondJudge = question(request, AIRequest::Discard, "nyarz_huanshi")
        || question(request, AIRequest::AmazingGrace, "nyarz_huanshi");
    if (firstJudge || secondJudge || response(request, "@@midao")) {
        const QVariant raw = firstJudge ? viewer->getTag("spjilvejudge")
            : secondJudge ? room.getTag("nyarz_huanshi") : viewer->getTag("judgeData");
        const auto *judgePointer = raw.value<JudgeStruct *>();
        const JudgeStruct judge = judgePointer ? *judgePointer : raw.value<JudgeStruct>();
        QJsonObject row = projectAIStrategyJudgeContext(request, room, viewer, &judge, card);
        if (row.isEmpty()) row = QJsonObject{{"reason", judge.reason}, {"pattern", judge.pattern},
            {"who", name(judge.who)}, {"predicate_good", judge.good}, {"outcomes_complete", false}};
        if (firstJudge) {
            auto out = context.value("NyarzFirst").toObject(); out.insert("spjilvejudge", row); context.insert("NyarzFirst", out);
        } else if (secondJudge) {
            auto out = context.value("NyarzSecond").toObject(); auto values = out.value("values").toObject();
            values.insert("nyarz_huanshi", QJsonObject{{"known", true}, {"present", raw.isValid()}, {"kind", "Judge"}, {"value", row}});
            out.insert("values", values);
            // Native public-expression guard controls availability of this map.
            const auto outcomes = row.value("unfiltered_outcome_by_id").toObject();
            if (row.value("predicate_good").isBool() && !outcomes.isEmpty()) {
                QJsonObject matches;
                for (auto it = outcomes.begin(); it != outcomes.end(); ++it)
                    if (it.value().isBool()) matches.insert(it.key(), it.value().toBool() == row.value("predicate_good").toBool());
                out.insert("judge_nil_matches", QJsonObject{{judge.pattern, matches}});
            }
            context.insert("NyarzSecond", out);
        } else context.insert("judge", row);
    }
    if (question(request, AIRequest::Discard, "olqingjian")) {
        auto facts = request.worldView.strategyFacts.value("ol_qifu").toObject();
        auto tags = facts.value("owner_tag_ids").toObject();
        tags.insert("olqingjian", ids(viewer->getTag("olqingjian")));
        facts.insert("owner_tag_ids", tags); request.worldView.strategyFacts.insert("ol_qifu", facts);
    }
    if (request.kind == AIRequest::RespondCard && request.prompt.section(':', 0, 0) == "@rushB_wushuang-jink") {
        // This donor reads the prior public SlashData offset, not current use;
        // only offset_num is consumed. Do not substitute the current CardUse.
        const auto effect = room.getTag("SlashData").value<CardEffectStruct>();
        auto out = context.value("rushB").toObject();
        out.insert("SlashData", QJsonObject{{"offset_num", effect.offset_num}}); context.insert("rushB", out);
    }
    // Owner bookkeeping used by a turn-use revise, not a foreign observation.
    auto second = context.value("NyarzSecond").toObject();
    auto values = second.value("values").toObject();
    values.insert("JilveWansha", QJsonObject{{"known", true}, {"present", true}, {"kind", "Bool"},
        {"value", viewer->getTag("JilveWansha").toBool()}});
    second.insert("values", values); context.insert("NyarzSecond", second);
}
}

void projectAIStrategyResponseContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context, const PublicCard &publicCard)
{
    if (!viewer || viewer->getRoom() != &room || !publicCard
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()) return;

    projectAIHTMSProperties(request, room, viewer);
    projectResponseProperties(request, room, viewer);
    projectAdditionalResponseTags(request, room, viewer, context, publicCard);

    QJsonObject ol = context.value("ol_sp").toObject();
    QJsonObject tags = ol.value("tags").toObject();
    // These tags are written by each trigger immediately before the named
    // question. Only the current viewer's tag is read, never another AI's tag.
    struct OwnerTag { AIRequest::DecisionKind kind; const char *reason; const char *key; const char *type; };
    const OwnerTag ownerTags[] = {
        {AIRequest::SkillInvoke, "jinjian", "JinjianDamage", "Damage"},
        {AIRequest::PlayersChosen, "hunjiang", "hunjiangUse", "CardUse"},
        {AIRequest::PlayersChosen, "jingzhou", "jingzhouDamage", "Damage"},
        {AIRequest::PlayersChosen, "pijing", "pijingUse", "CardUse"},
        {AIRequest::AmazingGrace, "changxin", "changxinDamage", "Damage"},
        {AIRequest::PlayerChosen, "xiaoshi", "xiaoshiUse", "CardUse"},
        {AIRequest::SkillInvoke, "dongxin", "dongxinData", "Damage"},
        // The effective donor sibing discard callback is the later dongxinData
        // body (ol_sp-ai.lua); do not revive the overwritten sibingData body.
        {AIRequest::Discard, "sibing", "dongxinData", "Damage"},
        {AIRequest::PlayerChosen, "quanyu2", "quanyu2_data", "CardUse"},
    };
    for (const auto &row : ownerTags)
        if (question(request, row.kind, row.reason))
            insertTag(tags, row.key, row.type, viewer->getTag(row.key), publicCard);
    if (response(request, "@@zhishi"))
        insertTag(tags, "ZhishiTarget", "Player", viewer->getTag("ZhishiTarget"), publicCard);
    if (response(request, "@@olqingshi"))
        insertTag(tags, "olqingshiUse", "CardUse", viewer->getTag("olqingshiUse"), publicCard);
    // Fixed native PanqinRecord stores QVariantList<int>. Preserve the actual
    // toIntList conversion, order and duplicates; unset QVariant yields {}.
    if (question(request, AIRequest::SkillInvoke, "panqin"))
        insertTag(tags, "PanqinRecord", "IntList", viewer->getTag("PanqinRecord"), publicCard);
    if (question(request, AIRequest::SkillInvoke, "SkillEffect")
        && (data.toString() == "s29" || data.toString() == "s30"))
        insertTag(tags, "SkillEffect", "Damage", viewer->getTag("SkillEffect"), publicCard);
    // The generated skill's description is explicitly delivered to this owner
    // before acquisition (newgenerals.lua26375-26379). No arbitrary tag keys.
    if (request.kind == AIRequest::PlayerChosen || request.kind == AIRequest::PlayersChosen
        || request.kind == AIRequest::Discard) {
        for (int i = 0; i < 100; ++i) {
            const QString skill = "qingshu_tianshu" + QString::number(i);
            if (request.choiceOptions.reason != skill || !viewer->hasSkill(skill, true)) continue;
            const QByteArray key = (skill + "SkillEffect").toUtf8();
            insertTag(tags, key.constData(), "String", room.getTag(QString::fromUtf8(key)), publicCard, true);
        }
    }
    if (!tags.isEmpty()) ol.insert("tags", tags);
    if (response(request, "@@guowu2")) {
        QJsonObject flags;
        // Native tenyear2.cpp:9047 discloses the eligible target set through
        // public player flags before this exact askForUseCard call.
        for (const ServerPlayer *p : room.getAllPlayers(true))
            flags.insert(p->objectName(), p->hasFlag("guowu_canchoose"));
        ol.insert("guowu_canchoose", flags);
    }
    if (question(request, AIRequest::Choice, "fuchao")) {
        const Card *reference = nullptr;
        QJsonObject f{{"known", false}};
        // Only the actual question's reference is queried. No caller-supplied
        // wire, prior question cache or arbitrary native card lookup is used.
        if (data.canConvert<CardUseStruct>()) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card) { reference = use.whocard; f.insert("mode", "use"); f.insert("input", useRow(use, publicCard)); }
        }
        if (!reference && data.canConvert<CardResponseStruct>()) {
            const CardResponseStruct resp = data.value<CardResponseStruct>();
            reference = resp.m_toCard;
            QJsonObject row{{"m_who", name(resp.m_who)}};
            if (resp.m_card) row.insert("m_card", publicCard(resp.m_card));
            if (reference) row.insert("m_toCard", publicCard(reference));
            f.insert("mode", "response"); f.insert("input", row);
        }
        if (reference) {
            f.insert("known", true);
            // Room::getUseStruct uses tag["UseHistory" + card->toString()].
            // getTag preserves its value/default semantics without inserting a
            // missing key during readonly projection. The key is never exposed.
            const QVariant history = room.getTag("UseHistory" + reference->toString());
            f.insert("use", useRow(history.value<CardUseStruct>(), publicCard));
        }
        ol.insert("fuchao", f);
    }
    if (!ol.isEmpty()) context.insert("ol_sp", ol);

    QJsonObject ny = context.value("nybeauty").toObject();
    QJsonObject known = ny.value("known_tags").toObject();
    QJsonObject kinds = ny.value("tag_kinds").toObject();
    auto nyTag = [&](const char *key, const char *kind, const QVariant &value) {
        known.insert(key, true); kinds.insert(key, kind);
        if (QLatin1String(kind) == "IntList") ny.insert(key, ids(value));
        else if (QLatin1String(kind) == "Player") ny.insert(key, name(value.value<ServerPlayer *>()));
        else if (QLatin1String(kind) == "CardUse") ny.insert(key, useRow(value.value<CardUseStruct>(), publicCard));
        else if (QLatin1String(kind) == "Damage") ny.insert(key, damageRow(value.value<DamageStruct>(), publicCard));
        else if (QLatin1String(kind) == "Card") {
            const Card *card = value.value<const Card *>();
            ny.insert(key, card ? QJsonValue(publicCard(card)) : QJsonValue(QJsonValue::Null));
        }
    };
    // Source nybeauty.lua sends the ID list to THIS viewer (private log and
    // notified pile) before @@nyyuqi; never expose it to another observer.
    if (response(request, "@@nyyuqi") && viewer->hasFlag("nyyuqiview"))
        nyTag("nyyuqiuse", "IntList", viewer->getTag("nyyuqiuse"));
    if (response(request, "@@nytongli")) nyTag("nytongliorigin", "Card", viewer->getTag("nytongliorigin"));
    if (question(request, AIRequest::PlayerChosen, "nyzhennan")) nyTag("nyzhennan_card", "Card", viewer->getTag("nyzhennan_card"));
    if (question(request, AIRequest::SkillInvoke, "ny_qiaoshi")) nyTag("ny_qiaoshi", "Damage", viewer->getTag("ny_qiaoshi"));
    if (response(request, "@@ny_qiaoshi")) nyTag("ny_qiaoshi_cards", "IntList", viewer->getTag("ny_qiaoshi_cards"));
    if (question(request, AIRequest::Choice, "nyarz_guangu") || response(request, "@@nyarz_guangu"))
        nyTag("nyarz_guangu", "Player", viewer->getTag("nyarz_guangu"));
    if (question(request, AIRequest::SkillInvoke, "nyarz_lingxi")) nyTag("nyarz_lingxi_use", "CardUse", viewer->getTag("nyarz_lingxi_use"));
    // nyarz_anzhi is an ID-only historical discard ledger, populated only by
    // public discard moves (nybeauty.lua:4286). No current face is re-read.
    if (response(request, "@@nyarz_anzhi")) nyTag("nyarz_anzhi", "IntList", room.getTag("nyarz_anzhi"));
    if (!known.isEmpty()) { ny.insert("known_tags", known); ny.insert("tag_kinds", kinds); context.insert("nybeauty", ny); }

    // The seven removed legacy Lua prototypes affect early policy declines.
    // Card_Parse only constructs a registered prototype; no policy is called.
    if (([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(request.kind==AIRequest::Activate&&request.reason==CardUseStruct::CARD_USE_REASON_PLAY&&!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkills("lualveji|luawu|luamouce|luahuntian|luafentian|luajice|luajiejiang"); }())) {
        QJsonObject facts = request.worldView.strategyFacts.value("new").toObject(), prototypes;
        for (const QString &selector : {"#lualveji", "#luawu", "#luamouce", "#luahuntian", "#luafentian", "#luajice", "#luajiejiang"}) {
            const std::unique_ptr<SkillCard> card(LuaSkillCard::Parse(selector + ":.:"));
            prototypes.insert(selector, bool(card));
        }
        facts.insert("legacy_prototypes", prototypes);
        request.worldView.strategyFacts.insert("new", facts);
    }
}

void projectAIStrategyResponseEventContext(AIWorldView &world, Room &room, ServerPlayer *observer,
    const AIEventView &event, const PublicCard &publicCard)
{
    Q_UNUSED(publicCard);
    if (!observer || observer->getRoom() != &room || world.self.objectName != observer->objectName()
        || event.triggerEvent != ChoiceMade || event.from != observer->objectName()
        || (!event.privateViewer.isEmpty() && event.privateViewer != observer->objectName())) return;
    const auto prompt = event.details.value("prompt_list").toArray();
    if (prompt.size() < 3 || prompt.first().toString() != "skillInvoke") return;
    const QString skill = prompt.at(1).toString();
    QString package, key; QVariant value; QString owner;
    if (skill == "spduorui") {
        package = "NyarzFirst"; key = "duoruita"; value = observer->getTag(key); owner = observer->objectName();
    } else if (skill == "nyarz_diaodu") {
        package = "NyarzSecond"; key = "nyarz_diaodu"; value = room.getTag(key); owner = "@room";
    } else return;
    const auto target = value.value<ServerPlayer *>();
    auto context = world.decisionContext.value(package).toObject();
    auto tags = context.value("player_tags").toObject();
    tags.insert(key, QJsonObject{{"known", true}, {"present", target != nullptr},
        {"owner", owner}, {"value", name(target)}});
    context.insert("player_tags", tags); world.decisionContext.insert(package, context);
}

// ---- response-observer ----
#include "server-info.h"

namespace {
QSet<QString> recipients(Room &room, const QList<ServerPlayer *> &specified = {})
{
    QSet<QString> result;
    for (const auto *p : specified.isEmpty() ? room.getAllPlayers(true) : specified)
        if (p && p->getRoom() == &room) result.insert(p->objectName());
    return result;
}
struct Frame {
    QString skill;
    QPointer<ServerPlayer> actor;
    int event = NonTrigger;
    QPointer<const Card> card;
    QStringList targets;
    QSet<QString> audience;
    QSet<QString> damageWindow;
    QSet<QString> completeAudience;
    QHash<QString, QSet<QString>> clearedTargets;
    bool eligible = false;
};
struct CardWitness {
    QString targets;
    bool damageFlag = true;
    QSet<QString> audience;
};
class Ledger final : public QObject {
public:
    explicit Ledger(Room &room) : QObject(&room) {}
    QList<Frame *> frames;
    QHash<const Card *, CardWitness> cards;
    QSet<const Card *> watched;
    QHash<QString, QHash<QString, bool>> shejian;
    QSet<QString> initialized;
};
Ledger *responseObserverLedger(Room &room, bool create)
{
    const QString key = QStringLiteral("isolated-ai-response-observer");
    auto *found = room.findChild<QObject *>(key, Qt::FindDirectChildrenOnly);
    if (!found && create) { found = new Ledger(room); found->setObjectName(key); }
    return static_cast<Ledger *>(found);
}
void watch(Ledger *l, const Card *card)
{
    if (l->watched.contains(card)) return;
    l->watched.insert(card);
    QObject::connect(card, &QObject::destroyed, l, [l, card]() {
        l->cards.remove(card); l->watched.remove(card);
    }, Qt::DirectConnection);
    if (const auto *wrapped = qobject_cast<const WrappedCard *>(card))
        QObject::connect(wrapped, &WrappedCard::identityChanged, l,
            [l, card]() { l->cards.remove(card); }, Qt::DirectConnection);
}
bool matches(const Card *card, const CardWitness &w)
{
    // Validate only an already witnessed value. A mismatch reveals no new value.
    return card && card->hasFlag("dwqingshidamage") == w.damageFlag
        && card->getTag("dwqingshitarget").toString() == w.targets;
}
}
struct AIResponseObservationScope::Data {
    Room &room;
    Ledger *store;
    Frame frame;
    bool committed = false;
    int effectCallDepth;
    quint64 failureGeneration;
    explicit Data(Room &r) : room(r), store(responseObserverLedger(r, true)),
        effectCallDepth(r.roomRuntime()->lua().protectedCallDepth() + 1),
        failureGeneration(r.roomRuntime()->lua().protectedCallFailureGenerationAtDepth(effectCallDepth)) {}
};
AIResponseObservationScope::AIResponseObservationScope(Room &room, const QString &skill,
    ServerPlayer *actor, int originalEvent, const SkillContext &context)
    : d(new Data(room))
{
    auto &f = d->frame;
    f.skill = skill; f.actor = actor; f.event = originalEvent;
    // Push even unrecognized effects: nested callbacks cannot donate mark
    // notifications to an enclosing audited effect.
    d->store->frames.append(&f);
    if (!actor || actor->getRoom() != &room || !context.original_data) return;
    f.audience = recipients(room);
    const QString publicSkill = skill == "#dwqingshibuff" ? QStringLiteral("dwqingshi") : skill;
    if (ServerInfo.EnableHegemony && !actor->hasShownSkill(publicSkill))
        f.audience = QSet<QString>{actor->objectName()};
    f.completeAudience = f.audience;
    if (skill == "dwqingshi" && originalEvent == CardUsed
        && context.original_data->canConvert<CardUseStruct>()) {
        const auto use = context.original_data->value<CardUseStruct>();
        if (!use.card || use.card->isKindOf("SkillCard") || use.from != actor) return;
        f.card = use.card;
        for (const auto *target : use.to) {
            if (!target || target->getRoom() != &room) return;
            f.targets.append(target->objectName());
        }
        f.eligible = true;
    } else if (skill == "#dwqingshibuff" && originalEvent == CardFinished
        && context.original_data->canConvert<CardUseStruct>()) {
        f.card = context.original_data->value<CardUseStruct>().card;
        f.eligible = f.card != nullptr;
    } else if (skill == "sk_shejian") {
        if (originalEvent == EventPhaseChanging && context.original_data->canConvert<PhaseChangeStruct>())
            f.eligible = context.original_data->value<PhaseChangeStruct>().to == Player::NotActive;
        else if (originalEvent == Death && context.original_data->canConvert<DeathStruct>())
            f.eligible = context.original_data->value<DeathStruct>().who == actor;
    }
}
AIResponseObservationScope::~AIResponseObservationScope()
{
    d->store->frames.removeAll(&d->frame);
}
void AIResponseObservationScope::commit()
{
    if (d->committed) return;
    d->committed = true;
    // These audited Lua on_effect wrappers enter exactly one direct pcall at
    // P+1. Their own swallowed error invalidates completion. A deeper helper
    // error handled by the successful outer callback does not invalidate its
    // witnessed public operations. This is true pcall depth, not lifetime-pin
    // invocationDepth (which also includes manually entered scopes).
    if (d->room.roomRuntime()->lua().protectedCallFailureGenerationAtDepth(d->effectCallDepth)
        != d->failureGeneration) return;
    const auto &f = d->frame;
    if (!f.eligible) return;
    if (f.skill == "sk_shejian") {
        // Only the completed audited cleanup body, never a mere phase event,
        // proves this private clear. Do not consult/serialize other flags.
        for (const auto *p : d->room.getPlayers()) {
            if (p->hasFlag("shejian_used")) continue;
            for (const QString &observer : f.audience)
                d->store->shejian[observer].insert(p->objectName(), false);
        }
        return;
    }
    const Card *card = f.card.data();
    if (!card) return;
    if (f.skill == "#dwqingshibuff") {
        auto it = d->store->cards.find(card);
        if (it == d->store->cards.end()) return;
        auto updated = it.value(); updated.targets.clear();
        updated.audience.intersect(f.audience);
        if (!matches(card, updated)) { d->store->cards.erase(it); return; }
        it.value() = updated;
        return;
    }
    // The source records names in the original use.to order, not target-mark
    // notification order. Repeated targets are consumed by the first clear.
    QHash<QString, QSet<QString>> groups;
    for (const QString &observer : f.damageWindow) {
        if (!f.completeAudience.contains(observer)) continue;
        QStringList names; QSet<QString> used;
        const auto cleared = f.clearedTargets.value(observer);
        for (const QString &target : f.targets)
            if (cleared.contains(target) && !used.contains(target)) {
                names.append(target); used.insert(target);
            }
        if (!names.isEmpty()) groups[names.join('+')].insert(observer);
    }
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        CardWitness candidate;
        candidate.targets = it.key(); candidate.audience = it.value();
        if (!matches(card, candidate)) continue;
        watch(d->store, card);
        d->store->cards.insert(card, candidate);
        break; // Only one full predicted value can match the same actual card.
    }
}
void recordAIResponseObservedMark(Room &room, ServerPlayer *target, const QString &mark,
    int value, const QList<ServerPlayer *> &audience)
{
    if (!target || target->getRoom() != &room || mark != "dwqingshitarget" || value != 0) return;
    auto *l = responseObserverLedger(room, false);
    if (!l || l->frames.isEmpty()) return;
    auto *f = l->frames.last();
    if (!f->eligible || f->skill != "dwqingshi" || !f->targets.contains(target->objectName())) return;
    const auto actual = recipients(room, audience);
    f->completeAudience.intersect(actual);
    for (const QString &observer : f->damageWindow)
        if (actual.contains(observer)) f->clearedTargets[observer].insert(target->objectName());
}
void recordAIResponseObservedPlayerFlag(Room &room, ServerPlayer *target,
    const QString &operation, const QList<ServerPlayer *> &audience)
{
    if (!target || target->getRoom() != &room) return;
    if (operation == "shejian_used" || operation == "-shejian_used" || operation == ".") {
        const bool value = operation == "shejian_used";
        if (target->hasFlag("shejian_used") != value) return;
        auto *l = responseObserverLedger(room, true);
        for (const QString &observer : recipients(room, audience))
            l->shejian[observer].insert(target->objectName(), value);
    }
    if (operation != "dwqingshidamage") return;
    auto *l = responseObserverLedger(room, false);
    if (!l || l->frames.isEmpty()) return;
    auto *f = l->frames.last();
    if (!f->eligible || f->skill != "dwqingshi" || f->actor != target) return;
    auto actual = recipients(room, audience); actual.intersect(f->audience);
    f->damageWindow.unite(actual);
}
void initializeAIResponseObservations(Room &room, ServerPlayer *observer, bool freshGame)
{
    if (!freshGame || room.isRestoringTakeoverSnapshot() || !observer || observer->getRoom() != &room) return;
    auto *l = responseObserverLedger(room, true);
    if (l->initialized.contains(observer->objectName())) return;
    l->initialized.insert(observer->objectName());
    // Fresh game construction is the witness for the literal's default absence.
    // A changed value before this observer's GameReady is not retroactively read.
    for (const auto *p : room.getAllPlayers(true))
        if (!p->hasFlag("shejian_used") && !l->shejian[observer->objectName()].contains(p->objectName()))
            l->shejian[observer->objectName()].insert(p->objectName(), false);
}
QJsonObject readAIResponseCardObservation(Room &room, ServerPlayer *observer, const Card *card)
{
    if (!observer || observer->getRoom() != &room || !card) return {};
    auto *l = responseObserverLedger(room, false); if (!l) return {};
    auto it = l->cards.find(card);
    if (it == l->cards.end() || !it.value().audience.contains(observer->objectName())) return {};
    if (!matches(card, it.value())) { l->cards.erase(it); return {}; }
    return {{"legacy_identity_token", aiLegacyCardIdentity(room, observer, card)}, {"dwqingshidamage", it.value().damageFlag}};
}
void projectAIResponseObservations(AIWorldView &world, Room &room, ServerPlayer *observer)
{
    if (!observer || observer->getRoom() != &room || world.self.objectName != observer->objectName()) return;
    auto *l = responseObserverLedger(room, false); if (!l) return;
    QJsonObject flags;
    for (const auto *target : room.getAllPlayers(true)) {
        auto &values = l->shejian[observer->objectName()];
        const auto value = values.find(target->objectName());
        if (value == values.end()) continue;
        if (target->hasFlag("shejian_used") != value.value()) {
            values.erase(value); // A later private reversal cannot revive it.
            continue;
        }
        flags.insert(target->objectName(), value.value());
    }
    auto sk = world.strategyFacts.value("sk").toObject();
    auto publicFlags = sk.value("public_flags").toObject();
    publicFlags.insert("shejian_used", flags); sk.insert("public_flags", publicFlags);
    world.strategyFacts.insert("sk", sk);
    QJsonObject cards;
    for (auto it = l->cards.begin(); it != l->cards.end();) {
        if (!matches(it.key(), it.value())) { it = l->cards.erase(it); continue; }
        if (it.value().audience.contains(observer->objectName())) {
            QJsonArray ordered;
            if (!it.value().targets.isEmpty())
                for (const auto &name : it.value().targets.split('+')) ordered.append(name);
            cards.insert(aiLegacyCardIdentity(room, observer, it.key()), QJsonObject{{"known", true}, {"dwqingshitarget", it.value().targets},
                {"ordered_targets", ordered}, {"dwqingshidamage", it.value().damageFlag}});
        }
        ++it;
    }
    auto ny = world.strategyFacts.value("NyarzFirst").toObject();
    ny.insert("card_observations", cards); world.strategyFacts.insert("NyarzFirst", ny);
}

// ---- response-catalog ----
namespace {
QJsonObject unavailable(const QString &reason) { return {{"error", reason}, {"unsupported", true}}; }
}
QJsonObject resolveAIResponseCatalogue(Room &room, ServerPlayer *viewer,
                                     const AIRequest &request, const QJsonObject &query)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision())
        return unavailable("public catalog request is stale or belongs to another viewer");
    if (query.size() != 2 || query.value("kind").toString() != "response_catalogue"
        || !query.value("key").isString()) return unavailable("invalid public catalog query");
    const QString key = query.value("key").toString();
    const bool basic = key == "ol_sp_basic_names";
    const bool trick = key == "ol_sp_ndtrick_names";
    const bool random = key == "rushB_xiongsuan";
    const bool qinyi = key == "ov_qinyi_names";
    if (!((basic && request.kind == AIRequest::Activate && viewer->hasSkill("huanpei"))
        || (trick && request.kind == AIRequest::Activate && viewer->hasSkill("oljinjin"))
        || (random && request.kind == AIRequest::Choice && request.choiceOptions.reason == "rushB_xiongsuan")
        || (qinyi && request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "ov_qinyi"
            && viewer->hasSkill("ov_qinyi"))))
        return unavailable("public catalog source is outside its audited question");
    if (!request.conversionLedger || request.conversionLedger->queryCount >= 128)
        return unavailable("public catalog query budget exhausted");
    ++request.conversionLedger->queryCount;
    QJsonObject result{{"known", true}, {"key", key}};
    if (basic || trick || qinyi) {
        // Engine::getCardNames intentionally calls getRandomCards before its
        // index-order scan. Do not optimize away that source RNG consumption.
        result.insert("names", QJsonArray::fromStringList(Sanguosha->getCardNames(
            qinyi ? "BasicCard+TrickCard+^DelayedTrick" : basic ? "BasicCard" : "TrickCard+^DelayedTrick")));
    } else {
        QJsonArray ids; QJsonObject cards;
        for (int id : Sanguosha->getRandomCards()) {
            ids.append(id);
            const Card *card = Sanguosha->getEngineCard(id);
            if (!card) return unavailable("public catalog contains an absent printed card");
            // Immutable printed identities, never Room::getCard wrappers.
            QJsonArray kinds;
            for (const char *name : {"BasicCard", "TrickCard", "EquipCard", "DelayedTrick"})
                if (card->isKindOf(name)) kinds.append(name);
            kinds.append(card->getClassName());
            cards.insert(QString::number(id), QJsonObject{{"id", id}, {"effective_id", id},
                {"name", card->objectName()}, {"class_name", card->getClassName()},
                {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())},
                {"number", card->getNumber()}, {"kind_of", kinds}, {"virtual_card", false},
                {"subcards", QJsonArray()}, {"skill_name", card->getSkillName(false)},
                {"red", card->isRed()}, {"black", card->isBlack()}});
        }
        result.insert("ids", ids); result.insert("cards", cards);
    }
    return result;
}

// ---- retrial-context-dapo-context ----
namespace {
bool role(const QString &s)
{ return s=="lord" || s=="loyalist" || s=="rebel" || s=="renegade"; }
const AIPlayerView *view(const AIWorldView &world, const QString &name)
{
    if (world.self.objectName==name) return &world.self;
    for (const auto &p:world.players) if(p.objectName==name) return &p;
    return nullptr;
}
}
void projectAIDapoEvent(AIWorldView &world, Room &room, ServerPlayer *viewer,
    ServerPlayer *eventPlayer, const AIEventView &event, const QVariant &data)
{
    world.strategyFacts.remove("dongmanbao_dapo_role_witness");
    if (!viewer || viewer->getRoom()!=&room || !eventPlayer || eventPlayer->getRoom()!=&room
        || world.self.objectName!=viewer->objectName() || event.triggerEvent!=Dying
        || !data.canConvert<DyingStruct>()) return;
    const auto dying=data.value<DyingStruct>();
    if (!dying.who || dying.who->getRoom()!=&room || !view(world,dying.who->objectName())) return;
    const auto *target=view(world,dying.who->objectName());
    const bool targetKnown=(dying.who==viewer || target->roleVisible) && role(target->role);
    QJsonArray sources;
    for (ServerPlayer *source:room.getOtherPlayers(eventPlayer)) {
        const auto *row=view(world,source->objectName());
        if (!row || row->hp!=1) continue;
        bool hasSkill=false;
        for(const auto &skill:row->skills)
            if(skill.skillName=="SE_Dapo" && !skill.invalid) {hasSkill=true;break;}
        if(!hasSkill) continue; // No hidden native hasSkill/role query.
        const bool known=(source==viewer || row->roleVisible) && role(row->role);
        QJsonObject proof{{"player",source->objectName()},{"role_known",known}};
        if(known) proof.insert("role",row->role);
        sources.append(proof);
    }
    world.strategyFacts.insert("dongmanbao_dapo_role_witness",QJsonObject{
        {"actor",eventPlayer->objectName()},{"player",dying.who->objectName()},
        {"target_role_known",targetKnown},{"sources",sources}});
}

// ---- retrial-context-retrial ----
// Fixed DevSaodongUse::onDamaged asks MethodNone with the owner HE card-ID
// pattern. These TWO source hypotheses precede selection; they issue no receipt.
bool aiSaodongConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
  ||!aiRangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::RespondCard||r.choiceOptions.question!="askForCard"
  ||r.prompt!="@dev_saodong"||r.handlingMethod!=Card::MethodNone
  ||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE||!p->hasSkill("dev_saodong")
  ||s.size()!=6||s.value("source_key")!=QJsonValue("dev_saodong")
  ||s.value("skill")!=QJsonValue("dev_saodong")
  ||(s.value("name")!=QJsonValue("peach")&&s.value("name")!=QJsonValue("ex_nihilo"))
  ||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)
  ||!s.value("subcards").isArray())return false;
 const auto ids=s.value("subcards").toArray();if(ids.size()!=1)return false;
 const double number=ids[0].toDouble(-1);
 if(!ids[0].isDouble()||!std::isfinite(number)||number<0
   ||number>std::numeric_limits<int>::max()||number!=std::floor(number))return false;
 const int id=int(number);bool disclosed=false;
 for(const AICardView &c:r.worldView.handCards)if(c.effectiveId==id)disclosed=true;
 for(const AICardView &c:r.worldView.self.equips)if(c.effectiveId==id)disclosed=true;
 const Room *room=p->getRoom();
 if(!disclosed||room->getCardOwner(id)!=p
   ||(room->getCardPlace(id)!=Player::PlaceHand&&room->getCardPlace(id)!=Player::PlaceEquip))return false;
 const Card *material=Sanguosha->getCard(id);
 // Do not filter by availability/pattern here: the fixed source constructs
 // both products for EVERY heart, before its weak/draw/availability branches.
 return material&&material->getSuit()==Card::Heart
   &&aiRangeRequestCurrent(*p->getRoom(),r,p);
}

// ---- retrial-context ----
namespace {
using CardProjector = std::function<QJsonObject(const Card *)>;
QObject *previewLedger(Room &room, bool create)
{
    const QString name = QStringLiteral("_isolated_retrial_preview_ledger");
    auto *ledger = room.findChild<QObject *>(name, Qt::FindDirectChildrenOnly);
    if (!ledger && create) { ledger = new QObject(&room); ledger->setObjectName(name); }
    return ledger;
}
void invalidatePreviewCard(QObject *ledger, int id)
{
    auto rows = ledger->property("rows").toJsonObject();
    // Invalidate the whole small preview, including pending handoff, on face
    // replacement. A same-place wrapper mutation must not retain stale knowledge.
    for (auto it = rows.begin(); it != rows.end();) {
        const auto row = it.value().toObject();
        const auto ids = row.value(row.contains("pending") ? "pending" : "ids").toArray();
        if (ids.contains(id)) it = rows.erase(it); else ++it;
    }
    ledger->setProperty("rows", rows);
}
bool previewSkill(const QString &skill) { return skill == "PlusYiji" || skill == "fatewangzhe" || skill == "caoduo"; }
QString caoduoSourceScope(Room &room, ServerPlayer *viewer)
{
    if (!viewer || room.getCurrent() != viewer || !viewer->hasSkill("caoduo")
        || !viewer->hasFlag("Global_InTempMoving")) return {};
    // TriggerSkillV2 does not allocate SkillExecutionRegistry IDs for this
    // rule. The actual live native history scope is the authoritative lifetime.
    const auto scope = room.historyParent(room.currentHistoryEventId(), "skill", true);
    const auto data = scope.value("data").toMap();
    if (scope.value("status").toString() != "active"
        || data.value("executing_skill").toString() != "caoduo") return {};
    return scope.value("id").toString();
}
QString previewKey(const ServerPlayer *viewer, const QString &skill)
{ return viewer->objectName() + QChar(0x1f) + skill; }
QJsonObject previewFace(const QJsonObject &card)
{
    QJsonObject face;
    // Frozen public face only: never retained action rules, flags or skill chain.
    for (const char *key : {"id", "effective_id", "name", "class_name", "type_id", "suit",
        "suit_string", "number", "kind_of", "red", "black", "name_length", "is_single_target"})
        if (card.contains(key)) face.insert(key, card.value(key));
    face.insert("flags_complete", false); face.insert("skill_names_complete", false);
    return face;
}
bool inRoom(const ServerPlayer *player, const Room &room)
{
    return !player || player->getRoom() == &room;
}
QJsonValue damageValue(const QVariant &value, const Room &room, const CardProjector &publicCard,
    const ServerPlayer *requiredTarget = nullptr)
{
    if (!value.canConvert<DamageStruct>()) return QJsonValue(QJsonValue::Undefined);
    const DamageStruct damage = value.value<DamageStruct>();
    if (!inRoom(damage.from, room) || !inRoom(damage.to, room)
        || (requiredTarget && damage.to != requiredTarget))
        return QJsonValue(QJsonValue::Undefined);
    return QJsonObject{
        {"from", damage.from ? QJsonValue(damage.from->objectName()) : QJsonValue(false)},
        {"to", damage.to ? QJsonValue(damage.to->objectName()) : QJsonValue(false)},
        {"amount", damage.damage}, {"nature", int(damage.nature)}, {"reason", damage.reason},
        {"chain", damage.chain}, {"transfer", damage.transfer}, {"by_user", damage.by_user},
        {"card", damage.card ? QJsonValue(publicCard(damage.card)) : QJsonValue(false)}};
}
QJsonValue useValue(const QVariant &value, const Room &room, const CardProjector &publicCard)
{
    if (!value.canConvert<CardUseStruct>()) return QJsonValue(QJsonValue::Undefined);
    const CardUseStruct use = value.value<CardUseStruct>();
    if (!inRoom(use.from, room)) return QJsonValue(QJsonValue::Undefined);
    QJsonArray targets;
    for (const auto *target : use.to) {
        if (!target || !inRoom(target, room)) return QJsonValue(QJsonValue::Undefined);
        targets.append(target->objectName());
    }
    return QJsonObject{{"from", use.from ? QJsonValue(use.from->objectName()) : QJsonValue(false)},
        {"to", targets}, {"targets", targets},
        {"card", use.card ? QJsonValue(publicCard(use.card)) : QJsonValue(false)},
        {"m_addHistory", use.m_addHistory}};
}
QJsonValue pindianValue(const QVariant &value, const Room &room, const CardProjector &publicCard)
{
    if (!value.canConvert<PindianStruct *>()) return QJsonValue(QJsonValue::Undefined);
    const auto *p = value.value<PindianStruct *>();
    if (!p || !p->from || !p->to || !inRoom(p->from, room) || !inRoom(p->to, room)
        || !p->from_card || !p->to_card) return QJsonValue(QJsonValue::Undefined);
    // ServerPlayer::pindianSelect sends both $PindianResult logs and completes
    // a visible move before finishPindian triggers PindianVerifying.
    return QJsonObject{{"from", p->from->objectName()}, {"to", p->to->objectName()},
        {"reason", p->reason}, {"from_number", p->from_number}, {"to_number", p->to_number},
        {"from_card", publicCard(p->from_card)}, {"to_card", publicCard(p->to_card)}};
}
void scopedFlags(QJsonObject &context, const Room &room, const QStringList &names)
{
    QJsonObject rows;
    for (const auto *p : room.getAllPlayers(true)) {
        QJsonObject flags;
        for (const auto &name : names) flags.insert(name, p->hasFlag(name));
        rows.insert(p->objectName(), flags);
    }
    context.insert("biaofeng_flags", rows);
}
QJsonValue playerValue(const QVariant &value, const Room &room)
{
    if (!value.canConvert<ServerPlayer *>()) return QJsonValue(QJsonValue::Undefined);
    const ServerPlayer *target = value.value<ServerPlayer *>();
    if (!inRoom(target, room)) return QJsonValue(QJsonValue::Undefined);
    return target ? QJsonValue(target->objectName()) : QJsonValue(false);
}
QJsonValue cardValue(const QVariant &value, const CardProjector &publicCard)
{
    if (!value.canConvert<const Card *>()) return QJsonValue(QJsonValue::Undefined);
    const Card *card = value.value<const Card *>();
    return card ? QJsonValue(publicCard(card)) : QJsonValue(false);
}
void insertKnown(QJsonObject &context, const char *key, const QJsonValue &value)
{
    if (!value.isUndefined()) context.insert(QString::fromLatin1(key), value);
}
}

void recordAIStrategyRetrialPreview(Room &room, ServerPlayer *recipient,
    const QList<CardsMoveStruct> &moves, bool isLostPhase, const CardProjector &publicCard)
{
    if (!recipient || recipient->getRoom() != &room || !publicCard) return;
    auto *ledger = previewLedger(room, true);
    auto rows = ledger->property("rows").toJsonObject();
    for (const auto &move : moves) {
        // Both explicit preview cleanup and actual moves from the preview hand
        // revoke on loss, before any later callback can reuse the observation.
        if (isLostPhase && move.from == recipient && move.from_place == Player::PlaceHand) {
            for (const QString skill : {QStringLiteral("PlusYiji"), QStringLiteral("fatewangzhe"), QStringLiteral("caoduo")}) {
                const QString key = previewKey(recipient, skill);
                auto row = rows.value(key).toObject(); auto faces = row.value("faces").toObject();
                auto mappings = row.value("mappings").toObject();QJsonArray ids;
                for (const auto &value : row.value("ids").toArray()) {
                    const int id = value.toInt();
                    if (move.card_ids.contains(id)) { faces.remove(QString::number(id)); mappings.remove(QString::number(id)); }
                    else ids.append(id);
                }
                if (ids.isEmpty()) rows.remove(key);
                else { row.insert("ids", ids); row.insert("faces", faces); row.insert("mappings", mappings); rows.insert(key, row); }
            }
        }
        QString skill = move.reason.m_skillName;
        QString sourceScope;
        if (skill.isEmpty() && move.reason.m_reason == CardMoveReason::S_REASON_UNKNOWN) {
            sourceScope = caoduoSourceScope(room, recipient);
            if (!sourceScope.isEmpty()) skill = "caoduo";
        }
        if (!previewSkill(skill)
            || (skill == "caoduo" ? sourceScope.isEmpty() : move.reason.m_reason != CardMoveReason::S_REASON_PREVIEW)
            || move.reason.m_playerId != recipient->objectName()
            || move.to != recipient || move.from != nullptr
            || move.from_place != Player::PlaceTable || move.to_place != Player::PlaceHand) continue;
        const QString key = previewKey(recipient, skill);
        QJsonArray ids;QSet<int> seen;bool valid = move.open && !move.card_ids.isEmpty() && move.card_ids.size() <= (skill == "caoduo" ? 512 : 64);
        for (int id : move.card_ids) { if (id < 0 || seen.contains(id)) valid = false;seen.insert(id);ids.append(id); }
        if (skill == "caoduo" && valid) {
            auto *owner = room.getCardOwner(move.card_ids.first());
            valid = owner && owner != recipient && owner->getRoom() == &room
                && owner->handCards() == move.card_ids;
            for (int id : move.card_ids)
                valid = valid && room.getCardOwner(id) == owner && room.getCardPlace(id) == Player::PlaceHand;
        }
        if (!valid) { rows.remove(key);continue; }
        if (isLostPhase) { rows.insert(key, QJsonObject{{"pending", ids}, {"source_scope", sourceScope}});continue; }
        const auto pendingRow = rows.value(key).toObject();
        const auto pending = pendingRow.value("pending");
        if (!pending.isArray() || pending.toArray() != ids
            || pendingRow.value("source_scope").toString() != sourceScope) { rows.remove(key);continue; }
        QJsonObject faces, mappings;
        for (int id : move.card_ids) {
            const Card *card = room.getCard(id);
            if (!card || card->isVirtualCard() || card->getEffectiveId() != id) { valid = false;break; }
            const QString idKey = QString::number(id);
            auto watched = ledger->property("watched").toJsonObject();
            if (!watched.contains(idKey)) {
                if (const auto *wrapped = qobject_cast<const WrappedCard *>(card)) {
                    QObject::connect(wrapped, &WrappedCard::identityChanged, ledger,
                        [ledger, id]() { invalidatePreviewCard(ledger, id); }, Qt::DirectConnection);
                    watched.insert(idKey, true); ledger->setProperty("watched", watched);
                }
            }
            auto face = previewFace(publicCard(card));face.insert("id", id);face.insert("effective_id", id);
            if (skill == "caoduo") face.insert("known_flags", QJsonObject{{"caoduo", card->hasFlag("caoduo")}});
            faces.insert(idKey, face);
            mappings.insert(idKey, QJsonObject{{"place", int(room.getCardPlace(id))},
                {"owner", room.getCardOwner(id) ? room.getCardOwner(id)->objectName() : QString()}});
        }
        if (!valid) rows.remove(key);
        else {
            const qulonglong serial = ledger->property("next_serial").toULongLong() + 1;
            ledger->setProperty("next_serial", QVariant::fromValue(serial));
            rows.insert(key, QJsonObject{{"ids", ids}, {"faces", faces}, {"mappings", mappings},
                {"serial", QString::number(serial)}, {"source_scope", sourceScope}});
        }
    }
    ledger->setProperty("rows", rows);
}

void recordAIStrategyRetrialPreview(Room &room, ServerPlayer *recipient,
    const QList<CardsMoveStruct> &moves, bool isLostPhase)
{
    recordAIStrategyRetrialPreview(room, recipient, moves, isLostPhase, [](const Card *card) {
        // Invoked only AFTER a genuine recipient notification passed the narrow
        // PREVIEW gate. Intrinsic disclosed face, not actor/private card state.
        return QJsonObject{{"id", card->getEffectiveId()}, {"effective_id", card->getEffectiveId()},
            {"name", card->objectName()}, {"class_name", card->getClassName()},
            {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())},
            {"suit_string", card->getSuitString()}, {"number", card->getNumber()},
            {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
            {"red", card->isRed()}, {"black", card->isBlack()}, {"name_length", card->nameLength()}};
    });
}

void invalidateAIStrategyRetrialPreview(Room &room, int cardId)
{
    if (cardId < 0) return;
    if (auto *ledger = previewLedger(room, false)) invalidatePreviewCard(ledger, cardId);
}

QJsonObject readAIStrategyRetrialPreview(Room &room, ServerPlayer *viewer, const QString &skill)
{
    const QJsonObject unknown{{"known", false}};
    if (!viewer || viewer->getRoom() != &room || !previewSkill(skill)) return unknown;
    auto *ledger = previewLedger(room, false);if (!ledger) return unknown;
    const auto row = ledger->property("rows").toJsonObject().value(previewKey(viewer, skill)).toObject();
    if (!row.value("ids").isArray() || row.contains("pending")) return unknown;
    if (skill == "caoduo" && (!viewer->hasFlag("LuaTaoxiUsed")
        || row.value("source_scope").toString().isEmpty()
        || caoduoSourceScope(room, viewer) != row.value("source_scope").toString())) return unknown;
    const QVariant tag = skill == "caoduo" ? viewer->getTag("LuaZongxuan") : room.getTag(skill);
    if (tag.metaType().id() != QMetaType::QString || tag.toString().isEmpty()) return unknown;
    QJsonArray ids, cards;QSet<int> seen;
    const auto available = row.value("ids").toArray();const auto faces=row.value("faces").toObject();
    int cursor=0;
    for (const auto &part : tag.toString().split('+')) {
        bool ok=false;const int id=part.toInt(&ok);if (!ok || id<0 || seen.contains(id)) return unknown;seen.insert(id);
        while (cursor < available.size() && available.at(cursor).toInt()!=id) ++cursor;
        if (cursor==available.size()) return unknown;++cursor;
        const QString key=QString::number(id);const auto mapping=row.value("mappings").toObject().value(key).toObject();
        const auto *owner=room.getCardOwner(id);
        if (mapping.value("place").toInt()!=int(room.getCardPlace(id))
            || mapping.value("owner").toString()!=(owner?owner->objectName():QString())) return unknown;
        // Revalidate only a previously witnessed face; no changed/new face is
        // returned. setSuit/setNumber do not emit WrappedCard::identityChanged.
        if (!faces.value(key).isObject()) return unknown;
        const auto face = faces.value(key).toObject();
        const Card *current = room.getCard(id);
        if (!current || current->isVirtualCard() || current->getEffectiveId() != id
            || current->objectName() != face.value("name").toString()
            || current->getClassName() != face.value("class_name").toString()
            || int(current->getSuit()) != face.value("suit").toInt(-1)
            || current->getNumber() != face.value("number").toInt(-1)) {
            invalidatePreviewCard(ledger, id); return unknown;
        }
        if (skill == "caoduo" && (room.getCardPlace(id) != Player::PlaceHand
            || !owner || owner == viewer
            || face.value("known_flags").toObject().value("caoduo").toBool() != current->hasFlag("caoduo"))) {
            invalidatePreviewCard(ledger, id); return unknown;
        }
        ids.append(id);cards.append(face);
    }
    return {{"known", true}, {"ids", ids}, {"cards", cards}, {"skill", skill},
        {"serial", row.value("serial")}};
}

void projectAIStrategyRetrialContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context, const CardProjector &publicCard)
{
    Q_UNUSED(data);
    if (!viewer || viewer->getRoom() != &room || !publicCard
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()) return;
    const QString &reason = request.choiceOptions.reason;
    if (request.kind == AIRequest::UseCard && (request.pattern == "@PlusYiji" || request.pattern == "@fatewangzhe"
        || (request.pattern == "@@caoduo" && request.prompt == "@xinji-use"))) {
        const QString skill = request.pattern == "@@caoduo" ? "caoduo" : request.pattern.mid(1);
        const auto preview = readAIStrategyRetrialPreview(room, viewer, skill);
        if (preview.value("known").toBool()) {
            context.insert("retrial_preview", preview);
            if (skill == "PlusYiji") context.insert("biaofeng_yiji", room.getTag(skill).toString());
            else if (skill == "fatewangzhe") { QJsonArray ids;for (const auto &id : preview.value("ids").toArray()) ids.append(QString::number(id.toInt()));context.insert("fate_wangzhe_ids", ids); }
        }
    }

    // Exact public resolution windows, never general Room-tag availability.
    if (request.kind == AIRequest::UseCard && (request.pattern == "@PlusGuicai2"
        || request.pattern == "@PlusGuidao2" || request.pattern == "@@SixGuicai2")) {
        const auto value = pindianValue(room.getTag("CurrentPindianStruct"), room, publicCard);
        if (!value.isUndefined()) {
            context.insert("biaofeng_pindian", value);
            const QString stem = request.pattern == "@PlusGuicai2" ? "PlusGuicai_"
                : request.pattern == "@PlusGuidao2" ? "PlusGuidao_" : "SixGuicai";
            scopedFlags(context, room, {stem + "Source", stem + "Target"});
        }
    } else if ((request.kind == AIRequest::UseCard
        && (request.pattern == "@PlusYizhong" || request.pattern == "@@SixLongdan1"))
        || (request.kind == AIRequest::SkillInvoke && reason == "SixZhongyong")) {
        const auto value = useValue(room.getTag("CurrentUseStruct"), room, publicCard);
        if (!value.isUndefined()) {
            context.insert("biaofeng_use", value);
            if (request.pattern == "@@SixLongdan1") scopedFlags(context, room, {"SixLongdan_Target"});
            if (request.pattern == "@PlusYizhong") {
                QJsonObject marks;
                // addPlayerMark is broadcast before this public TargetConfirmed
                // response window (biaofeng.lua3623); no arbitrary mark oracle.
                for (const auto *p : room.getAllPlayers(true)) marks.insert(p->objectName(), p->getMark("PlusYizhong"));
                context.insert("biaofeng_yizhong_marks", marks);
            }
        }
    } else if (request.kind == AIRequest::SkillInvoke && reason == "SevenYueDan") {
        const QVariant value = room.getTag("SevenFengPing");
        if (value.metaType().id() == QMetaType::QString)
            context.insert("biaofeng_fengping", value.toString());
    } else if (request.kind == AIRequest::Discard && reason == "dev_caiyi") {
        // filterData[CardUsed] is updated only at the END of that event. During
        // nested TargetSpecified this is the previous completed use, not data.
        const auto *ledger = previewLedger(room, false);
        if (ledger) {
            const auto use = ledger->property("completed_card_uses").toJsonObject().value(viewer->objectName());
            if (use.isObject()) context.insert("challengedeveloper_caiyi_use", use);
        }
    } else if (request.kind == AIRequest::Discard && reason == "dev_saodong") {
        insertKnown(context, "challengedeveloper_saodong_damage",
            damageValue(viewer->getTag("dev_saodong_damage"), room, publicCard));
    } else if (request.kind == AIRequest::SkillInvoke && reason == "dev_jiaodao") {
        insertKnown(context, "challengedeveloper_jiaodao_use",
            useValue(viewer->getTag("dev_jiaodao_data"), room, publicCard));
    } else if (request.kind == AIRequest::Choice && reason == "luadushi") {
        // The only fixed writer stores an integer card ID (leo.lua2859), not
        // ServerPlayer*. QVariant::toPlayer is null in the original callback.
        context.insert("leo_luadushi", false);
    } else if (request.kind == AIRequest::AmazingGrace && reason == "newluaweiwo") {
        const QVariant value = room.getTag("newluaweiwo");
        const auto *judge = value.canConvert<JudgeStruct *>() ? value.value<JudgeStruct *>() : nullptr;
        if (judge) context.insert("leo_newluaweiwo_judge",
            projectAIStrategyJudgeContext(request, room, viewer, judge, publicCard));
        else context.insert("leo_newluaweiwo_judge", false);
    }

    // Fixed extensions/dongmanbao.lua 4995/4996, 6464/6465, 8482/8483.
    // These are the exact recipients of the current damage decision.
    if (request.kind == AIRequest::Discard && reason == "se_Fanshe")
        insertKnown(context, "dongmanbao_fanshe_damage",
            damageValue(room.getTag("FansheDamage"), room, publicCard, viewer));
    else if (request.kind == AIRequest::PlayerChosen && reason == "SE_Zhihui")
        insertKnown(context, "dongmanbao_zhihui_damage",
            damageValue(room.getTag("SE_Zhihui"), room, publicCard, viewer));
    else if (request.kind == AIRequest::SkillInvoke && reason == "SE_Zishang")
        insertKnown(context, "dongmanbao_zishang_damage",
            damageValue(room.getTag("SE_Zishang"), room, publicCard));
    // 8912/8913: the public dying target, not any hidden card/role.
    else if (request.kind == AIRequest::Discard && reason == "SE_Shouzang")
        insertKnown(context, "dongmanbao_shouzang_target",
            playerValue(room.getTag("SE_Shouzang_target"), room));
    // 13914/13915: exact type already included in this owner's prompt/pattern.
    else if (request.kind == AIRequest::Discard && reason == "SE_Lingshang") {
        const QVariant value = room.getTag("SE_Lingshang_type");
        if (value.metaType().id() == QMetaType::QString) {
            const QString type = value.toString();
            if (type == "BasicCard" || type == "TrickCard" || type == "EquipCard")
                context.insert("dongmanbao_lingshang_type", type);
        }
    // 14633/14634: previously declared public SkillCard target, same source asker.
    } else if (request.kind == AIRequest::PlayerChosen && reason == "se_biling")
        insertKnown(context, "dongmanbao_biling_target",
            playerValue(room.getTag("se_bilingvs_target"), room));
    // Fixed biaofeng.lua3983: just-inflicted public damage card.
    else if (request.kind == AIRequest::UseCard && request.pattern == "@PlusYuanmou")
        insertKnown(context, "biaofeng_yuanmou_card",
            cardValue(room.getTag("PlusYuanmou_Damage"), publicCard));
    // 6602-6610: recipient obtains and publicly shows this equipment first.
    else if (request.kind == AIRequest::SkillInvoke && reason == "PlusChenggui")
        insertKnown(context, "biaofeng_chenggui_card",
            cardValue(room.getTag("PlusChenggui_card"), publicCard));
    // 7360-7365: target is also named explicitly in the current prompt.
    else if (request.kind == AIRequest::UseCard && request.pattern == "@PlusCuorui")
        insertKnown(context, "biaofeng_cuorui_target",
            playerValue(room.getTag("PlusCuorui"), room));
    // 7929-7933: public Slash/Duel being redirected, no private hand access.
    else if (request.kind == AIRequest::UseCard && request.pattern == "@PlusLiuli")
        insertKnown(context, "biaofeng_liuli_card",
            cardValue(room.getTag("liuli-card"), publicCard));
    // 11987 broadcasts the selected public card's wire to this exact owner;
    // 11867 consumes it in the immediate attached-skill response.
    else if (request.kind == AIRequest::UseCard && request.pattern == "@@SixWulveOthers!") {
        const QVariant value = viewer->property("SixWulveCard");
        if (value.metaType().id() == QMetaType::QString)
            context.insert("biaofeng_six_wulve_card", value.toString());
    }
    else if (request.kind == AIRequest::Choice && reason == "SevenFengPing")
        insertKnown(context, "biaofeng_fengping",
            playerValue(room.getTag("SevenFengPing"), room));
}

void projectAIStrategyRetrialEventContext(AIWorldView &world, Room &room, ServerPlayer *viewer,
    const AIEventView &event, const CardProjector &publicCard)
{
    if (!viewer || viewer->getRoom() != &room || !publicCard
        || world.self.objectName != viewer->objectName()) return;
    if (event.triggerEvent == CardUsed && event.kind == "card_use" && !event.privateEvent) {
        // Called at the actual RoomThread end-of-event boundary, after the
        // per-observer projection. Preserve this immutable observed value;
        // never query a later live card/tag or another viewer's history.
        auto *ledger = previewLedger(room, true);
        auto histories = ledger->property("completed_card_uses").toJsonObject();
        const auto card = event.details.value("card");
        histories.insert(viewer->objectName(), QJsonObject{
            {"from", event.from.isEmpty() ? QJsonValue(false) : QJsonValue(event.from)},
            {"to", QJsonArray::fromStringList(event.targets)},
            {"card", card.isObject() && !card.toObject().isEmpty() ? card : QJsonValue(false)}});
        ledger->setProperty("completed_card_uses", histories);
    }
    if (event.triggerEvent != ChoiceMade || event.kind != "choice"
        || event.from != viewer->objectName()
        || !event.privateEvent || event.privateViewer != viewer->objectName()) return;
    // These tags exist during the synchronous private ChoiceMade callback,
    // before native skill execution resumes. Never export into later events.
    if (event.reason == "SE_Zishang"
        && event.details.value("choice_kind") == QJsonValue("skillInvoke"))
        insertKnown(world.decisionContext, "dongmanbao_zishang_damage",
            damageValue(room.getTag("SE_Zishang"), room, publicCard));
    else if (event.reason == "SevenFengPing"
        && event.details.value("choice_kind") == QJsonValue("skillChoice"))
        insertKnown(world.decisionContext, "biaofeng_fengping",
            playerValue(room.getTag("SevenFengPing"), room));
}

// ---- scarlet-context ----
namespace {
using ScarletContextPublicCard = std::function<QJsonObject(const Card *)>;
bool scarletContextRoster(Room &room, const ServerPlayer *p)
{
    return !p || room.findPlayerByObjectName(p->objectName(), true) == p;
}
QJsonValue scarletContextCardValue(const Card *card, const ScarletContextPublicCard &publicCard)
{
    return card ? QJsonValue(publicCard(card)) : QJsonValue(QJsonValue::Null);
}
QJsonObject scarletContextDamageValue(const DamageStruct &d, const ScarletContextPublicCard &publicCard)
{
    return {{"from", d.from ? d.from->objectName() : QString()},
        {"to", d.to ? d.to->objectName() : QString()}, {"amount", d.damage},
        {"nature", int(d.nature)}, {"chain", d.chain}, {"transfer", d.transfer},
        {"by_user", d.by_user}, {"reason", d.reason}, {"card", scarletContextCardValue(d.card, publicCard)}};
}
QJsonObject scarletContextUseValue(const CardUseStruct &u, const ScarletContextPublicCard &publicCard)
{
    QJsonArray to; for (const auto *p : u.to) if (p) to.append(p->objectName());
    return {{"from", u.from ? u.from->objectName() : QString()}, {"to", to},
        {"card", scarletContextCardValue(u.card, publicCard)}, {"m_addHistory", u.m_addHistory}};
}
QJsonObject effectValue(const CardEffectStruct &e, const ScarletContextPublicCard &publicCard)
{
    return {{"from", e.from ? e.from->objectName() : QString()},
        {"to", e.to ? e.to->objectName() : QString()}, {"card", scarletContextCardValue(e.card, publicCard)},
        {"multiple", e.multiple}, {"offset_num", e.offset_num}};
}
bool validUse(Room &room, const CardUseStruct &u)
{
    if (!scarletContextRoster(room, u.from)) return false;
    for (const auto *p : u.to) if (!scarletContextRoster(room, p)) return false;
    return true;
}
QMap<int, const AICardView *> disclosedCards(const AIRequest &r)
{
    QMap<int, const AICardView *> ids;
    const auto cards = [&](const QList<AICardView> &list) {
        for (const auto &c : list) if (c.effectiveId >= 0) ids.insert(c.effectiveId, &c);
    };
    cards(r.worldView.handCards); cards(r.worldView.discardPile); cards(r.choiceOptions.cards);
    const auto player = [&](const AIPlayerView &p) {
        cards(p.knownCards); cards(p.equips); cards(p.judgingArea);
        for (const auto &pile : p.piles) if (pile.open) cards(pile.cards);
    };
    player(r.worldView.self); for (const auto &p : r.worldView.players) player(p);
    return ids;
}
bool publicJudgePattern(const QString &pattern)
{
    if (pattern.contains('$')) return false;
    for (const auto &branch : pattern.split('#')) {
        const auto factors = branch.split('|');
        if (factors.size() >= 4 && factors.at(3) != ".") return false;
    }
    return true;
}
void merge(AIWorldView &world, const QJsonObject &values)
{
    if (values.isEmpty()) return;
    auto facts = world.strategyFacts.value("scarletayuhuo").toObject();
    for (auto it = values.begin(); it != values.end(); ++it) facts.insert(it.key(), it.value());
    world.strategyFacts.insert("scarletayuhuo", facts);
}
}

void projectAIStrategyScarletContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context, const ScarletContextPublicCard &publicCard)
{
    Q_UNUSED(data);
    if (!viewer || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName() || !scarletContextRoster(room, viewer)) return;
    const auto kind = request.kind;
    const auto reason = request.choiceOptions.reason;
    const auto pattern = request.pattern;
    QJsonObject facts;
    // Fixed extension 5443: the private AG has already revealed its offered cards
    // to this very recipient. No arbitrary card-id probe is introduced for the
    // donor's erroneous `for card_id in ipairs(card_ids)` index loop.
    if (kind == AIRequest::AmazingGrace && reason == "s2_guanhui") {
        const auto value = room.getTag("s2_guanhui");
        const auto *judge = value.canConvert<JudgeStruct *>() ? value.value<JudgeStruct *>() : nullptr;
        if (!judge) facts.insert("s2_guanhui", false);
        else if (judge->who && scarletContextRoster(room, judge->who) && judge->card && publicJudgePattern(judge->pattern)) {
            QJsonObject outcomes;
            const auto cards = disclosedCards(request);
            for (auto it = cards.begin(); it != cards.end(); ++it) {
                // Match only the already disclosed face, never a foreign live
                // wrapper (which might have privately changed since revelation).
                const auto &face = *it.value();
                std::unique_ptr<Card> card(Sanguosha->cloneCard(face.objectName, Card::Suit(face.suit), face.number));
                if (card) outcomes.insert(QString::number(it.key()), judge->isGood(card.get()));
            }
            facts.insert("s2_guanhui", QJsonObject{{"who", judge->who->objectName()},
                {"card", publicCard(judge->card)}, {"reason", judge->reason}, {"pattern", judge->pattern},
                {"good", judge->isGood()}, {"predicate_good", judge->good}, {"negative", judge->negative},
                {"play_animation", judge->play_animation}, {"outcome_by_id", outcomes}});
        }
    }
    // Fixed extension 6020/6021/6044: source reads exactly the old AmazingGrace
    // tag and calls fillAG before this Choice. Nonempty identities still require
    // independent disclosure in this viewer's snapshot/active fillAG ledger.
    // Missing visibility is not empty.
    if (kind == AIRequest::Choice && reason == "s2_lixian") {
        const auto value = room.getTag("AmazingGrace");
        QJsonArray ids; QJsonObject faces; bool known = true;
        const auto disclosed = disclosedCards(request);
        const auto notified = readAIStrategyNotifiedPile(room, viewer, "#ai_current_ag").value("cards").toObject();
        if (value.canConvert<QVariantList>()) for (const auto &v : value.toList()) {
            const int id = v.toInt(); const auto key = QString::number(id);
            // The existing fillAG recipient ledger is an independent visibility
            // witness. Its frozen face is used; never read a hidden live wrapper.
            if (notified.value(key).isObject()) faces.insert(key, notified.value(key));
            else if (!disclosed.contains(id)) { known = false; break; }
            ids.append(id);
        }
        if (known) { facts.insert("AmazingGrace", ids); facts.insert("disclosed_cards", faces); }
    }
    const bool useScope = (kind == AIRequest::UseCard && pattern == "@@s2_mieshi")
        || (kind == AIRequest::Choice && reason == "s3_zhuhai")
        || (kind == AIRequest::PlayerChosen && reason == "s3_yt_junlie")
        || ((kind == AIRequest::SkillInvoke || kind == AIRequest::Discard) && reason == "s3_fans_yinbing");
    if (useScope) {
        // QVariant::value is deliberately retained: absent/wrong-type tags give
        // the same default CardUseStruct as the donor SWIG toCardUse.
        const auto use = room.getTag("CurrentUseStruct").value<CardUseStruct>();
        if (validUse(room, use)) facts.insert("CurrentUseStruct", scarletContextUseValue(use, publicCard));
    }
    if (kind == AIRequest::UseCard && pattern == "@@s2_qiangrang") {
        // Source extension 13004 stores CardUseStruct, AI 3793 asks for
        // CardEffectStruct. Preserve Qt's empty default, not a fabricated effect.
        const auto effect = room.getTag("CurrentUseStruct").value<CardEffectStruct>();
        if (scarletContextRoster(room, effect.from) && scarletContextRoster(room, effect.to))
            facts.insert("CurrentUseStruct_CardEffect", effectValue(effect, publicCard));
    }
    if (kind == AIRequest::UseCard && pattern == "@@s3_yt_ruiqiDiscard") {
        const auto value = room.getTag("s3_yt_ruiqi");
        if (value.canConvert<CardEffectStruct>()) {
            const auto effect = value.value<CardEffectStruct>();
            // The asked player is the effect recipient, not necessarily its owner.
            if (effect.to == viewer && scarletContextRoster(room, effect.from))
                facts.insert("s3_yt_ruiqi", effectValue(effect, publicCard));
        } else if (!value.isValid()) facts.insert("s3_yt_ruiqi", effectValue(CardEffectStruct(), publicCard));
    }
    if (kind == AIRequest::UseCard && pattern == "@@s3_fans_jibian1") {
        const auto use = room.getTag("s3_fans_jibian").value<CardUseStruct>();
        if (validUse(room, use)) facts.insert("s3_fans_jibian", scarletContextUseValue(use, publicCard));
    }
    const bool kangdao = kind == AIRequest::PlayerChosen && reason == "s3_kangdao_target";
    const bool jibian = kind == AIRequest::UseCard && pattern == "@@s3_fans_jibian2";
    if (kangdao || jibian) {
        const auto *p = room.getTag(kangdao ? "s3_kangdao" : "s3_fans_jibian").value<ServerPlayer *>();
        if (scarletContextRoster(room, p)) facts.insert(kangdao ? "s3_kangdao" : "s3_fans_jibian_Player",
            p ? QJsonValue(p->objectName()) : QJsonValue(false));
    }
    const bool damageScope = (kind == AIRequest::SkillInvoke && reason == "s3_lianzhan")
        || (kind == AIRequest::UseCard && pattern == "@@s3_huanxiangbenghuai")
        || (kind == AIRequest::Choice && reason == "s3_jxwj_duce");
    if (damageScope) {
        const auto damage = room.getTag("CurrentDamageStruct").value<DamageStruct>();
        if (scarletContextRoster(room, damage.from) && scarletContextRoster(room, damage.to))
            facts.insert("CurrentDamageStruct", scarletContextDamageValue(damage, publicCard));
    }
    merge(request.worldView, facts);
    if (!facts.isEmpty()) context.insert("scarletayuhuo", facts);
}

void projectAIStrategyScarletEventContext(AIWorldView &world, Room &room, ServerPlayer *viewer,
    const AIEventView &event, const ScarletContextPublicCard &publicCard)
{
    if (!viewer || world.self.objectName != viewer->objectName() || !scarletContextRoster(room, viewer)
        || event.triggerEvent != ChoiceMade || event.from != viewer->objectName()
        || event.kind != "choice") return;
    const auto choice = event.details.value("choice_kind").toString();
    const auto fields = event.details.value("prompt_list").toArray();
    const bool invoke = choice == "skillInvoke" && (event.reason == "s_w_qimou" || event.reason == "s2_zhengyi");
    const bool selected = choice == "skillChoice" && event.reason == "s3_jxwj_duce";
    if ((!invoke && !selected) || fields.size() < 3 || fields.at(0).toString() != choice
        || fields.at(1).toString() != event.reason) return;
    // These ChoiceMade notifications are actor-private in the actual producer.
    // This closes only that lawful observer domain, not legacy cross-viewer AI
    // global sharing. Room::damage keeps CurrentDamageStruct until this event.
    const auto damage = room.getTag("CurrentDamageStruct").value<DamageStruct>();
    if (scarletContextRoster(room, damage.from) && scarletContextRoster(room, damage.to))
        merge(world, QJsonObject{{"CurrentDamageStruct", scarletContextDamageValue(damage, publicCard)}});
}

// ---- events ----
#include "standard.h"

namespace {
// QObject ownership gives each Room its own ledger and teardown. No static
// cross-Room state, legacy AI globals or scripting userdata are involved.
class EventKnowledge final : public QObject {
public:
    explicit EventKnowledge(Room &room) : QObject(&room)
    { setObjectName(QStringLiteral("isolated_ai_event_knowledge")); }
    QHash<QString, QHash<QString, QSet<int>>> hands;
    QHash<QString, QHash<QString, QSet<int>>> revoked;
};
EventKnowledge &eventsKnowledge(Room &room)
{
    QObject *child = room.findChild<QObject *>(QStringLiteral("isolated_ai_event_knowledge"), Qt::FindDirectChildrenOnly);
    if (auto *state = dynamic_cast<EventKnowledge *>(child)) return *state;
    return *new EventKnowledge(room);
}

bool publicPlace(Player::Place place)
{
    return place == Player::PlaceEquip || place == Player::PlaceDelayedTrick
        || place == Player::DiscardPile;
}

const AIPlayerView *playerView(const AIWorldView &world, const Player *player)
{
    if (!player) return nullptr;
    if (world.self.objectName == player->objectName()) return &world.self;
    for (const AIPlayerView &view : world.players)
        if (view.objectName == player->objectName()) return &view;
    return nullptr;
}

bool knownInWorld(const AIWorldView &world, const Player *owner, int id)
{
    if (!owner) return false;
    const AIPlayerView *view = playerView(world, owner);
    if (!view) return false;
    const QList<AICardView> &cards = owner->objectName() == world.self.objectName ? world.handCards : view->knownCards;
    for (const AICardView &card : cards) if (card.effectiveId == id) return true;
    return false;
}

AICardView physicalView(const Card *card, bool own)
{
    AICardView view;
    view.cardId = card->getId(); view.effectiveId = card->getEffectiveId();
    view.objectName = card->objectName(); view.className = card->getClassName();
    view.nameLength = card->nameLength();
    view.skillName = card->getSkillName(false);
    if (view.effectiveId >= 0) if (const Card *original = Sanguosha->getEngineCard(view.effectiveId)) view.engineClassName = original->getClassName();
    if (own || card->getSourceSkillName() == view.skillName) view.sourceSkillName = card->getSourceSkillName();
    view.skillNamesComplete = own;
    if (own) view.skillNames = card->getSkillNames();
    view.kindOfNames = card->getKindOfNames();
    view.suit = int(card->getSuit()); view.number = card->getNumber();
    view.red = card->isRed(); view.black = card->isBlack();
    view.typeId = int(card->getTypeId()); view.handlingMethod = int(card->getHandlingMethod());
    view.virtualCard = card->isVirtualCard(); view.targetFixed = card->targetFixed();
    view.damageCard = card->isDamageCard(); view.singleTarget = card->isSingleTargetCard(); view.canRecast = card->canRecast();
    if (const auto *equip = dynamic_cast<const EquipCard *>(card->getRealCard())) {
        view.equipSlot = int(equip->location()); view.equipOccupyLocations = equip->getOccupyLocations();
    }
    if (const auto *weapon = dynamic_cast<const Weapon *>(card->getRealCard())) view.weaponRange = weapon->getRange();
    return view;
}

QJsonObject cardJson(const Card *card, bool own, Room &room, ServerPlayer *observer)
{
    const AICardView view = physicalView(card, own);
    QJsonObject out{{"id",view.cardId},{"effective_id",view.effectiveId},{"name",view.objectName},
        {"class_name",view.className},{"name_length",view.nameLength},{"engine_class_name",view.engineClassName},{"type_id",view.typeId},{"suit",view.suit},{"number",view.number},
        {"red",view.red},{"black",view.black},{"skill_name",view.skillName},
        {"kind_of",QJsonArray::fromStringList(view.kindOfNames)},
        {"skill_names_complete",own},
        {"target_fixed",view.targetFixed},{"damage_card",view.damageCard},{"single_target",view.singleTarget},
        {"virtual_card",view.virtualCard},{"handling_method",view.handlingMethod},
        {"equip_slot",view.equipSlot},{"weapon_range",view.weaponRange},{"can_recast",view.canRecast},
        {"subtype",card->getSubtype()}};
    if (own) out.insert("skill_names",QJsonArray::fromStringList(view.skillNames));
    if (own || card->getSourceSkillName() == view.skillName) out.insert("source_skill",view.sourceSkillName);
    QJsonObject flags = readAIStrategyPublicCardFlags(room, observer, card);
    if (own) {
        for (const QString &flag : card->getFlags())
            if (!flag.startsWith("visible_")) flags.insert(flag, true);
        out.insert("flags_complete", true);
        out.insert("flags_complete_scope", "owner_non_visibility");
    }
    out.insert("known_flags", flags);
    out.insert("subcards",QJsonArray{});
    QJsonArray equipLocations; for (int slot : view.equipOccupyLocations) equipLocations.append(slot);
    out.insert("equip_occupy_locations",equipLocations);
    return out;
}

void grantHand(EventKnowledge &state, ServerPlayer *observer, Player *owner, int id)
{
    if (observer && owner && owner->handCards().contains(id)) {
        state.hands[observer->objectName()][owner->objectName()].insert(id);
        state.revoked[observer->objectName()][owner->objectName()].remove(id);
    }
}
}

void projectAIStrategyKnownCards(AIWorldView &world, Room &room, ServerPlayer *observer)
{
    if (!observer) return;
    EventKnowledge &state = eventsKnowledge(room);
    // History is projected by the trusted viewer-scoped persistence boundary.
    // An unpartitioned legacy file cannot establish ownership of its samples.
    // With no configured store, preserve the Lua viewer shadow between rounds.
    if (!world.strategyFacts.contains("ai_data_known")) {
        world.strategyFacts.insert("ai_data_known",true);
        world.strategyFacts.insert("ai_data_persistent",false);
    }
    auto &viewer = state.hands[observer->objectName()];
    for (AIPlayerView &view : world.players) {
        ServerPlayer *owner = room.findPlayerByObjectName(view.objectName, true);
        if (!owner) continue;
        const QSet<int> revoked = state.revoked.value(observer->objectName()).value(view.objectName);
        if (!revoked.isEmpty() && !observer->canSeeHandcard(owner)) {
            for (auto it=view.knownCards.begin(); it!=view.knownCards.end();) {
                const Card *card = Sanguosha->getCard(it->effectiveId);
                if (revoked.contains(it->effectiveId) && card && !card->hasFlag("visible")) it=view.knownCards.erase(it);
                else ++it;
            }
        }
        auto known = viewer.find(view.objectName);
        if (known == viewer.end()) continue;
        QSet<int> projected;
        for (const AICardView &card : view.knownCards) projected.insert(card.effectiveId);
        // Stale knowledge never exposes a card in a new hidden owner/zone.
        const QList<int> hand = owner->handCards();
        for (auto it = known->begin(); it != known->end();) {
            if (!hand.contains(*it)) { it = known->erase(it); continue; }
            const Card *card = Sanguosha->getCard(*it);
            if (card && !projected.contains(*it)) view.knownCards.append(physicalView(card, false));
            ++it;
        }
    }
}

void projectAIStrategyEvent(AIEventView &event, const AIWorldView &world, Room &room,
                           ServerPlayer *observer, ServerPlayer *target, const QVariant &data)
{
    if (!observer || !target) return;
    EventKnowledge &state = eventsKnowledge(room);
    // Full borrowed/source skill chains belong only to the actual card actor.
    // Foreign observers retain the public activation-only record, without even
    // revealing whether an additional private root exists.
    const Card *actorCard = nullptr;
    const Player *cardActor = nullptr;
    if (data.canConvert<DamageStruct>()) {
        const auto value=data.value<DamageStruct>(); actorCard=value.card; cardActor=value.from;
    } else if (data.canConvert<CardUseStruct>()) {
        const auto value=data.value<CardUseStruct>(); actorCard=value.card; cardActor=value.from;
    } else if (data.canConvert<CardEffectStruct>()) {
        const auto value=data.value<CardEffectStruct>(); actorCard=value.card; cardActor=value.from;
    } else if (data.canConvert<RecoverStruct>()) {
        const auto value=data.value<RecoverStruct>(); actorCard=value.card; cardActor=value.who;
    } else if (data.canConvert<DeathStruct>()) {
        const auto value=data.value<DeathStruct>();
        if (value.damage) { actorCard=value.damage->card; cardActor=value.damage->from; }
    }
    if (actorCard) {
        QJsonObject card = event.details.value("card").toObject();
        if (!card.isEmpty()) {
            // Two native constructor origins only, at the actual declared-use
            // viewer boundary. Never export a different/private current chain.
            if (cardActor != observer && event.triggerEvent == PreCardUsed
                && event.kind == QStringLiteral("card_use") && data.canConvert<CardUseStruct>()) {
                auto names = readAIXufaConstructedSkills(room, observer, actorCard);
                if (names.isEmpty()) names = readAIOLJijiangConstructedSkills(room, observer, actorCard);
                if (!names.isEmpty()) {
                    card.insert("skill_names", names.value("skill_names"));
                    card.insert("skill_names_complete", true);
                    event.details.insert("card_skills", names.value("skill_names"));
                    event.details.insert("card_skills_complete", true);
                }
            }
            QJsonObject flags = readAIStrategyPublicCardFlags(room, observer, actorCard);
            if (cardActor == observer) {
                for (const QString &flag : actorCard->getFlags())
                    if (!flag.startsWith("visible_")) flags.insert(flag, true);
                card.insert("flags_complete", true);
                card.insert("flags_complete_scope", "owner_non_visibility");
                event.intentionSuppressed = actorCard->hasFlag("AIGlobal_ComboFallback")
                    || actorCard->hasFlag("meihuomoyan") || actorCard->hasFlag("sgkgodshunshi")
                    || actorCard->hasFlag("kenewmieyao");
                event.details.insert("intention_suppression_known", true);
            }
            card.insert("known_flags", flags);
            event.details.insert("card", card);
        }
    }
    if (actorCard && cardActor==observer) {
        event.details.insert("card_skills",QJsonArray::fromStringList(actorCard->getSkillNames()));
        event.details.insert("card_skills_complete",true);
        QJsonObject card=event.details.value("card").toObject();
        if (!card.isEmpty()) {
            card.insert("skill_names",QJsonArray::fromStringList(actorCard->getSkillNames()));
            card.insert("skill_names_complete",true);
            card.insert("source_skill",actorCard->getSourceSkillName());
            // Owner PreCardUsed valuation compares this declared recipe with
            // the owner's current hand. Publish only currently owned/public
            // material IDs; no foreign private cost is inferred from actorhood.
            if (event.triggerEvent == PreCardUsed) {
                bool known = true;
                QJsonArray materials;
                for (int id : actorCard->getSubcards()) {
                    const auto place = room.getCardPlace(id);
                    if (id < 0 || (room.getCardOwner(id) != observer
                        && place != Player::PlaceTable && !publicPlace(place))) {
                        known = false;
                        break;
                    }
                    materials.append(id);
                }
                if (known) {
                    card.insert("virtual_card", actorCard->isVirtualCard());
                    card.insert("subcards", materials);
                    card.insert("effective_id", actorCard->getEffectiveId());
                }
            }
            event.details.insert("card",card);
        }
    }
    if (event.kind == QStringLiteral("damage") || event.kind == QStringLiteral("judge")) {
        const QString skill = !event.reason.isEmpty() ? event.reason : event.cardSkill;
        bool complete = true;
        QString owner;
        if (!skill.isEmpty()) for (ServerPlayer *candidate : room.getAllPlayers()) {
            const AIPlayerView *view = playerView(world,candidate);
            if (candidate != observer && (!view || !view->skillsComplete)) { complete=false; break; }
            // findPlayerBySkillName's second argument is includeLose, not dead.
            if (candidate->hasSkill(skill,event.kind == QStringLiteral("damage"))) { owner=candidate->objectName(); break; }
        }
        event.details.insert("intention_from_complete",complete);
        event.details.remove("intention_from");
        if (complete) event.details.insert("intention_from",owner);
        else event.details.insert("intention_from_private",true);
    }
    if (event.triggerEvent == CardsMoveOneTime && data.canConvert<CardsMoveOneTimeStruct>()) {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        QJsonObject value{{"from",move.from ? move.from->objectName() : QString()},
            {"to",move.to ? move.to->objectName() : QString()}, {"to_place",int(move.to_place)},
            {"reason",move.reason.m_reason},
            {"basic_reason",move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON},
            {"reason_player",move.reason.m_playerId},{"skill",move.reason.m_skillName},
            {"card_count",move.card_ids.size()}};
        QJsonArray rows;
        for (int index=0; index<move.card_ids.size(); ++index) {
            const int id = move.card_ids.at(index);
            const Player::Place fromPlace = move.from_places.value(index, Player::PlaceUnknown);
            const Card *card = Sanguosha->getCard(id);
            QJsonObject row{{"from_place",int(fromPlace)}, {"public",card && card->hasFlag("visible")},
                {"open",index < move.open.size() && move.open.at(index)}};
            if (fromPlace == Player::PlaceHand && target == room.getCurrent()) {
                // Source filterEvent removes visible_* flags, across owners.
                auto &owned = state.hands[observer->objectName()];
                for (auto it=owned.begin(); it!=owned.end(); ++it) it->remove(id);
                for (ServerPlayer *owner : room.getAllPlayers(true))
                    state.revoked[observer->objectName()][owner->objectName()].insert(id);
            }
            if (move.to_place == Player::PlaceHand && move.to == target && move.from == observer)
                grantHand(state,observer,move.to,id);
            const bool authorized = card && (publicPlace(fromPlace) || publicPlace(move.to_place)
                || move.from == observer || move.to == observer
                || (fromPlace == Player::PlaceHand && move.from && observer->canSeeHandcard(move.from))
                || (move.to_place == Player::PlaceHand && move.to && observer->canSeeHandcard(move.to))
                || card->hasFlag("visible") || knownInWorld(world,move.from,id) || knownInWorld(world,move.to,id));
            // PresentCard is a package tag. Only its owner gets the comparison;
            // other observers receive explicit private provenance, not its value.
            if (!move.to) row.insert("present_match",false);
            else if (move.to == observer && card) row.insert("present_match",move.to->getTag("PresentCard").toString() == card->toString());
            else row.insert("present_match_private",true);
            if (authorized) {
                row.insert("card",cardJson(card,move.from == observer || move.to == observer,room,observer));
                auto *source = qobject_cast<ServerPlayer *>(move.from);
                QJsonObject legality;
                bool privateLegality = false;
                if (source == observer) {
                    // ServerPlayer::canUse changes a live mark and emits signals.
                    // Reproduce its query under the read-only mark overlay instead.
                    const bool limited = source->isCardLimited(card,Card::MethodUse);
                    for (ServerPlayer *other : room.getAlivePlayers()) {
                        bool allowed = false;
                        if (!limited) {
                            AIMarkQueryScope scope(source,QStringLiteral("&xiyan1-Clear"),source->getMark("&xiyan1-Clear")+1);
                            int maxVotes = 0;
                            allowed = card->targetFilter(QList<const Player *>(),other,source,maxVotes) || maxVotes > 0;
                        }
                        legality.insert(other->objectName(),allowed);
                    }
                } else if (source) {
                    // Even a public skill callback can consult its owner's hand.
                    // Do not turn another player's hypothetical canUse into an oracle.
                    privateLegality = true;
                }
                row.insert("can_use",legality);
                if (privateLegality) row.insert("can_use_private",true);
            }
            rows.append(row);
        }
        value.insert("cards",rows); event.details.insert("move",value);
    } else if (event.triggerEvent == ChoiceMade) {
        const QString raw = data.toString();
        const QStringList prompt = raw.split(':');
        if (raw.contains(QStringLiteral("fenxin:yes"))) {
            event.details.insert("fenxin_invoked",true);
            // Fenxin's invoker received the target in its invoke request. Another
            // viewer cannot inspect a third player's private FenxinTarget flag.
            const bool complete = observer == target;
            event.details.insert("fenxin_target_complete",complete);
            if (complete) {
                QString chosen;
                for (ServerPlayer *player : room.getAlivePlayers()) if (player->hasFlag("FenxinTarget")) { chosen=player->objectName(); break; }
                event.details.insert("fenxin_target",chosen);
            }
        }
        if (observer == target && prompt.value(0) == QStringLiteral("cardChosen")) {
            bool ok=false; const int id=prompt.value(2).toInt(&ok);
            if (ok) {
                const Player::Place place = room.getCardPlace(id);
                event.details.insert("chosen_card_place",int(place));
                ServerPlayer *owner = room.findPlayerByObjectName(prompt.value(3),true);
                const Card *card = Sanguosha->getCard(id);
                if (card && (publicPlace(place) || owner == observer
                    || (owner && observer->canSeeHandcard(owner)) || knownInWorld(world,owner,id)))
                    event.details.insert("chosen_card",cardJson(card,owner == observer,room,observer));
            }
        }
        if (observer == target) {
            if (prompt.value(0) == QStringLiteral("viewCards")) {
                // Room::showCard can disclose only a subset, while doGongxin
                // supplies the full list. The event name is not a full-hand grant.
                ServerPlayer *owner = room.findPlayerByObjectName(prompt.value(1),true);
                for (const QString &item : prompt.value(2).split('+',Qt::SkipEmptyParts)) {
                    bool ok=false; const int id=item.toInt(&ok);
                    if (ok) grantHand(state,observer,owner,id);
                }
            } else if (prompt.value(0) == QStringLiteral("cardChosen") && prompt.value(4) == QStringLiteral("visible")) {
                ServerPlayer *owner = room.findPlayerByObjectName(prompt.value(3),true);
                if (owner) for (int id : owner->handCards()) grantHand(state,observer,owner,id);
            } else if (prompt.value(0) == QStringLiteral("Yiji")) {
                ServerPlayer *owner = room.findPlayerByObjectName(prompt.value(2),true);
                for (const QString &item : prompt.value(3).split('+',Qt::SkipEmptyParts)) {
                    bool ok=false; const int id=item.toInt(&ok); if (ok) grantHand(state,observer,owner,id);
                }
            }
        }
    } else if (event.triggerEvent == ShowCards) {
        const QStringList fields = data.toString().split(':');
        if (fields.size()>1 && fields.at(1)==observer->objectName())
            for (const QString &item : fields.at(0).split('+',Qt::SkipEmptyParts)) {
                bool ok=false; const int id=item.toInt(&ok); if (ok) grantHand(state,observer,target,id);
            }
    } else if (event.triggerEvent == EventPhaseChanging && data.canConvert<PhaseChangeStruct>()) {
        const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::NotActive) {
            // GameRule clears -Clear marks for every player, not just the
            // player whose turn ended. This receipt exposes only this viewer.
            event.details.insert("strategy_marks_cleared",QJsonArray{observer->objectName()});
        }
        // GameRule clears every living player's -PlayClear marks when Play
        // ends, and also clears dead players' marks at the turn boundary.
        // Only this observer's legacy policy cache is disclosed here.
        if (change.to == Player::NotActive || (change.from == Player::Play && observer->isAlive()))
            event.details.insert("strategy_play_marks_cleared",QJsonArray{observer->objectName()});
    }
}

// ---- damage ----
// Deliberately limited to declared damage state. Arbitrary Room/Card tags and
// another player's hand-card private flags never enter this projection.
void projectAIStrategyDamage(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!viewer) return;
    QJsonObject damage;
    // extensions/extra.lua:2165-2172 broadcasts $guandu_sj with the chosen event.
    damage.insert("guandu_event", room.getTag("guandu_sj").toString());
    // extensions/kearjsrg.lua:815-824 sets this only on publicly logged Death.
    damage.insert("keqiyanhuo_count", room.getTag("keqiyanhuonum").toInt());
    damage.insert("mode_player_count", Sanguosha->getPlayerCount(room.getMode()));
    QJsonArray armorNames;
    QSet<QString> seenArmor;
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const auto *card = Sanguosha->getEngineCard(id);
        if (card && card->isKindOf("Armor") && !seenArmor.contains(card->objectName())) {
            seenArmor.insert(card->objectName());
            armorNames.append(card->objectName());
        }
    }
    damage.insert("armor_names", armorNames);
    QJsonObject viewAsSkills;
    const auto takeSkills = [&](const QList<AISkillView> &skills, bool own) {
        for (const auto &skill : skills) {
            const auto *viewAs = Sanguosha->getViewAsSkill(skill.skillName);
            QJsonObject row{{"exists", viewAs != nullptr}};
            if (viewAs && own) row.insert("owner_enabled_at_play", viewAs->isEnabledAtPlay(viewer));
            if (own || !viewAsSkills.contains(skill.skillName)) viewAsSkills.insert(skill.skillName, row);
        }
    };
    takeSkills(world.self.skills, true);
    for (const auto &player : world.players) takeSkills(player.skills, false);
    damage.insert("view_as_skills", viewAsSkills);
    QJsonObject records, jianyuan;
    for (const auto *player : room.getAllPlayers(true)) {
        // NyarzFirst.lua:84-95 and 152-175 broadcast $Arecord / $Aremove and
        // change the public skill description whenever this record changes.
        const auto names = player->property("SkillDescriptionRecord_Djianxiong").toString()
            .split('+', Qt::SkipEmptyParts);
        records.insert(player->objectName(), QJsonArray::fromStringList(names));
        // genius.lua:1801-1803 changes the public skill description on addition.
        jianyuan.insert(player->objectName(), QJsonArray::fromStringList(
            player->property("SkillDescriptionRecord_f_jianyuan").toString().split('+', Qt::SkipEmptyParts)));
    }
    damage.insert("records_by_player", records);
    damage.insert("jianyuan_records_by_player", jianyuan);

    QSet<int> visibleIds;
    const auto take = [&visibleIds](const QList<AICardView> &cards) {
        for (const auto &card : cards) if (card.effectiveId >= 0) visibleIds.insert(card.effectiveId);
    };
    take(world.handCards);
    take(world.self.equips);
    take(world.self.judgingArea);
    take(world.discardPile);
    for (const auto &pile : world.self.piles) if (pile.open) take(pile.cards);
    for (const auto &player : world.players) {
        take(player.equips);
        take(player.judgingArea);
        // A previously revealed hand card's identity does not reveal its tags.
        for (const auto &pile : player.piles) if (pile.open) take(pile.cards);
    }
    const QStringList allowedFlags{"drank", "Qinggang", "yj_nvzhuang_debuff", "hanyong",
        "yb_canqu1_add_damage", "cuijinAddDamage_1", "cuijinAddDamage_2", "gd_liangjunxiangchi"};
    QJsonObject cards;
    for (int id : visibleIds) {
        const auto *card = room.getCard(id);
        if (!card) continue;
        const auto *owner = room.getCardOwner(id);
        const auto place = room.getCardPlace(id);
        // Owner-private card state is authorized. Foreign cards require a
        // currently public zone; an open pile is not permission for hidden tags.
        const bool own = owner == viewer;
        const bool publicZone = place == Player::PlaceTable || place == Player::PlaceEquip
            || place == Player::PlaceDelayedTrick || place == Player::DiscardPile;
        if (!own && !publicZone) continue;
        QJsonObject state{{"midao_nature", card->getTag("heg_midao_nature").toString()},
            {"basis", own ? "owner_card_state" : "public_midao_choice_log"}};
        // extraheg.lua:5598-5601 logs the selected Midao nature publicly.
        // Other damage flags/tags are disclosed only to the current owner.
        if (own) {
            QJsonArray flags;
            for (const auto &flag : allowedFlags) if (card->hasFlag(flag)) flags.append(flag);
            state.insert("flags", flags);
            state.insert("drank_amount", card->getTag("drank").toInt());
        }
        cards.insert(QString::number(id), state);
    }
    damage.insert("cards_by_id", cards);
    world.strategyFacts.insert("damage", damage);
}

// ---- dragon-events ----
void projectAIStrategyDragonEvent(AIWorldView &world, const AIEventView &event)
{
    world.strategyFacts.remove("tongling_role_witness");
    if (event.triggerEvent != CardFinished || event.cardName != "tonglingCard"
        || event.from.isEmpty()) return;
    const QString actor = event.details.value("player").toString();
    if (!actor.isEmpty() && actor != event.from) return;
    const AIPlayerView *player = nullptr;
    if (world.self.objectName == event.from) player = &world.self;
    else for (const AIPlayerView &candidate : world.players)
        if (candidate.objectName == event.from) { player = &candidate; break; }
    // Role visibility to the owner alone is insufficient: this callback follows
    // the actual public broadcast in tonglingCard::on_use, not a speculative fill.
    if (!player || !player->roleRevealed) return;
    const QString role = player->role;
    if (role != "lord" && role != "loyalist" && role != "rebel" && role != "renegade") return;
    world.strategyFacts.insert("tongling_role_witness",
        QJsonObject{{"player",player->objectName},{"role",role}});
}
