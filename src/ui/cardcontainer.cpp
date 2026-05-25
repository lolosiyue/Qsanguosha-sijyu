#include "cardcontainer.h"
#include "carditem.h"
#include "engine.h"
#include "client.h"
#include "graphics-box.h"
#include "roomscene.h"

CardContainer::CardContainer()
    : _m_background("image/system/card-container.png")
{
    setTransform(QTransform::fromTranslate(-_m_background.width() / 2, -_m_background.height() / 2), true);
    _m_boundingRect = QRectF(QPoint(0, 0), _m_background.size());
    setFlag(ItemIsFocusable);
    setFlag(ItemIsMovable);
    close_button = new CloseButton;
    close_button->setParentItem(this);
    close_button->setPos(517, 21);
    close_button->hide();
    connect(close_button, SIGNAL(clicked()), this, SLOT(clear()));
    scene_width = 0;
    itemCount = 0;
}

void CardContainer::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->drawPixmap(0, 0, _m_background);
}

QRectF CardContainer::boundingRect() const
{
    return _m_boundingRect;
}

void CardContainer::fillCards(const QList<int> &card_ids, const QList<int> &disabled_ids)
{
    if (card_ids.isEmpty() && items.isEmpty())
        return;
    QList<CardItem *> card_items;
	if(!items.isEmpty()){
		if(card_ids.isEmpty()){
			card_items = items;
			items.clear();
		}else{
			retained_stack.push(retained());
			items_stack.push(items);
			foreach(CardItem *item, items)
				item->hide();
			items.clear();
		}
	}

    close_button->hide();
    if (card_items.isEmpty())
        card_items = _createCards(card_ids);

    int card_width = G_COMMON_LAYOUT.m_cardNormalWidth;
    QPointF pos1(30 + card_width / 2, 40 + G_COMMON_LAYOUT.m_cardNormalHeight / 2);
    QPointF pos2(30 + card_width / 2, 184 + G_COMMON_LAYOUT.m_cardNormalHeight / 2);
    int skip = 102;
    qreal whole_width = skip * 4;
    items.append(card_items);
    int n = items.length();
    itemCount = n;

    for (int i = 0; i < n; i++) {
        QPointF pos;
        if (n <= 10) {
            if (i < 5) {
                pos = pos1;
                pos.setX(pos.x() + i * skip);
            } else {
                pos = pos2;
                pos.setX(pos.x() + (i - 5) * skip);
            }
        } else {
            int half = (n + 1) / 2;
            qreal real_skip = whole_width / (half - 1);

            if (i < half) {
                pos = pos1;
                pos.setX(pos.x() + i * real_skip);
            } else {
                pos = pos2;
                pos.setX(pos.x() + (i - half) * real_skip);
            }
        }
        items[i]->setPos(pos);
        items[i]->setHomePos(pos);
        items[i]->setOpacity(1.0);
        items[i]->setHomeOpacity(1.0);
        items[i]->setFlag(QGraphicsItem::ItemIsFocusable);
        items[i]->setEnabled(!disabled_ids.contains(items[i]->getId()));
        items[i]->show();
    }
}

bool CardContainer::_addCardItems(QList<CardItem *> &, const CardsMoveStruct &)
{
    return true;
}

bool CardContainer::retained()
{
    return close_button != nullptr && close_button->isVisible();
}

void CardContainer::clear()
{
    foreach (CardItem *item, items) {
        item->hide();
        delete item;
    }
    items.clear();
    itemCount = 0;
    if (items_stack.isEmpty()) {
        close_button->hide();
        hide();
    } else {
        items = items_stack.pop();
        bool retained = retained_stack.pop();
        fillCards();
        if (retained && close_button)
            close_button->show();
    }
}

void CardContainer::freezeCards(bool is_frozen)
{
    foreach(CardItem *item, items)
        item->setFrozen(is_frozen);
}

