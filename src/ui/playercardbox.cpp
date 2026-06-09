#include "playercardbox.h"
#include "clientplayer.h"
#include "skin-bank.h"
#include "engine.h"
#include "carditem.h"
#include "client.h"
#include "clientstruct.h"
#include "timed-progressbar.h"
#include "qsanbutton.h"
#include "util.h"
#include "standard.h"

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
      m_compactMode(false), rowCount(0), intervalsBetweenAreas(-1), intervalsBetweenRows(0), maxCardsInOneRow(0)
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
    equipSlots.clear();

    this->player = player;
    this->title = tr("%1: please choose %2's card").arg(Sanguosha->translate(reason)).arg(ClientInstance->getPlayerName(player->objectName()));
    this->flags = flags;
    this->canCancel = canCancel;
    bool handcard = false;
    bool equip = false;
    bool judging = false;

    if (flags.contains(handcardFlag) && !player->isKongcheng()) {
        updateNumbers(player->getHandcardNum());
        handcard = true;
    }

    if (flags.contains(equipmentFlag)) {
        for (int slot = 0; slot < S_EQUIP_AREA_LENGTH; ++slot) {
            if (!player->hasEquipArea(slot)) continue;
            QList<const Card *> slotCards;
            foreach (const Card *e, player->getEquips()) {
                const EquipCard *equipCard = qobject_cast<const EquipCard *>(e->getRealCard());
                if (equipCard && equipCard->getOccupyLocations().contains(slot)) {
                    slotCards << e;
                    break;
                }
            }
            if (!slotCards.isEmpty()) {
                updateNumbers(slotCards.length());
                equipSlots << slot;
            }
        }
        equip = true;
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

    if (equip) {
        foreach (int slot, equipSlots) {
            QList<const Card *> slotCards;
            foreach (const Card *e, player->getEquips()) {
                const EquipCard *equipCard = qobject_cast<const EquipCard *>(e->getRealCard());
                if (equipCard && equipCard->getOccupyLocations().contains(slot)) {
                    slotCards << e;
                    break;
                }
            }
            if (!slotCards.isEmpty()) {
                arrangeCards(slotCards, QPoint(startX, nameRects.at(index).y()));
                ++ index;
            }
        }
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

void PlayerCardBox::globalchooseCard(const ClientPlayer *player, const QString &reason, const QString &flags,
                                     bool handcardVisible, const QList<int> &disabledIds, const QList<int> &handcards)
{
    nameRects.clear();
    rowCount = 0;
    intervalsBetweenAreas = -1;
    intervalsBetweenRows = 0;
    maxCardsInOneRow = 0;

    this->player = player;
    this->handcards = handcards;
    this->title = Sanguosha->translate(reason) + ":" + ClientInstance->text;
    this->flags = flags;
    bool handcard = false;
    bool equip = false;
    bool judging = false;

    if (flags.contains(handcardFlag) && !player->isKongcheng()) {
        updateNumbers(player->getHandcardNum());
        handcard = true;
    }

    if (flags.contains(equipmentFlag) && player->hasEquip()) {
        equip = true;
    }

    if (flags.contains(judgingFlag) && !player->getJudgingArea().isEmpty()) {
        updateNumbers(player->getJudgingArea().length());
        judging = true;
    }

    int max = maxCardsInOneRow;
    int maxNumber = maxCardNumberInOneRow;
    maxCardsInOneRow = qMin(max, maxNumber);
    if (maxCardsInOneRow < 2) maxCardsInOneRow = 2;

    prepareGeometryChange();
    moveToCenter();

    this->handcardVisible = handcardVisible;
    this->disabledIds = disabledIds;

    const int startX = verticalBlankWidth + placeNameAreaWidth + intervalBetweenNameAndCard;
    int index = 0;

    if (handcard) {
        QList<const Card *> cards;
        for (int i = 0; i < handcards.length(); ++i)
            cards << Sanguosha->getCard(handcards.at(i));
        arrangeCards(cards, QPoint(startX, nameRects.at(index).y()), true);

        ++index;
    }

    if (equip) {
        QList<const Card *> equips;
        foreach (const Card *e, player->getEquips()) {
            if (!disabledIds.contains(e->getEffectiveId()))
                equips << e;
        }
        updateNumbers(equips.length());
        arrangeCards(equips, QPoint(startX, nameRects.at(index).y()), true);

        ++index;
    }

    if (judging)
        arrangeCards(player->getJudgingArea(), QPoint(startX, nameRects.at(index).y()), true);
    hide();
}

void PlayerCardBox::setfalse()
{
    foreach (CardItem *item, items) {
        item->setEnabled(false);
    }
}

void PlayerCardBox::reset()
{
    foreach (CardItem *item, items) {
        item->setEnabled(true);
    }
}

void PlayerCardBox::paintLayout(QPainter *painter)
{
    if (m_compactMode) {
        paintCompactLayout(painter);
        return;
    }

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
        foreach (int slot, equipSlots) {
            QString areaName;
            switch (slot) {
                case 0: areaName = tr("Weapon area"); break;
                case 1: areaName = tr("Armor area"); break;
                case 2: areaName = tr("Defensive horse"); break;
                case 3: areaName = tr("Offensive horse"); break;
                case 4: areaName = tr("Treasure area"); break;
                default: areaName = QString(); break;
            }
            if (!areaName.isEmpty()) {
                G_COMMON_LAYOUT.playerCardBoxPlaceNameText.paintText(painter, nameRects.at(index),
                                                                     Qt::AlignCenter,
                                                                     areaName);
                ++ index;
            }
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

void PlayerCardBox::paintCompactLayout(QPainter *painter)
{
    painter->save();
    
    QString playerName = ClientInstance->getPlayerName(player->objectName());
    QString kingdom = player->getKingdom();
    QString header = QString("%1 (%2)").arg(playerName).arg(Sanguosha->translate(kingdom));
    
    QFont font = painter->font();
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(Qt::white);
    painter->drawText(10, 15, header);
    
    font.setBold(false);
    painter->setFont(font);
    
    QString cardsText = tr("Selected: ");
    bool first = true;
    foreach (CardItem *item, items) {
        if (item && item->isSelected()) {
            const Card *card = item->getCard();
            if (card) {
                if (!first) cardsText += ", ";
                cardsText += Sanguosha->translate(card->objectName());
                first = false;
            }
        }
    }
    if (first) cardsText += tr("None");
    
    painter->drawText(10, 32, cardsText);
    
    painter->restore();
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

void PlayerCardBox::arrangeCards(const QList<const Card *> &cards, const QPoint &topLeft, bool is_globalchoose)
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
        if (!is_globalchoose)
            connect(item, &CardItem::clicked, this, &PlayerCardBox::reply);
        else
            connect(item, &CardItem::clicked, this, &PlayerCardBox::global_click);
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

void PlayerCardBox::global_click()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (!item) return;

    item->setSelected(!item->isSelected());

    int index = items.indexOf(item);
    int id;
    if (handcards.length() > 0 && index < handcards.length())
        id = handcards.at(index);
    else
        id = item->getId();
    emit global_choose(player, id);
}

void PlayerCardBox::setCompactMode(bool compact)
{
    m_compactMode = compact;
    prepareGeometryChange();
}

QRectF PlayerCardBox::boundingRect() const
{
    if (player == NULL)
        return QRectF();

    if (m_compactMode) {
        return QRectF(0, 0, 150, 40);
    }

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
