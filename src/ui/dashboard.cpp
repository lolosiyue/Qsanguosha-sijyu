#include "dashboard.h"
#include "engine.h"
#include "standard.h"
#include "roomscene.h"
#include "pixmapanimation.h"
#include "timed-progressbar.h"
#include "rolecombobox.h"
#include "clientstruct.h"
#include "carditem.h"
#include "aux-skills.h"
#include "sprite.h"
#include "cardcontainer.h"
#include "button.h"
#include "magatamas-item.h"

#include <QGraphicsTextItem>

using namespace QSanProtocol;

Dashboard::Dashboard(QGraphicsPixmapItem *widget)
    : button_widget(widget), selected(nullptr), view_as_skill(nullptr), filter(nullptr), m_secondarySkillDock(nullptr),
      _m_guhuoActive(false)
{
    Q_ASSERT(button_widget);
    _dlayout = &G_DASHBOARD_LAYOUT;
    _dlayoutDouble = &G_DASHBOARD_LAYOUT_DOUBLE;
    _m_layout = _dlayout;
    m_currentPlayer = Self;
    m_player = m_currentPlayer;
    _m_leftFrame = _m_rightFrame = _m_middleFrame = nullptr;
    animations = new EffectAnimation();
    pending_card = nullptr;
    _m_pile_expanded = QMap<QString, QList<int> >();
    _m_filterContainer = nullptr;
    _m_filterCurrentCategory = "";
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        _m_equipSkillBtns[i] = nullptr;
        _m_isEquipsAnimOn[i] = false;
    }
    _m_width = G_DASHBOARD_LAYOUT.m_leftWidth + G_DASHBOARD_LAYOUT.m_rightWidth + 20;

    _createLeft();
    _createMiddle();
    _createRight();

    _createControls();
    _createExtraButtons();

    _m_sort_menu = new QMenu(RoomSceneInstance->mainWindow());
    _m_shefu_menu = new QMenu(RoomSceneInstance->mainWindow());
}

void Dashboard::bindPlayer(ClientPlayer *player)
{
    if (player == nullptr || player == m_currentPlayer)
        return;

    if (m_currentPlayer != nullptr) {
        disconnect(m_currentPlayer, nullptr, this, nullptr);
        if (_m_roleComboBox != nullptr)
            disconnect(m_currentPlayer, nullptr, _m_roleComboBox, nullptr);
        disconnect(m_currentPlayer->getMarkDoc(), SIGNAL(contentsChanged()), this, SLOT(updateMarks()));
    }

    m_currentPlayer = player;
    m_player = m_currentPlayer;
    PlayerCardContainer::setPlayer(m_currentPlayer);

    QList<CardItem *> card_items = _createCards(m_currentPlayer->handCards());
    for (int i = 0; i < m_handCards.length(); i++)
        delete m_handCards[i];
    m_handCards.clear();
    addHandCards(card_items);
    adjustCards(true);
    _updateFrames();
    updateAvatar();
    updateSmallAvatar();
    updateHp();
    _updateEquips();
    updateDelayedTricks();
    refreshHandCardTooltips();
    refresh();
}

int Dashboard::_findEquipSkillButtonIndex(const QString &skillName) const
{
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) {
        if (_m_equipSkillBtns[i] != nullptr && _m_equipSkillBtns[i]->objectName() == skillName)
            return i;
    }
    return -1;
}

QSanSkillButton *Dashboard::_getEquipSkillButton(const CardItem *equip) const
{
    if (equip == nullptr) return nullptr;
    const Card *card = equip->getCard();
    if (card == nullptr) return nullptr;
    QString skillName;
    if (card->isKindOf("Weapon"))
        skillName = card->objectName();
    else if (card->isKindOf("Armor"))
        skillName = card->objectName();
    else if (card->isKindOf("Horse"))
        skillName = card->objectName();
    else if (card->isKindOf("Treasure"))
        skillName = card->objectName();
    else
        return nullptr;
    int index = _findEquipSkillButtonIndex(skillName);
    if (index >= 0)
        return _m_equipSkillBtns[index];
    return nullptr;
}

void Dashboard::_setEquipSkillHighlight(const QString &skillName, bool turnOn)
{
    int index = _findEquipSkillButtonIndex(skillName);
    if (index >= 0 && _m_equipSkillBtns[index] != nullptr)
        _m_equipSkillBtns[index]->highlight(turnOn);
}

bool Dashboard::isAvatarUnderMouse()
{
    return _m_rightFrame->isUnderMouse();
}

void Dashboard::hideControlButtons()
{
    button_widget->hide();
}

void Dashboard::showControlButtons()
{
    button_widget->show();
}

void Dashboard::showProgressBar(QSanProtocol::Countdown countdown)
{
    _m_progressBarItem->setCountdown(countdown);
    _m_progressBarItem->show();
    _m_progressBarItem->start();
}

QGraphicsItem *Dashboard::getMouseClickReceiver()
{
    return _m_rightFrame;
}

void Dashboard::_createLeft()
{
    _m_leftFrame = new QGraphicsPixmapItem(this);
}

int Dashboard::getButtonWidgetWidth() const
{
    if (button_widget == nullptr) return 0;
    return button_widget->boundingRect().width();
}

void Dashboard::_createMiddle()
{
    _m_middleFrame = new QGraphicsPixmapItem(this);
}

void Dashboard::_adjustComponentZValues(bool killed)
{
    PlayerCardContainer::_adjustComponentZValues(killed);
}

int Dashboard::width()
{
    return _m_width;
}

void Dashboard::repaintAll(bool all)
{
    PlayerCardContainer::repaintAll(all);
    _updateFrames();
    if (all) {
        foreach (CardItem *item, m_handCards)
            item->refreshTooltip();
    }
}

void Dashboard::_createRight()
{
    _m_rightFrame = new QGraphicsPixmapItem(this);
}

