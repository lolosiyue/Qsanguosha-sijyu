#include "ai-strategy-round31-planning.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include <QObject>
#include <QJsonObject>
namespace {
class MidaoGlobalSnapshot final : public QObject {
public:
    MidaoGlobalSnapshot(Room &room, const QJsonObject &row) : QObject(&room), value(row) {}
    const QJsonObject value;
};
const MidaoGlobalSnapshot *snapshot(Room &room) {
    for (auto *child : room.children())
        if (auto *row = dynamic_cast<MidaoGlobalSnapshot *>(child)) return row;
    return nullptr;
}
}
void captureAIRound31PlanningMetadata(Room &room) {
    if (snapshot(room)) return;
    auto &vm = room.roomRuntime()->lua();
    LuaRuntime::Binding binding(vm, false);
    auto *L = vm.state();
    QJsonObject row{{"known", false}, {"known_nil", false}};
    if (!L) { new MidaoGlobalSnapshot(room, row); return; }
    const int top = lua_gettop(L);
    lua_pushglobaltable(L);
    const int globals = lua_absindex(L, -1);
    lua_pushliteral(L, "na"); lua_rawget(L, globals);
    const bool absent = lua_isnil(L, -1); lua_pop(L, 1);
    bool plainLookup = true;
    if (absent && lua_getmetatable(L, globals)) {
        lua_pushliteral(L, "__index"); lua_rawget(L, -2);
        plainLookup = lua_isnil(L, -1); lua_pop(L, 2);
    }
    bool source = false;
    lua_pushliteral(L, "SmartAI"); lua_rawget(L, globals);
    if (lua_istable(L, -1)) {
        const int klass = lua_absindex(L, -1);
        // middleclass.lua27-43 installs instance methods in __instanceDict,
        // not the raw class table. Real AI instances use this exact dictionary
        // (middleclass.lua100). Authenticate its finite layout without calling
        // the class/static __index closures or any strategy callback.
        lua_pushliteral(L, "__instanceDict"); lua_rawget(L, klass);
        if (lua_istable(L, -1)) {
            const int dictionary = lua_absindex(L, -1);
            lua_pushliteral(L, "__index"); lua_rawget(L, dictionary);
            const bool instanceLookup = lua_rawequal(L, -1, dictionary);
            lua_pop(L, 1);
            bool assignmentLookup = false;
            if (lua_getmetatable(L, klass)) {
                lua_pushliteral(L, "__newindex"); lua_rawget(L, -2);
                assignmentLookup = lua_rawequal(L, -1, dictionary);
                lua_pop(L, 2);
            }
            row.insert("instance_dictionary", instanceLookup && assignmentLookup);
            lua_pushliteral(L, "ajustDamage"); lua_rawget(L, dictionary);
            if (instanceLookup && assignmentLookup && lua_isfunction(L, -1)) {
                lua_Debug info{}; lua_pushvalue(L, -1);
                if (lua_getinfo(L, ">S", &info)) {
                    const QString file = QString::fromUtf8(info.source ? info.source : "");
                    source = file.endsWith("/smart-ai.lua") && info.linedefined == 8180
                        && QString::fromLatin1(info.what ? info.what : "") == "Lua";
                    row.insert("source", file); row.insert("line", info.linedefined);
                }
            }
        }
    }
    // Fixed-load writer audit: both smart-ai na tables and mobile-ai's na are
    // lexical locals; no global na writer/restore exists in the audited AI load.
    // Never sample a mutable gameplay global at decision time.
    row.insert("known", plainLookup && source);
    row.insert("known_nil", plainLookup && source && absent);
    lua_settop(L, top);
    new MidaoGlobalSnapshot(room, row);
}
void projectAIRound31PlanningMetadata(AIWorldView &world, Room &room) {
    const auto *row = snapshot(room);
    world.strategyFacts.insert("legacy_midao_na", row ? row->value
        : QJsonObject{{"known", false}, {"known_nil", false}});
}
