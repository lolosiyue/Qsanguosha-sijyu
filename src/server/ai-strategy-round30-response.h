#ifndef AI_STRATEGY_ROUND30_RESPONSE_H
#define AI_STRATEGY_ROUND30_RESPONSE_H
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <memory>
class Card;
class Room;
class ServerPlayer;
struct AIRequest;
// Construction only. The caller must obtain issuedScope from the active
// request's native ledger, never from a Lua-supplied object. No receipt is issued.
struct AIRound30ResponsePolicy {
    enum Status { Parsed, Absent, Unknown } status = Unknown;
    std::unique_ptr<Card> card;
    const Card *physicalCard = nullptr; // Borrowed, disclosed owner card only.
    QJsonObject bindingSpec;
    QString error;
    AIRound30ResponsePolicy();
    ~AIRound30ResponsePolicy();
    AIRound30ResponsePolicy(AIRound30ResponsePolicy &&) noexcept;
    AIRound30ResponsePolicy &operator=(AIRound30ResponsePolicy &&) noexcept;
};
AIRound30ResponsePolicy buildAIRound30ResponsePolicy(
    const AIRequest &, Room &, ServerPlayer *, const QJsonObject &issuedScope,
    const QString &registry, const QString &hookKey, const QJsonValue &proposal,
    bool attachRequest);
#endif
