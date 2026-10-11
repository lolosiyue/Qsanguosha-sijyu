// Included once from the already built native-types translation unit.
#include "ai-strategy-round56-activate-public-proof.h"
#include "ai-strategy-round47-xufa-proof.h"
#include "ai-strategy-round45-lua-inspect.h"
#include "room.h"
#include "room-runtime.h"
#include "engine.h"
#include "card.h"
#include "ai-strategy-round56-factory-source.h"
#include <algorithm>

QString AIRound56ActivatePublicSourceReport::firstUncoveredSource() const
{
    for (const auto &row : rows)
        if (row.status == AIRound56ActivateSourceStatus::Uncovered)
            return row.publicPath + QStringLiteral(" :: ") + row.role
                + QStringLiteral(" [") + row.exactType + QStringLiteral("] ")
                + row.source + QStringLiteral(" : ") + row.detail;
    return enumerationComplete ? QString() : QStringLiteral("public enumeration incomplete");
}

namespace {
using ActivateStatus = AIRound56ActivateSourceStatus;
struct ActivatePublicInspector {
    Room &room;
    lua_State *L;
    AIRound56ActivatePublicSourceReport report;
    QSet<const Skill *> activeDefinitions;

    void row(const QString &path, const Skill *skill, const char *role,
             ActivateStatus status, const QString &source, const QString &detail,
             int nargs = -1, const void *closure = nullptr)
    {
        AIRound56ActivateSourceRow r;
        r.publicPath = path;
        // RTTI spelling is diagnostic only. No string/type-name equality grants
        // a source class; all positive classes use exact compiled identities.
        const auto *native = skill ? aiRound56ActivateNativeTypeSource(skill) : nullptr;
        r.exactType = native ? QString::fromLatin1(native->typeSpelling)
            : skill ? QString::fromLatin1(typeid(*skill).name()) : QStringLiteral("public-family");
        r.role = QString::fromLatin1(role);
        r.source = source;
        r.detail = detail;
        r.nargs = nargs;
        r.definitionIdentity = reinterpret_cast<quintptr>(skill);
        r.closureIdentity = reinterpret_cast<quintptr>(closure);
        r.status = status;
        report.rows.push_back(r);
    }

    void callback(const QString &path, const Skill *skill, const LuaFunction &fn,
                  const char *role, int nargs, bool getterDomain, bool selectionDomain = false)
    {
        // An absent field does not prove the native fallback method. Those
        // methods always have independent rows below, including factory defaults.
        if (!fn.isValid()) {
            row(path, skill, role, ActivateStatus::NoAuthoredCallback,
                QStringLiteral("swig/luaskills.i"), QStringLiteral("absent authored field; native fallback obligation retained"), nargs);
            return;
        }
        if (fn.state() != L || fn.generation() != report.generation) {
            row(path, skill, role, ActivateStatus::Uncovered,
                QStringLiteral("src/core/lua-runtime.cpp:LuaCallbackRef"), QStringLiteral("foreign or expired public callback reference"), nargs);
            return;
        }
        const int top = lua_gettop(L);
        // Read the source registry reference without calling the warning/host
        // branch in LuaCallbackRef::push. Inspection never executes the closure.
        AIRound45Inspect::rawGeti(L, LUA_REGISTRYINDEX, fn.registryRef());
        const void *identity = lua_topointer(L, -1);
        QString source = QStringLiteral("non-Lua or missing callback source");
        const auto *value = AIRound45Inspect::value(L, -1);
        if (ttisLclosure(value)) {
            const Proto *proto = clLvalue(value)->p;
            // Never print arbitrary Proto source text: a load-string chunk
            // can store its entire body there. Route diagnostic labels only
            // through the finite public bootstrap source names. This name
            // match is diagnostic and cannot prove a closure or binding.
            struct PublicSourceName { const char *path; const char *sha; };
            static const PublicSourceName publicSources[] = {
#include "ai-strategy-round56-bootstrap-source-rows.inc"
            };
            source = QStringLiteral("unregistered-public-source");
            if (proto->source) {
                QByteArray hint(getstr(proto->source), qsizetype(tsslen(proto->source)));
                if (hint.startsWith('@')) hint.remove(0, 1);
                while (hint.startsWith("./")) hint.remove(0, 2);
                for (const auto &known : publicSources)
                    if (hint == known.path) { source = QString::fromLatin1(known.path); break; }
            }
            source += QStringLiteral(":") + QString::number(proto->linedefined)
                + QStringLiteral("-") + QString::number(proto->lastlinedefined);
        }
        QString why;
        const bool proved = lua_isfunction(L, -1) && (getterDomain
            ? proveAIRound56ActivateGetterFunction(room, L, -1, &why)
            : selectionDomain && proveAIRound56ActivateSelectionFunction(room, L, -1, role, &why));
        row(path, skill, role,
            proved ? (selectionDomain ? ActivateStatus::ReadOnlySelectionClosureProved : ActivateStatus::GetterClosureProved)
                   : ActivateStatus::Uncovered,
            source, proved ? QStringLiteral("callback source/Proto/captures only; native descendants remain separate")
                : (!getterDomain && !selectionDomain ? QStringLiteral("callback role has no complete source-effect/factory certificate")
                                 : why.isEmpty() ? QStringLiteral("getter source/Proto/capture proof failed") : why),
            nargs, identity);
        lua_settop(L, top);
    }

