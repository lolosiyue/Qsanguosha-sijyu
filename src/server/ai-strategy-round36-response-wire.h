#ifndef AI_STRATEGY_ROUND36_RESPONSE_WIRE_H
#define AI_STRATEGY_ROUND36_RESPONSE_WIRE_H
#include <QString>
#include <functional>
struct AIRequest;
struct AIResult;
class Room;
class ServerPlayer;
class Card;
// This result is final for the finite source-wire path, including a native
// rejection after the source tag was committed. Never run legacy fallback then.
struct AIOriginalResponseWireOutcome {
    bool applicable = false;
    bool committed = false;
    bool accepted = false;
    const Card *card = nullptr;
    QString error;
};
bool aiRound36OriginalWireSyntax(const QString &wire, int *physicalId = nullptr);
bool aiRound36OriginalWireRequest(const AIRequest &request);
AIOriginalResponseWireOutcome commitAIRound36OriginalResponseWire(
    Room &room, ServerPlayer *viewer, const AIRequest &request, const AIResult &result,
    const std::function<bool()> &commitPriorStrategyEffects);
#endif