QList<CardItem *> CardContainer::removeCardItems(const QList<int> &card_ids, Player::Place)
{
    QList<CardItem *> result;/*
    foreach (int card_id, card_ids) {
        CardItem *to_take = nullptr;
        foreach (CardItem *item, items) {
            if (item->getId() == card_id) {
                to_take = item;
                break;
            }
        }
        if (to_take == nullptr) continue;

        to_take->setEnabled(false);

        CardItem *copy = new CardItem(to_take->getCard());
        copy->setPos(mapToScene(to_take->pos()));
        copy->setEnabled(false);
        result.append(copy);

        if (m_currentPlayer)
            to_take->setFootnote(m_currentPlayer->getLogName());
            //to_take->showAvatar(m_currentPlayer->getGeneralName());
    }*/
	foreach (CardItem *item, items) {
        if (card_ids.contains(item->getId())){
			item->setEnabled(false);
			CardItem *copy = new CardItem(item->getCard());
			copy->setPos(mapToScene(item->pos()));
			copy->setEnabled(false);
			result.append(copy);
			if (m_currentPlayer){
				item->showAvatar(m_currentPlayer->getGeneralName());
				item->setFootnote(m_currentPlayer->getLogName());
			}
		}
    }
    return result;
}

int CardContainer::getFirstEnabled() const
{
    foreach (CardItem *card, items) {
        if (card->isEnabled())
            return card->getId();
    }
    return -1;
}

void CardContainer::startChoose()
{
    close_button->hide();
    foreach (CardItem *item, items) {
        connect(item, SIGNAL(leave_hover()), this, SLOT(grabItem()));
        connect(item, SIGNAL(double_clicked()), this, SLOT(chooseItem()));
    }
}

void CardContainer::startGongxin(const QList<int> &enabled_ids)
{
    if (enabled_ids.isEmpty()) return;
    foreach (CardItem *item, items) {
        if (enabled_ids.contains(item->getId()))
            connect(item, SIGNAL(double_clicked()), this, SLOT(gongxinItem()));
        else
            item->setEnabled(false);
    }
}

void CardContainer::addCloseButton()
{
    close_button->show();
}

void CardContainer::grabItem()
{
    CardItem *card_item = qobject_cast<CardItem *>(sender());
    if (card_item && !collidesWithItem(card_item)) {
        card_item->disconnect(this);
        emit item_chosen(card_item->getId());
    }
}

void CardContainer::chooseItem()
{
    CardItem *card_item = qobject_cast<CardItem *>(sender());
    if (card_item) {
        card_item->disconnect(this);
        emit item_chosen(card_item->getId());
    }
}

void CardContainer::gongxinItem()
{
    CardItem *card_item = qobject_cast<CardItem *>(sender());
    if (card_item) {
        emit item_gongxined(card_item->getId());
        clear();
    }
}

CloseButton::CloseButton()
    : QSanSelectableItem("image/system/close.png", false)
{
    setFlag(ItemIsFocusable);
    setAcceptedMouseButtons(Qt::LeftButton);
}

void CloseButton::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    event->accept();
}

void CloseButton::mouseReleaseEvent(QGraphicsSceneMouseEvent *)
{
    emit clicked();
}

void CardContainer::view(const ClientPlayer *player)
{
    QList<int> card_ids;
    foreach(const Card *card, player->getKnownCards())
        card_ids << card->getEffectiveId();

    fillCards(card_ids);
}

GuanxingBox::GuanxingBox()
    : CardContainer()
{
}

void GuanxingBox::doGuanxing(const QList<int> &cardIds, int type)
{
    if (cardIds.isEmpty()) {
        clear();
        return;
    }

    zhuge.clear();
    this->type = type;
    upItems.clear();
    downItems.clear();
    scene_width = RoomSceneInstance->sceneRect().width();

    foreach (int cardId, cardIds) {
        CardItem *cardItem = new CardItem(Sanguosha->getCard(cardId));
        cardItem->setAutoBack(false);
        cardItem->setFlag(QGraphicsItem::ItemIsFocusable);

        connect(cardItem, &CardItem::released, this, &GuanxingBox::onItemReleased);
        connect(cardItem, &CardItem::clicked, this, &GuanxingBox::onItemClicked);

        if (type == -1)
            downItems << cardItem;
        else
            upItems << cardItem;
        cardItem->setParentItem(this);
    }

    itemCount = upItems.length() + downItems.length();
    prepareGeometryChange();
    GraphicsBox::moveToCenter(this);
    show();

    int cardWidth = G_COMMON_LAYOUT.m_cardNormalWidth;
    int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;
    int totalItems = upItems.length() + downItems.length();
    int width = (cardWidth + cardInterval) * totalItems - cardInterval + 50;
    if (width * 1.5 > RoomSceneInstance->sceneRect().width())
        width = (cardWidth + cardInterval) * ((totalItems + 1) / 2) - cardInterval + 50;

    const int firstRow = itemNumberOfFirstRow();

    int upIndex = 0;
    int downIndex = 0;
    for (int i = 0; i < totalItems; i++) {
        CardItem *cardItem;
        bool isUp;
        if (i < upItems.length()) {
            cardItem = upItems.at(upIndex);
            isUp = true;
            upIndex++;
        } else {
            cardItem = downItems.at(downIndex);
            isUp = false;
            downIndex++;
        }

        QPointF pos;
        if (i < firstRow) {
            pos.setX(25 + (cardWidth + cardInterval) * i);
            pos.setY(isUp ? 45 : 45 + (cardHeight + cardInterval) * (isOneRow() ? 1 : 2));
        }
        else {
            if (totalItems % 2 == 1)
                pos.setX(25 + cardWidth / 2 + cardInterval / 2
                + (cardWidth + cardInterval) * (i - firstRow));
            else
                pos.setX(25 + (cardWidth + cardInterval) * (i - firstRow));
            pos.setY(isUp ? 45 + cardHeight + cardInterval : 45 + cardHeight * 3 + cardInterval * 3);
        }

        cardItem->resetTransform();
        cardItem->setPos(25, isUp ? 45 : 45 + cardHeight * 2);
        cardItem->setHomePos(pos);
        cardItem->goBack(true);
    }
}

