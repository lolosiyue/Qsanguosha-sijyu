#include "engine-bootstrap.h"
#include "card.h"
#include "engine.h"
#include "tui-card-text.h"

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

// The text client has no room context, so anything resolved through
// Engine::getCard() comes back null and every card degrades to its raw id.
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

    std::printf("[AUTOTEST] TUI_CARD_TEXT_RESULT status=%s sample_id=%d text=%s\n",
        failures == 0 ? "PASS" : "FAIL", cardId, text.toUtf8().constData());
    return failures == 0 ? 0 : 1;
}
