#include "card.h"
#include "engine-bootstrap.h"

#include <QDebug>
#include <QtGlobal>

class Player;
extern Player *Self;
void setEngineSelf(Player *player);

int runEngineSelfBridgeTests()
{
    Player *expected = reinterpret_cast<Player *>(quintptr(1));
    setEngineSelf(expected);
    if (Self != expected) {
        qCritical() << "engine Self did not follow the GUI-selected player";
        return 1;
    }

    setEngineSelf(nullptr);
    if (Self != nullptr) {
        qCritical() << "engine Self did not clear with the GUI-selected player";
        return 2;
    }

    qInfo() << "engine Self bridge Axe regression passed";
    return 0;
}

int runCardParseTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    const Card *card = Card::Parse(QStringLiteral("@KurouCard=."));
    if (card == nullptr) {
        qCritical() << "Card::Parse(@KurouCard=.) returned nullptr";
        return 2;
    }

    // cloneSkillCard 以 metaObject className 識別卡牌，未必寫入 objectName
    if (card->getClassName() != QStringLiteral("KurouCard")) {
        qCritical() << "Card::Parse(@KurouCard=.) className" << card->getClassName();
        return 3;
    }

    qInfo() << "Card::Parse(@KurouCard=.) regression passed";
    return 0;
}