void Dashboard::_updateSkillDockGeometry()
{
    if (_m_skillDock == nullptr) return;
    int rightWidth = _dlayout->m_rightWidth;
    if (Self && Self->getGeneral2())
        rightWidth = _dlayoutDouble->m_rightWidth;
    _m_skillDock->setPos(_m_width - rightWidth + _dlayout->m_skillDockOffset.x(), _dlayout->m_skillDockOffset.y());
    if (m_secondarySkillDock != nullptr) {
        m_secondarySkillDock->setPos(_m_width - rightWidth + _dlayout->m_skillDockOffset.x(),
            _dlayout->m_skillDockOffset.y() + _m_skillDock->boundingRect().height() + 5);
    }
}

void Dashboard::_clearSkillDock(QSanInvokeSkillDock *dock)
{
    if (dock == nullptr) return;
    QList<QSanSkillButton *> buttons = dock->findChildren<QSanSkillButton *>();
    foreach (QSanSkillButton *button, buttons) {
        disconnect(button, nullptr, this, nullptr);
        button->deleteLater();
    }
}

void Dashboard::refreshLayout()
{
    _updateFrames();
    _updateSkillDockGeometry();
    adjustCards();
}

void Dashboard::_updateFrames()
{
    _paintLeftFrame();
    QRect rect = QRect(0, 0, _m_width - _dlayout->m_leftWidth - _dlayout->m_rightWidth, _dlayout->m_normalHeight);
    _paintMiddleFrame(rect);
    _paintRightFrame();
}

void Dashboard::_paintLeftFrame()
{
    QPixmap leftPixmap(_dlayout->m_leftWidth, _dlayout->m_normalHeight);
    leftPixmap.fill(Qt::transparent);
    QPainter painter(&leftPixmap);
    painter.drawPixmap(0, 0, G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_DASHBOARD_LEFTFRAME));
    _m_leftFrame->setPixmap(leftPixmap);
    _m_leftFrame->setPos(0, 0);
}

void Dashboard::_paintMiddleFrame(const QRect &rect)
{
    QPixmap middlePixmap(rect.width(), rect.height());
    middlePixmap.fill(Qt::transparent);
    QPainter painter(&middlePixmap);
    painter.drawPixmap(0, 0, G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_DASHBOARD_MIDDLEFRAME));
    _m_middleFrame->setPixmap(middlePixmap);
    _m_middleFrame->setPos(_dlayout->m_leftWidth, 0);
}

void Dashboard::_paintRightFrame()
{
    int rightWidth = _dlayout->m_rightWidth;
    if (Self && Self->getGeneral2())
        rightWidth = _dlayoutDouble->m_rightWidth;
    QPixmap rightPixmap(rightWidth, _dlayout->m_normalHeight);
    rightPixmap.fill(Qt::transparent);
    QPainter painter(&rightPixmap);
    painter.drawPixmap(0, 0, G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_DASHBOARD_RIGHTFRAME));
    _m_rightFrame->setPixmap(rightPixmap);
    _m_rightFrame->setPos(_m_width - rightWidth, 0);
}

void Dashboard::setTrust(bool trust)
{
    if (trust) {
        trusting_item = new QGraphicsPathItem(this);
        trusting_text = new QGraphicsSimpleTextItem(trusting_item);
        trusting_text->setText(tr("Trust"));
        trusting_text->setBrush(Qt::yellow);
        QFont font = trusting_text->font();
        font.setBold(true);
        font.setPointSize(14);
        trusting_text->setFont(font);
        trusting_text->setPos((_m_width - trusting_text->boundingRect().width()) / 2, 10);
    } else {
        if (trusting_item) {
            delete trusting_item;
            trusting_item = nullptr;
            trusting_text = nullptr;
        }
    }
}

void Dashboard::killPlayer()
{
    PlayerCardContainer::killPlayer();
    foreach (CardItem *item, m_handCards)
        item->setEnabled(false);
}

void Dashboard::revivePlayer()
{
    PlayerCardContainer::revivePlayer();
    foreach (CardItem *item, m_handCards)
        item->setEnabled(true);
}

bool Dashboard::_addCardItems(QList<CardItem *> &card_items, const CardsMoveStruct &moveInfo)
{
    foreach (CardItem *item, card_items) {
        item->setParentItem(_m_middleFrame);
    }
    addHandCards(card_items);
    return true;
}

void Dashboard::addHandCards(QList<CardItem *> &card_items)
{
    foreach (CardItem *card_item, card_items) {
        _addHandCard(card_item);
    }
    updateHandcardNum();
}

void Dashboard::_addHandCard(CardItem *card_item, bool prepend, const QString &footnote)
{
    card_item->setParentItem(_m_middleFrame);
    if (prepend)
        m_handCards.prepend(card_item);
    else
        m_handCards.append(card_item);

    connect(card_item, SIGNAL(clicked()), this, SLOT(onCardItemClicked()));
    connect(card_item, SIGNAL(double_clicked()), this, SLOT(onCardItemDoubleClicked()));
    connect(card_item, SIGNAL(thrown()), this, SLOT(onCardItemThrown()));
    connect(card_item, SIGNAL(show_tooltip()), this, SLOT(onCardItemHover()));
    connect(card_item, SIGNAL(hide_tooltip()), this, SLOT(onCardItemLeaveHover()));
    connect(card_item, SIGNAL(mark_changed()), this, SLOT(onMarkChanged()));

    if (!footnote.isEmpty())
        card_item->setFootnote(footnote);
}

void Dashboard::selectCard(const QString &pattern, bool forward, bool multiple)
{
    QRegExp rx(pattern);
    rx.setCaseSensitivity(Qt::CaseInsensitive);
    rx.setPatternSyntax(QRegExp::Wildcard);

    int index = -1;
    if (selected != nullptr) {
        index = m_handCards.indexOf(selected);
        if (forward)
            index++;
        else
            index--;
        if (index < 0)
            index = m_handCards.length() - 1;
        else if (index >= m_handCards.length())
            index = 0;
    } else {
        if (forward)
            index = 0;
        else
            index = m_handCards.length() - 1;
    }

    int start = index;
    do {
        CardItem *item = m_handCards.at(index);
        const Card *card = item->getCard();
        if (card != nullptr && rx.exactMatch(card->objectName()) && item->isEnabled()) {
            selectCard(item, true);
            return;
        }
        if (forward) {
            index++;
            if (index >= m_handCards.length())
                index = 0;
        } else {
            index--;
            if (index < 0)
                index = m_handCards.length() - 1;
        }
    } while (index != start);
}

