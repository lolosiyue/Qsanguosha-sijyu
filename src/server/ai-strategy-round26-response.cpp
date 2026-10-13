#include "ai-strategy-round26-response.h"
#include "ai-strategy-room-ledger.h"
#include "ai.h"
#include "room.h"
#include "serverplayer.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QJsonArray>
#include <QSet>
#include <QHash>
#include <QScopeGuard>

namespace {
constexpr const char *flagLedgerProperty = "_ai_round26_public_flag_ledger";
constexpr const char *feilongFlag = "LxtxFeiLongDuoFeng";
struct PublicFlagLedger : AIRoomLedger {
    explicit PublicFlagLedger(Room &room) : AIRoomLedger(room) {}
    QHash<ServerPlayer *, QHash<ServerPlayer *, bool>> values;
    QSet<ServerPlayer *> watched;
    void invalidate(ServerPlayer *target) {
        for (auto it=values.begin();it!=values.end();++it) it.value().remove(target);
    }
    void watch(ServerPlayer *target) {
        if(watched.contains(target))return;
        watched.insert(target);
        // No writer-specific private epoch is exposed. Any property mutation
        // conservatively retires this target's previous public witness.
        QObject::connect(target,&Player::gameplay_property_changed,this,[this,target]{invalidate(target);}, Qt::DirectConnection);
        QObject::connect(target,&QObject::destroyed,this,[this,target]{invalidate(target);values.remove(target);watched.remove(target);}, Qt::DirectConnection);
    }
};
PublicFlagLedger *flagLedger(Room &room,bool create) {
    auto *result=static_cast<PublicFlagLedger *>(room.property(flagLedgerProperty).value<QObject *>());
    if(!result&&create){result=new PublicFlagLedger(room);room.setProperty(flagLedgerProperty,QVariant::fromValue(static_cast<QObject *>(result)));}
    return result;
}
constexpr const char *propertyName = "_ai_round26_scalar_initial";
bool pushRaw(lua_State *state, const char *registry, const char *name)
{
    lua_getglobal(state,"sgs");
    if (!lua_istable(state,-1)) return false;
    lua_pushstring(state,registry);lua_rawget(state,-2);
    if (!lua_istable(state,-1)) return false;
    lua_pushstring(state,name);lua_rawget(state,-2);return true;
}
bool sourceMatches(lua_State *state,const char *name,const char *source,int line)
{
    const int top=lua_gettop(state);
    const auto restore=qScopeGuard([&]{lua_settop(state,top);});
    if (!pushRaw(state,"ai_skill_invoke",name)||!lua_isfunction(state,-1)) return false;
    lua_Debug info{};
    return lua_getinfo(state,">S",&info) && QString::fromUtf8(info.source)==QString::fromLatin1(source)
        && info.linedefined==line && QString::fromUtf8(info.what)=="Lua";
}
}
void recordAIStrategyRound26PublicFlag(Room &room,ServerPlayer *target,const QString &operation)
{
    if(!target||target->getRoom()!=&room||!room.getPlayers().contains(target))return;
    if(operation!=feilongFlag&&operation!=QStringLiteral("-")+feilongFlag&&operation!=".")return;
    const bool value=operation==feilongFlag;
    if(target->hasFlag(feilongFlag)!=value)return;
    auto *l=flagLedger(room,true);l->watch(target);
    for(ServerPlayer *viewer:room.getPlayers()) {
        l->watch(viewer);
        l->values[viewer].insert(target,value);
    }
}
QJsonObject readAIStrategyRound26PublicFlag(Room &room,ServerPlayer *viewer,ServerPlayer *target,const QString &flag)
{
    if(flag!=feilongFlag||!viewer||!target||viewer->getRoom()!=&room||target->getRoom()!=&room
        ||!room.getPlayers().contains(viewer)||!room.getPlayers().contains(target))return {{"known",false}};
    auto *l=flagLedger(room,false);
    if(!l||!l->values.contains(viewer)||!l->values[viewer].contains(target))return {{"known",false}};
    const bool witnessed=l->values[viewer].value(target);
    if(target->hasFlag(feilongFlag)!=witnessed){l->invalidate(target);return {{"known",false}};}
    return {{"known",true},{"value",witnessed}};
}
void clearAIStrategyRound26Response(Room &room)
{
    auto *l=flagLedger(room,false);room.setProperty(flagLedgerProperty,QVariant());delete l;
    room.setProperty(propertyName,QVariant());
    room.setProperty("_ai_round26_initial_snapshot",QVariant());
}
void captureAIStrategyRound26Response(Room &room)
{
    if (room.property(propertyName).isValid()) return;
    const bool firstBootstrap=!room.property("_ai_round26_ever_captured").toBool();
    room.setProperty("_ai_round26_ever_captured",true);
    QJsonObject seeds;
    auto &runtime=room.roomRuntime()->lua();LuaRuntime::Binding binding(runtime,false);
    lua_State *state=runtime.state();
    for (const auto &entry : {qMakePair(QStringLiteral("xuhe"),8508),qMakePair(QStringLiteral("olxuhe"),3926)}) {
        bool known=false;
        if (state && firstBootstrap) {
            const int top=lua_gettop(state);const auto restore=qScopeGuard([&]{lua_settop(state,top);});
            const QByteArray name=entry.first.toLatin1();
            const char *source=entry.first=="xuhe"?"@lua/ai/sp-ai.lua":"@lua/ai/ol_sp-ai.lua";
            known=sourceMatches(state,name.constData(),source,entry.second)
                && pushRaw(state,"ai_skill_choice",name.constData()) && lua_isnil(state,-1);
        }
        seeds.insert(entry.first,known?QJsonObject{{"known",true},{"present",false}}:QJsonObject{{"known",false}});
    }
    room.setProperty(propertyName,seeds.toVariantMap());
}
void projectAIStrategyRound26Response(AIRequest &request,Room &room,ServerPlayer *viewer)
{
    if (!viewer||request.worldView.self.objectName!=viewer->objectName())return;
    // The first request can use bootstrap nil. Later requests may follow
    // arbitrary private callbacks; no writer-specific epoch/value is exposed.
    // Re-projecting this identical request is harmless, but never refreshes it.
    const QString stamp=viewer->objectName()+QStringLiteral(":")+QString::number(request.decisionId);
    QVariant first=room.property("_ai_round26_initial_snapshot");
    if(!first.isValid()) {room.setProperty("_ai_round26_initial_snapshot",stamp);first=stamp;}
    QJsonObject seeds;
    if(first.toString()==stamp)seeds=QJsonObject::fromVariantMap(room.property(propertyName).toMap());
    else for(const auto &name:{QStringLiteral("xuhe"),QStringLiteral("olxuhe")})seeds.insert(name,QJsonObject{{"known",false}});
    request.worldView.strategyFacts.insert("response_scalar_choices",seeds);
}
void projectAIStrategyRound26ResponseEvent(AIWorldView &world,Room &room,ServerPlayer *viewer,const AIEventView &event)
{
    world.strategyFacts.remove("loong_feilong_role_witness");
    auto previousLoong=world.strategyFacts.value("Loong").toObject();
    auto previousFlags=previousLoong.value("public_flags").toObject();
    previousFlags.remove(feilongFlag);previousLoong.insert("public_flags",previousFlags);
    world.strategyFacts.insert("Loong",previousLoong);
    if (!viewer||world.self.objectName!=viewer->objectName()||event.triggerEvent!=ChoiceMade
        ||event.kind!="choice"||event.reason!="_lxtx_feilongduofeng_re"
        ||event.from!=viewer->objectName()||!event.privateEvent||event.privateViewer!=viewer->objectName())return;
    const auto fields=event.details.value("prompt_list").toArray();
    if(fields.size()!=3||fields.at(0)!="skillInvoke"||fields.at(1)!="_lxtx_feilongduofeng_re"||fields.at(2)!="yes")return;
    static const QSet<QString>roles{"lord","loyalist","rebel","renegade"};
    // The actor's role is already an owner DTO; no foreign role query occurs.
    if(!roles.contains(world.self.role))return;
    QJsonArray targets;QJsonObject flags;bool complete=true;
    for(ServerPlayer *other:room.getOtherPlayers(viewer)) {
        const auto observed=readAIStrategyRound26PublicFlag(room,viewer,other,feilongFlag);
        if(!observed.value("known").toBool()){complete=false;continue;}
        const bool flagged=observed.value("value").toBool();
        flags.insert(other->objectName(),flagged);
        if(flagged)targets.append(other->objectName());
    }
    auto loong=world.strategyFacts.value("Loong").toObject();
    auto publicFlags=loong.value("public_flags").toObject();
    publicFlags.insert("LxtxFeiLongDuoFeng",flags);loong.insert("public_flags",publicFlags);
    world.strategyFacts.insert("Loong",loong);
    // The complete source traversal must be observed. A partial positive list
    // cannot authorize effects while silently omitting an unknown recipient.
    if(!complete)return;
    world.strategyFacts.insert("loong_feilong_role_witness",QJsonObject{
        {"actor",viewer->objectName()},{"role",world.self.role},{"targets",targets},
        {"event_sequence",QString::number(event.sequence)},{"revision",QString::number(event.revision)}});
}