void GuanxingBox::mirrorGuanxingStart(const QString &who, bool up_only, const QList<int> &cards)
{
    doGuanxing(cards, up_only ? 1 : 0);

    foreach (CardItem *item, upItems) {
        item->setFlag(QGraphicsItem::ItemIsMovable, false);
        item->disconnect(this);
    }
    foreach (CardItem *item, downItems) {
        item->setFlag(QGraphicsItem::ItemIsMovable, false);
        item->disconnect(this);
    }

    zhuge = who;
}

void GuanxingBox::mirrorGuanxingMove(int from, int to)
{
    if (from == 0 || to == 0)
        return;

    QList<CardItem *> *fromItems = NULL;
    if (from > 0) {
        fromItems = &upItems;
        from = from - 1;
    } else {
        fromItems = &downItems;
        from = -from - 1;
    }

    if (from < fromItems->length()) {
        CardItem *card = fromItems->at(from);

        QList<CardItem *> *toItems = NULL;
        if (to > 0) {
            toItems = &upItems;
            to = to - 1;
        } else {
            toItems = &downItems;
            to = -to - 1;
        }

        if (to >= 0 && to <= toItems->length()) {
            fromItems->removeOne(card);
            toItems->insert(to, card);
            adjust();
        }
    }
}

void GuanxingBox::onItemReleased()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == NULL) return;

    int fromPos = 0;
    if (upItems.contains(item)) {
        fromPos = upItems.indexOf(item);
        upItems.removeOne(item);
        fromPos = fromPos + 1;
    } else {
        fromPos = downItems.indexOf(item);
        downItems.removeOne(item);
        fromPos = -fromPos - 1;
    }

    const int count = upItems.length() + downItems.length();
    const int cardWidth = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;
    const int middleY = 45 + (isOneRow() ? cardHeight : (cardHeight * 2 + cardInterval));

    bool toUpItems;
    if (type == 1)
        toUpItems = true;
    else if (type == -1)
        toUpItems = false;
    else
        toUpItems = (item->y() + cardHeight / 2 <= middleY);

    QList<CardItem *> *items = toUpItems ? &upItems : &downItems;
    bool oddRow = true;
    if (!isOneRow() && count % 2) {
        const qreal y = item->y() + cardHeight / 2;
        if ((y >= 45 + cardHeight && y <= 45 + cardHeight * 2 + cardInterval)
            || y >= 45 + cardHeight * 3 + cardInterval * 3) oddRow = false;
    }
    const int startX = 25 + (oddRow ? 0 : (cardWidth / 2 + cardInterval / 2));
    int c = (item->x() + item->boundingRect().width() / 2 - startX) / cardWidth;
    c = qBound(0, c, items->length());
    items->insert(c, item);

    int toPos = toUpItems ? c + 1: -c - 1;
    ClientInstance->onPlayerDoGuanxingStep(fromPos, toPos);
    adjust();
}

void GuanxingBox::onItemClicked()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == NULL || type != 0) return;

    int fromPos, toPos;
    if (upItems.contains(item)) {
        fromPos = upItems.indexOf(item) + 1;
        toPos = -downItems.size() - 1;
        upItems.removeOne(item);
        downItems.append(item);
    } else {
        fromPos = -downItems.indexOf(item) - 1;
        toPos = upItems.size() + 1;
        downItems.removeOne(item);
        upItems.append(item);
    }

    ClientInstance->onPlayerDoGuanxingStep(fromPos, toPos);
    adjust();
}