void Dashboard::selectEquip(int position)
{
    if (position < 0 || position >= S_EQUIP_AREA_LENGTH) return;
    if (_m_equipCards[position] == nullptr) return;
    selectCard(_m_equipCards[position], true);
}

void Dashboard::selectOnlyCard(bool need_only)
{
    if (m_handCards.length() == 1 && m_handCards.first()->isEnabled()) {
        selectCard(m_handCards.first(), true);
    } else if (!need_only) {
        int index = -1;
        for (int i = 0; i < m_handCards.length(); i++) {
            if (m_handCards[i]->isEnabled()) {
                if (index == -1)
                    index = i;
                else
                    return;
            }
        }
        if (index != -1)
            selectCard(m_handCards[index], true);
    }
}

const Card *Dashboard::getSelected() const
{
    if (selected == nullptr) return nullptr;
    return selected->getCard();
}

void Dashboard::selectCard(CardItem *item, bool isSelected)
{
    if (item == nullptr) return;

    if (isSelected) {
        if (selected != nullptr && selected != item) {
            selected->setSelected(false);
            QPointF homePos = selected->homePos();
            homePos.setY(homePos.y() - S_PENDING_OFFSET_Y);
            selected->setHomePos(homePos);
            selected->goBack(true);
        }
        item->setSelected(true);
        QPointF homePos = item->homePos();
        homePos.setY(homePos.y() + S_PENDING_OFFSET_Y);
        item->setHomePos(homePos);
        item->goBack(true);
        selected = item;
        emit card_selected(item->getCard());
    } else {
        item->setSelected(false);
        QPointF homePos = item->homePos();
        homePos.setY(homePos.y() - S_PENDING_OFFSET_Y);
        item->setHomePos(homePos);
        item->goBack(true);
        if (selected == item) {
            selected = nullptr;
            emit card_selected(nullptr);
        }
    }
}

void Dashboard::unselectAll(const CardItem *except)
{
    foreach (CardItem *item, m_handCards) {
        if (item == except) continue;
        if (item->isSelected()) {
            item->setSelected(false);
            QPointF homePos = item->homePos();
            homePos.setY(homePos.y() - S_PENDING_OFFSET_Y);
            item->setHomePos(homePos);
            item->goBack(true);
        }
    }
    foreach (CardItem *item, pendings) {
        if (item == except) continue;
        if (item->isSelected()) {
            item->setSelected(false);
            QPointF homePos = item->homePos();
            homePos.setY(homePos.y() - S_PENDING_OFFSET_Y);
            item->setHomePos(homePos);
            item->goBack(true);
        }
    }
    if (selected != except) {
        selected = nullptr;
        emit card_selected(nullptr);
    }
}

QRectF Dashboard::boundingRect() const
{
    return QRectF(0, 0, _m_width, _dlayout->m_normalHeight);
}

void Dashboard::setWidth(int width)
{
    _m_width = width;
    _updateFrames();
    _updateSkillDockGeometry();
    adjustCards();
}

QSanSkillButton *Dashboard::addSkillButton(const QString &skillName, bool isPrimary)
{
    const ViewAsSkill *skill = Sanguosha->getViewAsSkill(skillName);
    if (skill == nullptr) return nullptr;

    QSanInvokeSkillDock *dock = isPrimary ? _m_skillDock : m_secondarySkillDock;
    if (dock == nullptr) return nullptr;

    QSanSkillButton *button = dock->addSkillButton(skillName);
    if (button == nullptr) return nullptr;

    connect(button, SIGNAL(clicked()), this, SLOT(skillButtonActivated()));
    connect(button, SIGNAL(selected_changed()), this, SLOT(skillButtonDeactivated()));

    return button;
}

QSanSkillButton *Dashboard::removeSkillButton(const QString &skillName)
{
    QSanSkillButton *button = nullptr;
    if (_m_skillDock != nullptr) {
        button = _m_skillDock->removeSkillButton(skillName);
        if (button != nullptr) return button;
    }
    if (m_secondarySkillDock != nullptr) {
        button = m_secondarySkillDock->removeSkillButton(skillName);
        if (button != nullptr) return button;
    }
    return nullptr;
}

void Dashboard::highlightEquip(QString skillName, bool highlight)
{
    _setEquipSkillHighlight(skillName, highlight);
}

void Dashboard::_createExtraButtons()
{
    m_btnReverseSelection = new QSanButton(":/dashboard/button/reverse.png", ":/dashboard/button/reverse-down.png",
        ":/dashboard/button/reverse-hover.png", button_widget);
    m_btnReverseSelection->setObjectName("reverse-selection");
    m_btnReverseSelection->setPos(0, 0);
    connect(m_btnReverseSelection, SIGNAL(clicked()), this, SLOT(reverseSelection()));

    m_btnFilterCard = new QSanButton(":/dashboard/button/filter.png", ":/dashboard/button/filter-down.png",
        ":/dashboard/button/filter-hover.png", button_widget);
    m_btnFilterCard->setObjectName("filter-card");
    m_btnFilterCard->setPos(0, 0);
    connect(m_btnFilterCard, SIGNAL(clicked()), this, SLOT(showCardFilterContainer()));

    m_btnSortHandcard = new QSanButton(":/dashboard/button/sort.png", ":/dashboard/button/sort-down.png",
        ":/dashboard/button/sort-hover.png", button_widget);
    m_btnSortHandcard->setObjectName("sort-handcard");
    m_btnSortHandcard->setPos(0, 0);
    connect(m_btnSortHandcard, SIGNAL(clicked()), this, SLOT(sortCards()));

    m_btnNoNullification = new QSanButton(":/dashboard/button/no-nullification.png", ":/dashboard/button/no-nullification-down.png",
        ":/dashboard/button/no-nullification-hover.png", button_widget);
    m_btnNoNullification->setObjectName("no-nullification");
    m_btnNoNullification->setPos(0, 0);
    m_btnNoNullification->hide();
    connect(m_btnNoNullification, SIGNAL(clicked()), this, SLOT(cancelNullification()));

    m_btnShefu = new QSanButton(":/dashboard/button/shefu.png", ":/dashboard/button/shefu-down.png",
        ":/dashboard/button/shefu-hover.png", button_widget);
    m_btnShefu->setObjectName("shefu");
    m_btnShefu->setPos(0, 0);
    m_btnShefu->hide();
    connect(m_btnShefu, SIGNAL(clicked()), this, SLOT(setShefuState()));

    m_btnRenPile = new QSanButton(":/dashboard/button/renpile.png", ":/dashboard/button/renpile-down.png",
        ":/dashboard/button/renpile-hover.png", button_widget);
    m_btnRenPile->setObjectName("renpile");
    m_btnRenPile->setPos(0, 0);
    m_btnRenPile->hide();
    connect(m_btnRenPile, SIGNAL(clicked()), this, SLOT(setRenPileState()));
}

