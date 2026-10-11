#include "ai-strategy-yjcm2012.h"
#include "ai.h"
#include "engine.h"
#include "standard.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua.hpp"
#include <QScopeGuard>
#include <memory>
#include <cmath>
#include <limits>

namespace {
// The source uses sgs.weapon_range[class] or 1, not Weapon::getRange().
// A table metatable could synthesize a value: never run that callback here.
bool sourceWeaponRange(Room &room, const Card *weapon, double &range)
{
    lua_State *state = room.roomRuntime()->lua().state();
    if (!state || !weapon) return false;
    const int top = lua_gettop(state);
    const auto restore = qScopeGuard([&] { lua_settop(state, top); });
    lua_pushglobaltable(state); lua_pushliteral(state, "sgs"); lua_rawget(state, -2);
    if (!lua_istable(state, -1)) return false;
    lua_pushliteral(state, "weapon_range"); lua_rawget(state, -2);
    if (!lua_istable(state, -1)) return false;
    const bool meta = lua_getmetatable(state, -1) != 0;
    if (meta) return false;
    const QByteArray key = weapon->getClassName().toUtf8();
    lua_pushlstring(state, key.constData(), size_t(key.size())); lua_rawget(state, -2);
    if (lua_isnil(state, -1) || (lua_isboolean(state, -1) && !lua_toboolean(state, -1))) {
        range = 1;
        return true;
    }
    if (!lua_isnumber(state, -1)) return false;
    range = lua_tonumber(state, -1);
    return std::isfinite(range);
}
}

void projectAIStrategyYjcm2012(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || world.self.objectName != viewer->objectName()) return;
    const QVariant color = viewer->getTag("qianxi");
    const QVariant optional = viewer->getTag("SecondMijiOptional");
    QJsonObject facts{
        {"qianxi_color_present", color.isValid()}, {"qianxi_color", color.toString()},
        {"second_miji_optional_present", optional.isValid()}, {"second_miji_optional", optional.toBool()}};
    ServerPlayer *dying = room.getCurrentDyingPlayer();
    facts.insert("current_dying_present", dying != nullptr);
    facts.insert("current_dying", dying ? dying->objectName() : QString());
    if (dying) {
        // No held card is inspected: this is the exact fresh dummy used by
        // Chunlao's source, and only the public rule answer is disclosed.
        const std::unique_ptr<Card> analeptic(Sanguosha->cloneCard("analeptic", Card::NoSuit, 0));
        if (analeptic) facts.insert("dying_analeptic_locked", dying->isLocked(analeptic.get()));
    }
    if (viewer->hasSkill("qice") && world.strategyFacts.value("pattern_names").isArray()) {
        QJsonObject patterns;
        for (const QJsonValue &value : world.strategyFacts.value("pattern_names").toArray()) {
            const QString name = value.toString();
            const std::unique_ptr<Card> clone(Sanguosha->cloneCard(name, Card::NoSuit, 0));
            patterns.insert(name, QJsonObject{{"exists", bool(clone)}, {"nd_trick", clone && clone->isNDTrick()}});
        }
        facts.insert("qice_patterns", patterns);
    }
    if (const Card *weapon = viewer->getWeapon()) {
        double sourceRange = 0;
        if (sourceWeaponRange(room, weapon, sourceRange)) {
            const double fix = sourceRange - viewer->getAttackRange(false);
            // The fixed SWIG Player::distanceTo wrapper casts lua_tonumber to
            // int (truncation toward zero), including fractional metadata.
            // Values outside C++ int would be undefined in the donor binding.
            if (std::isfinite(fix) && fix >= std::numeric_limits<int>::min()
                && fix <= std::numeric_limits<int>::max()) {
                QJsonObject distances;
                for (ServerPlayer *target : room.getAllPlayers())
                    distances.insert(target->objectName(), viewer->distanceTo(target, int(fix)));
                facts.insert("jiefan_weapon_range", sourceRange);
                facts.insert("jiefan_range_fix", fix);
                facts.insert("jiefan_distances", distances);
            }
        }
    }
    world.strategyFacts.insert("yjcm2012", facts);
}
