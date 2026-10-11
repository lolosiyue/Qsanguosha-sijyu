#ifndef AI_STRATEGY_NESTED_CARDASK_H
#define AI_STRATEGY_NESTED_CARDASK_H
#include <QJsonObject>
#include <QString>
class Room;
class ServerPlayer;
struct AIRequest;
// Read-only classification for the fixed Aocai askForCard(pattern,prompt,1)
// call. No Card descriptor, authority receipt or material identity is returned.
QJsonObject inspectAINestedCardaskString(Room &, ServerPlayer *, const AIRequest &, const QString &);
// Call only after projectAIAocaiInvokeDispatch has installed current asked data.
void projectAINestedCardaskCompulsory(AIRequest &, Room &, ServerPlayer *);
// Finite source protected-call error sink; never invokes legacy AI or issues a card.
QJsonObject reportAILianliCardaskError(Room &, ServerPlayer *, const AIRequest &, const QString &);
#endif
