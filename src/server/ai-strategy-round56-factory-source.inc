// Included only by engine.cpp, sharing its original public definition maps.
#include "ai-strategy-round56-factory-source.h"
#include "room.h"
#include "room-runtime.h"
#include "room-definition-registry.h"
#include "card.h"
#include "tenyear.h"
#include <algorithm>
#include <typeinfo>
#include "ai-strategy-round56-equipment-recipes.inc"
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
    QHash<QString, const Card *> globalNames;
    QHash<QString, const QMetaObject *> globalMetas;
    QList<const Card *> originalEquipment;
    {
        QMutexLocker lock(&factoryRegistrySource->mutex);
        globalNames = Sanguosha->name2cards;
        globalMetas = Sanguosha->metaobjects;
        const auto origin = factoryRegistrySource->origins.constFind(Sanguosha);
        out.globalMapOrigin = origin != factoryRegistrySource->origins.cend() && origin->valid;
        if(out.globalMapOrigin) originalEquipment = origin->equipment;
    }
    out.roomMapOrigin = runtime->definitions().hasFactorySourceOrigin(room, runtime->lua().rawState());
    if(!out.globalMapOrigin) {
        out.firstUncoveredLuaSkillCloneSource=QStringLiteral("$frame: global factory-map source retired");
        return out;
    }
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
        cards("global/named/", globalNames);
        cards("runtime/named/", runtime->definitions().m_cardTemplates);
        auto names = globalMetas.keys(); std::sort(names.begin(), names.end());
        for (const auto &name : names) {
            AIRound56FactorySourceRow row;
            row.publicPath = QStringLiteral("global/metaobject/") + name;
            row.exactType = globalMetas.value(name) ? QStringLiteral("registered-QMetaObject") : QStringLiteral("<null>");
            row.source = QStringLiteral("src/core/engine.cpp:QMetaObject::newInstance(Suit,int) / newInstance(); constructor dispatch unproved");
            out.rows.push_back(row);
        }
        appendAIRound56LuaSkillFactorySources(runtime->definitions(), out);
        // All public alternatives of the fixed native cloneSkillCard recipe.
        // Exact RTTI precedes metaObject dispatch; no selected owner/private
        // skill state, current product value, or Lua prototype grants this.
        const QString zunweiName=QStringLiteral("ZunweiCard");
        const Card *zunweiRuntime=runtime->cardTemplate(zunweiName);
        const Card *zunweiGlobal=globalNames.value(zunweiName,nullptr);
        const QMetaObject *zunweiMeta=globalMetas.value(zunweiName,nullptr);
        const auto exactZunwei=[](const Card *c) {
            return !c||(typeid(*c)==typeid(ZunweiCard)&&c->metaObject()==&ZunweiCard::staticMetaObject);
        };
        out.zunweiSkillRecipeComplete=out.globalMapOrigin&&out.roomMapOrigin
            &&exactZunwei(zunweiRuntime)&&exactZunwei(zunweiGlobal)
            &&(!globalMetas.contains(zunweiName)||zunweiMeta==&ZunweiCard::staticMetaObject)
            &&(zunweiRuntime||zunweiMeta||zunweiGlobal);

        // Enumerate the whole constructor-issued equipment-name domain BEFORE
        // any selected name/holder/mark. Current equality never issues origin.
        out.equipmentRecipeFamilyComplete = out.globalMapOrigin && out.roomMapOrigin
            && !HegemonyMode::enabledForCatalog(true);
        out.globalHorseCallbackSourceComplete=out.globalMapOrigin;
        QSet<QString> seen;
        for(const Card *original : originalEquipment) {
            AIRound56EquipmentFactorySourceRow row;
            const auto originalRecipe = equipmentRecipe(original);
            if(!originalRecipe.meta || !isPublishedGlobalCard(original)) {
                row.reason = QStringLiteral("unknown or unpublished original equipment type");
                out.equipmentRows.append(row);out.equipmentRecipeFamilyComplete=false;continue;
            }
            const LuaFunction *horseCallback=nullptr;
            if(typeid(*original)==typeid(LuaHorse))horseCallback=&static_cast<const LuaHorse*>(original)->correct_func;
            else if(typeid(*original)==typeid(LuaOffensiveHorse))horseCallback=&static_cast<const LuaOffensiveHorse*>(original)->correct_func;
            else if(typeid(*original)==typeid(LuaDefensiveHorse))horseCallback=&static_cast<const LuaDefensiveHorse*>(original)->correct_func;
            if(horseCallback) {
                const bool source=horseCallback->hasOriginalNullSource();
                out.globalHorseCallbackSourceComplete &= source;
                if(source)++out.globalHorseOriginalNullCallbacks;
            }
            row.publicName=original->objectName();
            if(seen.contains(row.publicName))continue;
            seen.insert(row.publicName);
            const Card *resolved=runtime->cardTemplate(row.publicName);
            if(resolved && !equipmentRecipe(resolved).meta) {
                row.exactType=QString::fromLatin1(typeid(*resolved).name());
                row.reason=QStringLiteral("runtime resolver has an unreviewed exact equipment type");
                out.equipmentRows.append(row);out.equipmentRecipeFamilyComplete=false;continue;
            }
            if(resolved && isOriginalHegemonyCardPackage(resolved->getPackage())
                && row.publicName != QString::fromLatin1(resolved->metaObject()->className())) resolved=nullptr;
            row.roomLocal = resolved != nullptr;
            if(!resolved)resolved=globalNames.value(row.publicName,nullptr);
            // Preserve the original normal-mode null result for names that
            // exist only behind HEG aliases. Both maps have original writer
            // history; this is a source-derived resolver branch, not regrant.
            if(!resolved && !globalMetas.contains(row.publicName)) {
                row.exactType=QStringLiteral("original-null-result");
                row.nullResult=true;
                row.recipe=out.globalMapOrigin&&out.roomMapOrigin;
                row.reason=QStringLiteral("original named/meta resolver has no normal-mode product; callers guard null");
                out.equipmentRecipeFamilyComplete &= row.recipe;
                out.equipmentRows.append(row);continue;
            }
            const auto recipe=equipmentRecipe(resolved);
            row.exactType=resolved?QString::fromLatin1(typeid(*resolved).name()):QStringLiteral("no-template");
            // Public equipment names came from actual definitions, so failure
            // to resolve a template is not converted into a meta-only grant.
            row.recipe=recipe.meta && recipe.clonable && resolved->metaObject()==recipe.meta
                && (row.roomLocal ? out.roomMapOrigin && runtime->definitions().m_cardIds.contains(resolved)
                                      && !isPublishedGlobalCard(resolved)
                                  : out.globalMapOrigin && !recipe.luaCopy && isPublishedGlobalCard(resolved));
            row.reason=row.recipe?QStringLiteral("exact constructor/copy recipe; effects and lexical scope separate")
                :recipe.luaCopy&&!row.roomLocal?QStringLiteral("global Lua mutable template is not Room-confined")
                :!recipe.meta?QStringLiteral("resolved exact type absent from equipment recipes")
                :!recipe.clonable?QStringLiteral("original named dispatcher has no exact constructor recipe")
                :resolved->metaObject()!=recipe.meta?QStringLiteral("resolved metaobject differs from exact constructor recipe")
                :row.roomLocal?QStringLiteral("resolved runtime template has no original Room-local indexed custody")
                :QStringLiteral("resolved global native template has no original published custody");
            out.equipmentRecipeFamilyComplete &= row.recipe;
            out.equipmentRows.append(row);
        }

        out.remainingEffectObligations = {
            QStringLiteral("src/core/card.cpp:Card::Card: atomic ID allocation established; exact product custody remains separate"),
            QStringLiteral("QObject construction/setObjectName and C++/Qt allocation/error exits: product birth/custody receipt absent"),
            QStringLiteral("src/core/card.cpp:Card::setSkillName -> RoomDefinitionRegistry::setPrintedSkillName: fresh-product nondefinition provenance required"),
            QStringLiteral("LuaCallbackRef synchronized scalar tuple copies: callback retirement/capture mutation and future dispatch remain separate"),
            QStringLiteral("src/core/card.cpp:Card::~Card/deleteLater/event -> CardLifetimeManager: native custody/deferred deletion/userdata GC union unproved"),
            QStringLiteral("Engine named/metaobject/LuaBasicCard/LuaTrickCard/Lua equipment/physical clone and LuaSkillCard::Parse wrapper families remain unproved")
        };
        out.enumerationComplete = !runtime->isClosing() && runtime->lua().generation() == generation
            && hasAIRound56GlobalFactoryMapOrigin(Sanguosha)
            && runtime->definitions().hasFactorySourceOrigin(room, runtime->lua().rawState());
        if(!out.enumerationComplete) {
            out.equipmentRecipeFamilyComplete=false;
            out.zunweiSkillRecipeComplete=false;
            out.globalHorseCallbackSourceComplete=false;
        }
        if (!out.enumerationComplete) {
            out.luaSkillCloneSourceFamilyComplete = false;
            out.firstUncoveredLuaSkillCloneSource = QStringLiteral("$frame: public factory enumeration invalidated");
        }
    } catch (...) {
        out.enumerationComplete = out.luaSkillCloneSourceFamilyComplete = out.zunweiSkillRecipeComplete = false;
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

bool hasAIRound56GlobalFactoryMapOrigin(const Engine *engine) noexcept
{
    QMutexLocker lock(&factoryRegistrySource->mutex);
    const auto origin=factoryRegistrySource->origins.constFind(engine);
    return origin!=factoryRegistrySource->origins.cend()&&origin->valid;
}