    void method(const QString &path, const Skill *skill, const char *role)
    {
        const auto *native = aiRound56ActivateNativeTypeSource(skill);
        if (native) {
            for (unsigned i = 0; i < native->methodCount; ++i) {
                const auto &method = native->methods[i];
                if (std::strcmp(method.method, role) != 0) continue;
                row(path, skill, role,
                    method.scalarBodyProved ? ActivateStatus::ScalarMethodBodyProved : ActivateStatus::Uncovered,
                    QString::fromLatin1(method.source),
                    method.scalarBodyProved
                        ? QStringLiteral("reviewed scalar body; exact C++ declaring-class static_assert; no caller/custody grant")
                        : QStringLiteral("source route located; complete body/descendant family not proved"));
                return;
            }
        }
        row(path, skill, role, ActivateStatus::Uncovered,
            native ? QString::fromLatin1(native->registrationFile) : QStringLiteral("compiled exact-type virtual method"),
            QStringLiteral("complete inherited implementation and descendant proof unavailable"));
    }

    void definition(const QString &path, const Skill *skill)
    {
        if (!skill) {
            row(path, nullptr, "definition", ActivateStatus::Uncovered,
                QStringLiteral("Engine::getSkill"), QStringLiteral("null public definition"));
            return;
        }
        if (activeDefinitions.contains(skill)) {
            row(path, skill, "definition-cycle", ActivateStatus::Uncovered,
                QStringLiteral("TriggerSkill::getViewAsSkill"), QStringLiteral("cyclic public component relation"));
            return;
        }
        activeDefinitions.insert(skill);
        struct LeaveDefinition { QSet<const Skill *> &set; const Skill *skill;
            ~LeaveDefinition() { set.remove(skill); } } leave{activeDefinitions, skill};
        // Existing bits classify their original metadata/correction domains
        // only. Here they provide census coverage, NEVER Activate method proof.
        const auto *native = aiRound56ActivateNativeTypeSource(skill);
        const bool catalogued = native != nullptr;
        row(path, skill, "exact-native-type",
            catalogued ? ActivateStatus::CataloguedType : ActivateStatus::Uncovered,
            native ? QString::fromLatin1(native->registrationFile) : QStringLiteral("compiled per-TU RTTI registrations"),
            catalogued ? QStringLiteral("catalogue membership only; no Activate method bit")
                       : QStringLiteral("unknown public native dynamic type"));
        if (dynamic_cast<const AmountSkillV2 *>(skill)) method(path, skill, "getBaseAmount");

        const auto *view = dynamic_cast<const ViewAsSkill *>(skill);
        if (view) {
            if (typeid(*view) == typeid(LuaViewAsSkillV2)) {
                const auto *v = static_cast<const LuaViewAsSkillV2 *>(view);
                callback(path, skill, v->can_activate, "can_activate", 2, true);
                callback(path, skill, v->can_select_card, "can_select_card", 3, false, true);
                callback(path, skill, v->card_selection_feasible, "card_selection_feasible", 2, false, true);
                callback(path, skill, v->create_card, "create_card", 2, false);
                callback(path, skill, v->get_amount_ref, "get_amount_ref", 2, false);
                callback(path, skill, v->get_usage_ref, "get_usage_ref", 2, false);
                callback(path, skill, v->allow_declaration, "allow_declaration", 3, false);
                callback(path, skill, v->build_card, "build_card", 3, false);
                callback(path, skill, v->can_select_target, "can_select_target", 4, false, true);
                callback(path, skill, v->targets_feasible, "targets_feasible", 3, false, true);
                // Conservatively retain product/effect routes until the owner
                // request boundary has proved they cannot execute inside it.
                callback(path, skill, v->on_cost, "on_cost", 4, false);
                callback(path, skill, v->on_pay, "on_pay", 4, false);
                callback(path, skill, v->on_effect, "on_effect", 2, false);
                callback(path, skill, v->on_effect_target, "on_effect_target", 2, false);
                callback(path, skill, v->on_effect_target, "on_effect_target", 3, false);
                callback(path, skill, v->on_effect_target_group, "on_effect_target_group", 3, false);
            } else if (typeid(*view) == typeid(LuaViewAsSkill)) {
                const auto *v = static_cast<const LuaViewAsSkill *>(view);
                callback(path, skill, v->enabled_at_play, "enabled_at_play", 2, true);
                callback(path, skill, v->enabled_at_response, "enabled_at_response", 3, true);
                callback(path, skill, v->enabled_at_nullification, "enabled_at_nullification", 2, true);
                callback(path, skill, v->view_filter, "view_filter", 3, false);
                callback(path, skill, v->view_as, "view_as", 2, false);
                callback(path, skill, v->should_be_visible, "should_be_visible", 2, false);
            } else if (typeid(*view) == typeid(LuaFilterSkill)) {
                const auto *v = static_cast<const LuaFilterSkill *>(view);
                callback(path, skill, v->view_filter, "view_filter", 2, false);
                callback(path, skill, v->view_as, "view_as", 2, false);
            }
            for (const char *role : {"isEnabledAtPlay", "isEnabledAtResponse", "isEnabledAtNullification",
                    "viewFilter", "viewAs", "shouldBeVisible", "isResponseOrUse", "getDialogInfo",
                    "getFrequency", "isEquipSkill", "isUsable", "getLimitScope", "getUsageRef",
                    "getMaxUsageLimit", "checkCustomUsage", "declarationReason", "declarationCandidates"})
                method(path, skill, role);
            if (dynamic_cast<const ViewAsSkillV2 *>(view))
                for (const char *role : {"canActivateRequest", "canActivate", "canSelectCard",
                        "cardSelectionFeasible", "createCard", "getAmountRef", "hasIndependentAIConversion",
                        "aiConversionStages", "allowDeclaration", "buildCard", "targetMode", "canSelectTarget",
                        "targetsFeasible", "willThrowSelectedCards", "historyKey", "cost", "pay",
                        "effect", "effectOnTarget", "effectOnTargetGroup", "targetEffectMode"})
                    method(path, skill, role);
        }
        // Nonvirtual public definition relation. Do not select via any player,
        // skill instance, availability or amount. Keep alias paths individually.
        if (const auto *trigger = dynamic_cast<const TriggerSkill *>(skill))
            if (const auto *child = trigger->getViewAsSkill())
                definition(path + QStringLiteral("/trigger-view-as"), child);
    }
};
}

