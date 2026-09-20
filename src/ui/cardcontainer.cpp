#include "cardcontainer.h"
#include "carditem.h"
#include "engine.h"
#include "client.h"
#include "graphics-box.h"
#include "roomscene.h"

static int pileContainerTitleWidth(const QString &pileName)
{
	const QString title = pileName.isEmpty() ? QObject::tr("Pile") : Sanguosha->translate(pileName);
	const IQSanComponentSkin::QSanSimpleTextFont &font = G_COMMON_LAYOUT.graphicsBoxTitleFont;
	return title.length() * (font.m_fontSize.width() + font.m_spacing) - font.m_spacing + 80;
}

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
    connect(close_button, &CloseButton::clicked, this, [this]() {
        if (m_gongxinActive) submitGongxin();
        else clear();
    });
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
    m_chooseActive = false;
    m_keyboardCardId = -1;
    m_gongxinActive = false;
    m_gongxinSelection = -1;
    m_gongxinEnabled.clear();
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
        items[i]->setScale(1.0);
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
    m_chooseActive = false;
    m_keyboardCardId = -1;
    m_gongxinActive = false;
    m_gongxinSelection = -1;
    m_gongxinEnabled.clear();
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
    emit gongxinDraftChanged();
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
    m_chooseActive = true;
    m_gongxinActive = false;
    m_keyboardCardId = -1;
    close_button->hide();
    foreach (CardItem *item, items) {
        item->setSelected(false);
        item->setScale(1.0);
        item->setPos(item->homePos());
        connect(item, SIGNAL(leave_hover()), this, SLOT(grabItem()), Qt::UniqueConnection);
        connect(item, SIGNAL(double_clicked()), this, SLOT(chooseItem()), Qt::UniqueConnection);
    }
}

bool CardContainer::handleChooseKey(int key)
{
    const bool forward = key == Qt::Key_Right || key == Qt::Key_Down || key == Qt::Key_Tab;
    const bool backward = key == Qt::Key_Left || key == Qt::Key_Up || key == Qt::Key_Backtab;
    const bool confirm = key == Qt::Key_Return || key == Qt::Key_Enter;
    const bool select = key == Qt::Key_Space;
    if (!m_chooseActive || !isVisible() || (!forward && !backward && !confirm && !select)) return false;

    QList<CardItem *> candidates;
    int current = -1;
    for (CardItem *item : items) {
        if (!item->isVisible() || !item->isEnabled()) continue;
        if (item->isSelected()) current = candidates.size();
        candidates << item;
    }
    if (candidates.isEmpty()) return true;

    int next = current;
    if (current < 0)
        next = backward ? candidates.size() - 1 : 0;
    else if (forward || backward)
        next = (current + (forward ? 1 : candidates.size() - 1)) % candidates.size();

    CardItem *candidate = candidates.at(next);
    if (confirm) {
        // Reuse the mouse handler; it disconnects this choice before replying.
        emit candidate->double_clicked();
        return true;
    }
    for (CardItem *item : items) {
        item->setSelected(item == candidate);
        item->setPos(item->homePos() + QPointF(0, item == candidate ? -15 : 0));
    }
    return true;
}

void CardContainer::startGongxin(const QList<int> &enabled_ids)
{
    m_chooseActive = false;
    m_keyboardCardId = -1;
    m_gongxinActive = true;
    m_gongxinSelection = -1;
    m_gongxinEnabled = enabled_ids;
    foreach (CardItem *item, items) {
        item->setSelected(false);
        item->setScale(1.0);
        item->setEnabled(item->getId() >= 0 && enabled_ids.contains(item->getId()));
        connect(item, &CardItem::clicked, this, &CardContainer::selectGongxinItem, Qt::UniqueConnection);
        connect(item, &CardItem::double_clicked, this, &CardContainer::gongxinItem, Qt::UniqueConnection);
    }
    emit gongxinDraftChanged();
}