void Dashboard::skillButtonActivated()
{
    QSanSkillButton *button = qobject_cast<QSanSkillButton *>(sender());
    if (button == nullptr) return;

    const ViewAsSkill *skill = Sanguosha->getViewAsSkill(button->getSkillName());
    if (skill == nullptr) return;

    emit card_to_use();
}

void Dashboard::skillButtonDeactivated()
{
    emit card_selected(nullptr);
}

void Dashboard::selectAll()
{
    foreach (CardItem *item, m_handCards) {
        if (item->isEnabled() && !item->isSelected()) {
            selectCard(item, true);
        }
    }
}

void Dashboard::cardTip()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == nullptr) return;
    item->showTooltip();
}

void Dashboard::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
}

void Dashboard::mouseReleaseEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    PlayerCardContainer::mouseReleaseEvent(mouseEvent);
}

void Dashboard::_onEquipSelectChanged()
{
    PlayerCardContainer::_onEquipSelectChanged();
}

void Dashboard::_createEquipBorderAnimations()
{
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipBorders[i] == nullptr) {
            _m_equipBorders[i] = new PixmapAnimation;
            _m_equipBorders[i]->setParentItem(_m_leftFrame);
        }
    }
}

void Dashboard::_setEquipBorderAnimation(int index, bool turnOn)
{
    if (index < 0 || index >= S_EQUIP_AREA_LENGTH) return;
    if (_m_equipBorders[index] == nullptr) return;

    if (turnOn) {
        _m_equipBorders[index]->start();
    } else {
        _m_equipBorders[index]->stop();
    }
}

void Dashboard::sortHandCards(QList<int> hands)
{
    QList<CardItem *> sorted;
    foreach (int id, hands) {
        CardItem *item = CardItem::FindItem(m_handCards, id);
        if (item != nullptr) {
            m_handCards.removeOne(item);
            sorted.append(item);
        }
    }
    foreach (CardItem *item, sorted) {
        m_handCards.append(item);
    }
    adjustCards();
}

void Dashboard::adjustCards(bool playAnimation)
{
    if (_m_guhuoActive) return;
    _adjustCards();
}

void Dashboard::refreshHandCardTooltips()
{
    foreach (CardItem *item, m_handCards) {
        item->refreshTooltip();
    }
}

void Dashboard::_adjustCards()
{
    int n = m_handCards.length();
    if (n<1) return;
    int maxCards = Config.MaxCards;
    if (maxCards >= n)
        maxCards = n;
    else if (maxCards <= (n - 1) / 2 + 1)
        maxCards = (n - 1) / 2 + 1;
    QList<CardItem *> row;
    int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;
    QSanRoomSkin::DashboardLayout *layout = (QSanRoomSkin::DashboardLayout *)_m_layout;
    int middleWidth = _m_width - layout->m_leftWidth - layout->m_rightWidth - getButtonWidgetWidth();
    QRect rowRect = QRect(layout->m_leftWidth, layout->m_normalHeight - cardHeight - 3, middleWidth, cardHeight);
    for (int i = 0; i < maxCards; i++)
        row.push_back(m_handCards[i]);

    _m_highestZ = n;
    _disperseCards(row, rowRect, Qt::AlignLeft, true, true);

    if(maxCards<n){
		row.clear();
		rowRect.translate(0, 1.5 * S_PENDING_OFFSET_Y);
		for (int i = maxCards; i < n; i++)
			row.push_back(m_handCards[i]);

		_m_highestZ = 0;
		_disperseCards(row, rowRect, Qt::AlignLeft, true, true);
	}

    for (int i = 0; i < n; i++) {
        if (m_handCards[i]->isSelected()) {
            QPointF newPos = m_handCards[i]->homePos();
            newPos.setY(newPos.y() + S_PENDING_OFFSET_Y);
            m_handCards[i]->setHomePos(newPos);
        }
    }
}

int Dashboard::getMiddleWidth()
{
    bool isDouble = (Self && Self->getGeneral2());
    int rightWidth = isDouble ? _dlayoutDouble->m_rightWidth : _dlayout->m_rightWidth;
    return _m_width - _dlayout->m_leftWidth - rightWidth;
}

QList<CardItem *> Dashboard::cloneCardItems(QList<int> card_ids)
{
    QList<CardItem *> result;
    foreach (int card_id, card_ids) {
        CardItem *card_item = CardItem::FindItem(m_handCards, card_id);
        CardItem *new_card = _createCard(card_id);
        if (card_item) {
            new_card->setPos(card_item->pos());
            new_card->setHomePos(card_item->homePos());
        }
        result.append(new_card);
    }
    return result;
}

