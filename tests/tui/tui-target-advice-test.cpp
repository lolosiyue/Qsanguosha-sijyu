// What the text client is allowed to say about a card's targets before it sends
// an answer. Everything here is the engine's own opinion, asked exactly the way
// RoomScene asks it -- the point of the suite is that "exactly" is load-bearing:
// three of these cards answer only the four-argument targetFilter(), and one of
// them asserts if the three-argument form is called at all.
#include "engine-bootstrap.h"
#include "card.h"
#include "client-game-state.h"
#include "engine.h"
#include "skill.h"
#include "tui-client-player.h"
#include "tui-room-context.h"
#include "tui-target-advice.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

int firstCardOfClass(const char *className)
{
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card != nullptr && card->isKindOf(className))
            return id;
    }
    return -1;
}

int firstCardNamed(const char *name)
{
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card != nullptr && card->objectName() == QLatin1String(name))
            return id;
    }
    return -1;
}

void addPlayer(ClientGameState *state, const QString &name, int seat, const QString &general)
{
    state->addPlayer(name);
    state->setPlayerValue(name, QStringLiteral("object_name"), name);
    state->setPlayerValue(name, QStringLiteral("seat"), seat);
    state->setPlayerValue(name, QStringLiteral("general"), general);
    state->setPlayerValue(name, QStringLiteral("hp"), 4);
    state->setPlayerValue(name, QStringLiteral("max_hp"), 4);
    state->setPlayerValue(name, QStringLiteral("role"), QStringLiteral("loyalist"));
    state->setPlayerAlive(name, true);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    ClientGameState state;
    state.setSetup(QVariantMap{{QStringLiteral("mode"), QStringLiteral("04_1v3")}});
    state.setSelfName(QStringLiteral("sgs1"));
    addPlayer(&state, QStringLiteral("sgs1"), 1, QStringLiteral("caocao"));
    addPlayer(&state, QStringLiteral("sgs2"), 2, QStringLiteral("liubei"));
    addPlayer(&state, QStringLiteral("sgs3"), 3, QStringLiteral("sunquan"));
    addPlayer(&state, QStringLiteral("sgs4"), 4, QStringLiteral("zhaoyun"));
    const QStringList pool{QStringLiteral("sgs1"), QStringLiteral("sgs2"),
                           QStringLiteral("sgs3"), QStringLiteral("sgs4")};
    state.setPlayerNames(pool);

    TuiRoomContext room(&state);
    TuiPlayerModel players(&state);
    room.setOwnerResolver([&players](int cardId) { return players.cardOwner(cardId); });
    room.enterGame();
    players.sync();
    room.setCardUseContext(CardUseStruct::CARD_USE_REASON_PLAY, QString());

    ClientPlayer *self = players.self();
    check(self != nullptr, "the model knows which player is us");
    if (self == nullptr) {
        std::printf("[AUTOTEST] TUI_TARGET_ADVICE_RESULT status=FAIL\n");
        return 1;
    }
    const TuiPlayerLookup lookup = [&players](const QString &name) -> const Player * {
        return players.player(name);
    };
    const auto validate = [&](const Card *card, const QStringList &targets,
                              bool *incomplete = nullptr) {
        return tuiValidateTargets(card, targets, lookup, self, {}, {}, incomplete);
    };

    // A card nobody can place is not a card anyone should have an opinion about.
    check(!tuiTargetStep(nullptr, {}, pool, lookup, self).known,
          "no card means no opinion");
    check(tuiTargetStep(Sanguosha->getEngineCard(0), {}, pool, {}, self).known == false,
          "no way to look players up means no opinion");

    // --- the ordinary case: one target, chosen from what the prompt offered ---
    const int slashId = firstCardOfClass("Slash");
    check(slashId >= 0, "the engine has a slash to reason about");
    if (slashId >= 0) {
        state.setCardValue(slashId, QStringLiteral("owner"), QStringLiteral("sgs1"));
        state.setCardValue(slashId, QStringLiteral("place"),
                           static_cast<int>(Player::PlaceHand));
        players.sync();
        const Card *slash = Sanguosha->getCard(slashId);
        const TuiTargetStep step = tuiTargetStep(slash, {}, pool, lookup, self);
        check(step.known && !step.fixed, "a slash has an opinion and no fixed target");
        check(!step.candidates.contains(QStringLiteral("sgs1")),
              "a slash is not offered against the player using it");
        check(step.candidates.contains(QStringLiteral("sgs2")),
              "a slash is offered against the neighbour it can reach");
        check(!step.candidates.contains(QStringLiteral("sgs3")),
              "a slash is not offered past its attack range");
        check(!step.feasible, "no target chosen is not a finished play");
        check(step.maxVotes.value(QStringLiteral("sgs2")) == 1,
              "an ordinary card gives each target one vote");

        check(validate(slash, {QStringLiteral("sgs2")}).isEmpty(),
              "one reachable target is a legal slash");
        bool incomplete = false;
        check(!validate(slash, {}, &incomplete).isEmpty() && incomplete,
              "a slash with no target is unfinished rather than wrong");
        check(!validate(slash, {QStringLiteral("sgs1")}, &incomplete).isEmpty() && !incomplete,
              "a slash aimed at its own user is wrong rather than unfinished");
        check(!validate(slash, {QStringLiteral("sgs2"), QStringLiteral("sgs2")}).isEmpty(),
              "an ordinary card cannot spend its one vote twice");
        // A player we cannot place leaves the verdict to the server.
        check(validate(slash, {QStringLiteral("nobody")}).isEmpty(),
              "an unplaceable name draws no objection");
        // Only what the prompt offered: the engine's blessing is not enough.
        check(!tuiTargetStep(slash, {}, {QStringLiteral("sgs3")}, lookup, self)
                   .candidates.contains(QStringLiteral("sgs2")),
              "a target the prompt withheld never appears");
    }

    // --- a card that picks its own targets is never filtered ---
    //
    // The ask-for-peach prompt puts the dying player in the answer itself, and
    // Peach::targetFilter() asks that the target be wounded, which a virtual
    // peach from a view-as skill routinely denies. RoomScene sends on
    // targetFixed() || targetsFeasible() and never consults the filter here.
    const int peachId = firstCardNamed("peach");
    check(peachId >= 0, "the engine has a peach to reason about");
    if (peachId >= 0) {
        const Card *peach = Sanguosha->getCard(peachId);
        const ClientPlayer *other = players.player(QStringLiteral("sgs2"));
        check(peach->targetFixed(), "a peach fixes its own target");
        check(!peach->targetFilter({}, other, self),
              "and its filter refuses an unwounded target, which is the trap");
        check(validate(peach, {QStringLiteral("sgs2")}).isEmpty(),
              "a target-fixed card is accepted without asking its filter");
        const TuiTargetStep step = tuiTargetStep(peach, {}, pool, lookup, self);
        check(step.known && step.fixed && step.feasible && step.candidates.isEmpty(),
              "and there is nothing to ask the player about");
    }

    // --- Collateral: two targets, and only the four-argument filter knows it ---
    const int collateralId = firstCardOfClass("Collateral");
    const int bowId = firstCardNamed("kylin_bow");
    check(collateralId >= 0 && bowId >= 0, "the engine has a collateral and a weapon");
    if (collateralId >= 0 && bowId >= 0) {
        state.setCardValue(bowId, QStringLiteral("owner"), QStringLiteral("sgs2"));
        state.setCardValue(bowId, QStringLiteral("place"),
                           static_cast<int>(Player::PlaceEquip));
        players.sync();
        const Card *collateral = Sanguosha->getCard(collateralId);
        const ClientPlayer *armed = players.player(QStringLiteral("sgs2"));
        const ClientPlayer *unarmed = players.player(QStringLiteral("sgs3"));
        check(armed->getWeapon() != nullptr, "the weapon reached its wearer");

        // Collateral overrides only targetFilter(..., maxVotes) and returns
        // false from it whatever the answer. The inherited three-argument form
        // is SingleTargetTrick's, which says yes to everybody, unlimited.
        check(collateral->targetFilter({}, unarmed, self),
              "the three-argument filter offers even a weaponless player");
        const TuiTargetStep first = tuiTargetStep(collateral, {}, pool, lookup, self);
        check(first.candidates.contains(QStringLiteral("sgs2")),
              "the first target is a player who carries a weapon");
        check(!first.candidates.contains(QStringLiteral("sgs3")),
              "and not one who does not");
        check(!first.candidates.contains(QStringLiteral("sgs1")),
              "nor the player using it");

        const TuiTargetStep second = tuiTargetStep(collateral,
            {QStringLiteral("sgs2")}, pool, lookup, self);
        check(!second.feasible, "one target is not a finished collateral");
        check(second.candidates.contains(QStringLiteral("sgs3")),
              "the second target is someone the first can shoot at");

        bool incomplete = false;
        check(!validate(collateral, {QStringLiteral("sgs2")}, &incomplete).isEmpty()
                  && incomplete,
              "a collateral with half a pair is unfinished");
        check(validate(collateral,
                  {QStringLiteral("sgs2"), QStringLiteral("sgs3")}).isEmpty(),
              "a collateral with a full pair is legal");
        check(!validate(collateral,
                  {QStringLiteral("sgs3"), QStringLiteral("sgs2")}).isEmpty(),
              "a collateral whose first target has no weapon is not");
    }

    // --- GreatYeyanCard: the same player, up to three times ---
    //
    // Its three-argument targetFilter() is Q_ASSERT(false), so a client that
    // reaches for the wrong overload takes the whole process down in a debug
    // build. Nothing below may call it.
    SkillCard *yeyan = Sanguosha->cloneSkillCard(QStringLiteral("GreatYeyanCard"));
    check(yeyan != nullptr, "the engine can build a great yeyan card");
    if (yeyan != nullptr) {
        const TuiTargetStep first = tuiTargetStep(yeyan, {}, pool, lookup, self);
        check(first.maxVotes.value(QStringLiteral("sgs2")) == 3,
              "an unspent yeyan offers three votes per player");
        const TuiTargetStep twice = tuiTargetStep(yeyan,
            {QStringLiteral("sgs2"), QStringLiteral("sgs2")}, pool, lookup, self);
        check(twice.candidates.contains(QStringLiteral("sgs2")),
              "a player already named twice can still be named again");
        check(!twice.feasible, "two of the three points is not a finished play");

        const QStringList thrice{QStringLiteral("sgs2"), QStringLiteral("sgs2"),
                                 QStringLiteral("sgs2")};
        check(validate(yeyan, thrice).isEmpty(),
              "three points on one player is a legal yeyan");
        check(!tuiTargetStep(yeyan, thrice, pool, lookup, self)
                   .candidates.contains(QStringLiteral("sgs2")),
              "and there is no fourth vote to spend");
        check(!validate(yeyan, thrice + QStringList{QStringLiteral("sgs2")}).isEmpty(),
              "a fourth point is refused");
        bool incomplete = false;
        check(!validate(yeyan, {QStringLiteral("sgs2"), QStringLiteral("sgs2")},
                        &incomplete).isEmpty() && incomplete,
              "two points is unfinished rather than wrong");
        yeyan->deleteLater();
    }

    std::printf("[AUTOTEST] TUI_TARGET_ADVICE_RESULT status=%s failures=%d\n",
                failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
