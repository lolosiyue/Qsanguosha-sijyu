#include <QCoreApplication>
#include <QDebug>
#include <QObject>

#include <memory>

#include "engine-bootstrap.h"
#include "engine-runtime-context.h"
#include "card.h"
#include "engine.h"
#include "player.h"
#include "room-state.h"

// Regression: the server revalidates every client/AI-selected use through
// Room::areCardTargetsLegal(), where the engine-global Self is always nullptr.
// ShimouCard used to clone its follow-up trick from the "shimouPN" property on
// that path even for the play-phase activation, before the property exists, and
// dereferenced the null clone (08p network soak SIGSEGV, 2026-09-13).
namespace {

class ShimouProbePlayer : public Player
{
public:
    ShimouProbePlayer(QObject *room, const QString &name)
        : Player(room)
    {
        setObjectName(name);
        setAlive(true);
    }

    int aliveCount(bool = false) const override { return 2; }
    QString getGameMode() const override { return QStringLiteral("test"); }
    Player *getNextAlive(int = 1) const override { return const_cast<ShimouProbePlayer *>(this); }
    Player *getLastAlive(int = 1) const override { return const_cast<ShimouProbePlayer *>(this); }
};

class ShimouServerContext : public EngineRuntimeContext
{
public:
    ShimouServerContext()
        : m_state(false)
    {
        m_state.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
    }

    QObject *runtimeObject() override { return &m_object; }
    RoomState *roomState() override { return &m_state; }
    const Player *cardOwner(int) const override { return nullptr; }
    Player::Place cardPlace(int) const override { return Player::PlaceUnknown; }
    Card *card(int) const override { return nullptr; }

private:
    QObject m_object;
    RoomState m_state;
};

bool check(bool condition, const char *what)
{
    if (!condition)
        qCritical() << "shimou-card:" << what;
    return condition;
}

} // namespace

int runShimouCardTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    ShimouServerContext context;
    EngineRuntimeContextScope scope(*Sanguosha, &context);
    setEngineSelf(nullptr);

    QObject room;
    ShimouProbePlayer owner(&room, QStringLiteral("owner"));
    ShimouProbePlayer other(&room, QStringLiteral("other"));
    const QList<const Player *> none;
    bool ok = true;

    // tenyear.h drags in UI headers, so build the card through the engine registry.
    std::unique_ptr<SkillCard> activationCard(Sanguosha->cloneSkillCard(QStringLiteral("ShimouCard")));
    std::unique_ptr<SkillCard> followUpCard(Sanguosha->cloneSkillCard(QStringLiteral("ShimouCard")));
    if (!activationCard || !followUpCard) {
        qCritical() << "shimou-card: ShimouCard is not registered";
        return 3;
    }
    const Card &activation = *activationCard;

    // Play-phase activation: no user string, shimouPN not written yet.
    ok &= check(activation.targetFixed(), "activation must be target-fixed");
    ok &= check(activation.targetsFeasible(none, &owner),
                "activation without shimouPN must be feasible on the server");
    ok &= check(!activation.targetFilter(none, &other, &owner),
                "activation picks its own targets in onUse");

    // A stale shimouPN from an earlier resolution must not leak into activation.
    owner.setProperty("shimouPN", QStringLiteral("shimou1:other:duel"));
    ok &= check(activation.targetsFeasible(none, &owner),
                "activation must ignore a stale shimouPN");

    // Forced follow-up: "other" uses the recorded duel, so owner is selectable
    // and other (the user itself) is not.
    followUpCard->setUserString(QStringLiteral("@@shimou!"));
    const Card &followUp = *followUpCard;
    ok &= check(!followUp.targetFixed(),
                "server-side follow-up must defer to player-aware target checks");
    ok &= check(followUp.targetFilter(none, &owner, &owner),
                "follow-up duel may target the skill owner");
    ok &= check(!followUp.targetFilter(none, &other, &owner),
                "follow-up duel acts as 'other' and cannot target itself");
    ok &= check(followUp.targetsFeasible(QList<const Player *>{&owner}, &owner),
                "follow-up duel with a target is feasible");
    ok &= check(!followUp.targetsFeasible(none, &owner),
                "follow-up duel without a target is not feasible");

    // Malformed or missing records must fail closed instead of crashing.
    owner.setProperty("shimouPN", QStringLiteral("shimou1"));
    ok &= check(!followUp.targetsFeasible(QList<const Player *>{&other}, &owner),
                "follow-up with a malformed shimouPN is not feasible");
    ok &= check(!followUp.targetFilter(none, &other, &owner),
                "follow-up with a malformed shimouPN selects nobody");
    owner.setProperty("shimouPN", QStringLiteral("shimou1:other:no_such_card"));
    ok &= check(!followUp.targetsFeasible(QList<const Player *>{&other}, &owner),
                "follow-up naming an unknown card is not feasible");

    if (!ok)
        return 2;
    qInfo() << "shimou-card: PASS";
    return 0;
}