QList<CardItem *> Dashboard::removeHandCards(const QList<int> &card_ids)
{
    QList<CardItem *> result;
    foreach (int card_id, card_ids) {
        CardItem *card_item = CardItem::FindItem(m_handCards, card_id);
        if (card_item == selected) selected = nullptr;
        Q_ASSERT(card_item);
        if (card_item) {
            animations->effectOut(card_item);
            m_handCards.removeOne(card_item);
            card_item->disconnect(this);
            result.append(card_item);
        }
    }
    updateHandcardNum();
    return result;
}

QList<CardItem *> Dashboard::removeCardItems(const QList<int> &card_ids, Player::Place place)
{
    QList<CardItem *> result;
    bool pileNeedAdjust = false;
    if (place == Player::PlaceHand)
        result = removeHandCards(card_ids);
    else if (place == Player::PlaceEquip)
        result = removeEquips(card_ids);
    else if (place == Player::PlaceDelayedTrick)
        result = removeDelayedTricks(card_ids);
    else if (place == Player::PlaceSpecial) {
        foreach (int card_id, card_ids) {
            result.push_back(_createCard(card_id));
			
            foreach (QList<int> expanded, _m_pile_expanded) {
                if (expanded.contains(card_id)) {
                    QString key = _m_pile_expanded.key(expanded);
                    _m_pile_expanded[key].removeOne(card_id);
                    pileNeedAdjust = true;
                }
            }
        }
    }
    if (pileNeedAdjust) {
        adjustCards();
    }
    return result;
}

void Dashboard::updateAvatar()
{
    PlayerCardContainer::updateAvatar();
}

void Dashboard::sortCards()
{
    if (_m_sort_menu->isEmpty()) {
        QAction *byType = _m_sort_menu->addAction(tr("By type"));
        byType->setData(ByType);
        QAction *bySuit = _m_sort_menu->addAction(tr("By suit"));
        bySuit->setData(BySuit);
        QAction *byNumber = _m_sort_menu->addAction(tr("By number"));
        byNumber->setData(ByNumber);
    }
    QAction *action = _m_sort_menu->exec(QCursor::pos());
    if (action == nullptr) return;

    SortType sortType = (SortType)action->data().toInt();
    QList<CardItem *> sorted = m_handCards;

    switch (sortType) {
    case ByType:
        std::sort(sorted.begin(), sorted.end(), [](CardItem *a, CardItem *b) {
            const Card *cardA = a->getCard();
            const Card *cardB = b->getCard();
            if (cardA == nullptr || cardB == nullptr) return false;
            if (cardA->getTypeId() != cardB->getTypeId())
                return cardA->getTypeId() < cardB->getTypeId();
            return cardA->getId() < cardB->getId();
        });
        break;
    case BySuit:
        std::sort(sorted.begin(), sorted.end(), [](CardItem *a, CardItem *b) {
            const Card *cardA = a->getCard();
            const Card *cardB = b->getCard();
            if (cardA == nullptr || cardB == nullptr) return false;
            if (cardA->getSuit() != cardB->getSuit())
                return cardA->getSuit() < cardB->getSuit();
            return cardA->getNumber() > cardB->getNumber();
        });
        break;
    case ByNumber:
        std::sort(sorted.begin(), sorted.end(), [](CardItem *a, CardItem *b) {
            const Card *cardA = a->getCard();
            const Card *cardB = b->getCard();
            if (cardA == nullptr || cardB == nullptr) return false;
            if (cardA->getNumber() != cardB->getNumber())
                return cardA->getNumber() > cardB->getNumber();
            return cardA->getSuit() < cardB->getSuit();
        });
        break;
    }

    m_handCards = sorted;
    adjustCards();
}

void Dashboard::beginSorting()
{
    m_handCards = CardItem::SortItems(m_handCards, ByType);
    adjustCards();
}

void Dashboard::reverseSelection()
{
    foreach (CardItem *item, m_handCards) {
        if (item->isEnabled()) {
            selectCard(item, !item->isSelected());
        }
    }
}

void Dashboard::cancelNullification()
{
    ClientInstance->onPlayerResponseCard(nullptr);
}

void Dashboard::controlNullificationButton(bool show)
{
    if (show) {
        m_btnNoNullification->show();
    } else {
        m_btnNoNullification->hide();
    }
}

void Dashboard::setShefuState()
{
    if (_m_shefu_menu->isEmpty()) {
        foreach (const Player *p, m_player->getAliveSiblings()) {
            QAction *action = _m_shefu_menu->addAction(p->getGeneralName());
            action->setData(p->objectName());
        }
    }
    QAction *action = _m_shefu_menu->exec(QCursor::pos());
    if (action == nullptr) return;

    QString targetName = action->data().toString();
    ClientInstance->onPlayerChoosePlayer(ClientInstance->getPlayer(targetName));
}

void Dashboard::changeShefuState()
{
    ClientInstance->onPlayerChoosePlayer(nullptr);
}

void Dashboard::setRenPileState()
{
    ClientInstance->onPlayerResponseCard(nullptr);
}

void Dashboard::disableAllCards()
{
    m_mutexEnableCards.lock();
    foreach (CardItem *card_item, m_handCards)
        card_item->setEnabled(false);
    m_mutexEnableCards.unlock();
}

void Dashboard::enableCards()
{
    m_mutexEnableCards.lock();
    foreach (CardItem *card_item, m_handCards) {
        const Card *card = card_item->getCard();
        if (card != nullptr && card->isAvailable(Self))
            card_item->setEnabled(true);
        else
            card_item->setEnabled(false);
    }
    m_mutexEnableCards.unlock();
}

void Dashboard::enableAllCards()
{
    m_mutexEnableCards.lock();
    foreach(CardItem *card_item, m_handCards)
        card_item->setEnabled(true);
    m_mutexEnableCards.unlock();
}

