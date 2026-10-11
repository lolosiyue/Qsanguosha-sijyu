#ifndef AI_STRATEGY_READONLY_CARD_STORE_H
#define AI_STRATEGY_READONLY_CARD_STORE_H

#include <QString>
#include <memory>
#include <functional>

class Card;
class Room;
class ServerPlayer;
struct AIRequest;

// Native-only observation custody. Each call validates the retained graph
// lifetime; this callable is never serialized or exposed to isolated Lua.
using AIReadonlyCardLease = std::function<const Card *()>;

// Native-only custody of the exact detached product of an audited producer.
// A key grants readonly queries, never payment, selection or reconstruction.
class AIOwnerReadonlyCardStore final {
public:
    AIOwnerReadonlyCardStore(Room &, const AIRequest &);
    ~AIOwnerReadonlyCardStore();
    QString issue(ServerPlayer *, const AIRequest &, std::unique_ptr<Card>, const QString &sourceKey);
    QString issueBorrowed(ServerPlayer *, const AIRequest &, const Card *, const QString &sourceKey);
    const Card *lookup(ServerPlayer *, const AIRequest &, const QString &key) const;
    QString sourceKey(ServerPlayer *, const AIRequest &, const QString &key) const;
    AIReadonlyCardLease retain(ServerPlayer *, const AIRequest &, const QString &key) const;
    void clear();
    static AIReadonlyCardLease observe(const Card *);
    static AIReadonlyCardLease own(std::unique_ptr<Card>);
    AIOwnerReadonlyCardStore(const AIOwnerReadonlyCardStore &) = delete;
    AIOwnerReadonlyCardStore &operator=(const AIOwnerReadonlyCardStore &) = delete;
private:
    struct State;
    std::unique_ptr<State> m_state;
};
#endif
