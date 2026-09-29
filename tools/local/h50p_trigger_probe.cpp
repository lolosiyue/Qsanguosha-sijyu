#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "room-runtime.h"
#include "room-roster.h"
#include "runtime-paths.h"
#include "serverplayer.h"
#include "lua-wrapper.h"
#include "invalidity-gate.h"
#include "lua.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <functional>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct RoomTestAccess {
    static void add(Room &room, ServerPlayer *player) { room.addPlayerToRoster(player); }
    static bool order(Room &room, ServerPlayer *player, QMap<ServerPlayer *, QStringList> &choices,
                      bool optional, QString &answer) {
        return room.decideAiTriggerOrder(player, "GameRule:TriggerOrder", {"first", "second"},
                                        choices, optional, 42, answer);
    }
    static void startup(Room &room, RoomThread &thread) {
        room.thread = &thread;
        thread.event_stack << EventTriplet(GameReady, &room, nullptr)
                           << EventTriplet(CardsMoveOneTime, &room, nullptr);
    }
    static void dispatch(RoomThread &thread, Room *room, TriggerSkill *skill) {
        QVariant data;
        QList<TriggerSkill *> skills{skill};
        thread.triggerV2Skills(GameReady, room, nullptr, data, &skills);
    }
};

class RecordProbe : public TriggerSkillV2 {
public:
    RecordProbe() : TriggerSkillV2("h50p_record_probe") {}
    bool collectTriggerContexts(TriggerEvent, Room *, ServerPlayer *, QVariant &,
                                QList<SkillContext> &) const override { return true; }
    void record(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        seen << ctx.owner->objectName() + '#' + QString::number(ctx.instanceID);
        if (callback) callback(ctx);
    }
    mutable QStringList seen;
    std::function<void(SkillContext &)> callback;
};

class OrderAI : public TrustAI {
public:
    OrderAI(ServerPlayer *p) : TrustAI(p) {}
    QString askForTriggerOrder(const QString &reason, QMap<ServerPlayer *, QStringList> &skills,
                               bool optional, const QVariant &data) override {
        ++calls;
        check(reason == "GameRule:TriggerOrder" && data.toInt() == 42, "legacy arguments retained");
        check(skills.size() == 1, "legacy ownership retained");
        return optional ? "cancel" : "second";
    }
    int calls = 0;
};

class BoardCounter : public MaxCardsSkill {
public:
    BoardCounter() : MaxCardsSkill("h50p_board_counter") {}
    int getExtra(const Player *) const override { ++calls; return 0; }
    mutable int calls = 0;
};

