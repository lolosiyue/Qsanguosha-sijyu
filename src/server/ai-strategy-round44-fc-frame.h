#ifndef AI_STRATEGY_ROUND44_FC_FRAME_H
#define AI_STRATEGY_ROUND44_FC_FRAME_H
#include <QtGlobal>
class Room; class ServerPlayer; class QVariant; struct AIRequest; struct CardUseStruct;
// Native dispatch lifetime only. No copied Room tag may create this scope.
class AIRound44FCUseFrame {
public:
 AIRound44FCUseFrame(Room &, int event, ServerPlayer *eventTarget, const QVariant &);
 ~AIRound44FCUseFrame();
 AIRound44FCUseFrame(const AIRound44FCUseFrame &) = delete;
 AIRound44FCUseFrame &operator=(const AIRound44FCUseFrame &) = delete;
private:
 Room *m_room = nullptr;
 quint64 m_serial = 0;
};
void clearAIRound44FCUseFrames(Room &);
void bindAIRound44FCUseFrame(AIRequest &, Room &, const ServerPlayer *);
// 0: old unscoped request; +1: exact current owner frame; -1: scoped rejection.
int aiRound44FCUseFrameWitness(const AIRequest &, Room &, const ServerPlayer *, const CardUseStruct &);
#endif