bool CardContainer::handleGongxinKey(int key)
{
    const bool forward = key == Qt::Key_Right || key == Qt::Key_Down || key == Qt::Key_Tab;
    const bool backward = key == Qt::Key_Left || key == Qt::Key_Up || key == Qt::Key_Backtab;
    const bool confirm = key == Qt::Key_Return || key == Qt::Key_Enter;
    if (!m_gongxinActive || !isVisible()
        || (!forward && !backward && !confirm && key != Qt::Key_Space && key != Qt::Key_Escape))
        return false;
    if (key == Qt::Key_Escape) {
        // The caller checks request cancellation policy before forwarding Escape.
        submitGongxin();
        return true;
    }
    QList<CardItem *> candidates;
    int current = -1;
    for (CardItem *item : items) {
        if (!item->isVisible() || !item->isEnabled()) continue;
        if (item->getId() == m_keyboardCardId) current = candidates.size();
        candidates << item;
    }
    if (candidates.isEmpty()) {
        // Read-only inspection still needs an acknowledgement, including no cards.
        if (confirm) submitGongxin();
        return true;
    }
    int next = current;
    if (next < 0) next = backward ? candidates.size() - 1 : 0;
    else if (forward || backward)
        next = (next + (forward ? 1 : candidates.size() - 1)) % candidates.size();
    CardItem *candidate = candidates.at(next);
    m_keyboardCardId = candidate->getId();
    for (CardItem *item : items) item->setScale(item == candidate ? 1.06 : 1.0);
    if (key == Qt::Key_Space)
        selectGongxinCard(m_keyboardCardId, m_gongxinSelection != m_keyboardCardId);
    else if (confirm)
        submitGongxin(m_gongxinSelection);
    return true;
}

bool CardContainer::selectGongxinCard(int cardId, bool selected)
{
    if (!m_gongxinActive || !isVisible() || cardId < 0 || !m_gongxinEnabled.contains(cardId)) return false;
    CardItem *candidate = nullptr;
    for (CardItem *item : items)
        if (item->getId() == cardId && item->isEnabled()) candidate = item;
    if (!candidate) return false;
    const int next = selected ? cardId : (m_gongxinSelection == cardId ? -1 : m_gongxinSelection);
    if (next == m_gongxinSelection) return true;
    m_gongxinSelection = next;
    for (CardItem *item : items) {
        const bool wasSelected = item->isSelected();
        item->setSelected(item->getId() == next);
        item->setHomePos(item->homePos() + QPointF(0, (item->isSelected() ? -15 : 0) - (wasSelected ? -15 : 0)));
        item->setPos(item->homePos());
        item->update();
    }
    emit gongxinDraftChanged();
    return true;
}

bool CardContainer::submitGongxin(int cardId)
{
    if (!m_gongxinActive || !isVisible()) return false;
    if (cardId >= 0 && !selectGongxinCard(cardId, true)) return false;
    // The status callback may clear this container synchronously. Consume the
    // draft first, and never clear a restored or replacement card display.
    const QList<CardItem *> submittedItems = items;
    m_gongxinActive = false;
    emit item_gongxined(cardId);
    if (!m_gongxinActive && items == submittedItems) clear();
    return true;
}

void CardContainer::selectGongxinItem()
{
    if (CardItem *item = qobject_cast<CardItem *>(sender()))
        selectGongxinCard(item->getId(), m_gongxinSelection != item->getId());
}

void CardContainer::addCloseButton()
{
    close_button->show();
}

void CardContainer::grabItem()
{
    CardItem *card_item = qobject_cast<CardItem *>(sender());
    if (m_chooseActive && card_item && card_item->isEnabled() && !collidesWithItem(card_item)) {
        m_chooseActive = false;
        card_item->disconnect(this);
        emit item_chosen(card_item->getId());
    }
}

void CardContainer::chooseItem()
{
    CardItem *card_item = qobject_cast<CardItem *>(sender());
    if (m_chooseActive && card_item && card_item->isEnabled()) {
        m_chooseActive = false;
        card_item->disconnect(this);
        emit item_chosen(card_item->getId());
    }
}

