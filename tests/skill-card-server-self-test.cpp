#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QObject>

#include <memory>

#include "engine-bootstrap.h"
#include "engine-runtime-context.h"
#include "card.h"
#include "engine.h"
#include "general.h"
#include "player.h"
#include "room-state.h"
#include "skill.h"

// Regression: the server revalidates every client/AI-selected use through
// Room::areCardTargetsLegal(), where the engine-global Self is always nullptr.
// Skill cards whose target checks read the global Self (or state that only
// exists later) crash the server there instead of answering.
//
// - ShimouCard cloned its follow-up trick from the "shimouPN" property even for
//   the play-phase activation, before the property exists, and dereferenced the
//   null clone (08p network soak SIGSEGV, 2026-09-13).
// - MTYinglveCard::targetFixed() read the global Self directly.
// - Guhuo-style cards read their declared card from a client-only Self tag in
//   the play phase, and a few target filters/enablement checks used the
//   global Self instead of their player parameter.
//
// The package headers drag in UI headers, so cards are built through the engine
// registry.
namespace {

class ServerSelfProbePlayer : public Player
{
public:
    ServerSelfProbePlayer(QObject *room, const QString &name)
        : Player(room)
    {
        setObjectName(name);
        setAlive(true);
    }

    int aliveCount(bool = false) const override { return 2; }
    QString getGameMode() const override { return QStringLiteral("test"); }
    Player *getNextAlive(int = 1) const override { return const_cast<ServerSelfProbePlayer *>(this); }
    Player *getLastAlive(int = 1) const override { return const_cast<ServerSelfProbePlayer *>(this); }
    int getHandcardNum() const override { return m_handcardNum; }
    void setHandcardNum(int num) { m_handcardNum = num; }

private:
    int m_handcardNum = 0;
};

class ServerSelfContext : public EngineRuntimeContext
{
public:
    ServerSelfContext()
        : m_state(false)
    {
        m_state.setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
    }

    ~ServerSelfContext() override { qDeleteAll(m_cards); }

    void addCard(int id, const QString &name)
    {
        Card *card = Sanguosha->cloneCard(name);
        card->setId(id);
        m_cards.insert(id, card);
    }

