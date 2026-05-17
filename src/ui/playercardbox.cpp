#include "playercardbox.h"
#include "clientplayer.h"
#include "skin-bank.h"
#include "engine.h"
#include "carditem.h"
#include "client.h"
#include "clientstruct.h"
#include "timed-progressbar.h"
#include "qsanbutton.h"

#include <QGraphicsProxyWidget>

static QChar handcardFlag('h');
static QChar equipmentFlag('e');
static QChar judgingFlag('j');

const int PlayerCardBox::maxCardNumberInOneRow = 10;

const int PlayerCardBox::verticalBlankWidth = 37;
const int PlayerCardBox::placeNameAreaWidth = 80;
const int PlayerCardBox::intervalBetweenNameAndCard = 20;
const int PlayerCardBox::topBlankWidth = 42;
const int PlayerCardBox::bottomBlankWidth = 25;
const int PlayerCardBox::intervalBetweenAreas = 10;
const int PlayerCardBox::intervalBetweenRows = 5;
const int PlayerCardBox::intervalBetweenCards = 3;

PlayerCardBox::PlayerCardBox()
    : player(NULL), progressBar(NULL), cancelButton(NULL), canCancel(false),
      rowCount(0), intervalsBetweenAreas(-1), intervalsBetweenRows(0), maxCardsInOneRow(0)
{
    setZValue(1000);
}

void PlayerCardBox::chooseCard(const QString &reason, const ClientPlayer *player,
                          const QString &flags, bool handcardVisible,
                          Card::HandlingMethod method, const QList<int> &disabledIds,
                          bool canCancel)
{
    nameRects.clear();
    rowCount = 0;
    intervalsBetweenAreas = -1;
    intervalsBetweenRows = 0;
    maxCardsInOneRow = 0;

    this->player = player;
    this->title = tr("%1: please choose %2's card").arg(Sanguosha->translate(reason)).arg(ClientInstance->getPlayerName(player->objectName()));
    this->flags = flags;
    this->canCancel = canCancel;
    bool handcard = false;
    bool weapon = false;
    bool armor = false;
    bool defensiveHorse = false;
    bool offensiveHorse = false;
    bool judging = false;

    if (flags.contains(handcardFlag) && !player->isKongcheng()) {
        updateNumbers(player->getHandcardNum());
        handcard = true;
    }

    if (flags.contains(equipmentFlag)) {
        if (player->getWeapon()) {
            updateNumbers(1);
            weapon = true;
        }
        if (player->getArmor()) {
            updateNumbers(1);
            armor = true;
        }
        if (player->getDefensiveHorse()) {
            updateNumbers(1);
            defensiveHorse = true;
        }
        if (player->getOffensiveHorse()) {
            updateNumbers(1);
            offensiveHorse = true;
        }
    }

    if (flags.contains(judgingFlag) && !player->getJudgingArea().isEmpty()) {
        updateNumbers(player->getJudgingArea().length());
        judging = true;
    }

    int max = maxCardsInOneRow;
    int maxNumber = maxCardNumberInOneRow;
    maxCardsInOneRow = qMin(max, maxNumber);

    prepareGeometryChange();

    moveToCenter();
    show();

    this->handcardVisible = handcardVisible;
    this->method = method;
    this->disabledIds = disabledIds;

    const int startX = verticalBlankWidth + placeNameAreaWidth + intervalBetweenNameAndCard;
    int index = 0;

    if (handcard) {
        if (Self == player || handcardVisible) {
            arrangeCards(player->getHandcards(), QPoint(startX, nameRects.at(index).y()));
        } else {
            const int handcardNumber = player->getHandcardNum();
            QList<int> shownIds = player->getShownHandcards();
            QList<int> handIds = player->handCards();
            bool canSeeAll = Self && Self->canSeeHandcard(player);
            
            QList<const Card *> cards;
            for (int i = 0; i < handcardNumber; ++i) {
                int cardId = (i < handIds.size()) ? handIds[i] : -1;
                
                if (canSeeAll || shownIds.contains(cardId)) {
                    const Card *card = Sanguosha->getCard(cardId);
                    cards << card;
                } else {
                    cards << NULL;
                }
            }
            arrangeCards(cards, QPoint(startX, nameRects.at(index).y()));
        }

        ++ index;
    }

    if (weapon) {
        QList<const Card *> weaponCards;
        weaponCards << player->getWeapon();
        arrangeCards(weaponCards, QPoint(startX, nameRects.at(index).y()));
        ++ index;
    }

    if (armor) {
        QList<const Card *> armorCards;
        armorCards << player->getArmor();
        arrangeCards(armorCards, QPoint(startX, nameRects.at(index).y()));
        ++ index;
    }

    if (defensiveHorse) {
        QList<const Card *> defensiveHorseCards;
        defensiveHorseCards << player->getDefensiveHorse();
        arrangeCards(defensiveHorseCards, QPoint(startX, nameRects.at(index).y()));
        ++ index;
    }

    if (offensiveHorse) {
        QList<const Card *> offensiveHorseCards;
        offensiveHorseCards << player->getOffensiveHorse();
        arrangeCards(offensiveHorseCards, QPoint(startX, nameRects.at(index).y()));
        ++ index;
    }

    if (judging)
        arrangeCards(player->getJudgingArea(), QPoint(startX, nameRects.at(index).y()));

    if (canCancel) {
        if (!cancelButton) {
            cancelButton = new QSanButton("platter", "cancel", this);
            cancelButton->setRect(QRect(0, 0, 80, 30));
        }
        cancelButton->setEnabled(true);
        cancelButton->setPos(boundingRect().width() - 90, boundingRect().height() - 40);
        cancelButton->show();
        connect(cancelButton, &QSanButton::clicked, this, &PlayerCardBox::cancel);
    }

    if (ServerInfo.OperationTimeout != 0) {
        if (!progressBar) {
            progressBar = new QSanCommandProgressBar();
            progressBar->setMaximumWidth(qMin(boundingRect().width() - 16, (qreal) 150));
            progressBar->setMaximumHeight(12);
            progressBar->setTimerEnabled(true);
            progressBarItem = new QGraphicsProxyWidget(this);
            progressBarItem->setWidget(progressBar);
            progressBarItem->setPos(boundingRect().center().x() - progressBarItem->boundingRect().width() / 2, boundingRect().height() - 20);
            connect(progressBar, &QSanCommandProgressBar::timedOut, this, &PlayerCardBox::reply);
        }
        progressBar->setCountdown(QSanProtocol::S_COMMAND_CHOOSE_CARD);
        progressBar->show();
    }
}

