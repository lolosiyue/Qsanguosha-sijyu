#ifndef AI_STRATEGY_PUBLIC_ACTION_FLAGS_H
#define AI_STRATEGY_PUBLIC_ACTION_FLAGS_H

#include <QJsonObject>
#include <QString>

class Card;
class Room;
class ServerPlayer;

// Call only after the actual all-recipient CARD_FLAG notification succeeds.
// A private notification or a virtual-card setFlags call is not this witness.
// The fixed domain is Qinggang, SlashIgnoreArmor and nosjiefan-slash.
void recordAIStrategyPublicCardFlag(Room &room, const Card *card,
                                   const QString &operation);

// Merge the returned per-name booleans into a viewer's known_flags. An empty
// object means unknown, never a complete negative flag set. The caller must
// already have lawful disclosure of this card; this API grants no card face.
QJsonObject readAIStrategyPublicCardFlags(Room &room, ServerPlayer *viewer,
                                        const Card *card);

// Active decision projection scope only. Shared event serialization has no
// viewer capability and therefore receives no observation from this function.
QJsonObject readAIStrategyScopedPublicCardFlags(const Card *card);

#endif
