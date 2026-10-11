#ifndef AI_STRATEGY_ROUND47E_CHOICE_H
#define AI_STRATEGY_ROUND47E_CHOICE_H
struct AIRequest;
class Room;
class ServerPlayer;
// Fixed qhstandard qhfireqinyin PlayerChosen reads this answering player's
// current public property. This is a snapshot fact, not retained history.
void projectAIRound47EQinyin(AIRequest &, Room &, ServerPlayer *);
#endif