QRectF PlayerCardBox::boundingRect() const
{
    if (player == NULL)
        return QRectF();

    if (rowCount == 0)
        return QRectF();

    const int cardWidth = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;

    int width = verticalBlankWidth * 2 + placeNameAreaWidth + intervalBetweenNameAndCard;

    if (maxCardsInOneRow > maxCardNumberInOneRow / 2) {
        width += cardWidth * maxCardNumberInOneRow / 2
                + intervalBetweenCards * (maxCardNumberInOneRow / 2 - 1);
    } else {
        width += cardWidth * maxCardsInOneRow
                + intervalBetweenCards * (maxCardsInOneRow - 1);
    }

    QFont font;
    QFontMetrics fm(font);
    int titleWidth = fm.boundingRect(title).width() + 40;
    width = qMax(width, titleWidth);

    int areaInterval = intervalBetweenAreas;
    int height = topBlankWidth + bottomBlankWidth + cardHeight * rowCount
            + intervalsBetweenAreas * qMax(areaInterval, 0)
            + intervalsBetweenRows * intervalBetweenRows;

    if (ServerInfo.OperationTimeout != 0)
        height += 12;

    return QRectF(0, 0, width, height);
}

void PlayerCardBox::paintLayout(QPainter *painter)
{
    if (nameRects.isEmpty())
        return;

    foreach (const QRect &rect, nameRects)
        painter->drawRoundedRect(rect, 3, 3);

    int index = 0;

    if (flags.contains(handcardFlag) && !player->isKongcheng()) {
        G_COMMON_LAYOUT.playerCardBoxPlaceNameText.paintText(painter, nameRects.at(index),
                                                             Qt::AlignCenter,
                                                             tr("Handcard area"));
        ++ index;
    }
    if (flags.contains(equipmentFlag)) {
        if (player->getWeapon()) {
            G_COMMON_LAYOUT.playerCardBoxPlaceNameText.paintText(painter, nameRects.at(index),
                                                                 Qt::AlignCenter,
                                                                 tr("Weapon area"));
            ++ index;
        }
        if (player->getArmor()) {
            G_COMMON_LAYOUT.playerCardBoxPlaceNameText.paintText(painter, nameRects.at(index),
                                                                 Qt::AlignCenter,
                                                                 tr("Armor area"));
            ++ index;
        }
        if (player->getDefensiveHorse()) {
            G_COMMON_LAYOUT.playerCardBoxPlaceNameText.paintText(painter, nameRects.at(index),
                                                                 Qt::AlignCenter,
                                                                 tr("Defensive horse"));
            ++ index;
        }
        if (player->getOffensiveHorse()) {
            G_COMMON_LAYOUT.playerCardBoxPlaceNameText.paintText(painter, nameRects.at(index),
                                                                 Qt::AlignCenter,
                                                                 tr("Offensive horse"));
            ++ index;
        }
    }
    if (flags.contains(judgingFlag) && !player->getJudgingArea().isEmpty()) {
        G_COMMON_LAYOUT.playerCardBoxPlaceNameText.paintText(painter, nameRects.at(index),
                                                             Qt::AlignCenter,
                                                             tr("Judging area"));
    }
}

