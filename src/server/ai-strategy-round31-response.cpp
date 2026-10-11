#include "ai-strategy-round31-response.h"
#include "ai-strategy-projection.h"
#include "ai.h"
#include "card.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <cmath>

void projectAIStrategyRound31Response(AIRequest &request, Room &room,
                                    ServerPlayer *viewer, const QVariant &data,
                                    QJsonObject &context)
{
    // sijyuoffline.lua:35953-35977: actual TargetConfirming -> on_cost ->
    // askForSkillInvoke(player, "sfofl_haiyou", ctx.original_data).
    if (!viewer || viewer->getRoom() != &room
        || request.kind != AIRequest::SkillInvoke
        || request.choiceOptions.reason != QStringLiteral("sfofl_haiyou")
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()
        || !viewer->hasSkill("sfofl_haiyou")
        || !data.canConvert<CardUseStruct>() || !context.value("use").isObject())
        return;
    const CardUseStruct use = data.value<CardUseStruct>();
    if (!use.card || !use.card->isKindOf("Slash") || !use.from
        || use.from->getRoom() != &room) return;
    QJsonObject row = context.value("use").toObject();
    row.remove("expected_jink_num");
    row.remove("source_expected_jink_error");
    if (!use.to.contains(viewer)) {
        // Source standard_cards-ai.lua:1140 never dereferences a tag entry for
        // a non-target viewer. No foreign tag read is necessary to return one.
        row.insert("expected_jink_num", 1);
        context.insert("use", row);
        return;
    }
    // The question recipient is a current target. Read only its required slots;
    // never export another player's count or the source's complete tag vector.
    const QStringList numbers = use.from->getTag("Jink_" + use.card->toString()).toStringList();
    double expected = 1;
    bool sourceError = false;
    LuaRuntime::Binding binding(room.roomRuntime()->lua(), false);
    lua_State *state = room.roomRuntime()->lua().state();
    if (!state) return;
    for (int index = 0; index < use.to.size(); ++index) {
        if (use.to[index] != viewer) continue;
        if (index >= numbers.size()) { sourceError = true; break; }
        const QByteArray value = numbers[index].toUtf8();
        // Lua C numeric conversion is the exact tonumber(string) operation,
        // including hexadecimal/exponents. It executes no Lua function or hook.
        lua_pushlstring(state, value.constData(), size_t(value.size()));
        int numeric = 0;
        const double number = lua_tonumberx(state, -1, &numeric);
        lua_pop(state, 1);
        if (!numeric) { sourceError = true; break; }
        if (!std::isfinite(number)) return; // JSON cannot attest nonfinite numbers.
        if (number == 0) { expected = 0; break; }
        if (number > expected) expected = number;
    }
    if (sourceError) row.insert("source_expected_jink_error", true);
    else row.insert("expected_jink_num", expected);
    context.insert("use", row);
}
