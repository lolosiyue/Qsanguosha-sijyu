#include "engine-bootstrap.h"
#include "card.h"
#include "client-game-state.h"
#include "engine.h"
#include "protocol.h"
#include "protocol/protocol-message.h"
#include "tui-client-player.h"
#include "tui-room-context.h"

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
    state.setSetup(QVariantMap{{QStringLiteral("mode"), QStringLiteral("03_1v2")}});
    state.setSelfName(QStringLiteral("sgs1"));
    addPlayer(&state, QStringLiteral("sgs1"), 1, QStringLiteral("caocao"));
    addPlayer(&state, QStringLiteral("sgs2"), 2, QStringLiteral("liubei"));
    addPlayer(&state, QStringLiteral("sgs3"), 3, QStringLiteral("sunquan"));
    state.setPlayerNames({QStringLiteral("sgs1"), QStringLiteral("sgs2"), QStringLiteral("sgs3")});

    TuiRoomContext room(&state);
    TuiPlayerModel players(&state);
    room.setOwnerResolver([&players](int cardId) { return players.cardOwner(cardId); });
    room.enterGame();
    players.sync();

    ClientPlayer *self = players.self();
    check(self != nullptr, "the model knows which player is us");
    if (self == nullptr) {
        std::printf("[AUTOTEST] TUI_CLIENT_PLAYER_RESULT status=FAIL\n");
        return 1;
    }

    // The whole point of the class name: fifteen engine sites branch on it.
    check(self->inherits("ClientPlayer"),
          "the model's players answer to inherits(\"ClientPlayer\")");
    check(self->isClientPlayer(), "Player::isClientPlayer() agrees");
    check(QSanEngine::Self == self, "setEngineSelf() points the engine at us");

    check(self->getHp() == 4 && self->getMaxHp() == 4, "hit points come across");
    check(self->getGeneralName() == QLatin1String("caocao"), "the general comes across");
    check(!self->getKingdom().isEmpty(), "the general answers for the kingdom");
    check(self->getSeat() == 1, "the seat comes across");
    check(self->aliveCount() == 3, "the player counts its own table");
    check(self->getGameMode() == QLatin1String("03_1v2"), "the game mode comes across");

    Player *next = self->getNextAlive();
    check(next != nullptr && next->objectName() == QLatin1String("sgs2"),
          "the seat ring walks forward");
    Player *last = self->getLastAlive();
    check(last != nullptr && last->objectName() == QLatin1String("sgs3"),
          "the seat ring walks backward");

    // distanceTo() takes the client branch and reads the matrix the server
    // pushes as plain player properties.
    state.setPlayerValue(QStringLiteral("sgs1"), QStringLiteral("distanceTo_sgs3"), 7);
    players.sync();
    check(self->distanceTo(players.player(QStringLiteral("sgs3"))) == 7,
          "distance comes from the server-pushed matrix, not a local guess");
    check(self->distanceTo(players.player(QStringLiteral("sgs2"))) == 1,
          "a distance the server never sent falls back to the seat ring");

    // The hand limit is the server's PlayerUIState value, as on the desktop.
    state.setPlayerValue(QStringLiteral("sgs1"), QStringLiteral("hand_max"), 9);
    players.sync();
    check(self->getMaxCards() == 9, "the hand limit is the server's, not a local re-derivation");

    const int slashId = firstCardOfClass("Slash");
    check(slashId >= 0, "the engine has a slash to reason about");
    if (slashId >= 0) {
        state.setCardValue(slashId, QStringLiteral("owner"), QStringLiteral("sgs1"));
        state.setCardValue(slashId, QStringLiteral("place"),
                           static_cast<int>(Player::PlaceHand));
        players.sync();
        check(room.cardOwner(slashId) == self, "the room can name a card's owner");
        check(self->getHandcards().size() == 1, "a known hand card reaches the player");

        // The slash limit only applies in the play phase, and the room is where
        // the engine looks for that.
        room.setCardUseContext(CardUseStruct::CARD_USE_REASON_PLAY, QString());
        const Card *slash = Sanguosha->getCard(slashId);
        check(slash != nullptr && slash->isAvailable(self),
              "a slash with no history is available in the play phase");
        // usedTimes() drives the slash limit, and ADD_HISTORY is already on the
        // wire, so the client can answer this without asking the server.
        state.setPlayerValue(QStringLiteral("sgs1"), QStringLiteral("history"),
                             QVariantMap{{QStringLiteral("Slash"), 1}});
        players.sync();
        check(self->usedTimes(QStringLiteral("Slash")) == 1, "card-use history comes across");
        check(slash != nullptr && !slash->isAvailable(self),
              "a slash already used this turn is not available again");
    }

    // Card limitations are on the wire too, and isCardLimited() is the one
    // judgement the client genuinely runs the engine for.
    const int peachId = firstCardOfClass("Peach");
    if (peachId >= 0) {
        const Card *peach = Sanguosha->getCard(peachId);
        check(peach != nullptr && !self->isCardLimited(peach, Card::MethodUse),
              "an unrestricted card is not limited");
        state.setPlayerValue(QStringLiteral("sgs1"), QStringLiteral("card_limitations"),
            QVariantList{QVariantMap{{QStringLiteral("limit_list"), QStringLiteral("use")},
                {QStringLiteral("pattern"), QStringLiteral("Peach")},
                {QStringLiteral("reason"), QStringLiteral("test")},
                {QStringLiteral("single_turn"), false}}});
        players.sync();
        check(peach != nullptr && self->isCardLimited(peach, Card::MethodUse),
              "a limitation the server sent is enforced locally");
    }

    // Prohibit / TargetMod / AttackRange all walk skill lists that Lua packages
    // extend, so they only work if the text client really carries the whole
    // engine -- Lua state included.
    check(Sanguosha->getLuaState() != nullptr, "the text client has the engine's Lua state");
    if (peachId >= 0) {
        const Card *peach = Sanguosha->getCard(peachId);
        check(!self->isProhibited(players.player(QStringLiteral("sgs2")), peach),
              "a plain target is not prohibited");
    }
    check(self->getAttackRange() == 1, "an unarmed player reaches one seat");

    int crossbowId = -1;
    int weaponId = -1;
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *entry = Sanguosha->getEngineCard(id);
        if (entry == nullptr)
            continue;
        if (crossbowId < 0 && entry->objectName() == QLatin1String("crossbow"))
            crossbowId = id;
        if (weaponId < 0 && entry->objectName() == QLatin1String("kylin_bow"))
            weaponId = id;
    }
    if (weaponId >= 0) {
        state.setCardValue(weaponId, QStringLiteral("owner"), QStringLiteral("sgs1"));
        state.setCardValue(weaponId, QStringLiteral("place"),
                           static_cast<int>(Player::PlaceEquip));
        players.sync();
        check(self->getWeapon() != nullptr, "an equipped weapon reaches the player");
        // kylin_bow has range 5; the engine reads that off the room's own card.
        check(self->getAttackRange() == 5, "the weapon's range is the player's range");
        state.setCardValue(weaponId, QStringLiteral("place"),
                           static_cast<int>(Player::DiscardPile));
        state.setCardValue(weaponId, QStringLiteral("owner"), QString());
        players.sync();
        check(self->getWeapon() == nullptr && self->getAttackRange() == 1,
              "losing the weapon takes the range back");
    }
    if (crossbowId >= 0 && slashId >= 0) {
        // The crossbow lifts the one-slash-per-turn limit through the same
        // TargetModSkill path a Lua package would extend. The player has one
        // slash in their history from above.
        const Card *slash = Sanguosha->getCard(slashId);
        check(slash != nullptr && !slash->isAvailable(self),
              "the slash limit still stands before the crossbow");
        state.setCardValue(crossbowId, QStringLiteral("owner"), QStringLiteral("sgs1"));
        state.setCardValue(crossbowId, QStringLiteral("place"),
                           static_cast<int>(Player::PlaceEquip));
        players.sync();
        check(slash != nullptr && slash->isAvailable(self),
              "the crossbow lifts the slash limit, client-side");
    }

    players.clear();
    check(QSanEngine::Self == nullptr, "clearing the model unhooks the engine's Self");
    room.leaveGame();

    std::printf("[AUTOTEST] TUI_CLIENT_PLAYER_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