void CardContainer::gongxinItem()
{
    CardItem *card_item = qobject_cast<CardItem *>(sender());
    if (card_item) submitGongxin(card_item->getId());
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
    : CardContainer(), type(0)
{
}

void GuanxingBox::doGuanxing(const QList<int> &cardIds, int type)
{
    m_keyboardCardId = -1;
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
    emit draftChanged();
}

QList<int> GuanxingBox::cardIds(const QList<CardItem *> &items) const
{
    QList<int> ids;
    foreach (CardItem *item, items) {
        if (item != NULL && item->getCard() != NULL)
            ids << item->getCard()->getId();
    }
    return ids;
}

QList<int> GuanxingBox::topCards() const
{
    return cardIds(upItems);
}

QList<int> GuanxingBox::bottomCards() const
{
    return cardIds(downItems);
}

bool GuanxingBox::editable() const
{
    return isVisible() && (!upItems.isEmpty() || !downItems.isEmpty())
        && zhuge.isEmpty() && (type == -1 || type == 0 || type == 1);
}

bool GuanxingBox::moveCard(int cardId, bool toBottom, int index)
{
    if (!editable() || (type == 1 && toBottom) || (type == -1 && !toBottom))
        return false;

    CardItem *item = NULL;
    bool fromBottom = false;
    int fromIndex = -1;
    for (int i = 0; i < upItems.size(); ++i) {
        if (upItems.at(i)->getCard()->getId() == cardId) {
            item = upItems.at(i);
            fromIndex = i;
            break;
        }
    }
    if (item == NULL) {
        for (int i = 0; i < downItems.size(); ++i) {
            if (downItems.at(i)->getCard()->getId() == cardId) {
                item = downItems.at(i);
                fromBottom = true;
                fromIndex = i;
                break;
            }
        }
    }
    if (item == NULL)
        return false;

    QList<CardItem *> *fromItems = fromBottom ? &downItems : &upItems;
    QList<CardItem *> *toItems = toBottom ? &downItems : &upItems;
    const int destinationSize = toItems->size() - (fromItems == toItems ? 1 : 0);
    if (index < 0 || index > destinationSize)
        return false;

    const int fromPos = fromBottom ? -fromIndex - 1 : fromIndex + 1;
    fromItems->removeAt(fromIndex);
    applyMove(item, toBottom, index, fromPos);
    return true;
}

bool GuanxingBox::handleArrangeKey(int key, Qt::KeyboardModifiers modifiers)
{
    const bool forward = key == Qt::Key_Right || key == Qt::Key_Down || key == Qt::Key_Tab;
    const bool backward = key == Qt::Key_Left || key == Qt::Key_Up || key == Qt::Key_Backtab;
    const bool confirm = key == Qt::Key_Return || key == Qt::Key_Enter;
    if (!editable() || (!forward && !backward && !confirm && key != Qt::Key_Space)) return false;
    if (confirm) {
        reply();
        return true;
    }
    const QList<CardItem *> candidates = upItems + downItems;
    int current = -1;
    for (int i = 0; i < candidates.size(); ++i)
        if (candidates.at(i)->getId() == m_keyboardCardId) current = i;
    if (current < 0) current = backward ? candidates.size() - 1 : 0;
    CardItem *candidate = candidates.at(current);
    if ((modifiers & Qt::ShiftModifier) && key != Qt::Key_Tab && key != Qt::Key_Backtab) {
        const bool bottom = downItems.contains(candidate);
        const QList<CardItem *> &pile = bottom ? downItems : upItems;
        const int index = pile.indexOf(candidate);
        // Use the same draft mutation and step notification as drag-and-drop.
        if (key == Qt::Key_Left || key == Qt::Key_Right)
            moveCard(candidate->getId(), bottom, qBound(0, index + (forward ? 1 : -1), int(pile.size()) - 1));
        else if (key == Qt::Key_Up || key == Qt::Key_Down) {
            const bool toBottom = key == Qt::Key_Down;
            if (toBottom != bottom)
                moveCard(candidate->getId(), toBottom, (toBottom ? downItems : upItems).size());
        }
    } else if (key == Qt::Key_Space) {
        // Space transfers the current card between piles when both are allowed.
        const bool toBottom = upItems.contains(candidate);
        moveCard(candidate->getId(), toBottom, (toBottom ? downItems : upItems).size());
    } else if (m_keyboardCardId >= 0) {
        current = (current + (forward ? 1 : candidates.size() - 1)) % candidates.size();
        candidate = candidates.at(current);
    }
    m_keyboardCardId = candidate->getId();
    for (CardItem *item : candidates) {
        item->setScale(item == candidate ? 1.06 : 1.0);
        item->setZValue(item == candidate ? 1 : 0);
    }
    return true;
}

void GuanxingBox::applyMove(CardItem *item, bool toBottom, int index, int fromPos)
{
    QList<CardItem *> *items = toBottom ? &downItems : &upItems;
    items->insert(index, item);
    const int toPos = toBottom ? -index - 1 : index + 1;
    ClientInstance->onPlayerDoGuanxingStep(fromPos, toPos);
    adjust();
    emit draftChanged();
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
    applyMove(item, !toUpItems, c, fromPos);
}

void GuanxingBox::onItemClicked()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == NULL || type != 0) return;

    int fromPos;
    if (upItems.contains(item)) {
        fromPos = upItems.indexOf(item) + 1;
        upItems.removeOne(item);
        applyMove(item, true, downItems.size(), fromPos);
    } else {
        fromPos = -downItems.indexOf(item) - 1;
        downItems.removeOne(item);
        applyMove(item, false, upItems.size(), fromPos);
    }
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
    m_keyboardCardId = -1;
    foreach (CardItem *card_item, upItems)
        card_item->deleteLater();
    foreach (CardItem *card_item, downItems)
        card_item->deleteLater();

    upItems.clear();
    downItems.clear();
    itemCount = 0;

    prepareGeometryChange();
    hide();
    emit draftChanged();
}