class InvalidityCounter : public InvaliditySkill {
public:
    InvalidityCounter(const QString &name) : InvaliditySkill(name) {}
    bool isSkillValid(const Player *, const Skill *) const override {
        ++calls;
        return false;
    }
    mutable int calls = 0;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString error;
    check(QSanRuntimePaths::resolve(app.arguments(), &error), qPrintable(error));
    Engine engine;
    auto *room = new Room(nullptr, "50p", GameSessionConfig(26092924),
                          Room::RuntimeInitializationPolicy::Deferred);
    QList<ServerPlayer *> players;
    for (int i = 0; i < 50; ++i) {
        auto *p = new ServerPlayer(room);
        p->setObjectName(QString("p%1").arg(i));
        RoomTestAccess::add(*room, p);
        players << p;
    }
    auto *markRule = new InvalidityCounter("h50p_mark_invalidity");
    auto *currentRule = new InvalidityCounter("h50p_current_invalidity");
    markRule->setProperty("InvalidityMarkPrefix", "h50p_mark_");
    currentRule->setProperty("InvalidityCurrentSiblingSkill", "h50p_current_skill");
    room->roomRuntime()->addSkills({markRule, currentRule});
    engine.registerRoom(room->roomRuntime());
    RecordProbe validityTarget;
    check(engine.correctSkillValidity(players[0], &validityTarget), "inactive invalidity gates");
    check(markRule->calls == 0 && currentRule->calls == 0, "inactive gates avoid callbacks");
    players[0]->setMark("h50p_mark_" + validityTarget.objectName(), 1);
    check(!engine.correctSkillValidity(players[0], &validityTarget) && markRule->calls == 1,
          "live positive mark runs callback");
    players[0]->setMark("h50p_mark_" + validityTarget.objectName(), 0);
    check(engine.correctSkillValidity(players[0], &validityTarget), "mark removal stays live");
    players[1]->setFlags("CurrentPlayer");
    check(engine.correctSkillValidity(players[0], &validityTarget), "flag without ownership");
    const int currentId = players[1]->createSkillInstance("h50p_current_skill", SourceAcquired);
    check(!engine.correctSkillValidity(players[0], &validityTarget) && currentRule->calls == 1,
          "live flag and attachment run callback");
    check(engine.correctSkillValidity(players[1], &validityTarget), "current owner excludes self");
    players[1]->setAlive(false);
    check(engine.correctSkillValidity(players[0], &validityTarget), "dead current sibling excluded");
    players[1]->setAlive(true);
    check(!engine.correctSkillValidity(players[0], &validityTarget), "revival visible");
    players[0]->setFlags("CurrentPlayer");
    check(engine.correctSkillValidity(players[2], &validityTarget), "first flagged sibling wins");
    players[0]->setFlags("-CurrentPlayer");
    players[1]->removeSkillInstance("h50p_current_skill", currentId);
    check(engine.correctSkillValidity(players[0], &validityTarget), "detachment visible");
    players[1]->setFlags("-CurrentPlayer");
    markRule->setProperty("InvalidityMarkPrefix", QVariant());
    check(!engine.correctSkillValidity(players[0], &validityTarget), "unconfigured callback retained");
    markRule->setProperty("InvalidityMarkPrefix", "h50p_mark_");
    engine.unregisterRoom();
    printf("PASS: actual Engine invalidity gates: live marks/flags/attachment/death/revival, self exclusion, first flag, unconfigured fallback\n");
    {
        LuaRuntime lua(LuaRuntime::Auxiliary);
        check(lua.initialize(&error), qPrintable(error));
        LuaRuntime::Binding binding(lua);
        check(lua.loadScript("lua/utilities.lua", &error), qPrintable(error));
        QFile source("extensions/sijyuoffline.lua");
        check(source.open(QIODevice::ReadOnly), "open real offline callbacks");
        const QByteArray code = source.readAll();
        LuaInvaliditySkill podai("h50p_podai", Skill::Compulsory);
        LuaInvaliditySkill ducai("h50p_ducai", Skill::Compulsory);
        const auto loadCallback = [&](LuaInvaliditySkill &rule, const QByteArray &name) {
            const int begin = code.indexOf(name + " = sgs.CreateInvaliditySkill{");
            const int end = code.indexOf("\n}", begin);
            check(begin >= 0 && end > begin, "locate real callback source");
            const QByteArray chunk = "sgs.CreateInvaliditySkill = function(spec) return spec end\n"
                + code.mid(begin, end + 2 - begin) + "\nreturn " + name + ".skill_valid";
            check(luaL_dostring(lua.state(), chunk.constData()) == LUA_OK, "load real callback");
            rule.skill_valid = LuaFunction(lua.state(), luaL_ref(lua.state(), LUA_REGISTRYINDEX));
        };
        loadCallback(podai, "sfofl_podaiInvalidity");
        loadCallback(ducai, "sfofl_ducaiInvalidity");
        podai.setProperty("InvalidityMarkPrefix", "sfofl_podai");
        ducai.setProperty("InvalidityCurrentSiblingSkill", "sfofl_ducai");
        const auto compare = [&] {
            for (ServerPlayer *p : players) {
                for (LuaInvaliditySkill *rule : {&podai, &ducai}) {
                    const bool original = rule->isSkillValid(p, &validityTarget);
                    const bool gated = !invalidityCallbackMayReject(rule, p, &validityTarget)
                        || rule->isSkillValid(p, &validityTarget);
                    check(original == gated, "native gate matches actual Lua callback");
                }
            }
        };
        compare();
        players[0]->setMark("sfofl_podai" + validityTarget.objectName(), 1);
        players[1]->setFlags("CurrentPlayer");
        const int id = players[1]->createSkillInstance("sfofl_ducai", SourceAcquired);
        compare();
        players[0]->setFlags("CurrentPlayer"); compare();
        players[0]->setFlags("-CurrentPlayer");
        players[1]->setAlive(false); compare();
        players[1]->setAlive(true); compare();
        players[1]->removeSkillInstance("sfofl_ducai", id); compare();
        players[1]->setFlags("-CurrentPlayer");
        players[0]->setMark("sfofl_podai" + validityTarget.objectName(), 0); compare();
        LuaTriggerSkillV2 noRecord("h50p_empty_record", Skill::Compulsory, "");
        QVariant data;
        check(noRecord.recordEvent(GameReady, room, players[0], data), "absent record callback skips owners");
        check(luaL_dostring(lua.state(), "return function() end") == LUA_OK, "record callback fixture");
        noRecord.on_record = LuaFunction(lua.state(), luaL_ref(lua.state(), LUA_REGISTRYINDEX));
        check(!noRecord.recordEvent(GameReady, room, players[0], data), "present record retains owner dispatch");
        printf("PASS: native gates match real sijyuoffline Lua callbacks for 50 players across seven live states; absent/present Lua V2 record dispatch\n");
    }
    RoomRoster roster;
    roster.replacePlayers(players);
    const auto rotated = roster.orderedFrom(players[20], true);
    check(rotated.first() == players[20] && rotated.last() == players[19], "rotation");
    check(roster.orderedFrom(players[20], true) == rotated, "repeat");
    check(roster.orderedFrom(players[21], true).first() == players[21], "current change");
    roster.remove(players[22]);
    check(!roster.orderedFrom(players[20], true).contains(players[22]), "removal");
    roster.insertAfter(players[20], players[22], false);
    check(roster.orderedFrom(players[20], true)[1] == players[22], "insertion");
    roster.replacePlayers(players);
    players[21]->setAlive(false);
    check(roster.orderedFrom(players[20], true).contains(players[21]), "include dead");
    check(!roster.orderedFrom(players[20], false).contains(players[21]), "alive query stays live");
    players[21]->setAlive(true);
    check(roster.orderedFrom(players[20], false).contains(players[21]), "revival");
    roster.swapSeats(players[20], players[21]);
    check(roster.orderedFrom(players[20], true)[1] == players[22], "seat swap invalidation");
    check(roster.orderedFrom(nullptr, false) == roster.players(), "null-current legacy behavior");