    QObject *runtimeObject() override { return &m_object; }
    RoomState *roomState() override { return &m_state; }
    const Player *cardOwner(int) const override { return nullptr; }
    Player::Place cardPlace(int) const override { return Player::PlaceUnknown; }
    Card *card(int id) const override { return m_cards.value(id, nullptr); }

private:
    QObject m_object;
    RoomState m_state;
    QHash<int, Card *> m_cards;
};

bool check(bool condition, const char *what)
{
    if (!condition)
        qCritical() << "skill-card-server-self:" << what;
    return condition;
}

bool runShimouChecks(ServerSelfProbePlayer &owner, ServerSelfProbePlayer &other)
{
    const QList<const Player *> none;
    std::unique_ptr<SkillCard> activationCard(Sanguosha->cloneSkillCard(QStringLiteral("ShimouCard")));
    std::unique_ptr<SkillCard> followUpCard(Sanguosha->cloneSkillCard(QStringLiteral("ShimouCard")));
    if (!check(activationCard && followUpCard, "ShimouCard is not registered"))
        return false;
    const Card &activation = *activationCard;
    bool ok = true;

    // Play-phase activation: no user string, shimouPN not written yet.
    ok &= check(activation.targetFixed(), "shimou activation must be target-fixed");
    ok &= check(activation.targetsFeasible(none, &owner),
                "shimou activation without shimouPN must be feasible on the server");
    ok &= check(!activation.targetFilter(none, &other, &owner),
                "shimou activation picks its own targets in onUse");

    // A stale shimouPN from an earlier resolution must not leak into activation.
    owner.setProperty("shimouPN", QStringLiteral("shimou1:other:duel"));
    ok &= check(activation.targetsFeasible(none, &owner),
                "shimou activation must ignore a stale shimouPN");

    // Forced follow-up: "other" uses the recorded duel, so owner is selectable
    // and other (the user itself) is not.
    followUpCard->setUserString(QStringLiteral("@@shimou!"));
    const Card &followUp = *followUpCard;
    ok &= check(!followUp.targetFixed(),
                "server-side shimou follow-up must defer to player-aware target checks");
    ok &= check(followUp.targetFilter(none, &owner, &owner),
                "shimou follow-up duel may target the skill owner");
    ok &= check(!followUp.targetFilter(none, &other, &owner),
                "shimou follow-up duel acts as 'other' and cannot target itself");
    ok &= check(followUp.targetsFeasible(QList<const Player *>{&owner}, &owner),
                "shimou follow-up duel with a target is feasible");
    ok &= check(!followUp.targetsFeasible(none, &owner),
                "shimou follow-up duel without a target is not feasible");

    // Malformed or missing records must fail closed instead of crashing.
    owner.setProperty("shimouPN", QStringLiteral("shimou1"));
    ok &= check(!followUp.targetsFeasible(QList<const Player *>{&other}, &owner),
                "shimou follow-up with a malformed shimouPN is not feasible");
    ok &= check(!followUp.targetFilter(none, &other, &owner),
                "shimou follow-up with a malformed shimouPN selects nobody");
    owner.setProperty("shimouPN", QStringLiteral("shimou1:other:no_such_card"));
    ok &= check(!followUp.targetsFeasible(QList<const Player *>{&other}, &owner),
                "shimou follow-up naming an unknown card is not feasible");
    owner.setProperty("shimouPN", QVariant());
    return ok;
}

bool runMTYinglveChecks(ServerSelfContext &context, ServerSelfProbePlayer &owner,
                        ServerSelfProbePlayer &other)
{
    const QList<const Player *> none;
    std::unique_ptr<SkillCard> yinglve(Sanguosha->cloneSkillCard(QStringLiteral("MTYinglveCard")));
    if (!check(yinglve != nullptr, "MTYinglveCard is not registered"))
        return false;
    const int duelId = 9001;
    const int exNihiloId = 9002;
    context.addCard(duelId, QStringLiteral("duel"));
    context.addCard(exNihiloId, QStringLiteral("ex_nihilo"));
    bool ok = true;

    // No recorded card: answer without touching the missing engine Self.
    ok &= check(!yinglve->targetFixed(), "yinglve without a record is not target-fixed");
    ok &= check(!yinglve->targetsFeasible(QList<const Player *>{&other}, &owner),
                "yinglve without a record is not feasible");
    ok &= check(!yinglve->targetFilter(none, &other, &owner),
                "yinglve without a record selects nobody");

    // Recorded duel: validated through the player-aware checks.
    owner.setMark(QStringLiteral("mtyinglve_cardID"), duelId + 1);
    ok &= check(!yinglve->targetFixed(),
                "server-side yinglve must defer to player-aware target checks");
    ok &= check(yinglve->targetFilter(none, &other, &owner), "yinglve duel may target another player");
    ok &= check(!yinglve->targetFilter(none, &owner, &owner), "yinglve duel cannot target its user");
    ok &= check(yinglve->targetsFeasible(QList<const Player *>{&other}, &owner),
                "yinglve duel with a target is feasible");
    ok &= check(!yinglve->targetsFeasible(none, &owner), "yinglve duel without a target is not feasible");

    // A target-fixed card still validates with an empty target list on the server.
    owner.setMark(QStringLiteral("mtyinglve_cardID"), exNihiloId + 1);
    ok &= check(yinglve->targetsFeasible(none, &owner),
                "yinglve ex_nihilo without targets is feasible");

    // A record naming a card the room does not have fails closed.
    owner.setMark(QStringLiteral("mtyinglve_cardID"), 424243);
    ok &= check(!yinglve->targetsFeasible(QList<const Player *>{&other}, &owner),
                "yinglve naming a missing card is not feasible");
    owner.setMark(QStringLiteral("mtyinglve_cardID"), 0);
    return ok;
}

// Guhuo-style cards carry the declared card name in user_string (see their
// viewAs); the client additionally keeps it in a Self tag set by the dialog,
// which the server never has. In the play phase the server must still answer
// from user_string instead of dereferencing the missing Self.
bool runDeclaredCardChecks(const char *className, ServerSelfProbePlayer &owner,
                           ServerSelfProbePlayer &other)
{
    const QList<const Player *> none;
    const QByteArray name(className);
    std::unique_ptr<SkillCard> card(Sanguosha->cloneSkillCard(QString::fromLatin1(className)));
    if (!check(card != nullptr, (name + " is not registered").constData()))
        return false;
    bool ok = true;

    card->setUserString(QStringLiteral("duel"));
    ok &= check(!card->targetFixed(), (name + ": declared duel is not target-fixed").constData());
    ok &= check(card->targetFilter(none, &other, &owner),
                (name + ": declared duel may target another player").constData());
    ok &= check(!card->targetFilter(none, &owner, &owner),
                (name + ": declared duel cannot target its user").constData());
    ok &= check(card->targetsFeasible(QList<const Player *>{&other}, &owner),
                (name + ": declared duel with a target is feasible").constData());
    ok &= check(!card->targetsFeasible(none, &owner),
                (name + ": declared duel without a target is not feasible").constData());

    card->setUserString(QStringLiteral("ex_nihilo"));
    ok &= check(card->targetFixed(), (name + ": declared ex_nihilo is target-fixed").constData());
    ok &= check(card->targetsFeasible(none, &owner),
                (name + ": declared ex_nihilo without targets is feasible").constData());

    // Nothing declared: answer without touching the missing engine Self.
    card->setUserString(QString());
    ok &= check(!card->targetsFeasible(QList<const Player *>{&other}, &owner),
                (name + ": nothing declared is not feasible").constData());
    card->targetFixed();
    return ok;
}

bool runPlayerParameterChecks(ServerSelfContext &context, ServerSelfProbePlayer &owner,
                              ServerSelfProbePlayer &other)
{
    const QList<const Player *> none;
    bool ok = true;

    // Target filters that must consult their Self parameter, not the engine global.
    std::unique_ptr<Card> yanxiao(Sanguosha->cloneCard(QStringLiteral("YanxiaoCard")));
    if (check(yanxiao != nullptr, "YanxiaoCard is not registered")) {
        ok &= check(yanxiao->targetFilter(none, &other, &owner), "yanxiao may target a player");
        ok &= check(!yanxiao->targetFilter(QList<const Player *>{&other}, &owner, &owner),
                    "yanxiao takes a single target");
    } else {
        ok = false;
    }

    std::unique_ptr<SkillCard> zhufu(Sanguosha->cloneSkillCard(QStringLiteral("ZhufuCard")));
    if (check(zhufu != nullptr, "ZhufuCard is not registered")) {
        ok &= check(zhufu->targetFilter(none, &other, &owner), "zhufu may target another player");
        ok &= check(!zhufu->targetFilter(none, &owner, &owner), "zhufu cannot target its user");
    } else {
        ok = false;
    }

    std::unique_ptr<SkillCard> wulie(Sanguosha->cloneSkillCard(QStringLiteral("OLWulieCard")));
    if (check(wulie != nullptr, "OLWulieCard is not registered")) {
        owner.setHp(1);
        ok &= check(wulie->targetFilter(none, &other, &owner), "olwulie may target another player");
        ok &= check(!wulie->targetFilter(none, &owner, &owner), "olwulie cannot target its user");
        ok &= check(!wulie->targetFilter(QList<const Player *>{&other}, &other, &owner),
                    "olwulie is limited by its user's hp");
    } else {
        ok = false;
    }

    std::unique_ptr<SkillCard> jieyin(Sanguosha->cloneSkillCard(QStringLiteral("TenyearJieyinCard")));
    if (check(jieyin != nullptr, "TenyearJieyinCard is not registered")) {
        const int armorId = 9101;
        context.addCard(armorId, QStringLiteral("eight_diagram"));
        jieyin->addSubcard(armorId);
        other.setGender(General::Male);
        ok &= check(jieyin->targetFilter(none, &other, &owner),
                    "tenyear jieyin may give an equip to a male without that slot");
    } else {
        ok = false;
    }

    // View-as enablement consults its player argument.
    const ViewAsSkill *xiedou = Sanguosha->getViewAsSkill(QStringLiteral("xiedou"));
    if (check(xiedou != nullptr, "xiedou is not registered")) {
        owner.setHandcardNum(3);
        ok &= check(!xiedou->isEnabledAtPlay(&owner), "xiedou needs its user to have equips");
        owner.setHandcardNum(0);
    } else {
        ok = false;
    }
    return ok;
}

} // namespace

int runSkillCardServerSelfTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    ServerSelfContext context;
    EngineRuntimeContextScope scope(*Sanguosha, &context);
    setEngineSelf(nullptr);

    QObject room;
    ServerSelfProbePlayer owner(&room, QStringLiteral("owner"));
    ServerSelfProbePlayer other(&room, QStringLiteral("other"));

    bool ok = runShimouChecks(owner, other);
    ok &= runMTYinglveChecks(context, owner, other);
    for (const char *className : {"GuhuoCard", "NosGuhuoCard", "HuomoCard", "InovationFengzhuCard",
                                  "ZhanyiViewAsBasicCard", "TaoluanCard", "YHYurenCard",
                                  "MTZhiheCard", "YizanCard", "MobileZhiMiewuCard",
                                  "OLGuhuoCard", "JinBingxinCard"}) {
        ok &= runDeclaredCardChecks(className, owner, other);
    }
    ok &= runPlayerParameterChecks(context, owner, other);
    if (!ok)
        return 2;
    qInfo() << "skill-card-server-self: PASS";
    return 0;
}
