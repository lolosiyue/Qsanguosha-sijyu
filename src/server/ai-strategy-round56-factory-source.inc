// Included only by engine.cpp, sharing its original public definition maps.
#include "ai-strategy-round56-factory-source.h"
#include "room.h"
#include "room-runtime.h"
#include "room-definition-registry.h"
#include "card.h"
#include <algorithm>
#include <typeinfo>
AIRound56FactorySourceReport inspectAIRound56PublicFactorySources(Room &room)
{
    AIRound56FactorySourceReport out;
    auto *runtime = room.roomRuntime();
    if (!Sanguosha || !runtime || runtime->isClosing()
        || !runtime->lua().isCurrentThreadOwner() || !runtime->lua().rawState()
        || !Sanguosha->currentRoomContext()
        || Sanguosha->currentRoomContext()->roomRuntime() != runtime) {
        out.firstUncoveredLuaSkillCloneSource = QStringLiteral("$frame: original Room runtime binding unavailable");
        return out;
    }
    const auto generation = runtime->lua().generation();
    // No provider calls or reentrant native/Lua calls occur during enumeration.
    try {
        auto cards = [&](const char *prefix, const auto &map) {
            auto keys = map.keys(); std::sort(keys.begin(), keys.end());
            for (const auto &key : keys) {
                const Card *card = map.value(key);
                AIRound56FactorySourceRow row;
                row.publicPath = QString::fromLatin1(prefix) + key;
                row.exactType = card ? QString::fromLatin1(typeid(*card).name()) : QStringLiteral("<null>");
                row.source = QStringLiteral("src/core/engine.cpp:Engine::cloneCard / Engine::cloneSkillCard; exact factory recipe unresolved");
                out.rows.push_back(row);
            }
        };
        // Include shadowed global rows as well as every runtime template.
        // No current mode, named-card argument, selected owner or card filtering.
        cards("global/named/", Sanguosha->name2cards);
        cards("runtime/named/", runtime->definitions().m_cardTemplates);
        auto names = Sanguosha->metaobjects.keys(); std::sort(names.begin(), names.end());
        for (const auto &name : names) {
            AIRound56FactorySourceRow row;
            row.publicPath = QStringLiteral("global/metaobject/") + name;
            row.exactType = Sanguosha->metaobjects.value(name) ? QStringLiteral("registered-QMetaObject") : QStringLiteral("<null>");
            row.source = QStringLiteral("src/core/engine.cpp:QMetaObject::newInstance(Suit,int) / newInstance(); constructor dispatch unproved");
            out.rows.push_back(row);
        }
        appendAIRound56LuaSkillFactorySources(runtime->definitions(), out);
        out.remainingEffectObligations = {
            QStringLiteral("src/core/card.cpp:Card::Card: --cardId global birth writer; no preservation receipt"),
            QStringLiteral("QObject construction/setObjectName and C++/Qt allocation/error exits: product birth/custody receipt absent"),
            QStringLiteral("src/core/card.cpp:Card::setSkillName -> RoomDefinitionRegistry::setPrintedSkillName: fresh-product nondefinition provenance required"),
            QStringLiteral("LuaCallbackRef trivial handle copies: callback retirement/capture mutation and future dispatch remain separate"),
            QStringLiteral("src/core/card.cpp:Card::~Card/deleteLater/event -> CardLifetimeManager: native custody/deferred deletion/userdata GC union unproved"),
            QStringLiteral("Engine named/metaobject/LuaBasicCard/LuaTrickCard/Lua equipment/physical clone and LuaSkillCard::Parse wrapper families remain unproved")
        };
        out.enumerationComplete = !runtime->isClosing() && runtime->lua().generation() == generation;
        if (!out.enumerationComplete) {
            out.luaSkillCloneSourceFamilyComplete = false;
            out.firstUncoveredLuaSkillCloneSource = QStringLiteral("$frame: public factory enumeration invalidated");
        }
    } catch (...) {
        out.enumerationComplete = out.luaSkillCloneSourceFamilyComplete = false;
        out.firstUncoveredLuaSkillCloneSource = QStringLiteral("$frame: public factory enumeration threw; no proof");
    }
    return out;
}
bool proveAIRound56LuaSkillCloneSourceFamily(Room &room, QString *reason)
{
    const auto report = inspectAIRound56PublicFactorySources(room);
    const bool ok = report.enumerationComplete && report.luaSkillCloneSourceFamilyComplete;
    if (reason) *reason = ok ? QStringLiteral("exact public LuaSkillCard clone source family only; birth/writer/custody/GC effects unproved")
                            : report.firstUncoveredLuaSkillCloneSource;
    return ok;
}
