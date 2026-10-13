#include "room-runtime.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-animecard.h"
#include "ai.h"
#include "room.h"
#include "serverplayer.h"
#include "lua-wrapper.h"
#include <memory>
#include <QHash>
#include <QJsonArray>
#include <QObject>

namespace {
class ReijyuuWitness final : public AIRoomLedger {
public:
    explicit ReijyuuWitness(Room &room) : AIRoomLedger(room) {
        setObjectName(QStringLiteral("isolated-reijyuu-public-choice-witness"));
    }
    QHash<QString, QString> targets;
};
ReijyuuWitness *witness(Room &room, bool create) {
    for (QObject *child : room.children())
        if (auto *state = dynamic_cast<ReijyuuWitness *>(child)) return state;
    return create ? new ReijyuuWitness(room) : nullptr;
}
bool inRoom(Room &room, const ServerPlayer *player) {
    return player && room.findPlayerByObjectName(player->objectName(), true) == player;
}
}
void witnessAIStrategyReijyuuChoice(Room &room, const ServerPlayer *viewer,
                                  const ServerPlayer *target) {
    if (!inRoom(room, viewer)) return;
    ReijyuuWitness *state = witness(room, true);
    // A failed/invalid accepted result cannot leave an older target available.
    state->targets.remove(viewer->objectName());
    if (inRoom(room, target) && target->isAlive())
        state->targets.insert(viewer->objectName(), target->objectName());
}
void projectAIStrategyReijyuuChoice(Room &room, const ServerPlayer *viewer,
                                  AIRequest &request) {
    if (request.kind != AIRequest::Choice || request.choiceOptions.reason != QStringLiteral("reijyuu")
        || !inRoom(room, viewer) || request.viewerObjectName != viewer->objectName()) return;
    request.choiceOptions.context.remove(QStringLiteral("reijyuu_flagged_players"));
    const ReijyuuWitness *state = witness(room, false);
    if (!state || !state->targets.contains(viewer->objectName())) return;
    ServerPlayer *target = room.findPlayerByObjectName(state->targets.value(viewer->objectName()), true);
    // This is the public target of this viewer's actual accepted inquiry, not
    // an arbitrary scan of opponents' flags or a new hidden-information oracle.
    if (!target || !target->isAlive() || !target->hasFlag("reijyuuT")) return;
    request.choiceOptions.context.insert(QStringLiteral("reijyuu_flagged_players"),
                                        QJsonArray{target->objectName()});
}

void projectAIStrategyAnimecardCatalog(AIWorldView &world, Room &room,
                                      const ServerPlayer *viewer) {
    if (!inRoom(room, viewer)) return;
    // The attached view-as skill uses an old SkillCard parse spelling even
    // though the fixed package now registers its product as a BasicCard.
    // Report public registry metadata; do not manufacture a replacement card.
    if (([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(viewer),gameLua));
        return viewer->hasSkill(QStringLiteral("Promote_jiuyuanVS")); }())
        || ([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(viewer),gameLua));
        return viewer->hasSkill(QStringLiteral("Promote_jiuyuan")); }())) {
        std::unique_ptr<LuaSkillCard> prototype(
            LuaSkillCard::Parse(QStringLiteral("#Promote_jiuyuanCard:.:")));
        QJsonObject dragon = world.strategyFacts.value(QStringLiteral("dragon")).toObject();
        dragon.insert(QStringLiteral("promote_jiuyuan_skill_prototype"), bool(prototype));
        world.strategyFacts.insert(QStringLiteral("dragon"), dragon);
    }
    if (!([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(viewer),gameLua));
        return viewer->hasSkill(QStringLiteral("ark_guozai")); }())
        && !([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,const_cast<ServerPlayer *>(viewer),gameLua));
        return viewer->hasSkill(QStringLiteral("ark_zhuilie")); }())) return;
    QJsonObject prototypes;
    for (const QString &name : {QStringLiteral("#ark_guozai"), QStringLiteral("#ark_zhuilie")}) {
        // Public registered prototype metadata only. This never invokes an AI
        // callback or substitutes the modern V2 produced card for an old class.
        std::unique_ptr<LuaSkillCard> prototype(LuaSkillCard::Parse(name + QStringLiteral(":.:")));
        prototypes.insert(name, bool(prototype));
    }
    QJsonObject arknights = world.strategyFacts.value(QStringLiteral("arknights")).toObject();
    arknights.insert(QStringLiteral("legacy_prototypes"), prototypes);
    world.strategyFacts.insert(QStringLiteral("arknights"), arknights);
}
