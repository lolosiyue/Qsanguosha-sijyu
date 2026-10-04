#include "resolution-history.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "room.h"
#include "lua-wrapper.h"
#include "runtime-paths.h"
#include "settings.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QRegularExpression>
#include <lua.hpp>

#define CHECK(x) do { if (!(x)) qFatal("CHECK failed at %s:%d: %s",__FILE__,__LINE__,#x); } while(false)
static QByteArray typed(const QVariant &value) {
    QByteArray out; QDataStream stream(&out,QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_6); stream << value;
    CHECK(stream.status() == QDataStream::Ok); return out;
}
static QByteArray journal(bool sharing) {
    qputenv("QSAN_HISTORY_PAYLOAD_SHARING",sharing ? "1" : "0");
    ResolutionHistoryService history;
    history.beginRound();
    QList<QVariant> values{1,uint(1),qlonglong(1),qulonglong(1),0.0,-0.0,
                          QString(),QStringLiteral(""),QString(5000,'x')};
    QVariant deep = 1;
    for (int i=0;i<20;++i) deep = QVariantMap{{"nested",deep}};
    values << deep;
    QVariantList wide; for (int i=0;i<200;++i) wide << i;
    values << QVariant(wide);
    for (const auto &value : values) {
        const QVariantMap payload{{"value",value}};
        const auto first = history.beginEvent("skill",payload); history.finishEvent(first);
        const auto second = history.beginEvent("skill",payload);
        CHECK(first != second);
        CHECK(typed(history.event(first)["data"]) == typed(payload));
        CHECK(typed(history.event(second)["data"]) == typed(payload));
        CHECK(history.appendFact(second,"skill_invoked",payload) > 0);
        history.finishEvent(second);
    }
    // Exercise bounded-cache eviction, preserving every event and fact identity.
    for (int i=0;i<4200;++i) {
        const QVariantMap payload{{"skill","test"},{"instance",i}};
        const auto id = history.beginEvent("skill",payload);
        CHECK(history.appendFact(id,"skill_invoked",payload) > 0);
        history.finishEvent(id);
    }
    history.endRound();
    const auto snapshot = history.snapshot();
    const auto before = typed(snapshot.serialize());
    const auto id = history.beginEvent("skill",{{"value",99}});
    history.updateEvent(id,{{"value",100}}); history.finishEvent(id);
    CHECK(typed(snapshot.serialize()) == before);
    QString error; CHECK(history.restore(snapshot,&error));
    CHECK(typed(history.snapshot().serialize()) == before);
    ResolutionHistoryService detached;
    CHECK(detached.restore(snapshot,&error));
    const auto extra = detached.beginEvent("skill",{{"value",100}}); detached.finishEvent(extra);
    CHECK(typed(history.snapshot().serialize()) == before);
    return before;
}
static QByteArray read(const QString &path) {
    QFile file(path); CHECK(file.open(QIODevice::ReadOnly)); return file.readAll();
}
static void companion(const QString &root) {
    Room room(nullptr,"02p");
    EngineRuntimeContextScope scope(*Sanguosha,&room);
    LuaRuntime::Binding binding(room.roomRuntime()->lua());
    const QRegularExpression spec(R"rx(sgs\.CreateViewAsSkillV2\s*\{\s*name\s*=\s*"([^"]+)",\s*history_key\s*=\s*"([^"]+)")rx");
    for (const auto &item : {qMakePair(QString("OverseasVersion.lua"),31),qMakePair(QString("sgs10th.lua"),23)}) {
        auto matches = spec.globalMatch(QString::fromUtf8(read(root+"/extensions/"+item.first)));
        int count=0;
        while (matches.hasNext()) {
            const auto match=matches.next(); ++count;
            LuaViewAsSkillV2 skill(match.captured(1),Skill::NotFrequent,QString());
            skill.setHistoryKey(match.captured(2));
            ActiveSkillRequest request;
            CHECK(skill.historyKey(request) == match.captured(2));
        }
        CHECK(count == item.second);
    }
    const auto source=read(root+"/ai/OverseasVersion-ai.lua");
    const auto start=source.indexOf("sgs.ai_skill_invoke.ov_enyuan = function(self,data)");
    const auto end=source.indexOf("\nend\n",start);
    CHECK(start>=0 && end>start);
    const QByteArray test=source.mid(start,end+5-start)+R"lua(
local invoke = sgs.ai_skill_invoke.ov_enyuan
local flags = {}
local wounded = true
local friend = {isWounded=function() return wounded end}
local enemy = {isWounded=function() return true end}
local target = friend
local input = ""
local self = {
    player={hasFlag=function(_,name) return flags[name] end}, room={},
    isFriend=function(_,p) assert(p ~= nil); return p==friend end
}
local oldBeMan=BeMan
BeMan=function(room,name) assert(room==self.room and name=="target"); return target end
local data={
    toString=function() return input end,
    toPlayer=function() assert(input==""); return target end,
    toDamage=function() error("wrong QVariant conversion") end
}
flags.Damaged=true
assert(invoke(self,data)==false)
target=enemy; assert(invoke(self,data)==true)
flags.Damaged=false; flags.CardsMoveOneTime=true
assert(invoke(self,data)==false)
target=friend; assert(invoke(self,data)==true)
input="ov_enyuan1:target"; flags.Damaged=true
wounded=false; assert(invoke(self,data)==false)
wounded=true; assert(invoke(self,data)==true)
target=enemy; assert(invoke(self,data)==false)
BeMan=oldBeMan
)lua";
    auto *L=room.roomRuntime()->lua().rawState();
    CHECK(luaL_loadbuffer(L,test.constData(),test.size(),"ov_enyuan-input-regression")==LUA_OK);
    if (LuaRuntime::protectedCall(L,0,0,0)!=LUA_OK) qFatal("%s",lua_tostring(L,-1));
    qInfo() << "PASS recovered companion repairs: 31+23 history keys and actual ov_enyuan callback input cases";
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    CHECK(argc==2);
    CHECK(journal(true)==journal(false)); qunsetenv("QSAN_HISTORY_PAYLOAD_SHARING");
    qInfo() << "PASS payload sharing ON/OFF: typed values, identity, large/deep payloads, eviction, snapshot/restore isolation";
    QString error; CHECK(QSanRuntimePaths::resolve(app.arguments(),&error));
    CHECK(EngineBootstrap::initialize(false,&error));
    QObject::disconnect(&app,SIGNAL(aboutToQuit()),Sanguosha,SLOT(deleteLater()));
    Config.init(); companion(app.arguments().last());
    EngineBootstrap::shutdown(); return 0;
}