void PlayerCardBox::clear()
{
    if (progressBar != NULL) {
        progressBar->hide();
        progressBar->deleteLater();
        progressBar = NULL;

        progressBarItem->deleteLater();
    }

    if (cancelButton != NULL) {
        cancelButton->disconnect();
        cancelButton->hide();
    }

    foreach (CardItem *item, items) {
        item->setParentItem(nullptr);
        item->goBack(false, false);
        item->deleteLater();
    }
    items.clear();

    disappear();
}

int PlayerCardBox::getRowCount(const int &cardNumber) const
{
    return (cardNumber + maxCardNumberInOneRow - 1) / maxCardNumberInOneRow;
}

void PlayerCardBox::updateNumbers(const int &cardNumber)
{
    ++ intervalsBetweenAreas;
    if (cardNumber > maxCardsInOneRow)
        maxCardsInOneRow = cardNumber;

    const int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;
    const int y = topBlankWidth + rowCount * cardHeight
            + intervalsBetweenAreas * intervalBetweenAreas
            + intervalsBetweenRows * intervalBetweenRows;

    const int count = getRowCount(cardNumber);
    rowCount += count;
    intervalsBetweenRows += count - 1;

    const int height = count * cardHeight
            + (count - 1) * intervalsBetweenRows;

    nameRects << QRect(verticalBlankWidth, y, placeNameAreaWidth, height);
}

void PlayerCardBox::arrangeCards(const QList<const Card *> &cards, const QPoint &topLeft)
{
    QList<CardItem *> areaItems;
    foreach (const Card *card, cards) {
        CardItem *item = new CardItem(card);
        item->setAutoBack(false);
        item->resetTransform();
        item->setParentItem(this);
        item->setFlag(ItemIsMovable, false);
        if (card) {
            item->setEnabled(!disabledIds.contains(card->getEffectiveId())
                            && (method != Card::MethodDiscard
                    || Self->canDiscard(player, card->getEffectiveId())));
        } else {
            item->setEnabled(method != Card::MethodDiscard || Self->canDiscard(player, "h"));
        }
        connect(item, &CardItem::clicked, this, &PlayerCardBox::reply);
        items << item;
        areaItems << item;
    }

    int n = items.size();
    if (n == 0)
        return;

    const int rows = (n + maxCardNumberInOneRow - 1) / maxCardNumberInOneRow;
    const int cardWidth = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;
    const int min = qMin(maxCardsInOneRow, maxCardNumberInOneRow / 2);
    const int maxWidth = min * cardWidth + intervalBetweenCards * (min - 1);
    for(int row = 0; row < rows; ++ row) {
        int count = qMin(maxCardNumberInOneRow, areaItems.size());
        double step = 0;
        if (count > 1) {
            step = qMin((double)cardWidth + intervalBetweenCards,
                        (double)(maxWidth - cardWidth) / qMax(count - 1, 0));
        }
        for(int i = 0; i < count; ++ i) {
            CardItem *item = areaItems.takeFirst();
            const double x = topLeft.x() + step * i;
            const double y = topLeft.y() + (cardHeight + intervalBetweenRows) * row;
            item->setPos(x, y);
        }
    }
}

void PlayerCardBox::reply()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    int id = -2;

    if (item)
        id = item->getId();

    clear();
    ClientInstance->onPlayerChooseCard(id);
}

void PlayerCardBox::cancel()
{
    clear();
    ClientInstance->onPlayerChooseCard(-1);
}