void GuanxingBox::reply()
{
    if (!editable()) return;
    QList<int> up_cards, down_cards;
    foreach (CardItem *card_item, upItems)
        up_cards << card_item->getCard()->getId();

    foreach (CardItem *card_item, downItems)
        down_cards << card_item->getCard()->getId();

    // Consume the visible draft before a synchronous reply can replace it.
    clear();
    ClientInstance->onPlayerReplyGuanxing(up_cards, down_cards);
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
    int content_width = (card_width + cardInterval) * m_itemCount - cardInterval;
    if ((content_width + 50) * 1.5 > m_sceneWidth) {
        one_row = false;
    }
    int first_row = one_row ? m_itemCount : (m_itemCount + 1) / 2;
	const int container_width = boundingRect().width();

    for (int i = 0; i < m_itemCount; i++) {
        QPointF pos;
		const int row_count = i < first_row ? first_row : m_itemCount - first_row;
		const int row_width = (card_width + cardInterval) * row_count - cardInterval;
		const int row_index = i < first_row ? i : i - first_row;
		pos.setX((container_width - row_width) / 2 + (card_width + cardInterval) * row_index);
        if (i < first_row) {
            pos.setY(45);
        } else {
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
	width = qMax(width, pileContainerTitleWidth(m_pileName));
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
    int content_width = (card_width + cardInterval) * m_itemCount - cardInterval;
    if ((content_width + 50) * 1.5 > RoomSceneInstance->sceneRect().width()) {
        one_row = false;
    }
    int first_row = one_row ? m_itemCount : (m_itemCount + 1) / 2;
	const int container_width = boundingRect().width();

    for (int i = 0; i < m_itemCount; ++i) {
        int x, y = 0;
		const int row_count = i < first_row ? first_row : m_itemCount - first_row;
		const int row_width = (card_width + cardInterval) * row_count - cardInterval;
		const int row_index = i < first_row ? i : i - first_row;
		x = (container_width - row_width) / 2 + (card_width + cardInterval) * row_index;
        if (i < first_row) {
            y = 45;
        } else {
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