void Dashboard::startPending(const ViewAsSkill *skill)
{
    m_mutexEnableCards.lock();
    view_as_skill = skill;
    pendings.clear();
    unselectAll();

    bool expand = (skill && skill->isResponseOrUse());
    if (!expand && skill && skill->inherits("ResponseSkill")) {
        const ResponseSkill *resp_skill = qobject_cast<const ResponseSkill *>(skill);
        expand = resp_skill->getRequest() == Card::MethodResponse || resp_skill->getRequest() == Card::MethodUse;
    }

    retractAllSkillPileCards();
	foreach (const QString &pile, m_player->getPileNames()) {
		if (pile == "wooden_ox" || pile.startsWith("&")){
			if(expand) expandPileCards(pile);
			else retractPileCards(pile);
		}
	}

    if (skill && !skill->getExpandPile().isEmpty()) {
        foreach(const QString &pile_name, skill->getExpandPile().split(",")) {
            if (expand&&(pile_name == "wooden_ox" || pile_name.startsWith("&"))) continue;
            expandPileCards(pile_name);
        }
    }
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipCards[i] != nullptr)
            connect(_m_equipCards[i], SIGNAL(mark_changed()), this, SLOT(onMarkChanged()));
    }

    updatePending();
    m_mutexEnableCards.unlock();
}

void Dashboard::stopPending()
{
    m_mutexEnableCards.lock();
    if (view_as_skill) {
        if (view_as_skill->objectName().contains("guhuo")) {
            foreach(CardItem *item, m_handCards)
                item->hideFootnote();
        } 
        if (!view_as_skill->getExpandPile().isEmpty()) {
            foreach (const QString &pile_name, view_as_skill->getExpandPile().split(","))
                retractPileCards(pile_name);
        }
    }
    view_as_skill = nullptr;
    pending_card = nullptr;
    foreach (const QString &pile, m_player->getPileNames()) {
        if (pile == "wooden_ox" || pile.startsWith("&"))
            retractPileCards(pile);
    }
    emit card_selected(nullptr);

    foreach (CardItem *item, m_handCards) {
        item->setEnabled(false);
        animations->effectOut(item);
    }

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipCards[i]) {
            _m_equipCards[i]->mark(false);
            _m_equipCards[i]->setMarkable(false);
            _m_equipRegions[i]->setOpacity(1.0);
            _m_equipCards[i]->setEnabled(false);
            disconnect(_m_equipCards[i], SIGNAL(mark_changed()));
        }
    }
    pendings.clear();
    adjustCards();
    m_mutexEnableCards.unlock();
}

void Dashboard::expandPileCards(const QString &pile_name)
{
    if (_m_pile_expanded.contains(pile_name)) return;
    QString new_name = pile_name;
    QList<int> pile;

    if (new_name.startsWith("/")) {
        new_name = new_name.mid(1);

        QString equip = new_name.split("/").first();

        foreach(const Player *p, m_player->getAliveSiblings()) {
            foreach (const Card *card, p->getEquips()) {
                if (card->isKindOf(equip.toStdString().c_str()))
                    pile << card->getId();
            }
        }
    } else {
        pile = m_player->getPile(new_name);
    }

    if (pile.isEmpty()) return;

    _m_pile_expanded[pile_name] = pile;

    QList<CardItem *> card_items = _createCards(pile);
    foreach (CardItem *item, card_items) {
        item->setFlags(ItemIsFocusable);
        item->setAcceptedMouseButtons(Qt::LeftButton);
        connect(item, SIGNAL(clicked()), this, SLOT(onCardItemClicked()));
        connect(item, SIGNAL(show_tooltip()), this, SLOT(onCardItemHover()));
        connect(item, SIGNAL(hide_tooltip()), this, SLOT(onCardItemLeaveHover()));
    }

    int n = pile.length();
    int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;
    QSanRoomSkin::DashboardLayout *layout = (QSanRoomSkin::DashboardLayout *)_m_layout;
    int middleWidth = _m_width - layout->m_leftWidth - layout->m_rightWidth - getButtonWidgetWidth();
    QRect rowRect = QRect(layout->m_leftWidth, layout->m_normalHeight - cardHeight - 3 - 1.5 * S_PENDING_OFFSET_Y, middleWidth, cardHeight);
    _disperseCards(card_items, rowRect, Qt::AlignLeft, true, true);

    foreach (CardItem *item, card_items) {
        item->show();
    }
}

void Dashboard::retractPileCards(const QString &pile_name)
{
    if (!_m_pile_expanded.contains(pile_name)) return;

    QList<int> pile = _m_pile_expanded[pile_name];
    _m_pile_expanded.remove(pile_name);

    QList<CardItem *> toRemove;
    foreach (CardItem *item, m_handCards) {
        if (pile.contains(item->getId())) {
            toRemove.append(item);
        }
    }

    foreach (CardItem *item, toRemove) {
        m_handCards.removeOne(item);
        item->hide();
        delete item;
    }

    adjustCards();
}

void Dashboard::retractAllSkillPileCards()
{
    QList<QString> piles = _m_pile_expanded.keys();
    foreach (const QString &pile, piles) {
        if (!pile.startsWith("&") && pile != "wooden_ox") {
            retractPileCards(pile);
        }
    }
}

void Dashboard::onCardItemClicked()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == nullptr) return;

    if (view_as_skill != nullptr) {
        if (item->isMarkable()) {
            item->mark(!item->isMarked());
            return;
        }
        if (item->isSelected()) {
            pendings.removeOne(item);
            selectCard(item, false);
        } else {
            pendings.append(item);
            selectCard(item, true);
        }
        updatePending();
    } else {
        selectCard(item, !item->isSelected());
    }
}

void Dashboard::updatePending()
{
    if (view_as_skill == nullptr) return;

    QString card_pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
    QList<CardItem *> original_pendings = pendings;
    pending_card = view_as_skill->viewAs(original_pendings, m_player);

    if (pending_card == nullptr) {
        emit card_selected(nullptr);
        return;
    }

    if (!view_as_skill->isAvailable(Self) && Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
        pending_card = nullptr;
        emit card_selected(nullptr);
        return;
    }

    if (!card_pattern.isEmpty()) {
        ExpPattern pattern(card_pattern);
        if (!pattern.match(Self, pending_card)) {
            pending_card = nullptr;
            emit card_selected(nullptr);
            return;
        }
    }

    emit card_selected(pending_card);
}