    auto *skill = new RecordProbe;
    room->roomRuntime()->addSkills({skill});
    RoomThread thread(room);
    const QString name = skill->objectName();
    players[0]->createSkillInstance(name, SourceAcquired);
    const int removed = players[40]->createSkillInstance(name, SourceAcquired);
    skill->callback = [&](SkillContext &ctx) {
        if (ctx.owner == players[0]) {
            players[30]->createSkillInstance(name, SourceAcquired);
            players[40]->removeSkillInstance(name, removed);
        }
    };
    RoomTestAccess::dispatch(thread, room, skill);
    check(skill->seen == QStringList({"p0#1", "p30#1"}), "live additions and removals during record");
    skill->callback = {};
    skill->seen.clear();
    players[0]->createSkillInstance(name, SourceAcquired);
    RoomTestAccess::dispatch(thread, room, skill);
    check(skill->seen == QStringList({"p0#1", "p0#2", "p30#1"}), "multiple instances and generation invalidation");
    skill->seen.clear();
    RoomTestAccess::dispatch(thread, room, skill);
    check(skill->seen == QStringList({"p0#1", "p0#2", "p30#1"}), "cached positive IDs retain order");
    players[0]->removeSkillInstance(name, 1);
    players[0]->removeSkillInstance(name, 2);
    players[30]->removeSkillInstance(name, 1);
    skill->seen.clear();
    RoomTestAccess::dispatch(thread, room, skill);
    check(skill->seen.isEmpty(), "empty ownership after detach");
    players[49]->createSkillInstance(name, SourceAcquired);
    RoomTestAccess::dispatch(thread, room, skill);
    check(skill->seen == QStringList({"p49#1"}), "attachment after cached empty ownership");
    auto *nestedRoom = new Room(nullptr, "02p", GameSessionConfig(26092925),
                                Room::RuntimeInitializationPolicy::Deferred);
    auto *nestedPlayer = new ServerPlayer(nestedRoom);
    nestedPlayer->setObjectName("nested");
    RoomTestAccess::add(*nestedRoom, nestedPlayer);
    auto *nestedSkill = new RecordProbe;
    nestedRoom->roomRuntime()->addSkills({nestedSkill});
    nestedPlayer->createSkillInstance(name, SourceAcquired);
    RoomThread nestedThread(nestedRoom);
    players[0]->createSkillInstance(name, SourceAcquired);
    skill->seen.clear();
    skill->callback = [&](SkillContext &ctx) {
        if (ctx.owner == players[0]) RoomTestAccess::dispatch(nestedThread, nestedRoom, nestedSkill);
    };
    RoomTestAccess::dispatch(thread, room, skill);
    check(skill->seen == QStringList({"p0#3", "p49#1"}), "outer ownership survives nested room dispatch");
    check(nestedSkill->seen == QStringList({"nested#1"}), "nested room uses its own owners");
    skill->callback = {};
    auto *board = new BoardCounter;
    room->roomRuntime()->addSkills({board});
    auto *ai = new OrderAI(players[0]);
    players[0]->setAI(ai);
    players[0]->setState("robot");
    players[0]->setOnsoleOwner(players[0]);
    RoomTestAccess::startup(*room, thread);
    QMap<ServerPlayer *, QStringList> choices{{players[0], {"first", "second"}}};
    QString answer;
    check(RoomTestAccess::order(*room, players[0], choices, true, answer) && answer == "cancel", "startup optional consent");
    check(RoomTestAccess::order(*room, players[0], choices, false, answer) && answer == "second", "startup mandatory order");
    check(ai->calls == 2 && board->calls == 0, "startup legacy route avoids snapshot build");
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 1000; ++i) RoomTestAccess::dispatch(thread, room, skill);
    printf("PASS: roster mutation, alive flags, V2 live record mutations, multiple instances, empty ownership, nested rooms, startup consent without snapshot; 1000 dispatches %.3f ms\n", timer.nsecsElapsed()/1e6);
    std::fflush(stdout);
    std::_Exit(0); // Native fixture deliberately does not exercise worker shutdown.
}
