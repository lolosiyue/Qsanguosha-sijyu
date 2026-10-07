#include "engine-bootstrap.h"
#include "engine.h"
#include "external-agent.h"
#include "external-agent-transport.h"
#include "lua-runtime.h"
#include "protocol/protocol-v2-codec.h"
#include "room.h"
#include "runtime-paths.h"
#include "server-info.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <lua.hpp>

#define CHECK(x) do { if (!(x)) qFatal("CHECK failed at %s:%d: %s", __FILE__, __LINE__, #x); } while (false)

static void runLua(lua_State *state, const char *script)
{
    if (luaL_dostring(state, script) != LUA_OK)
        qFatal("Lua binding failure: %s", lua_tostring(state, -1));
}

static QVariantList payloads(const QList<QByteArray> &packets)
{
    QVariantList result;
    for (const QByteArray &packet : packets) {
        QSanProtocol::ProtocolMessage message;
        CHECK(QSanProtocol::ProtocolV2Codec().decode(packet, &message).success);
        result << QVariantMap{{"command", message.command}, {"payload", message.payload}};
    }
    return result;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString error;
    CHECK(QSanRuntimePaths::resolve(app.arguments(), &error));
    CHECK(EngineBootstrap::initialize(false, &error));
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    Config.init();
    Config.EnableHegemony = false;
    ServerInfo.EnableHegemony = false;
    {
        Room room(nullptr, "02p");
        EngineRuntimeContextScope context(*Sanguosha, &room);
        LuaRuntime::Binding binding(room.roomRuntime()->lua());
        auto *owner = room.addAIPlayer(); owner->setObjectName("state_owner");
        auto *viewer = room.addAIPlayer(); viewer->setObjectName("state_viewer");
        owner->setGeneral(Sanguosha->getGeneral("caocao"));
        viewer->setGeneral(Sanguosha->getGeneral("caocao"));
        Sanguosha->addSkills({new Skill("state_root"), new AttackRangeSkillV2("#state_child"),
                             new AttackRangeSkillV2("#state_other"), new Skill("#state_noncorrect")});
        const int first = owner->createSkillInstance("state_root", SourceAcquired);
        const int second = owner->createSkillInstance("state_root", SourceAcquired);
        const int child = owner->createSkillInstance("#state_child", SourceHelper, "state_root", first);
        const int sibling = owner->createSkillInstance("#state_child", SourceHelper, "state_root", second);
        const int other = owner->createSkillInstance("#state_other", SourceHelper, "state_root", first);
        const int attached = owner->createSkillInstance("#state_child", SourceAttached, "state_root", first);
        owner->createSkillInstance("#state_noncorrect", SourceHelper, "state_root", first);
        const int foreignRoot = viewer->createSkillInstance("state_root", SourceAcquired);
        const int foreignChild = viewer->createSkillInstance("#state_child", SourceHelper, "state_root", foreignRoot);

        QList<QByteArray> ownerPackets, viewerPackets;
        QObject::connect(owner, &ServerPlayer::message_ready, &room,
                         [&](const QByteArray &packet) { ownerPackets << packet; });
        QObject::connect(viewer, &ServerPlayer::message_ready, &room,
                         [&](const QByteArray &packet) { viewerPackets << packet; });
        CHECK(owner->getSkillInstanceStateStringList("state_root", first, "missing").isEmpty());
        CHECK(!owner->setSkillInstanceStateStringList("state_root", 999, "targets", {"x"}));
        CHECK(!owner->setSkillInstanceStateStringList("state_root", first, "", {"x"}));
        CHECK(owner->setSkillInstanceStateStringList("state_root", first, "targets", {"private-state-secret", "a|b", "花色"}));
        CHECK(owner->getSkillInstanceStateValue("state_root", first, "targets").userType() == QMetaType::QStringList);
        CHECK(owner->getSkillInstanceStateStringList("state_root", first, "targets").size() == 3);
        CHECK(ownerPackets.size() == 1 && viewerPackets.isEmpty());
        CHECK(ownerPackets.first().contains("private-state-secret"));
        owner->setSkillInstanceStateValue("state_root", first, "scalar", QString("a,b"));
        CHECK(owner->getSkillInstanceStateStringList("state_root", first, "scalar").isEmpty());
        owner->setSkillInstanceStateValue("state_root", first, "flag", false);
        owner->setSkillInstanceStateValue("state_root", first, "number", 0);
        owner->setSkillInstanceStateValue("state_root", first, "untouched", 17);
        ownerPackets.clear();
        CHECK(owner->removeSkillInstanceStateKeys("state_root", first, {"flag", "flag", "number", "missing", ""}) == 2);
        CHECK(ownerPackets.size() == 2 && viewerPackets.isEmpty());
        CHECK(owner->getSkillInstanceStateValue("state_root", first, "untouched").toInt() == 17);
        CHECK(owner->setSkillInstanceStateStringList("state_root", first, "targets", {}));
        CHECK(!owner->getSkillInstanceState("state_root", first).contains("targets"));

        ownerPackets.clear();
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "count", 3) == 1);
        CHECK(owner->getSkillInstanceCorrectStateValue("#state_child", child, "count").toInt() == 3);
        for (int excluded : {sibling, attached})
            CHECK(owner->getSkillInstanceCorrectState("#state_child", excluded).isEmpty());
        CHECK(owner->getSkillInstanceCorrectState("#state_other", other).isEmpty());
        CHECK(viewer->getSkillInstanceCorrectState("#state_child", foreignChild).isEmpty());
        CHECK(!ownerPackets.isEmpty() && viewerPackets.isEmpty());
        // Same recipients and wire payloads as the old explicit child-ref call.
        ownerPackets.clear();
        CHECK(room.setSkillInstanceCorrectState(owner,
            SkillInstanceRef(owner->objectName(), SkillInstanceKey("#state_child", child)), "count", 3));
        const QVariantList oldPayloads = payloads(ownerPackets);
        ownerPackets.clear();
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "count", 3) == 1);
        CHECK(payloads(ownerPackets) == oldPayloads && viewerPackets.isEmpty());
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", first, "#state_noncorrect", "count", 3) == 0);
        CHECK(room.setChildSkillInstanceCorrectState(nullptr, "state_root", first, "#state_child", "count", 3) == 0);
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", 999, "#state_child", "count", 3) == 0);
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", first, "", "count", 3) == 0);
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "", 3) == 0);
        ServerPlayer alien(&room); alien.setObjectName(owner->objectName());
        CHECK(room.setChildSkillInstanceCorrectState(&alien, "state_root", first, "#state_child", "count", 3) == 0);
        CHECK(room.removeChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "count") == 1);
        CHECK(room.removeChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "count") == 0);
        CHECK(owner->getSkillInstanceCorrectState("#state_child", child).isEmpty());
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "flag", false) == 1);
        CHECK(owner->getSkillInstanceCorrectStateValue("#state_child", child, "flag").userType() == QMetaType::Bool);
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "zero", 0) == 1);
        CHECK(owner->getSkillInstanceCorrectState("#state_child", child).contains("zero"));
        CHECK(room.removeChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "flag") == 1);
        CHECK(room.removeChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "zero") == 1);

        // Real generated SWIG methods, inherited by ServerPlayer; no mock objects.
        auto *state = room.getLuaState();
        lua_pushinteger(state, first); lua_setglobal(state, "state_iid");
        runLua(state, R"lua(
local room = sgs.Sanguosha:currentRoom()
local p = room:findPlayerByObjectName("state_owner", true)
assert(p:setSkillInstanceStateStringList("state_root", state_iid, "targets", {"a|b", "花色"}))
local values = p:getSkillInstanceStateStringList("state_root", state_iid, "targets")
assert(type(values) == "table" and #values == 2 and values[1] == "a|b" and values[2] == "花色")
assert(not pcall(function() p:setSkillInstanceStateStringList("state_root", state_iid, "targets", "a|b") end))
assert(not pcall(function() p:setSkillInstanceStateStringList("state_root", state_iid, "targets", {1}) end))
assert(not pcall(function() p:setSkillInstanceStateStringList("state_root", state_iid, "targets", {false}) end))
assert(not pcall(function() p:setSkillInstanceStateStringList("state_root", state_iid, "targets", {[2]="hole"}) end))
assert(not pcall(function() p:setSkillInstanceStateStringList("state_root", state_iid, "targets", {named="x"}) end))
values = p:getSkillInstanceStateStringList("state_root", state_iid, "targets")
assert(#values == 2 and values[1] == "a|b" and values[2] == "花色")
assert(p:removeSkillInstanceStateKeys("state_root", state_iid, {"targets", "targets"}) == 1)
assert(#p:getSkillInstanceStateStringList("state_root", state_iid, "targets") == 0)
assert(p:setSkillInstanceStateStringList("state_root", state_iid, "targets", {"a\0b", "", "same", "same"}))
values = p:getSkillInstanceStateStringList("state_root", state_iid, "targets")
assert(#values == 4 and values[1] == "a\0b" and #values[1] == 3 and values[2] == "" and values[3] == values[4])
assert(p:setSkillInstanceStateStringList("state_root", state_iid, "targets", {}))
assert(room:setChildSkillInstanceCorrectState(p, "state_root", state_iid, "#state_child", "count", sgs.QVariant(4)) == 1)
assert(room:removeChildSkillInstanceCorrectState(p, "state_root", state_iid, "#state_child", "count") == 1)
)lua");

        const int duplicateChild = owner->createSkillInstance("#state_child", SourceHelper, "state_root", first);
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "count", 5) == 2);
        CHECK(owner->getSkillInstanceCorrectStateValue("#state_child", duplicateChild, "count").toInt() == 5);
        CHECK(room.removeChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "count") == 2);

        owner->setSkillInstanceStateStringList("state_root", first, "targets", {"private-state-secret"});
        AIRequest request; request.worldView = room.buildAIWorldView(viewer);
        QByteArray observation = QJsonDocument(externalAgentRequestJson(request)).toJson();
        CHECK(!observation.contains("private-state-secret"));
        CHECK(!observation.contains("#state_child"));
        // Hegemony concealed source: existing projections remain authoritative.
        Config.EnableHegemony = true;
        ServerInfo.EnableHegemony = true;
        owner->addSkill("state_root", true);
        const int concealed = owner->getSkillInstanceIds("state_root").last();
        CHECK(owner->findSkillInstance("state_root", concealed)->bindHead == 1);
        const int concealedChild = owner->createSkillInstance("#state_child", SourceHelper, "state_root", concealed);
        viewerPackets.clear();
        owner->setSkillInstanceStateStringList("state_root", concealed, "targets", {"concealed-state-secret"});
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", concealed, "#state_child", "count", 99) == 1);
        CHECK(viewerPackets.isEmpty());
        CHECK(owner->getSkillInstanceCorrectStateValue("#state_child", concealedChild, "count").toInt() == 99);
        request.worldView = room.buildAIWorldView(viewer);
        observation = QJsonDocument(externalAgentRequestJson(request)).toJson();
        CHECK(!observation.contains("#state_child") && !observation.contains("concealed-state-secret"));
        for (const auto &playerView : request.worldView.players) {
            if (playerView.objectName != owner->objectName()) continue;
            for (const auto &skillView : playerView.skills)
                CHECK(skillView.skillName != "state_root" || skillView.instanceId != concealed);
        }
        owner->setGeneralShowed(true);
        request.worldView = room.buildAIWorldView(viewer);
        bool revealedRoot = false;
        for (const auto &playerView : request.worldView.players) {
            if (playerView.objectName != owner->objectName()) continue;
            for (const auto &skillView : playerView.skills)
                revealedRoot |= skillView.skillName == "state_root" && skillView.instanceId == concealed;
        }
        CHECK(revealedRoot);
        CHECK(!QJsonDocument(externalAgentRequestJson(request)).toJson().contains("concealed-state-secret"));
        Config.EnableHegemony = false;
        ServerInfo.EnableHegemony = false;

        CHECK(room.detachSkillFromPlayer(owner, SkillInstanceUtils::formatName("state_root", first),
                                          false, false, false) == first);
        CHECK(!owner->hasSkillInstance("state_root", first));
        CHECK(!owner->hasSkillInstance("#state_child", child));
        CHECK(!owner->hasSkillInstance("#state_child", duplicateChild));
        CHECK(owner->hasSkillInstance("state_root", second));
        CHECK(owner->hasSkillInstance("#state_child", sibling));
        CHECK(owner->getSkillInstanceStateStringList("state_root", first, "targets").isEmpty());
        CHECK(room.setChildSkillInstanceCorrectState(owner, "state_root", first, "#state_child", "count", 1) == 0);
        const int recreated = owner->createSkillInstance("state_root", SourceAcquired);
        CHECK(recreated > second && owner->getSkillInstanceState("state_root", recreated).isEmpty());
    }
    EngineBootstrap::shutdown();
    qInfo("PASS skill state conveniences: types, SWIG, instance isolation, owner-only sync, concealed projections, lifecycle");
}