void Dashboard::clearPendings()
{
    foreach (CardItem *item, pendings) {
        selectCard(item, false);
    }
    pendings.clear();
    pending_card = nullptr;
}

void Dashboard::onCardItemDoubleClicked()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == nullptr) return;

    if (view_as_skill != nullptr) {
        pendings.clear();
        pendings.append(item);
        selectCard(item, true);
        updatePending();
        if (pending_card != nullptr) {
            emit card_to_use();
        }
    }
}

void Dashboard::onCardItemThrown()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == nullptr) return;

    if (view_as_skill != nullptr) {
        pendings.clear();
        pendings.append(item);
        selectCard(item, true);
        updatePending();
        if (pending_card != nullptr) {
            emit card_to_use();
        }
    }
}

void Dashboard::onCardItemHover()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == nullptr) return;
    item->showTooltip();
}

void Dashboard::onCardItemLeaveHover()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (item == nullptr) return;
    item->hideTooltip();
}

void Dashboard::onMarkChanged()
{
    updatePending();
}

const ViewAsSkill *Dashboard::currentSkill() const
{
    return view_as_skill;
}

const Card *Dashboard::pendingCard() const
{
    return pending_card;
}

void Dashboard::clearFilterUIElements()
{
    foreach (QGraphicsItem *item, _m_filterUIElements) {
        delete item;
    }
    _m_filterUIElements.clear();
}

void Dashboard::hideFilterContainer()
{
    clearFilterUIElements();
    _m_filterContainer->clear();
    _m_filterContainer->hide();
    _m_filterCurrentCategory = "";
}

void Dashboard::showCardFilterContainer()
{
    if (m_handCards.isEmpty() || (m_player && m_player->property("NotSortHands").toBool())) return;

    _m_filterCurrentCategory = "";
    clearFilterUIElements();

    QMap<QString, QList<int>> categoryMap;
    QList<int> representative_ids;

    foreach (CardItem *item, m_handCards){
        if (!item->isEnabled()) continue;
        const Card *c = item->getCard();
        QString cat = c->isKindOf("Slash") ? "Slash" : c->getClassName();

        if (!categoryMap.contains(cat)) {
            representative_ids.append(c->getId());
        }
        categoryMap[cat].append(c->getId());
    }

    if (representative_ids.isEmpty()) return;

    if (!_m_filterContainer) {
        _m_filterContainer = new CardContainer();
        _m_filterContainer->setParentItem(this);
        _m_filterContainer->setZValue(20000);
        connect(_m_filterContainer, SIGNAL(item_chosen(int)), this, SLOT(filterCardChosen(int)), Qt::QueuedConnection);
    }

    _m_filterContainer->clear();
    _m_filterContainer->fillCards(representative_ids);
    _m_filterContainer->show();

    foreach (CardItem *item, _m_filterContainer->getItems()) {
        connect(item, SIGNAL(clicked()), _m_filterContainer, SLOT(chooseItem()));
    }

    int container_width = representative_ids.length() * G_COMMON_LAYOUT.m_cardNormalWidth;
    _m_filterContainer->setPos(_dlayout->m_leftWidth + (getMiddleWidth() - container_width) / 2, -200);

    QList<QGraphicsItem*> children = _m_filterContainer->childItems();
    foreach (QGraphicsItem* child, children) {
        CardItem* cItem = qgraphicsitem_cast<CardItem*>(child);
        if (cItem && cItem->getCard()) {
            QString cat = cItem->getCard()->isKindOf("Slash") ? "Slash" : cItem->getCard()->getClassName();
            int count = categoryMap[cat].length();

            QGraphicsTextItem *label = new QGraphicsTextItem(cItem);
            QString cardName = Sanguosha->translate(cItem->getCard()->objectName());
            QString text = QString("%1 (%2)").arg(cardName).arg(count);

            label->setHtml(QString("<div style='background-color:rgba(0,0,0,200); color:#FFD700; padding:2px 8px; font-weight:bold; font-size:14px; border:1px solid #FFD700; border-radius:4px;'>%1</div>").arg(text));
            label->setTextWidth(G_COMMON_LAYOUT.m_cardNormalWidth);
            label->document()->setDefaultTextOption(QTextOption(Qt::AlignHCenter));

            QRectF textBounds = label->boundingRect();
            qreal cardCenterX = 0;
            label->setPos(cardCenterX - textBounds.width() / 2, -textBounds.height() / 2 + 15);

            _m_filterUIElements.append(label);
        }
    }

    Button *closeBtn = new Button(tr("Close"), 0.8);
    closeBtn->setParentItem(_m_filterContainer);
    closeBtn->setPos(_m_filterContainer->boundingRect().width() - 80, G_COMMON_LAYOUT.m_cardNormalHeight + 20);
    connect(closeBtn, SIGNAL(clicked()), this, SLOT(hideFilterContainer()), Qt::QueuedConnection);
    _m_filterUIElements.append(closeBtn);
}