void GuanxingBox::adjust()
{
    const int firstRowCount = itemNumberOfFirstRow();
    const int cardWidth = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int card_height = G_COMMON_LAYOUT.m_cardNormalHeight;
    const int count = upItems.length() + downItems.length();

    for (int i = 0; i < upItems.length(); i++) {
        QPointF pos;
        if (i < firstRowCount) {
            pos.setX(25 + (cardWidth + cardInterval) * i);
            pos.setY(45);
        }
        else {
            if (count % 2 == 1)
                pos.setX(25 + cardWidth / 2 + cardInterval / 2
                + (cardWidth + cardInterval) * (i - firstRowCount));
            else
                pos.setX(25 + (cardWidth + cardInterval) * (i - firstRowCount));
            pos.setY(45 + card_height + cardInterval);
        }
        upItems.at(i)->setHomePos(pos);
        upItems.at(i)->goBack(true);
    }

    for (int i = 0; i < downItems.length(); i++) {
        QPointF pos;
        if (i < firstRowCount) {
            pos.setX(25 + (cardWidth + cardInterval) * i);
            pos.setY(45 + (card_height + cardInterval) * (isOneRow() ? 1 : 2));
        }
        else {
            if (count % 2 == 1)
                pos.setX(25 + cardWidth / 2 + cardInterval / 2
                + (cardWidth + cardInterval) * (i - firstRowCount));
            else
                pos.setX(25 + (cardWidth + cardInterval) * (i - firstRowCount));
            pos.setY(45 + card_height * 3 + cardInterval * 3);
        }
        downItems.at(i)->setHomePos(pos);
        downItems.at(i)->goBack(true);
    }
}

int GuanxingBox::itemNumberOfFirstRow() const
{
    const int count = upItems.length() + downItems.length();

    return isOneRow() ? count : (count + 1) / 2;
}

bool GuanxingBox::isOneRow() const
{
    const int count = upItems.length() + downItems.length();

    const int cardWidth = G_COMMON_LAYOUT.m_cardNormalWidth;
    int width = (cardWidth + cardInterval) * count - cardInterval + 50;
    bool oneRow = true;
    if (width * 1.5 > RoomSceneInstance->sceneRect().width()) {
        width = (cardWidth + cardInterval) * (count + 1) / 2 - cardInterval + 50;
        oneRow = false;
    }

    return oneRow;
}

void GuanxingBox::clear()
{
    foreach (CardItem *card_item, upItems)
        card_item->deleteLater();
    foreach (CardItem *card_item, downItems)
        card_item->deleteLater();

    upItems.clear();
    downItems.clear();
    itemCount = 0;

    prepareGeometryChange();
    hide();
}

void GuanxingBox::reply()
{
    QList<int> up_cards, down_cards;
    foreach (CardItem *card_item, upItems)
        up_cards << card_item->getCard()->getId();

    foreach (CardItem *card_item, downItems)
        down_cards << card_item->getCard()->getId();

    ClientInstance->onPlayerReplyGuanxing(up_cards, down_cards);
    clear();
}

QRectF GuanxingBox::boundingRect() const
{
    const int card_width = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int card_height = G_COMMON_LAYOUT.m_cardNormalHeight;
    bool one_row = true;
    int width = (card_width + cardInterval) * itemCount - cardInterval + 50;
    if (width * 1.5 > (scene_width ? scene_width : 800)) {
        width = (card_width + cardInterval) * ((itemCount + 1) / 2) - cardInterval + 50;
        one_row = false;
    }
    int minWidth = 200;
    if (width < minWidth) width = minWidth;
    int height = (one_row ? 1 : 2) * card_height + (one_row ? 0 : cardInterval);
    if (type == 0) height = height * 2 + cardInterval;
    height += 90;

    return QRectF(0, 0, width, height);
}

