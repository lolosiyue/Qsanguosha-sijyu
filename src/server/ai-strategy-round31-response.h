#ifndef AI_STRATEGY_ROUND31_RESPONSE_H
#define AI_STRATEGY_ROUND31_RESPONSE_H
#include <QJsonObject>
class Room; class ServerPlayer; class QVariant; struct AIRequest;
// Finite actual sfofl_haiyou question only: the viewer's own source Jink
// requirement, or the ordinary source error at its reached missing slot.
void projectAIStrategyRound31Response(AIRequest &, Room &, ServerPlayer *,
                                    const QVariant &, QJsonObject &context);
#endif
