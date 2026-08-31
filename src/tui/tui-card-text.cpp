#include "tui-card-text.h"

#include "card.h"
#include "engine.h"

#include <QCoreApplication>

QString tuiCardDisplayText(int cardId)
{
    // Engine::getCard() resolves through the current room context, which the
    // text client never registers, so it always returns null here and every
    // card degraded to its raw id. getEngineCard() reads the engine's own card
    // table and needs no room.
    const Card *card = Sanguosha != nullptr ? Sanguosha->getEngineCard(cardId) : nullptr;
    if (card == nullptr)
        return QCoreApplication::translate("QSanguoshaTui", "牌 %1").arg(cardId);

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
