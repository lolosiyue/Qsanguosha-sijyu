#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round30-choice.h"
#include "ai.h"
#include "room.h"
#include "serverplayer.h"
#include "general.h"
#include "server-info.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QScopeGuard>
#include <atomic>

namespace {
QJsonObject scarletMetadata(Room &);
QJsonObject description(const ServerPlayer *player, const ServerPlayer *viewer,
                        const QString &disclosedName, bool second)
{
    const General *general = second ? player->getGeneral2() : player->getGeneral();
    const QString actualName = general ? general->objectName() : QString();
    // An absent foreign snapshot name may conceal a general: it never proves
    // native null. A nonempty disclosed name must still match the live slot.
    const bool slotPublic = !ServerInfo.EnableHegemony || player->isDead()
        || (second ? player->hasShownGeneral2() : player->hasShownGeneral());
    if (player != viewer && (!slotPublic || disclosedName != actualName))
        return {{QStringLiteral("known"), false}};
    if (!general)
        return {{QStringLiteral("known"), true}, {QStringLiteral("present"), false}};
    return {{QStringLiteral("known"), true}, {QStringLiteral("present"), true},
            {QStringLiteral("text"), general->getSkillDescription(true)}};
}
}
void projectAIRound30TargetRecommend(AIWorldView &world, Room &room, const ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || world.self.objectName != viewer->objectName()) return;
    QJsonObject rows;
    auto append = [&](const AIPlayerView &view) {
        const ServerPlayer *player = room.findPlayerByObjectName(view.objectName, true);
        if (!player || player->getRoom() != &room) return;
        rows.insert(view.objectName, QJsonObject{
            {QStringLiteral("first"), description(player, viewer, view.generalName, false)},
            {QStringLiteral("second"), description(player, viewer, view.general2Name, true)}});
    };
    append(world.self);
    for (const AIPlayerView &view : world.players) append(view);
    QJsonObject facts = world.strategyFacts.value(QStringLiteral("target_recommend")).toObject();
    facts.insert(QStringLiteral("general_descriptions"), rows);
    facts.insert(QStringLiteral("scarlet"), scarletMetadata(room));
    world.strategyFacts.insert(QStringLiteral("target_recommend"), facts);
}

