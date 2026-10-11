#ifndef AI_STRATEGY_ROUND33_RESPONSE_H
#define AI_STRATEGY_ROUND33_RESPONSE_H
#include <QList>
#include <QString>
struct AIRequest;
struct SkillInstanceRef;
class Room;
class ServerPlayer;
class Card;
// Fixed SmartAI getGuhuoCard/askForCard compulsory-Jink source product.
// This is not the V1 GUI viewAs product (whose userString is "jink!").
// Both issuance and final reconstruction must call this same live validator.
bool isAIRound33TongyiSource(const AIRequest &, const QString &skill,
                            const QString &policyClass);
Card *buildAIRound33TongyiSource(const AIRequest &, Room &, ServerPlayer *,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    const QString &policyClass, const QString &declaration, const QList<int> &costs);

// Only the fixed sxzhitu Card-object callback. Created/attached objects remain
// request-owned hypotheses; attachment does not issue or modify an authority.
#include <QJsonObject>
#include <memory>
struct AIRound33CardsViewObject {
    bool handled=false, absent=false;
    QString error;
    std::unique_ptr<Card> created;
    const Card *attached=nullptr;
    QJsonObject bindingSpec;
    AIRound33CardsViewObject();
    ~AIRound33CardsViewObject();
    AIRound33CardsViewObject(AIRound33CardsViewObject &&) noexcept;
    AIRound33CardsViewObject &operator=(AIRound33CardsViewObject &&) noexcept;
};
AIRound33CardsViewObject buildAIRound33CardsViewObject(const AIRequest &, Room &, ServerPlayer *,
    const QJsonObject &scope, const QString &registry, const QString &hook,
    const QJsonObject &proposal, bool attachRequest, const Card *ownedSource);

bool aiRound33TongyiConstruction(const AIRequest &, Room &, ServerPlayer *, const QJsonObject &spec);

#endif