void GuanxingBox::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (zhuge.isEmpty()) {
        GraphicsBox::paintGraphicsBoxStyle(painter, tr("Please arrange the cards"), boundingRect());
    } else {
        QString playerName = ClientInstance->getPlayerName(zhuge);
        GraphicsBox::paintGraphicsBoxStyle(painter, tr("%1 is arranging the cards").arg(playerName), boundingRect());
    }

    const int card_width = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int card_height = G_COMMON_LAYOUT.m_cardNormalHeight;
    bool one_row = true;
    int width = (card_width + cardInterval) * itemCount - cardInterval + 50;
    if (width * 1.5 > RoomSceneInstance->sceneRect().width()) {
        width = (card_width + cardInterval) * ((itemCount + 1) / 2) - cardInterval + 50;
        one_row = false;
    }
    const int firstRow = itemNumberOfFirstRow();

    for (int i = 0; i < itemCount; ++i) {
        int x, y = 0;
        if (i < firstRow) {
            x = 25 + (card_width + cardInterval) * i;
            y = 45;
        }
        else {
            if (itemCount % 2 == 1)
                x = 25 + card_width / 2 + cardInterval / 2
                + (card_width + cardInterval) * (i - firstRow);
            else
                x = 25 + (card_width + cardInterval) * (i - firstRow);
            y = 45 + card_height + cardInterval;
        }
        QRect top_rect(x, y, card_width, card_height);
        QPixmap pixmap = G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_CHOOSE_GENERAL_BOX_DEST_SEAT);
        if (pixmap.isNull()) {
            painter->save();
            painter->setBrush(QColor(30, 30, 30, 180));
            painter->setPen(QColor(100, 100, 100));
            painter->drawRoundedRect(top_rect, 5, 5);
            painter->restore();
        } else {
            painter->drawPixmap(top_rect, pixmap);
        }
        if (type == 0) {
            IQSanComponentSkin::QSanSimpleTextFont font = G_COMMON_LAYOUT.m_chooseGeneralBoxDestSeatFont;
            font.paintText(painter, top_rect, Qt::AlignCenter, tr("cards on the top of the pile"));
            QRect bottom_rect(x, y + (card_height + cardInterval) * (one_row ? 1 : 2), card_width, card_height);
            if (pixmap.isNull()) {
                painter->save();
                painter->setBrush(QColor(30, 30, 30, 180));
                painter->setPen(QColor(100, 100, 100));
                painter->drawRoundedRect(bottom_rect, 5, 5);
                painter->restore();
            } else {
                painter->drawPixmap(bottom_rect, pixmap);
            }
            font.paintText(painter, bottom_rect, Qt::AlignCenter, tr("cards at the bottom of the pile"));
        }
    }
}

GuanxingXBox::GuanxingXBox()
    : GuanxingBox()
{
}

void GuanxingXBox::addBox3(GuanxingBox* box)
{
    guanxing_box3 = box;
}

void GuanxingXBox::addBox7(GuanxingBox* box)
{
    guanxing_box7 = box;
}

void GuanxingXBox::addBox9(GuanxingBox* box)
{
    guanxing_box9 = box;
}

void GuanxingXBox::doGuanxing(const QList<int> &card_ids, int type)
{
	n = card_ids.length();
	if(n<=3){
		guanxing_box3->doGuanxing(card_ids,type);
	}else if(n<=5)
		GuanxingBox::doGuanxing(card_ids,type);
	else if(n<=7){
		guanxing_box7->doGuanxing(card_ids,type);
	}else
		guanxing_box9->doGuanxing(card_ids,type);
}

void GuanxingXBox::clear()
{
	if(n<=3)
		guanxing_box3->clear();
	else if(n<=5)
		GuanxingBox::clear();
	else if(n<=7)
		guanxing_box7->clear();
	else
		guanxing_box9->clear();
}

void GuanxingXBox::reply()
{
	if(n<=3)
		guanxing_box3->reply();
	else if(n<=5)
		GuanxingBox::reply();
	else if(n<=7)
		guanxing_box7->reply();
	else
		guanxing_box9->reply();
}

PileContainer::PileContainer()
    : m_closeButton(new CloseButton()), m_itemCount(0), m_sceneWidth(0)
{
    m_closeButton->setParentItem(this);
    m_closeButton->hide();
    connect(m_closeButton, SIGNAL(clicked()), this, SLOT(clear()));
    GraphicsBox::stylize(this);
}

void PileContainer::setPileName(const QString &pile_name)
{
    m_pileName = pile_name;
}