void Dashboard::filterCardChosen(int card_id)
{
    const Card *target_c = Sanguosha->getCard(card_id);
    if (!target_c) return;

    QString target_cat = target_c->isKindOf("Slash") ? "Slash" : target_c->getClassName();

    if (_m_filterCurrentCategory.isEmpty()) {
        QList<int> cat_cards;
        foreach (CardItem *item, m_handCards) {
            if (!item->isEnabled()) continue;
            const Card *c = item->getCard();
            QString cat = c->isKindOf("Slash") ? "Slash" : c->getClassName();
            if (cat == target_cat) cat_cards.append(c->getId());
        }

        if (cat_cards.length() == 1) {
            unselectAll();
            CardItem *targetItem = nullptr;
            foreach (CardItem *item, m_handCards) {
                if (item->getCard()->getId() == cat_cards.first()) {
                    targetItem = item;
                    break;
                }
            }

            foreach (CardItem *item, _m_filterContainer->getItems()) {
                disconnect(item, nullptr, nullptr, nullptr);
                item->setFlag(QGraphicsItem::ItemIsSelectable, false);
                item->setAcceptedMouseButtons(Qt::NoButton);
            }

            if (targetItem) {
                targetItem->clickItem();
            }

            clearFilterUIElements();
            _m_filterContainer->clear();
            _m_filterContainer->hide();

            if (targetItem) {
                targetItem->goBack(true);
            }
        } else {
            _m_filterCurrentCategory = target_cat;
            clearFilterUIElements();

            _m_filterContainer->clear();
            _m_filterContainer->fillCards(cat_cards);
            _m_filterContainer->show();

            foreach (CardItem *item, _m_filterContainer->getItems()) {
                connect(item, SIGNAL(clicked()), _m_filterContainer, SLOT(chooseItem()));
            }

            int container_width = cat_cards.length() * G_COMMON_LAYOUT.m_cardNormalWidth;
            _m_filterContainer->setPos(_dlayout->m_leftWidth + (getMiddleWidth() - container_width) / 2, -200);

            Button *backBtn = new Button(tr("Back"), 0.8);
            backBtn->setParentItem(_m_filterContainer);
            backBtn->setPos(10, G_COMMON_LAYOUT.m_cardNormalHeight + 20);
            connect(backBtn, SIGNAL(clicked()), this, SLOT(showCardFilterContainer()), Qt::QueuedConnection);

            _m_filterUIElements.append(backBtn);

            Button *closeBtn = new Button(tr("Close"), 0.8);
            closeBtn->setParentItem(_m_filterContainer);
            closeBtn->setPos(_m_filterContainer->boundingRect().width() - 80, G_COMMON_LAYOUT.m_cardNormalHeight + 20);
            connect(closeBtn, SIGNAL(clicked()), this, SLOT(hideFilterContainer()), Qt::QueuedConnection);
            _m_filterUIElements.append(closeBtn);
        }
    } else {
        unselectAll();
        CardItem *targetItem = nullptr;
        foreach (CardItem *item, m_handCards) {
            if (item->getCard()->getId() == card_id) {
                targetItem = item;
                break;
            }
        }

        foreach (CardItem *item, _m_filterContainer->getItems()) {
            disconnect(item, nullptr, nullptr, nullptr);
            item->setFlag(QGraphicsItem::ItemIsSelectable, false);
            item->setAcceptedMouseButtons(Qt::NoButton);
        }

        if (targetItem) {
            targetItem->clickItem();
        }

        clearFilterUIElements();
        _m_filterContainer->clear();
        _m_filterContainer->hide();
        _m_filterCurrentCategory = "";

        if (targetItem) {
            targetItem->goBack(true);
        }
    }
}

void Dashboard::showGuhuoCards(const QString &skillName, QList<Card *> cards)
{
    if (_m_guhuoActive && _m_guhuoSkillName == skillName) {
        hideGuhuoCards();
        emit guhuoCancelled();
        return;
    }

    if (_m_guhuoActive) {
        hideGuhuoCards();
    }

    foreach (CardItem *item, m_handCards)
        item->hide();

    m_guhuoItems.clear();
    m_guhuoCardMap.clear();
    _m_guhuoSelected = nullptr;

    foreach (Card *card, cards) {
        CardItem *item = new CardItem(card);
        item->setParentItem(_m_middleFrame);
        item->setZValue(100);
        item->setEnabled(true);
        connect(item, SIGNAL(clicked()), this, SLOT(_onGuhuoCardClicked()));
        m_guhuoItems.append(item);
        m_guhuoCardMap[item] = card;
    }

    _adjustGuhuoCards();

    _m_guhuoActive = true;
    _m_guhuoSkillName = skillName;
}

void Dashboard::_adjustGuhuoCards()
{
    int n = m_guhuoItems.length();
    if (n < 1) return;

    int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;
    QSanRoomSkin::DashboardLayout *layout = (QSanRoomSkin::DashboardLayout *)_m_layout;
    int middleWidth = _m_width - layout->m_leftWidth - layout->m_rightWidth - getButtonWidgetWidth();
    QRect rowRect = QRect(layout->m_leftWidth, layout->m_normalHeight - cardHeight - 3, middleWidth, cardHeight);

    _disperseCards(m_guhuoItems, rowRect, Qt::AlignLeft, true, true);

    for (int i = 0; i < n; i++) {
        m_guhuoItems[i]->show();
    }
}

void Dashboard::_onGuhuoCardClicked()
{
    CardItem *item = qobject_cast<CardItem *>(sender());
    if (!item) return;

    if (_m_guhuoSelected == item) {
        return;
    }

    if (_m_guhuoSelected) {
        _m_guhuoSelected->setSelected(false);
        QPointF homePos = _m_guhuoSelected->homePos();
        homePos.setY(homePos.y() - S_PENDING_OFFSET_Y);
        _m_guhuoSelected->setHomePos(homePos);
        _m_guhuoSelected->goBack(true);
    }

    item->setSelected(true);
    QPointF homePos = item->homePos();
    homePos.setY(homePos.y() + S_PENDING_OFFSET_Y);
    item->setHomePos(homePos);
    item->goBack(true);
    _m_guhuoSelected = item;

    Card *card = m_guhuoCardMap.value(item, nullptr);
    if (card) {
        Self->setTag(_m_guhuoSkillName, QVariant::fromValue(card));
    }
}

void Dashboard::_onGuhuoConfirm()
{
    if (!_m_guhuoSelected) return;

    Card *card = m_guhuoCardMap.value(_m_guhuoSelected, nullptr);
    if (card) {
        emit guhuoCardSelected(card);
    }
    hideGuhuoCards();
}

void Dashboard::hideGuhuoCards()
{
    foreach (CardItem *item, m_guhuoItems) {
        item->hide();
        item->deleteLater();
    }
    m_guhuoItems.clear();
    m_guhuoCardMap.clear();
    _m_guhuoSelected = nullptr;

    foreach (CardItem *item, m_handCards)
        item->show();
    adjustCards();

    _m_guhuoActive = false;
    _m_guhuoSkillName.clear();
}
