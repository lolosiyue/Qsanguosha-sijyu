#ifndef AI_STRATEGY_ROUND39_CONTEXT_H
#define AI_STRATEGY_ROUND39_CONTEXT_H
#include <QJsonObject>
struct AIRequest;
struct SkillContext;
struct JudgeStruct;
class Room;
class ServerPlayer;
class TriggerSkillV2;
// Native call-site capability, scoped around the admitted V2 cost/effect only.
// No gameplay mutations and no stored Room/tag/foreign-card lookup interface.
class AIRound39WendaoScope {
public:
    AIRound39WendaoScope(Room &, const TriggerSkillV2 *, SkillContext *);
    ~AIRound39WendaoScope();
    AIRound39WendaoScope(const AIRound39WendaoScope &) = delete;
    AIRound39WendaoScope &operator=(const AIRound39WendaoScope &) = delete;
private:
    friend void projectAIRound39Wendao(AIRequest &, Room &, ServerPlayer *);
    AIRound39WendaoScope *previous_;
    Room *room_ = nullptr;
    SkillContext *context_ = nullptr;
    JudgeStruct *judge_ = nullptr;
};
void projectAIRound39Wendao(AIRequest &, Room &, ServerPlayer *);
#endif