void PileContainer::fillCards(const QList<int> &card_ids)
{
    if (card_ids.isEmpty())
        return;

    m_sceneWidth = RoomSceneInstance->sceneRect().width();

    QList<CardItem *> card_items = _createCards(card_ids);
    m_items.append(card_items);
    m_itemCount = m_items.length();
    prepareGeometryChange();

    int card_width = G_COMMON_LAYOUT.m_cardNormalWidth;
    int card_height = G_COMMON_LAYOUT.m_cardNormalHeight;
    bool one_row = true;
    int width = (card_width + cardInterval) * m_itemCount - cardInterval + 50;
    if (width * 1.5 > m_sceneWidth) {
        width = (card_width + cardInterval) * ((m_itemCount + 1) / 2) - cardInterval + 50;
        one_row = false;
    }
    int first_row = one_row ? m_itemCount : (m_itemCount + 1) / 2;

    for (int i = 0; i < m_itemCount; i++) {
        QPointF pos;
        if (i < first_row) {
            pos.setX(25 + (card_width + cardInterval) * i);
            pos.setY(45);
        } else {
            if (m_itemCount % 2 == 1)
                pos.setX(25 + card_width / 2 + cardInterval / 2
                + (card_width + cardInterval) * (i - first_row));
            else
                pos.setX(25 + (card_width + cardInterval) * (i - first_row));
            pos.setY(45 + card_height + cardInterval);
        }
        CardItem *item = m_items[i];
        item->setParentItem(this);
        item->resetTransform();
        item->setPos(pos);
        item->setHomePos(pos);
        item->setOpacity(1.0);
        item->setHomeOpacity(1.0);
        item->setFlag(QGraphicsItem::ItemIsFocusable);
        item->show();
    }

    QRectF rect = boundingRect();
    m_closeButton->setPos(rect.width() - 40, 5);
    m_closeButton->show();
}

void PileContainer::clear()
{
    foreach (CardItem *item, m_items) {
        item->hide();
        item->deleteLater();
    }
    m_items.clear();
    m_pileName.clear();
    m_itemCount = 0;
    m_closeButton->hide();
    prepareGeometryChange();
    hide();
}

QRectF PileContainer::boundingRect() const
{
    const int card_width = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int card_height = G_COMMON_LAYOUT.m_cardNormalHeight;
    bool one_row = true;
    int width = (card_width + cardInterval) * m_itemCount - cardInterval + 50;
    if (width * 1.5 > (m_sceneWidth ? m_sceneWidth : 800)) {
        width = (card_width + cardInterval) * ((m_itemCount + 1) / 2) - cardInterval + 50;
        one_row = false;
    }
    int height = (one_row ? 1 : 2) * card_height + 90 + (one_row ? 0 : cardInterval);

    return QRectF(0, 0, width, height);
}

void PileContainer::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    QString title = m_pileName.isEmpty() ? tr("Pile") : Sanguosha->translate(m_pileName);
    GraphicsBox::paintGraphicsBoxStyle(painter, title, boundingRect());

    const int card_width = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int card_height = G_COMMON_LAYOUT.m_cardNormalHeight;
    bool one_row = true;
    int width = (card_width + cardInterval) * m_itemCount - cardInterval + 50;
    if (width * 1.5 > RoomSceneInstance->sceneRect().width()) {
        width = (card_width + cardInterval) * ((m_itemCount + 1) / 2) - cardInterval + 50;
        one_row = false;
    }
    int first_row = one_row ? m_itemCount : (m_itemCount + 1) / 2;

    for (int i = 0; i < m_itemCount; ++i) {
        int x, y = 0;
        if (i < first_row) {
            x = 25 + (card_width + cardInterval) * i;
            y = 45;
        } else {
            if (m_itemCount % 2 == 1)
                x = 25 + card_width / 2 + cardInterval / 2
                + (card_width + cardInterval) * (i - first_row);
            else
                x = 25 + (card_width + cardInterval) * (i - first_row);
            y = 45 + card_height + cardInterval;
        }
        QPixmap pixmap = G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_CHOOSE_GENERAL_BOX_DEST_SEAT);
        if (pixmap.isNull()) {
            painter->save();
            painter->setBrush(QColor(30, 30, 30, 180));
            painter->setPen(QColor(100, 100, 100));
            painter->drawRoundedRect(x, y, card_width, card_height, 5, 5);
            painter->restore();
        } else {
            painter->drawPixmap(x, y, card_width, card_height, pixmap);
        }
    }
}

QList<CardItem *> PileContainer::removeCardItems(const QList<int> &, Player::Place)
{
    return QList<CardItem *>();
}

bool PileContainer::_addCardItems(QList<CardItem *> &, const CardsMoveStruct &)
{
    return true;
}

