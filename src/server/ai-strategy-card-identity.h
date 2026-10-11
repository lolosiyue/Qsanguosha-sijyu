#ifndef AI_STRATEGY_CARD_IDENTITY_H
#define AI_STRATEGY_CARD_IDENTITY_H
#include <QString>
#include <memory>
class Card; class Room; class ServerPlayer; struct CardLifetimeToken;
// Pure equality labels, scoped to one viewer and Room. Never action authority.
QString aiLegacyCardIdentity(Room &, ServerPlayer *, const std::shared_ptr<const CardLifetimeToken> &);
QString aiLegacyCardIdentity(Room &, ServerPlayer *, const Card *);
QString aiScopedLegacyCardIdentity(const Card *);
class AIIdentityProjectionScope {
public:
    AIIdentityProjectionScope(Room &, ServerPlayer *);
    ~AIIdentityProjectionScope();
private:
    Room *previousRoom; ServerPlayer *previousViewer;
};
#endif