namespace {
class TargetCache final : public QObject {
public:
    explicit TargetCache(Room &room) : QObject(&room) {
        static std::atomic<quint64> next{0};
        scope = QString::number(++next);
    }
    QString scope;
    quint64 epoch = 0;
    bool initialized = false;
    bool entered = false;
    bool known = false;
    bool populated = false;
    QString viewer;
    QJsonObject scarlet{{QStringLiteral("captured"), false}};
};
TargetCache *cache(Room &room) {
    for (QObject *child : room.children())
        if (auto *value = dynamic_cast<TargetCache *>(child)) return value;
    return new TargetCache(room);
}
QJsonObject scarletMetadata(Room &room) { return cache(room)->scarlet; }
bool origin(lua_State *L, int index, const char *file, int line) {
    if (!lua_isfunction(L, index)) return false;
    lua_pushvalue(L, index); lua_Debug info{};
    return lua_getinfo(L, ">S", &info) && info.what && QByteArray(info.what) == "Lua"
        && info.source && QString::fromUtf8(info.source) == QStringLiteral("@lua/ai/") + QString::fromLatin1(file)
        && info.linedefined == line;
}
bool upvalue(lua_State *L, int index, const char *wanted) {
    index = lua_absindex(L, index);
    for (int i=1;;++i) {
        const char *name = lua_getupvalue(L, index, i);
        if (!name) return false;
        if (QByteArray(name) == wanted) return true;
        lua_pop(L, 1);
    }
}
bool plain(lua_State *L, int index) {
    if (!lua_istable(L,index)) return false;
    if (lua_getmetatable(L,index)) {lua_pop(L,1); return false;}
    return true;
}
QJsonObject captureScarlet(lua_State *L, int sgsIndex) {
    const int base=lua_gettop(L); sgsIndex=lua_absindex(L,sgsIndex);
    const auto restore=qScopeGuard([&]{lua_settop(L,base);});
    lua_pushliteral(L,"ai_target_recommend");lua_rawget(L,sgsIndex);
    const int registry=lua_absindex(L,-1);
    const bool registryKnown=plain(L,registry);
    QJsonObject entries;
    const QList<QPair<QString,int>> keys{{"s4_banjiang",1686},{"s4_ganglie",1828},{"s4_hunzi",2035},{"s4_qiaobian",2285}};
    for (const auto &key:keys) {
        const int top=lua_gettop(L);
        QJsonObject row{{"known",registryKnown}};
        if (registryKnown) {
            const QByteArray name=key.first.toUtf8();lua_pushlstring(L,name.constData(),size_t(name.size()));lua_rawget(L,registry);
            const int wrapper=lua_absindex(L,-1);
            const bool present=lua_toboolean(L,wrapper);
            row.insert("present",present);
            bool matched=origin(L,wrapper,"smart-ai.lua",11203);
            if (matched) {
                matched=upvalue(L,wrapper,"skill_name");
                if (matched) {matched=lua_type(L,-1)==LUA_TSTRING&&QString::fromUtf8(lua_tostring(L,-1))==key.first;lua_pop(L,1);}
            }
            if (matched) matched=upvalue(L,wrapper,"fn");
            if (matched && key.first=="s4_ganglie") {
                matched=origin(L,-1,"smart-ai.lua",11229)&&upvalue(L,-1,"spec")&&plain(L,-1);
                if (matched) {
                    const int spec=lua_absindex(L,-1);lua_pushliteral(L,"invalid");lua_rawget(L,spec);
                    matched=lua_type(L,-1)==LUA_TNUMBER&&lua_tonumber(L,-1)==3;lua_pop(L,1);
                    lua_pushliteral(L,"rules");lua_rawget(L,spec);matched=matched&&plain(L,-1)&&lua_rawlen(L,-1)==1;
                    if(matched) {lua_rawgeti(L,-1,1);matched=plain(L,-1);}
                    if(matched) {
                        const int rule=lua_absindex(L,-1);lua_pushliteral(L,"score");lua_rawget(L,rule);
                        matched=lua_type(L,-1)==LUA_TNUMBER&&lua_tonumber(L,-1)==-3;lua_pop(L,1);
                        lua_pushliteral(L,"when");lua_rawget(L,rule);matched=matched&&origin(L,-1,"scarlet-ai.lua",key.second);
                    }
                }
            } else if (matched) matched=origin(L,-1,"scarlet-ai.lua",key.second);
            row.insert("source_matches",matched);row.insert("enabled",present&&matched);
        }
        entries.insert(key.first,row);lua_settop(L,top);
    }
    QJsonObject types;
    for (const auto &pair:QList<QPair<QString,QString>>{{"draw","s4_ganglie"},{"decrease","s4_qiaobian"}}) {
        const QByteArray list=(pair.first+"SkillsList").toUtf8();lua_pushlstring(L,list.constData(),size_t(list.size()));lua_rawget(L,sgsIndex);
        bool known=plain(L,-1),present=false;
        if(known) {
            const size_t size=lua_rawlen(L,-1);
            if(size>4096) known=false;
            else for(size_t i=1;i<=size;++i) {lua_rawgeti(L,-1,lua_Integer(i));if(lua_type(L,-1)==LUA_TSTRING&&QString::fromUtf8(lua_tostring(L,-1))==pair.second)present=true;lua_pop(L,1);}
        }
        types.insert(pair.second,QJsonObject{{"known",known},{"type",pair.first},{"present",present}});lua_pop(L,1);
    }
    return {{"captured",true},{"entries",entries},{"types",types}};
}
QJsonObject unknownCache(const QString &reason) {
    return {{QStringLiteral("known"), false}, {QStringLiteral("reason"), reason}};
}
QJsonObject cacheRow(const TargetCache &state) {
    return {{QStringLiteral("known"), true}, {QStringLiteral("scope"), state.scope},
            {QStringLiteral("epoch"), QString::number(state.epoch)},
            {QStringLiteral("state"), state.populated ? QStringLiteral("captured") : QStringLiteral("empty")}};
}
}
void captureAIRound30TargetCache(Room &room) {
    TargetCache *state = cache(room);
    if (state->initialized) return;
    state->initialized = true;
    LuaRuntime &runtime = room.roomRuntime()->lua();
    LuaRuntime::Binding binding(runtime, false);
    lua_State *L = runtime.state();
    if (!L) return;
    const int top = lua_gettop(L);
    const auto restore = qScopeGuard([&] { lua_settop(L, top); });
    lua_pushglobaltable(L);
    lua_pushliteral(L, "sgs"); lua_rawget(L, -2);
    // SWIG's namespace has a metatable; these exact raw reads invoke no policy.
    if (!lua_istable(L, -1)) return;
    state->scarlet=captureScarlet(L,-1);
    if (state->entered) return;
    lua_pushliteral(L, "ai_active_recommends_cache"); lua_rawget(L, -2);
    // Do not export or reconstruct an already-populated donor table.
    state->known = !lua_toboolean(L, -1);
}
void resetAIRound30TargetCache(Room &room) {
    TargetCache *state = cache(room);
    ++state->epoch;
    state->known = true;
    state->populated = false;
    state->viewer.clear();
}
void invalidateAIRound30TargetCache(Room &room) {
    TargetCache *state = cache(room);
    state->entered = true;
    state->known = false;
}
QJsonObject queryAIRound30TargetCache(Room &room, const AIRequest &request,
                                    ServerPlayer *viewer, const QJsonObject &proposal) {
    // Pure observer caches need only their actual snapshot/lifecycle stamp.
    // Unlike legacy target-cache capture this does not read a GameVM table,
    // attest an old absence, or depend on card/range payment certificates.
    if (proposal.value(QStringLiteral("op")) == QStringLiteral("observer_epoch")) {
        if (proposal.size() != 1 || !room.roomRuntime() || room.roomRuntime()->isClosing()
            || !viewer || viewer->getRoom() != &room
            || request.viewerObjectName != viewer->objectName()
            || request.worldView.self.objectName != request.viewerObjectName
            || request.worldView.revision != request.stateRevision
            || request.stateRevision != room.roomRuntime()->stateRevision())
            return unknownCache(QStringLiteral("observer cache snapshot is stale or foreign"));
        const auto *state = cache(room);
        return {{"known",true},{"scope",state->scope},{"epoch",QString::number(state->epoch)},
                {"viewer",request.viewerObjectName},{"revision",QString::number(request.stateRevision)}};
    }
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || !aiRound44RangeRequestCurrent(room,request,viewer))
        return unknownCache(QStringLiteral("target recommendation cache request is stale or foreign"));
    TargetCache *state = cache(room);
    state->entered = true;
    const QString op = proposal.value(QStringLiteral("op")).toString();
    if ((op != QStringLiteral("read") && op != QStringLiteral("capture") && op != QStringLiteral("epoch"))
        || proposal.size() != (op == QStringLiteral("capture") ? 3 : 1))
        return unknownCache(QStringLiteral("target recommendation cache proposal is malformed"));
    if (op == QStringLiteral("epoch"))
        return {{"known",true},{"scope",state->scope},{"epoch",QString::number(state->epoch)}};
    if (!state->known)
        return unknownCache(QStringLiteral("target recommendation cache history is unknown"));
    if (state->populated && state->viewer != viewer->objectName())
        return unknownCache(QStringLiteral("target recommendation cache was captured by another viewer"));
    if (op == QStringLiteral("read")) return cacheRow(*state);
    if (!proposal.value(QStringLiteral("scope")).isString()
        || proposal.value(QStringLiteral("scope")).toString() != state->scope
        || !proposal.value(QStringLiteral("epoch")).isString()
        || proposal.value(QStringLiteral("epoch")).toString() != QString::number(state->epoch)
        || state->populated)
        return unknownCache(QStringLiteral("target recommendation cache capture stamp is stale"));
    state->populated = true;
    state->viewer = viewer->objectName();
    return cacheRow(*state);
}
