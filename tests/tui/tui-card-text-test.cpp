#include "engine-bootstrap.h"
#include "card.h"
#include "engine.h"
#include "client-game-state.h"
#include "tui-card-text.h"
#include "tui-room-context.h"

#include "protocol.h"
#include "protocol/protocol-message.h"

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

int firstConcreteCardId()
{
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card != nullptr && card->getSuit() != Card::NoSuit
            && card->getNumber() > 0 && !card->objectName().isEmpty()) {
            return id;
        }
    }
    return -1;
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

    const int cardId = firstConcreteCardId();
    if (cardId < 0) {
        qCritical() << "no concrete card available from the engine";
        return 1;
    }
    const Card *card = Sanguosha->getEngineCard(cardId);
    const QString text = tuiCardDisplayText(cardId);

    check(!text.contains(QStringLiteral("牌 %1").arg(cardId)),
          "a known card is not rendered as a bare id");
    check(text.contains(Sanguosha->translate(card->objectName())),
          "card text carries the translated card name");
    check(text.contains(Sanguosha->translate(card->getSuitString())),
          "card text carries the translated suit");
    check(text.contains(card->getNumberString()),
          "card text carries the card number");
    check(!text.contains(QLatin1Char('<')) && !text.contains(QChar(0x1b)),
          "card text is plain: no markup and no escape sequences");

    const QString unknown = tuiCardDisplayText(Sanguosha->getCardCount() + 4096);
    check(unknown.contains(QString::number(Sanguosha->getCardCount() + 4096)),
          "an unknown id degrades to a readable placeholder");

    // Outside a game nothing is registered, so the engine's own table is the
    // only answer Engine::getCard() can give.
    check(Sanguosha->getCard(cardId) == nullptr,
          "no room is registered before a game starts");

    ClientGameState state;
    TuiRoomContext room(&state);
    room.enterGame();
    check(room.isActive(), "entering a game registers the room");
    Card *roomCard = Sanguosha->getCard(cardId);
    check(roomCard != nullptr, "a registered room resolves cards through Engine::getCard()");
    check(roomCard != nullptr && roomCard->getId() == cardId,
          "the room hands back the card that was asked for");
    check(tuiCardDisplayText(cardId) == text,
          "an untouched card reads the same inside a room as outside one");

    // What the room is for: the server rewrites a card and the text client has
    // to show the new face, not the printed one.
    const Card::Suit otherSuit = card->getSuit() == Card::Heart ? Card::Spade : Card::Heart;
    const int otherNumber = card->getNumber() == 5 ? 6 : 5;
    QSanProtocol::ProtocolMessage update;
    update.command = QSanProtocol::S_COMMAND_UPDATE_CARD;
    update.payload = QVariantMap{{QStringLiteral("action"), QStringLiteral("update")},
        {QStringLiteral("card_id"), cardId},
        {QStringLiteral("suit"), static_cast<int>(otherSuit)},
        {QStringLiteral("number"), otherNumber},
        {QStringLiteral("card_name"), card->objectName()},
        {QStringLiteral("skill_name"), QString()},
        {QStringLiteral("object_name"), card->objectName()},
        {QStringLiteral("flags"), QStringList()}};
    room.applyMessage(update);
    const QString rewritten = tuiCardDisplayText(cardId);
    check(rewritten.contains(QString::number(otherNumber)),
          "a card the server rewrote shows its new number");
    check(rewritten != text, "a rewritten card no longer reads as the printed card");

    QSanProtocol::ProtocolMessage reset;
    reset.command = QSanProtocol::S_COMMAND_UPDATE_CARD;
    reset.payload = QVariantMap{{QStringLiteral("action"), QStringLiteral("reset")},
        {QStringLiteral("card_id"), cardId}};
    room.applyMessage(reset);
    check(tuiCardDisplayText(cardId) == text, "resetting a card restores the printed face");

    QSanProtocol::ProtocolMessage over;
    over.command = QSanProtocol::S_COMMAND_GAME_OVER;
    room.applyMessage(over);
    check(!room.isActive() && Sanguosha->getCard(cardId) == nullptr,
          "the room hands the thread back when the game ends");
    check(tuiCardDisplayText(cardId) == text,
          "card text still resolves once the room is gone");

    std::printf("[AUTOTEST] TUI_CARD_TEXT_RESULT status=%s sample_id=%d text=%s\n",
        failures == 0 ? "PASS" : "FAIL", cardId, text.toUtf8().constData());
    return failures == 0 ? 0 : 1;
}