AIRound56ActivatePublicSourceReport inspectAIRound56ActivatePublicSources(Room &room, lua_State *L)
{
    ActivatePublicInspector inspector{room, L, {}};
    auto *runtime = room.roomRuntime();
    if (!L || !runtime || runtime->isClosing() || runtime->lua().rawState() != L
        || !runtime->lua().isCurrentThreadOwner() || !Sanguosha || Sanguosha->currentRoom() != &room
        || !AIRound45Inspect::capacity(L, 4096)) {
        inspector.row(QStringLiteral("$frame"), nullptr, "public-definition-frame", ActivateStatus::Uncovered,
            QStringLiteral("RoomRuntime/LuaRuntime"), QStringLiteral("live owner-thread inspection frame unavailable"));
        return inspector.report;
    }
    const int top = lua_gettop(L);
    struct Restore { lua_State *L; int top; ~Restore() { lua_settop(L, top); } } restore{L, top};
    inspector.report.generation = runtime->lua().generation();
    inspector.report.definitionVersion = runtime->definitions().skillDefinitionVersion();
    try {
        QStringList names = Sanguosha->getSkillNames();
        std::sort(names.begin(), names.end());
        for (const QString &name : names)
            inspector.definition(QStringLiteral("skill/") + name, Sanguosha->getSkill(name));
        auto pindian = Sanguosha->getProhibitPindianSkills();
        std::stable_sort(pindian.begin(), pindian.end(), [](const ProhibitPindianSkill *a, const ProhibitPindianSkill *b) {
            return a && b ? a->objectName() < b->objectName() : !a && b;
        });
        for (const auto *skill : pindian) {
            const QString path = QStringLiteral("pindian/") + (skill ? skill->objectName() : QStringLiteral("<null>"));
            if (skill && typeid(*skill) == typeid(LuaProhibitPindianSkill))
                inspector.callback(path, skill, static_cast<const LuaProhibitPindianSkill *>(skill)->is_pindianprohibited,
                    "is_pindianprohibited", 3, false);
            inspector.method(path, skill, "isPindianProhibited");
        }
        // Public engine prototypes only. Do not call getCardNames: that helper
        // performs pattern/virtual policy work. Do not instantiate any factory.
        const int cards = Sanguosha->getCardCount();
        for (int id = 0; id < cards; ++id) {
            const Card *card = Sanguosha->getEngineCard(id);
            AIRound56ActivateSourceRow row;
            row.publicPath = QStringLiteral("engine-card/") + QString::number(id);
            row.exactType = card ? QString::fromLatin1(typeid(*card).name()) : QStringLiteral("<null>");
            row.role = QStringLiteral("factory/product/virtual-policy/lifetime");
            row.source = QStringLiteral("Engine::getEngineCard(public id)");
            row.detail = QStringLiteral("public prototype does not prove all named/metaobject/Lua clone factories or descendants");
            row.definitionIdentity = reinterpret_cast<quintptr>(card);
            inspector.report.rows.push_back(row);
        }
        const auto factories = inspectAIRound56PublicFactorySources(room);
        for (const auto &factory : factories.rows) {
            AIRound56ActivateSourceRow row;
            row.publicPath = QStringLiteral("factory/") + factory.publicPath;
            row.exactType = factory.exactType;
            row.role = QStringLiteral("original-factory-source-recipe");
            row.source = factory.source;
            row.status = factory.luaSkillCloneRecipe
                ? ActivateStatus::FactorySourceRecipeClassified : ActivateStatus::Uncovered;
            row.detail = QStringLiteral("source recipe only; global birth writer/custody/GC effects remain independent");
            inspector.report.rows.push_back(row);
        }
        if (!factories.enumerationComplete || !factories.luaSkillCloneSourceFamilyComplete)
            inspector.row(QStringLiteral("factory/$lua-skill-family"), nullptr, "complete-public-source-family",
                ActivateStatus::Uncovered, factories.firstUncoveredLuaSkillCloneSource,
                QStringLiteral("no selected prototype may replace the complete global+runtime union"));
        for (const auto &effect : factories.remainingEffectObligations)
            inspector.row(QStringLiteral("factory/$required-effect"), nullptr, "original-factory-effect-union",
                ActivateStatus::Uncovered, effect, QStringLiteral("no execution grant"));
        for (const char *role : {"named-metaobject-and-Lua-clone-factory-catalogue",
                "complete-correction-and-invalidity-family", "request-native-helper-and-host-effect-union",
                "callback-field-registry-capture-mutation-retirement", "fresh-product-birth-and-custody",
                "allocation-destructor-deferred-delete-userdata-GC-error-union",
                "source-specific-mark-property-tag-cache-writers", "original-collector-fill-mixed-sort-source-order"})
            inspector.row(QStringLiteral("$required-union"), nullptr, role, ActivateStatus::Uncovered,
                QStringLiteral("Activate makeRequest -> decideAiAction -> applyResult/commit"),
                QStringLiteral("independent complete source family not yet composed"));
        if (runtime->isClosing() || runtime->lua().rawState() != L
            || runtime->lua().generation() != inspector.report.generation
            || runtime->definitions().skillDefinitionVersion() != inspector.report.definitionVersion)
            inspector.row(QStringLiteral("$frame"), nullptr, "definition-frame-changed", ActivateStatus::Uncovered,
                QStringLiteral("RoomRuntime/LuaRuntime"), QStringLiteral("public enumeration invalidated"));
        else inspector.report.enumerationComplete = true;
    } catch (...) {
        inspector.row(QStringLiteral("$frame"), nullptr, "inspection-exception", ActivateStatus::Uncovered,
            QStringLiteral("public-source-inspector"), QStringLiteral("enumeration aborted; all execution authority remains unavailable"));
    }
    return inspector.report;
}
