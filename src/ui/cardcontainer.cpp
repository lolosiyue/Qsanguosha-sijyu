#include "cardcontainer.h"
//#include "clientplayer.h"
#include "carditem.h"
#include "engine.h"
#include "client.h"

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

GuanxingBox::GuanxingBox(const QString &box)
    : QSanSelectableItem(box, true)
{
    setFlag(ItemIsFocusable);
    setFlag(ItemIsMovable);
}

void GuanxingBox::doGuanxing(const QList<int> &card_ids, int type)
{
    if (card_ids.isEmpty()) {
        clear();
        return;
    }
    this->type = type;
    up_items.clear();
    down_items.clear();

    foreach (int card_id, card_ids) {
        const Card*c = Sanguosha->getCard(card_id);
		CardItem *card_item = new CardItem(c);
        card_item->setAutoBack(false);
        card_item->setFlag(QGraphicsItem::ItemIsFocusable);
		QStringList footnotes;
		foreach (const QString &tip, c->getTips()){
			footnotes << Sanguosha->translate(tip);
			if(Sanguosha->getGeneral(tip))
				card_item->showAvatar(tip);
		}
		card_item->setFootnote(footnotes.join(","));

        connect(card_item, SIGNAL(released()), this, SLOT(adjust()));

        if(type!=-1) up_items << card_item;
		else down_items << card_item;
        card_item->setParentItem(this);
    }

    show();

    if(type==-1){
		QPointF source(start_x, start_y2);
		for (int i = 0; i < down_items.length(); i++) {
			CardItem *card_item = down_items.at(i);
			QPointF pos(start_x + i * skip, start_y2);
			card_item->setPos(source);
			card_item->setHomePos(pos);
			card_item->goBack(true);
		}
	}else{
		QPointF source(start_x, start_y1);
		for (int i = 0; i < up_items.length(); i++) {
			CardItem *card_item = up_items.at(i);
			QPointF pos(start_x + i * skip, start_y1);
			card_item->setPos(source);
			card_item->setHomePos(pos);
			card_item->goBack(true);
		}
	}
}

void GuanxingBox::adjust()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == nullptr) return;

    up_items.removeOne(item);
    down_items.removeOne(item);

    QList<CardItem *> *items = (item->y()<=middle_y)?&up_items:&down_items;
	if(type==-1) items = &down_items;
	else if(type==1) items = &up_items;
    int n = (item->x() + item->boundingRect().width() / 2 - start_x) / G_COMMON_LAYOUT.m_cardNormalWidth;
    items->insert(qBound(0, n, items->length()), item);

    for (int i = 0; i < up_items.length(); i++) {
        QPointF pos(start_x + i * skip, start_y1);
        up_items.at(i)->setHomePos(pos);
        up_items.at(i)->goBack(true);
    }

    for (int i = 0; i < down_items.length(); i++) {
        QPointF pos(start_x + i * skip, start_y2);
        down_items.at(i)->setHomePos(pos);
        down_items.at(i)->goBack(true);
    }
}

void GuanxingBox::clear()
{
    foreach(CardItem *card_item, up_items)
        card_item->deleteLater();
    foreach(CardItem *card_item, down_items)
        card_item->deleteLater();

    up_items.clear();
    down_items.clear();

    hide();
}

void GuanxingBox::reply()
{
    QList<int> up_cards, down_cards;
    foreach(CardItem *card_item, up_items)
        up_cards << card_item->getId();

    foreach(CardItem *card_item, down_items)
        down_cards << card_item->getId();

    ClientInstance->onPlayerReplyGuanxing(up_cards, down_cards);
    clear();
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

