#include "tui-card-text.h"
#include "tui-text.h"

#include "card.h"
#include "engine.h"


QString tuiCardDisplayText(int cardId)
{
    // Prefer the room's own copy: a card the server has rewritten (a suit or a
    // name changed by a skill) only differs from the printed card there.
    // Engine::getCard() resolves through the registered room context, so it is
    // null whenever no game is running -- getEngineCard() reads the engine's own
    // table and needs no room, which is the right answer then.
    const Card *card = nullptr;
    if (Sanguosha != nullptr) {
        card = Sanguosha->getCard(cardId);
        if (card == nullptr)
            card = Sanguosha->getEngineCard(cardId);
    }
    if (card == nullptr)
        return tuiText("tui_card_unknown").arg(cardId);

    QString name = Sanguosha->translate(card->objectName());
    if (name.isEmpty())
        name = card->objectName();

    QString suit;
    if (card->getSuit() != Card::NoSuit) {
        suit = Sanguosha->translate(card->getSuitString());
        if (suit.isEmpty())
            suit = card->getSuitString();
    }
    const QString number = card->getNumber() > 0 ? card->getNumberString() : QString();

    if (suit.isEmpty() && number.isEmpty())
        return name;
    return QStringLiteral("%1[%2%3]").arg(name, suit, number);
}
