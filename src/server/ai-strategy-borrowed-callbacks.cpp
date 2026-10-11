#include "ai-strategy-borrowed-callbacks.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua.hpp"
#include <QJsonArray>
#include <QScopeGuard>

namespace {
struct Origin {const char *registry,*key,*source;int line,last;};
const Origin origins[]={
#include "ai-strategy-borrowed-origins.inc"
};
bool matchedOrigin(const char *registry,const QString &key,const lua_Debug &info)
{
    for(const Origin &o:origins)
        if(qstrcmp(o.registry,registry)==0 && key==QString::fromUtf8(o.key)
           && qstrcmp(o.source,info.source)==0 && o.line==info.linedefined
           && o.last==info.lastlinedefined) return true;
    return false;
}

// Metadata only: never invoke __index, a callback, or a userdata conversion.
// A non-plain registry is explicitly unknown, including apparent raw absence.
bool registry(lua_State *L, const char *name)
{
    lua_pushglobaltable(L); lua_pushliteral(L,"sgs"); lua_rawget(L,-2);
    if (!lua_istable(L,-1)) return false;
    lua_pushstring(L,name); lua_rawget(L,-2);
    return lua_istable(L,-1) && !lua_getmetatable(L,-1);
}
QJsonObject callback(lua_State *L,const char *name,const QString &key)
{
    const int top=lua_gettop(L);
    const auto restore=qScopeGuard([&]{lua_settop(L,top);});
    QJsonObject result{{"known",false}};
    if (!registry(L,name)) return result;
    const QByteArray bytes=key.toUtf8();
    lua_pushlstring(L,bytes.constData(),size_t(bytes.size())); lua_rawget(L,-2);
    const int type=lua_type(L,-1);
    result.insert("known",true);
    result.insert("type",QString::fromLatin1(lua_typename(L,type)));
    if(type==LUA_TBOOLEAN) result.insert("value",bool(lua_toboolean(L,-1)));
    if(type==LUA_TFUNCTION) {
        lua_Debug info{};lua_pushvalue(L,-1);
        if (!lua_getinfo(L,">S",&info) || !info.source || info.what[0]=='C') {
            result.insert("known",false);return result;
        }
        result.insert("source",QString::fromUtf8(info.source));
        result.insert("line",info.linedefined);
        result.insert("matched",matchedOrigin(name,key,info));
    }
    return result;
}
bool roster(lua_State *L,QJsonArray &names)
{
    const int top=lua_gettop(L);
    const auto restore=qScopeGuard([&]{lua_settop(L,top);});
    if(!registry(L,"ai_skills")) return false;
    const int list=lua_absindex(L,-1);
    for(int index=1;index<=4097;++index) {
        lua_rawgeti(L,list,index);
        if(lua_isnil(L,-1)) return true; // exact ipairs termination
        if(index>4096 || !lua_istable(L,-1) || lua_getmetatable(L,-1)) return false;
        lua_pushliteral(L,"name");lua_rawget(L,-2);
        if(lua_type(L,-1)!=LUA_TSTRING) return false;
        size_t length=0;const char *name=lua_tolstring(L,-1,&length);
        if(length>4096) return false;
        names.append(QString::fromUtf8(name,int(length)));lua_pop(L,2);
    }
    return false;
}
}
void projectAIRound39BorrowedCallbacks(AIWorldView &world,Room &room,ServerPlayer *viewer)
{
    if(!viewer || viewer->getRoom()!=&room || world.self.objectName!=viewer->objectName()) return;
    lua_State *L=room.roomRuntime()->lua().state();if(!L)return;
    const int top=lua_gettop(L);
    const auto restore=qScopeGuard([&]{lua_settop(L,top);});
    if(viewer->hasSkill("chenbiyi",true)) {
        QJsonObject facts=world.strategyFacts.value("shixinrumo_yi").toObject();
        const QString key=viewer->property("chenbiyi_skill").toString();
        if(key.toUtf8().size()<=4096) {
            facts.insert("chenbiyi_skill",key);QJsonObject groups;
            for(const char *name:{"ai_fill_skill","ai_guhuo_card","ai_cardsview"})
                groups.insert(QString::fromLatin1(name),QJsonObject{{key,callback(L,name,key)}});
            facts.insert("chenbiyi_callback_presence",groups);
        }
        world.strategyFacts.insert("shixinrumo_yi",facts);
    }
    if(viewer->hasSkill("kuilei",true)) {
        QJsonObject facts=world.strategyFacts.value("legends").toObject();
        const QString record=viewer->getTag("KuileiSkills").toString();
        if(record.toUtf8().size()<=4096)facts.insert("KuileiSkills",record);
        QJsonArray names;if(roster(L,names))facts.insert("source_fill_skills",names);
        world.strategyFacts.insert("legends",facts);
    }
}
