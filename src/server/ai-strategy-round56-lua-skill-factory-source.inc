// Included once from lua-wrapper.cpp: inspect the actual original global map.
#include "ai-strategy-round56-factory-source.h"
#include "room-definition-registry.h"
#include <algorithm>
#include <type_traits>
#include <typeinfo>
static_assert(std::is_same<decltype(&LuaSkillCard::clone), LuaSkillCard *(LuaSkillCard::*)() const>::value,
    "clone must remain the original nonoverloaded LuaSkillCard member");
static_assert(std::is_same<decltype(&LuaSkillCard::setSkillName), void(Card::*)(const QString &)>::value,
    "fresh clone setter must remain Card::setSkillName");
static_assert(std::is_trivially_copy_assignable<LuaCallbackRef>::value,
    "clone callback copies must not acquire registry/host effects");
static_assert(std::is_trivially_destructible<LuaCallbackRef>::value,
    "clone callback handle destruction must remain trivial");
void appendAIRound56LuaSkillFactorySources(const RoomDefinitionRegistry &definitions,
    AIRound56FactorySourceReport &out)
{
    bool complete = true; int count = 0;
    auto append = [&](const char *prefix, const auto &map) {
        auto names = map.keys(); std::sort(names.begin(), names.end());
        for (const auto &name : names) {
            ++count;
            const LuaSkillCard *card = map.value(name);
            AIRound56FactorySourceRow row;
            row.publicPath = QString::fromLatin1(prefix) + name;
            row.exactType = card ? QString::fromLatin1(typeid(*card).name()) : QStringLiteral("<null>");
            // Exact compiled type, never QObject inherits() or a name comparison.
            row.luaSkillCloneRecipe = card && typeid(*card) == typeid(LuaSkillCard);
            row.source = row.luaSkillCloneRecipe
                ? QStringLiteral("src/core/lua-wrapper.cpp:LuaSkillCard::clone -> LuaSkillCard(empty,empty) -> SkillCard::SkillCard -> Card::Card; setters + seven trivial LuaCallbackRef copies")
                : QStringLiteral("uncovered public LuaSkillCard clone exact type");
            if (!row.luaSkillCloneRecipe) {
                complete = false;
                if (out.firstUncoveredLuaSkillCloneSource.isEmpty())
                    out.firstUncoveredLuaSkillCloneSource = row.publicPath + QStringLiteral(" :: ") + row.exactType + QStringLiteral(" :: ") + row.source;
            }
            out.rows.push_back(row);
        }
    };
    append("global/lua-skill/", LuaSkillCards);
    append("runtime/lua-skill/", definitions.m_luaSkillCards);
    out.luaSkillCloneSourceFamilyComplete = complete && count > 0;
    if (!count) out.firstUncoveredLuaSkillCloneSource = QStringLiteral("public LuaSkillCard prototype family empty; no source witness");
}
