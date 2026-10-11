#ifndef AI_STRATEGY_ROUND33_PLANNING_H
#define AI_STRATEGY_ROUND33_PLANNING_H
#include "ai.h"
class Room;
class ServerPlayer;
class Card;

// AI-only, fixed-source sfofl_zhaobing Play/own-pile Equip product. This does
// not add a name to the global native/human physical-product classifier.
bool aiRound33PlanningPhysicalScope(const AIRequest &, const QString &skill);
bool authorizeAIRound33PlanningPhysicalProduct(const AIRequest &, Room &,
    ServerPlayer *, const SkillInstanceRef &activation, const SkillInstanceRef &source,
    const QList<int> &selected, const Card *produced, QString *error = nullptr);

// Opaque native-only continuation; declaration permits an incomplete private
// shared_ptr member in CardUseStruct, with no SWIG or serialization access.
class AIRound33PhysicalUseProof;
class AIRound33PhysicalUseAccess {
public:
    static bool attach(const AIRequest &, Room &, ServerPlayer *,
        const AICardConversionView &, CardUseStruct &);
    // -1 means no AI proof: native human/legacy behavior stays untouched.
    // 0 is an invalid/stale AI continuation; 1 is exact live acceptance.
    static int validate(Room &, CardUseStruct &);
};
#endif
