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

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>

namespace {

class DashboardDialogOptionItem : public QGraphicsObject
{
public:
    DashboardDialogOptionItem(Dashboard *dashboard, const QString &optionName, const QString &tooltip,
        const QSize &size, QGraphicsItem *parent = nullptr)
        : QGraphicsObject(parent), m_dashboard(dashboard), m_optionName(optionName), m_size(size),
          m_selected(false), m_hovered(false), m_baseZValue(0)
    {
        setObjectName(optionName);
        setAcceptedMouseButtons(Qt::LeftButton);
        setAcceptHoverEvents(true);
        setToolTip(tooltip);
        setEnabled(true);
        updateVisualState();
    }

    QRectF boundingRect() const
    {
        return QRectF(0, 0, m_size.width(), m_size.height());
    }

    void setOptionSelected(bool selected)
    {
        if (m_selected == selected)
            return;
        m_selected = selected;
        updateVisualState();
        update();
    }

    void setHomePos(const QPointF &homePos)
    {
        m_homePos = homePos;
        updateVisualState();
    }

    void setBaseZValue(qreal baseZValue)
    {
        m_baseZValue = baseZValue;
        updateVisualState();
    }

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
    {
        painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

        QPixmap pixmap = G_ROOM_SKIN.getCardMainPixmap(m_optionName, true)
            .scaled(m_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        painter->drawPixmap(QRect(QPoint(0, 0), m_size), pixmap);

        QRectF borderRect = boundingRect().adjusted(1, 1, -1, -1);
        QColor borderColor = m_selected ? QColor(0xF5, 0xD7, 0x6E)
            : (m_hovered ? QColor(0xF8, 0xF8, 0xF8, 0xC8) : QColor(0xF0, 0xF0, 0xF0, 0x90));
        painter->setPen(QPen(borderColor, m_selected ? 4 : (m_hovered ? 3 : 2)));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(borderRect, 6, 6);

        if (!isEnabled())
            painter->fillRect(boundingRect(), QColor(40, 40, 40, 160));
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event)
    {
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
    {
        event->accept();
        if (!isEnabled() || m_dashboard == nullptr)
            return;

        QMetaObject::invokeMethod(m_dashboard, "_onDialogOptionClicked", Qt::DirectConnection,
            Q_ARG(QString, m_optionName));
    }

    void hoverEnterEvent(QGraphicsSceneHoverEvent *)
    {
        if (!isEnabled())
            return;

        m_hovered = true;
        updateVisualState();
        update();
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *)
    {
        if (!isEnabled())
            return;

        m_hovered = false;
        updateVisualState();
        update();
    }

    QVariant itemChange(GraphicsItemChange change, const QVariant &value)
    {
        QVariant result = QGraphicsObject::itemChange(change, value);
        if (change == ItemEnabledHasChanged)
            updateVisualState();
        return result;
    }

private:
    void updateVisualState()
    {
        QPointF pos = m_homePos;
        if (m_selected)
            pos.setY(pos.y() - 20);
        else if (m_hovered)
            pos.setY(pos.y() - 12);
        QGraphicsObject::setPos(pos);

        qreal zValue = m_baseZValue;
        if (m_hovered)
            zValue += 100;
        if (m_selected)
            zValue += 200;
        QGraphicsObject::setZValue(zValue);

        if (!isEnabled())
            QGraphicsObject::setOpacity(0.55);
        else if (m_selected || m_hovered)
            QGraphicsObject::setOpacity(1.0);
        else
            QGraphicsObject::setOpacity(0.92);
    }

    Dashboard *m_dashboard;
    QString m_optionName;
    QSize m_size;
    bool m_selected;
    bool m_hovered;
    QPointF m_homePos;
    qreal m_baseZValue;
};

}

using namespace QSanProtocol;

Dashboard::Dashboard(QGraphicsPixmapItem *widget)
    : button_widget(widget), selected(nullptr), view_as_skill(nullptr), filter(nullptr), m_secondarySkillDock(nullptr)
{
    Q_ASSERT(button_widget);
    _dlayout = &G_DASHBOARD_LAYOUT;
    _dlayoutDouble = &G_DASHBOARD_LAYOUT_DOUBLE;
    _m_layout = _dlayout;
    m_currentPlayer = Self;
    m_player = m_currentPlayer;
    _m_leftFrame = _m_rightFrame = _m_middleFrame = nullptr;
    //_m_rightFrameBg = nullptr;
    animations = new EffectAnimation();
    pending_card = nullptr;
    _m_pile_expanded = QMap<QString, QList<int> >();
    _m_filterContainer = nullptr;
    _m_filterCurrentCategory = "";
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        _m_equipSkillBtns[i] = nullptr;
        _m_isEquipsAnimOn[i] = false;
    }
    // At this stage, we cannot decide the dashboard size yet, the whole
    // point in creating them here is to allow PlayerCardContainer to
    // anchor all controls and widgets to the correct frame.
    //
    // Note that 20 is just a random plug-in so that we can proceed with
    // control creation, the actual width is updated when setWidth() is
    // called by its graphics parent.
    _m_width = G_DASHBOARD_LAYOUT.m_leftWidth + G_DASHBOARD_LAYOUT.m_rightWidth + 20;

    _createLeft();
    _createMiddle();
    _createRight();

    // only do this after you create all frames.
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
    if (equip == nullptr)
        return nullptr;

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) {
        if (_m_equipCards[i] == equip && _m_equipSkillBtns[i] != nullptr)
            return _m_equipSkillBtns[i];
    }

    int buttonIndex = _findEquipSkillButtonIndex(equip->objectName());
    return buttonIndex >= 0 ? _m_equipSkillBtns[buttonIndex] : nullptr;
}

void Dashboard::_setEquipSkillHighlight(const QString &skillName, bool turnOn)
{
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) {
        if (_m_equipCards[i] != nullptr && _m_equipCards[i]->objectName() == skillName)
            _setEquipBorderAnimation(i, turnOn);
    }
}

bool Dashboard::isAvatarUnderMouse()
{
    return _m_avatarArea->isUnderMouse();
}

void Dashboard::hideControlButtons()
{
    m_btnReverseSelection->hide();
    m_btnFilterCard->hide();
    m_btnSortHandcard->hide();
    m_btnShefu->hide();
    m_btnRenPile->hide();
}

void Dashboard::showControlButtons()
{
    m_btnReverseSelection->show();
    m_btnFilterCard->show();
    m_btnSortHandcard->show();
}

void Dashboard::showProgressBar(QSanProtocol::Countdown countdown)
{
    _m_progressBar->setCountdown(countdown);
    connect(_m_progressBar, SIGNAL(timedOut()), this, SIGNAL(progressBarTimedOut()));
    _m_progressBar->show();
}

QGraphicsItem *Dashboard::getMouseClickReceiver()
{
    return _m_rightFrame;
}

void Dashboard::_createLeft()
{
    _paintLeftFrame();

    _m_leftFrame->setZValue(-1); // nobody should be under me.
    _createEquipBorderAnimations();
}

int Dashboard::getButtonWidgetWidth() const
{
    Q_ASSERT(button_widget);
    return button_widget->boundingRect().width();
}

void Dashboard::_createMiddle()
{
    _m_middleFrame = new QGraphicsPixmapItem(_m_groupMain);
    _m_middleFrame->setTransformationMode(Qt::SmoothTransformation);

    _m_middleFrame->setZValue(-1); // nobody should be under me.
    button_widget->setParentItem(_m_middleFrame);

    trusting_item = new QGraphicsPathItem(this);
    trusting_text = new QGraphicsSimpleTextItem(tr("Trusting ..."), this);

    QBrush trusting_brush(_dlayout->m_trustEffectColor);
    trusting_item->setBrush(trusting_brush);
    trusting_item->setOpacity(0.36);
    trusting_item->setZValue(19);

    trusting_text->setFont(Config.BigFont);
    trusting_text->setBrush(Qt::white);
    trusting_text->setZValue(20);

    trusting_item->hide();
    trusting_text->hide();
}

void Dashboard::_adjustComponentZValues(bool killed)
{
    PlayerCardContainer::_adjustComponentZValues(killed);
    // make sure right frame is on top because we have a lot of stuffs
    // attached to it, such as the rolecomboBox, which should not be under
    // middle frame
    _layUnder(_m_rightFrame);
    _layUnder(_m_leftFrame);
    _layUnder(_m_middleFrame);
    _layBetween(button_widget, _m_middleFrame, _m_roleComboBox);
    //_layBetween(_m_rightFrameBg, _m_faceTurnedIcon, _m_equipRegions[4]);
}

int Dashboard::width()
{
    return this->_m_width;
}

void Dashboard::repaintAll(bool all)
{
    button_widget->setPixmap(G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_DASHBOARD_BUTTON_SET_BG)
							.scaled(_dlayout->m_buttonSetSize));
    RoomSceneInstance->redrawDashboardButtons();

    _paintLeftFrame();
    _paintRightFrame();
    _m_skillDock->update();
    if (m_secondarySkillDock)
        m_secondarySkillDock->update();

    updateScreenName(m_player->screenName());

    PlayerCardContainer::repaintAll(all);
	if(all){
		QList<CardItem *> card_items = _createCards(m_player->handCards());
		for (int i = 0; i < m_handCards.length(); i++)
			delete m_handCards[i];
		m_handCards.clear();
		addHandCards(card_items);
		adjustCards(true);
		_updateFrames();
	}
}

void Dashboard::_createRight()
{
    _m_rightFrame = new QGraphicsPixmapItem(_m_groupMain);
    _m_rightFrame->setTransformationMode(Qt::SmoothTransformation);

    _m_rightFrame->setZValue(-1);
    _m_skillDock = new QSanInvokeSkillDock(_m_rightFrame);
    
    const ClientPlayer *player = getPlayer();
    if (player && player->getGeneral2()) {
        m_secondarySkillDock = new QSanInvokeSkillDock(_m_rightFrame);
    }
}

void Dashboard::_updateSkillDockGeometry()
{
    if (_m_skillDock == nullptr)
        return;

    const ClientPlayer *player = getPlayer();
    bool useDoubleLayout = (player && player->getGeneral2()) || (m_secondarySkillDock != nullptr);
    const QSanRoomSkin::DashboardLayout *layout = useDoubleLayout ? _dlayoutDouble : _dlayout;
    if (layout == nullptr)
        return;

    int rightFrameHeight = _m_rightFrame->boundingRect().height();
    int minDockWidth = layout->m_skillButtonsSize[0].width();

    if (!useDoubleLayout) {
        int rightFrameWidth = _m_rightFrame->boundingRect().width();
        _m_skillDock->setPos(layout->m_skillDockLeftMargin,
                             rightFrameHeight - layout->m_skillDockBottomMargin);
        _m_skillDock->setWidth(rightFrameWidth - layout->m_skillDockRightMargin);
        return;
    }

    QRect primaryAvatarArea = layout->m_avatarArea;
    QRect secondaryAvatarArea = layout->m_smallAvatarArea;
    int primaryWidth = qMax(minDockWidth, primaryAvatarArea.width() - 10);
    int secondaryWidth = qMax(minDockWidth, secondaryAvatarArea.width() - 10);
    int primaryY = primaryAvatarArea.bottom();
    int secondaryY = secondaryAvatarArea.bottom();

    _m_skillDock->setPos(primaryAvatarArea.left() + 5, primaryY);
    _m_skillDock->setWidth(primaryWidth);

    if (m_secondarySkillDock) {
        m_secondarySkillDock->setPos(secondaryAvatarArea.left() + 5, secondaryY);
        m_secondarySkillDock->setWidth(secondaryWidth);
    }
}

void Dashboard::_clearSkillDock(QSanInvokeSkillDock *dock)
{
    if (dock == nullptr)
        return;

    QList<QSanInvokeSkillButton *> buttons = dock->getAllSkillButtons();
    foreach (QSanInvokeSkillButton *button, buttons) {
        if (button == nullptr)
            continue;
        dock->removeSkillButton(button);
        button->setGraphicsEffect(nullptr);
        button->hide();
        button->setParentItem(nullptr);
        button->deleteLater();
    }
}

void Dashboard::refreshLayout()
{
    const ClientPlayer *player = getPlayer();
    const QSanRoomSkin::DashboardLayout *newLayout = 
        (player && player->getGeneral2()) ? _dlayoutDouble : _dlayout;
    
    bool wasDouble = (m_secondarySkillDock != nullptr);
    bool isDouble = (player && player->getGeneral2());
    bool skillButtonsUpdated = false;
    
    if (_m_layout != newLayout || wasDouble != isDouble) {
        _m_layout = newLayout;
        
        if (isDouble && !m_secondarySkillDock) {
            m_secondarySkillDock = new QSanInvokeSkillDock(_m_rightFrame);
        }
        
        if (!isDouble && m_secondarySkillDock) {
            if (RoomSceneInstance) {
                RoomSceneInstance->updateSkillButtons();
                skillButtonsUpdated = true;
            }
            _clearSkillDock(m_secondarySkillDock);
            delete m_secondarySkillDock;
            m_secondarySkillDock = nullptr;
        }
        
        _updateFrames();
        updateAvatar();
        updateSmallAvatar();
        updateHp();
        _updateEquips();
        updateDelayedTricks();
        updateMarks();
        _updateProgressBar();
        
        if (m_changePrimaryHeroSKinBtn)
            m_changePrimaryHeroSKinBtn->setPos(_m_layout->m_changePrimaryHeroSkinBtnPos);
        if (m_changeSecondaryHeroSkinBtn)
            m_changeSecondaryHeroSkinBtn->setPos(_m_layout->m_changeSecondaryHeroSkinBtnPos);
        if (_m_roleComboBox)
            _m_roleComboBox->setPos(_m_layout->m_roleComboBoxPos);
        
        _m_hpBox->setIconSize(_m_layout->m_magatamaSize);
        _m_hpBox->setOrientation(_m_layout->m_magatamasHorizontal ? Qt::Horizontal : Qt::Vertical);
        _m_hpBox->setBackgroundVisible(_m_layout->m_magatamasBgVisible);
        _m_hpBox->setAnchor(_m_layout->m_magatamasAnchor, _m_layout->m_magatamasAlign);
        _m_hpBox->setImageArea(_m_layout->m_magatamaImageArea);
        _m_hpBox->update();
        
        adjustCards(true);
        
        if (RoomSceneInstance && wasDouble != isDouble && !skillButtonsUpdated)
            RoomSceneInstance->updateSkillButtons();
    }
}

void Dashboard::_updateFrames()
{
    const QSanRoomSkin::DashboardLayout *layout = _dlayout;
    const ClientPlayer *player = getPlayer();
    bool isDouble = (player && player->getGeneral2());
    int rightWidth = isDouble ? _dlayoutDouble->m_rightWidth : layout->m_rightWidth;
    // Here is where we adjust all frames to actual width
    QRect rect = QRect(layout->m_leftWidth, 0,
                       this->width() - rightWidth - layout->m_leftWidth, layout->m_normalHeight);

    _paintMiddleFrame(rect);
    _m_groupDeath->setPos(rect.x(), rect.y());
    _m_groupDeath->setPixmap(QPixmap(rect.size()));

    QRect rect2 = QRect(0, 0, this->width(), layout->m_normalHeight);
    trusting_item->setPos(0, 0);
    trusting_text->setPos((rect2.width() - Config.BigFont.pixelSize() * 4.5) / 2,
                          (rect2.height() - Config.BigFont.pixelSize()) / 2);

    Q_ASSERT(button_widget);
    button_widget->setX(rect.width() - getButtonWidgetWidth());
    button_widget->setY(1);

    /*QRectF btnWidgetRect = button_widget->mapRectToItem(this, button_widget->boundingRect());
    m_btnNoNullification->setPos(btnWidgetRect.left() - m_btnNoNullification->boundingRect().width(),
                                 m_btnNoNullification->boundingRect().height() / 5);
    */
    _paintRightFrame();
    _m_rightFrame->setX(_m_width - rightWidth);
    _m_rightFrame->moveBy(0, m_middleFrameAndRightFrameHeightDiff);

    QPainterPath kingdomColorMaskPath;
    QRectF kingdomColorMaskRect;
    if (_m_kingdomColorMaskIcon) {
        kingdomColorMaskRect = _m_kingdomColorMaskIcon->boundingRect();
        kingdomColorMaskRect = _m_kingdomColorMaskIcon->mapRectToItem(this, kingdomColorMaskRect);
    } else {
        kingdomColorMaskRect = _m_rightFrame->mapRectToItem(this, layout->m_kingdomMaskArea);
    }
    kingdomColorMaskRect.adjust(0, -1, 0, -2);
    kingdomColorMaskPath.addRect(kingdomColorMaskRect);

    QRectF rightFrameRect = _m_rightFrame->boundingRect();
    rightFrameRect = _m_rightFrame->mapRectToItem(this, rightFrameRect);
    QPainterPath rightFramePath;
    rightFramePath.addRect(rightFrameRect);

    QRect leftFrameAndMiddleFrameRect = QRect(0, 0, this->width() - rightWidth,
                                              layout->m_normalHeight);
    rightFramePath.addRect(leftFrameAndMiddleFrameRect);

    trusting_item->setPath(kingdomColorMaskPath.united(rightFramePath));

    // Keep hand card number text position in sync after _m_rightFrame moves.
    if (_m_handCardNumText) updateHandcardNum();
}

void Dashboard::_paintLeftFrame()
{
    QRect rect = QRect(0, 0, _dlayout->m_leftWidth, _dlayout->m_normalHeight);
    _paintPixmap(_m_leftFrame, rect, _getPixmap(QSanRoomSkin::S_SKIN_KEY_LEFTFRAME), _m_groupMain);
}

void Dashboard::_paintMiddleFrame(const QRect &rect)
{
    _paintPixmap(_m_middleFrame, rect, _getPixmap(QSanRoomSkin::S_SKIN_KEY_MIDDLEFRAME), _m_groupMain);
}

void Dashboard::_paintRightFrame()
{
    QPixmap rightFramePixmap = _getPixmap(QSanRoomSkin::S_SKIN_KEY_RIGHTFRAME);
    int middleFrameHeight = _dlayout->m_normalHeight;
    int rightFrameHeight = rightFramePixmap.height();
    m_middleFrameAndRightFrameHeightDiff = middleFrameHeight - rightFrameHeight;

    const ClientPlayer *player = getPlayer();
    bool isDouble = (player && player->getGeneral2());
    int rightFrameWidth = isDouble ? _dlayoutDouble->m_rightWidth : _dlayout->m_rightWidth;
    const QSanRoomSkin::DashboardLayout *layout = isDouble ? _dlayoutDouble : _dlayout;

    QRect rect = QRect(_m_width - rightFrameWidth,
                       m_middleFrameAndRightFrameHeightDiff,
                       rightFrameWidth,
                       rightFrameHeight);

    _paintPixmap(_m_rightFrame, QRect(0, 0, rect.width(), rect.height()), rightFramePixmap, _m_groupMain);

    _updateSkillDockGeometry();
}

void Dashboard::setTrust(bool trust)
{
    trusting_item->setVisible(trust);
    trusting_text->setVisible(trust);
}

void Dashboard::killPlayer()
{
    trusting_item->hide();
    trusting_text->hide();
	_m_roleComboBox->fix(m_player->getRole());
	_m_roleComboBox->setEnabled(m_player->property("RestPlayer").toBool());
    _updateDeathIcon();
    _m_saveMeIcon->hide();
    if (_m_votesItem) _m_votesItem->hide();
    if (_m_distanceItem) _m_distanceItem->hide();
    if (!m_player->property("RestPlayer").toBool()) {
        QGraphicsColorizeEffect *effect = new QGraphicsColorizeEffect();
        effect->setColor(_m_layout->m_deathEffectColor);
        effect->setStrength(1.0);
        setGraphicsEffect(effect);
    }
    refresh(true);
	_m_deathIcon->show();
    if (ServerInfo.GameMode == "04_1v3" && !m_player->isLord()) {
        _m_votesGot = 6;
        updateVotes(false);
    }
}

void Dashboard::revivePlayer()
{
    _m_votesGot = 0;
    setGraphicsEffect(nullptr);
	_m_roleComboBox->setEnabled(true);
    Q_ASSERT(_m_deathIcon);
    _m_deathIcon->hide();
    refresh();
}

bool Dashboard::_addCardItems(QList<CardItem *> &card_items, const CardsMoveStruct &moveInfo)
{
    if (moveInfo.to_place == Player::PlaceSpecial) {
        foreach(CardItem *card, card_items)
            card->setHomeOpacity(0);
        QPointF center = mapFromItem(_getAvatarParent(), _dlayout->m_avatarArea.center());
        QRectF rect = QRectF(0, 0, _dlayout->m_disperseWidth, 0);
        rect.moveCenter(center);
        _disperseCards(card_items, rect, Qt::AlignCenter, true, false);
        return true;
    }

    if (moveInfo.to_place == Player::PlaceEquip)
        addEquips(card_items);
    else if (moveInfo.to_place == Player::PlaceDelayedTrick)
        addDelayedTricks(card_items);
    else if (moveInfo.to_place == Player::PlaceHand)
        addHandCards(card_items);

    adjustCards(true);
    return false;
}

void Dashboard::addHandCards(QList<CardItem *> &card_items)
{
    foreach(CardItem *card_item, card_items)
        _addHandCard(card_item);
    updateHandcardNum();
}

void Dashboard::_addHandCard(CardItem *card_item, bool prepend, const QString &footnote)
{
	//card_item->setEnabled(ClientInstance->getStatus()==Client::Playing&&card_item->getCard()->isAvailable(m_player));
	card_item->setEnabled(false);

    card_item->setHomeOpacity(1.0);
    card_item->setRotation(0.0);
    card_item->setFlag(ItemIsFocusable);
    //if (Config.EnableSuperDrag)
        card_item->setFlag(ItemIsMovable);

    if (!footnote.isEmpty()) {
        card_item->setFootnote(footnote);
        card_item->showFootnote();
    }
    if (prepend) m_handCards.prepend(card_item);
    else m_handCards.append(card_item);

    connect(card_item, SIGNAL(clicked()), this, SLOT(onCardItemClicked()));
    connect(card_item, SIGNAL(double_clicked()), this, SLOT(onCardItemDoubleClicked()));
    connect(card_item, SIGNAL(thrown()), this, SLOT(onCardItemThrown()));
    connect(card_item, SIGNAL(enter_hover()), this, SLOT(onCardItemHover()));
    connect(card_item, SIGNAL(leave_hover()), this, SLOT(onCardItemLeaveHover()));
}

void Dashboard::selectCard(const QString &pattern, bool forward, bool multiple)
{
    if (!multiple && selected && selected->isSelected())
        selected->clickItem();

    // find all cards that match the card type
    QList<CardItem *> matches;
    foreach (CardItem *card_item, m_handCards) {
        //if (card_item->isEnabled() && (pattern == "." || card_item->getCard()->match(pattern)))
        if (card_item->isEnabled() && (pattern == "." || card_item->getCard()->getClassName().startsWith(pattern)))
            matches << card_item;
    }

    if (matches.isEmpty()) {
        if (!multiple || !selected) {
            unselectAll();
            return;
        }
    }

    int index = matches.indexOf(selected), n = matches.length();
    index = (index + (forward ? 1 : n - 1)) % n;

    CardItem *to_select = matches[index];
    if (!to_select->isSelected())
        to_select->clickItem();
    else if (to_select->isSelected() && (!multiple || (multiple && to_select != selected)))
        to_select->clickItem();
    selected = to_select;

    adjustCards();
}

void Dashboard::selectEquip(int position)
{
    int i = position - 1;
    if (i < 0 || i >= S_EQUIP_AREA_LENGTH || _m_equipCards[i] == nullptr)
        return;

    QSanSkillButton *equipSkillButton = _getEquipSkillButton(_m_equipCards[i]);
    if (equipSkillButton != nullptr && equipSkillButton->isEnabled()) {
        equipSkillButton->click();
        return;
    }

    if (_m_equipCards[i]->isMarkable()) {
        _m_equipCards[i]->mark(!_m_equipCards[i]->isMarked());
        update();
    }
}

void Dashboard::selectOnlyCard(bool need_only)
{
    if (selected && selected->isSelected())
        selected->clickItem();

    QList<CardItem *> items;
    foreach (CardItem *card_item, m_handCards) {
        if (card_item->isEnabled()) {
            items << card_item;
            if (need_only&&items.length()>0) {
				unselectAll();
				return;
            }
        }
    }
	if(items.length()>0){
		selected = items.first();
		selected->clickItem();
		adjustCards();
		return;
	}

    QList<int> equip_pos;
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipCards[i] && _m_equipCards[i]->isMarkable()) {
            equip_pos << i;
            if (need_only && equip_pos.length()>1)
				return;
        }
    }
	if(equip_pos.length()>0){
		int pos = equip_pos.first();
		_m_equipCards[pos]->mark(!_m_equipCards[pos]->isMarked());
		update();
	}
}

const Card *Dashboard::getSelected() const
{
    if (view_as_skill)
        return pending_card;
    else if (selected)
        return selected->getCard();
    return nullptr;
}

void Dashboard::selectCard(CardItem *item, bool isSelected)
{
    if (item->isSelected() == isSelected) return;
    m_mutex.lock();

    item->setSelected(isSelected);
    QPointF newPos = item->homePos();
    newPos.setY(newPos.y() + (isSelected ? 1 : -1) * S_PENDING_OFFSET_Y);
    item->setHomePos(newPos);
    selected = item;

    m_mutex.unlock();
}

void Dashboard::unselectAll(const CardItem *except)
{
    selected = nullptr;

    foreach (CardItem *card_item, m_handCards) {
        if (card_item != except)
            selectCard(card_item, false);
    }

    adjustCards(true);
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipCards[i] && _m_equipCards[i] != except)
            _m_equipCards[i]->mark(false);
    }
    if (view_as_skill) {
        pendings.clear();
        updatePending();
    }
}

QRectF Dashboard::boundingRect() const
{
    return QRectF(0, 0, _m_width, _m_layout->m_normalHeight);
}

void Dashboard::setWidth(int width)
{
    prepareGeometryChange();
    adjustCards(true);
    _m_width = width;
    _updateFrames();
    _updateDeathIcon();
}

QSanSkillButton *Dashboard::addSkillButton(const QString &skillName, bool isPrimary)
{
    _mutexEquipAnim.lock();
    int existingSkillIndex = _findEquipSkillButtonIndex(skillName);
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipCards[i] && _m_equipCards[i]->objectName() == skillName) {
            if (_m_equipSkillBtns[i] != nullptr) {
                if (_m_equipSkillBtns[i]->objectName() == skillName) {
                    _mutexEquipAnim.unlock();
                    return _m_equipSkillBtns[i];
                }
                if (existingSkillIndex >= 0) {
                    _mutexEquipAnim.unlock();
                    return _m_equipSkillBtns[existingSkillIndex];
                }
            }

            if (existingSkillIndex >= 0 && existingSkillIndex != i && _m_equipSkillBtns[i] == nullptr) {
                _m_equipSkillBtns[i] = _m_equipSkillBtns[existingSkillIndex];
                _m_equipSkillBtns[existingSkillIndex] = nullptr;
                _mutexEquipAnim.unlock();
                return _m_equipSkillBtns[i];
            }

            _m_equipSkillBtns[i] = new QSanInvokeSkillButton(this);
            _m_equipSkillBtns[i]->setSkill(Sanguosha->getSkill(skillName));
            _m_equipSkillBtns[i]->setVisible(false);
            _m_equipSkillBtns[i]->setObjectName(skillName);
            connect(_m_equipSkillBtns[i], SIGNAL(clicked()), this, SLOT(_onEquipSelectChanged()));
            connect(_m_equipSkillBtns[i], SIGNAL(enable_changed()), this, SLOT(_onEquipSelectChanged()));
            _mutexEquipAnim.unlock();
            return _m_equipSkillBtns[i];
        }
    }
    _mutexEquipAnim.unlock();
    if (skillName == "shefu")
        m_btnShefu->show();
    
    QSanInvokeSkillDock *dock = _m_skillDock;
    if (!isPrimary) {
        if (!m_secondarySkillDock) {
            m_secondarySkillDock = new QSanInvokeSkillDock(_m_rightFrame);
            _updateSkillDockGeometry();
        }
        dock = m_secondarySkillDock;
    }

    return dock->addSkillButtonByName(skillName);
}

QSanSkillButton *Dashboard::removeSkillButton(const QString &skillName)
{
    _mutexEquipAnim.lock();
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipSkillBtns[i]&&_m_equipSkillBtns[i]->objectName()==skillName) {
            QSanSkillButton *btn = _m_equipSkillBtns[i];
            _m_equipSkillBtns[i] = nullptr;
			_mutexEquipAnim.unlock();
            return btn;
        }
    }
    _mutexEquipAnim.unlock();

	if (skillName == "shefu")
		m_btnShefu->hide();
    
    QSanSkillButton *btn = nullptr;
    
    if (_m_skillDock) {
        btn = _m_skillDock->getSkillButtonByName(skillName);
        if (btn) {
            _m_skillDock->removeSkillButtonByName(skillName);
            btn->setGraphicsEffect(nullptr);
            btn->hide();
            btn->setParentItem(nullptr);
            return btn;
        }
    }
    
    if (m_secondarySkillDock) {
        btn = m_secondarySkillDock->getSkillButtonByName(skillName);
        if (btn) {
            m_secondarySkillDock->removeSkillButtonByName(skillName);
            btn->setGraphicsEffect(nullptr);
            btn->hide();
            btn->setParentItem(nullptr);
            return btn;
        }
    }
    
	return btn;
}

void Dashboard::highlightEquip(QString skillName, bool highlight)
{
    _setEquipSkillHighlight(skillName, highlight);
}

void Dashboard::_createExtraButtons()
{
    m_btnReverseSelection = new QSanButton("handcard", "reverse-selection", this);
    m_btnFilterCard = new QSanButton("handcard", "filter", this);
    m_btnSortHandcard = new QSanButton("handcard", "sort", this);
    m_btnNoNullification = new QSanButton("handcard", "nullification", this);
    m_btnNoNullification->setStyle(QSanButton::S_STYLE_TOGGLE);
    m_btnShefu = new QSanButton("handcard", "shefu", this);
    m_btnRenPile = new QSanButton("handcard", "ren_pile", this);
    // @todo: auto hide.
    qreal pos = _dlayout->m_leftWidth, height=-m_btnReverseSelection->boundingRect().height();
    m_btnReverseSelection->setPos(pos, height);
    pos += m_btnReverseSelection->boundingRect().right();

    m_btnFilterCard->setPos(pos, height);
    pos += m_btnFilterCard->boundingRect().right();

    m_btnFilterCard->setZValue(100);

    m_btnSortHandcard->setPos(pos, height);
    pos += m_btnSortHandcard->boundingRect().right();
    m_btnNoNullification->setPos(pos, height);
    pos += m_btnNoNullification->boundingRect().right();
    m_btnShefu->setPos(pos, height);
    pos += m_btnShefu->boundingRect().right();
    m_btnRenPile->setPos(pos, height);

    m_btnNoNullification->hide();
    m_btnShefu->hide();
    m_btnRenPile->hide();
    connect(m_btnReverseSelection, SIGNAL(clicked()), this, SLOT(reverseSelection()));
    connect(m_btnFilterCard, SIGNAL(clicked()), this, SLOT(showCardFilterContainer()));
    connect(m_btnSortHandcard, SIGNAL(clicked()), this, SLOT(sortCards()));
    connect(m_btnNoNullification, SIGNAL(clicked()), this, SLOT(cancelNullification()));
    connect(m_btnShefu, SIGNAL(clicked()), this, SLOT(setShefuState()));
    connect(m_btnRenPile, SIGNAL(clicked()), this, SLOT(setRenPileState()));
}

void Dashboard::skillButtonActivated()
{
    QSanSkillButton *button = qobject_cast<QSanSkillButton *>(sender());
    foreach (QSanSkillButton *btn, _m_skillDock->getAllSkillButtons()) {
        if (button == btn) continue;

        if (btn->getViewAsSkill() && btn->isDown())
            btn->setState(QSanButton::S_STATE_UP);
    }

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (button == _m_equipSkillBtns[i]) continue;

        if (_m_equipSkillBtns[i])
            _m_equipSkillBtns[i]->setEnabled(false);
    }
}

void Dashboard::skillButtonDeactivated()
{
    foreach (QSanSkillButton *btn, _m_skillDock->getAllSkillButtons()) {
        if (btn->getViewAsSkill() && btn->isDown())
            btn->setState(QSanButton::S_STATE_UP);
    }

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipSkillBtns[i]) {
            _m_equipSkillBtns[i]->setEnabled(true);
            if (_m_equipSkillBtns[i]->isDown())
                _m_equipSkillBtns[i]->click();
        }
    }
}

void Dashboard::showDialogOptions(const QString &skillName, const QStringList &optionNames,
    const QStringList &enabledOptions, const QMap<QString, QString> &tooltips)
{
    hideDialogOptions();
    hideFilterContainer();
    unselectAll();

    foreach (CardItem *item, m_handCards)
        item->hide();

    if (m_btnFilterCard)
        m_btnFilterCard->setEnabled(false);

    m_dialogOptionSkillName = skillName;
    qreal availableWidth = qMax<qreal>(G_COMMON_LAYOUT.m_cardNormalWidth,
        _m_middleFrame->boundingRect().width() - getButtonWidgetWidth() - 16);
    qreal overlapStep = G_COMMON_LAYOUT.m_cardNormalWidth * 0.36;
    qreal scale = 0.92;
    if (optionNames.length() > 1) {
        qreal requiredWidth = G_COMMON_LAYOUT.m_cardNormalWidth + (optionNames.length() - 1) * overlapStep;
        if (requiredWidth > availableWidth)
            scale = qMax<qreal>(0.76, availableWidth / requiredWidth);
    }
    QSize optionSize(qRound(G_COMMON_LAYOUT.m_cardNormalWidth * scale), qRound(G_COMMON_LAYOUT.m_cardNormalHeight * scale));

    foreach (const QString &optionName, optionNames) {
        QString tooltip = tooltips.value(optionName);
        if (tooltip.isEmpty())
            tooltip = Sanguosha->translate(optionName);

        DashboardDialogOptionItem *item = new DashboardDialogOptionItem(this, optionName, tooltip, optionSize, _m_middleFrame);
        item->setZValue(5);
        item->setEnabled(enabledOptions.contains(optionName));
        m_dialogOptionItems << item;
        m_dialogOptionItemMap.insert(optionName, item);
    }

    _layoutDialogOptions();
    emit dialogOptionSelectionChanged(false);
}

void Dashboard::hideDialogOptions()
{
    if (!isShowingDialogOptions())
        return;

    qDeleteAll(m_dialogOptionItems);
    m_dialogOptionItems.clear();
    m_dialogOptionItemMap.clear();
    m_dialogOptionSkillName.clear();
    m_selectedDialogOption.clear();

    foreach (CardItem *item, m_handCards)
        item->show();
    if (m_btnFilterCard)
        m_btnFilterCard->setEnabled(true);
    adjustCards(true);
    update();

    emit dialogOptionSelectionChanged(false);
}

bool Dashboard::isShowingDialogOptions() const
{
    return !m_dialogOptionItems.isEmpty();
}

QString Dashboard::selectedDialogOption() const
{
    return m_selectedDialogOption;
}

void Dashboard::_layoutDialogOptions()
{
    if (m_dialogOptionItems.isEmpty() || _m_middleFrame == nullptr)
        return;

    DashboardDialogOptionItem *firstItem = static_cast<DashboardDialogOptionItem *>(m_dialogOptionItems.first());
    QSizeF itemSize = firstItem->boundingRect().size();
    int count = m_dialogOptionItems.length();
    qreal leftPadding = 8;
    qreal availableWidth = qMax<qreal>(itemSize.width(),
        _m_middleFrame->boundingRect().width() - getButtonWidgetWidth() - leftPadding * 2);
    qreal step = itemSize.width();
    if (count > 1)
        step = qMin<qreal>(itemSize.width() * 0.62, (availableWidth - itemSize.width()) / (count - 1));

    qreal totalWidth = itemSize.width() + step * (count - 1);
    qreal startX = qMax<qreal>(leftPadding, (availableWidth - totalWidth) / 2.0 + leftPadding);
    qreal startY = qMax<qreal>(8, _m_middleFrame->boundingRect().height() - itemSize.height() - 8);

    for (int index = 0; index < m_dialogOptionItems.length(); ++index) {
        DashboardDialogOptionItem *item = static_cast<DashboardDialogOptionItem *>(m_dialogOptionItems.at(index));
        qreal x = startX + index * step;
        item->setHomePos(QPointF(x, startY));
        item->setBaseZValue(index);
    }
}

void Dashboard::_onDialogOptionClicked(const QString &optionName)
{
    if (!m_dialogOptionItemMap.contains(optionName))
        return;

    bool clearSelection = (m_selectedDialogOption == optionName);
    m_selectedDialogOption.clear();

    foreach (QGraphicsObject *item, m_dialogOptionItems) {
        DashboardDialogOptionItem *optionItem = static_cast<DashboardDialogOptionItem *>(item);
        optionItem->setOptionSelected(false);
    }

    if (!clearSelection) {
        m_selectedDialogOption = optionName;
        DashboardDialogOptionItem *selectedItem = static_cast<DashboardDialogOptionItem *>(m_dialogOptionItemMap.value(optionName));
        selectedItem->setOptionSelected(true);
    }

    emit dialogOptionSelectionChanged(!m_selectedDialogOption.isEmpty());
}

void Dashboard::selectAll()
{
    foreach (const QString &pile, m_player->getPileNames()) {
        if (pile == "wooden_ox" || pile.startsWith("&"))
            retractPileCards(pile);
    }
    if (view_as_skill) {
        unselectAll();
        foreach (CardItem *card_item, m_handCards) {
            selectCard(card_item, true);
            pendings << card_item;
        }
        updatePending();
    }
    adjustCards(true);
}

void Dashboard::cardTip()
{
	//qShuffle(m_handCards);
	foreach(CardItem *h, m_handCards)
		h->hideFootnote();
	adjustCards(true);
}

void Dashboard::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
}

void Dashboard::mouseReleaseEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    PlayerCardContainer::mouseReleaseEvent(mouseEvent);

    int i;
	CardItem *to_select = nullptr;
    for (i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipRegions[i]->isUnderMouse()) {
            to_select = _m_equipCards[i];
            break;
        }
    }
    if (!to_select) return;
    QSanSkillButton *equipSkillButton = _getEquipSkillButton(to_select);
    if (equipSkillButton != nullptr && equipSkillButton->isEnabled())
        equipSkillButton->click();
    else if (to_select->isMarkable()) {
        // According to the game rule, you cannot select a weapon as a card when
        // you are invoking the skill of that equip. So something must be wrong.
        // Crash.
        Q_ASSERT(equipSkillButton == nullptr || !equipSkillButton->isDown());
        to_select->mark(!to_select->isMarked());
        update();
    }
}

void Dashboard::_onEquipSelectChanged()
{
    QSanSkillButton *btn = qobject_cast<QSanSkillButton *>(sender());
    if (btn) {
        _setEquipSkillHighlight(btn->objectName(), btn->isDown());
    } else {
        CardItem *equip = qobject_cast<CardItem *>(sender());
        // Do not remove this assertion. If equip is nullptr here, some other
        // sources that could select equip has not been considered and must
        // be implemented.
        Q_ASSERT(equip);
        for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
            if (_m_equipCards[i] == equip) {
                _setEquipBorderAnimation(i, equip->isMarked());
                break;
            }
        }
    }
}

void Dashboard::_createEquipBorderAnimations()
{
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        _m_equipBorders[i] = new PixmapAnimation();
        _m_equipBorders[i]->setParentItem(_getEquipParent());
        _m_equipBorders[i]->setPath("image/system/emotion/equipborder/");
        if (!_m_equipBorders[i]->valid()) {
            delete _m_equipBorders[i];
            _m_equipBorders[i] = nullptr;
            continue;
        }
        _m_equipBorders[i]->setPos(_dlayout->m_equipBorderPos + _dlayout->m_equipSelectedOffset + _dlayout->m_equipAreas[i].topLeft());
        _m_equipBorders[i]->hide();
    }
}

void Dashboard::_setEquipBorderAnimation(int index, bool turnOn)
{
    _mutexEquipAnim.lock();
    if (_m_isEquipsAnimOn[index] == turnOn) {
        _mutexEquipAnim.unlock();
        return;
    }

    QPoint newPos = _dlayout->m_equipAreas[index].topLeft();
    if (turnOn) newPos = _dlayout->m_equipSelectedOffset + newPos;

    _m_equipAnim[index]->stop();
    _m_equipAnim[index]->clear();
    QPropertyAnimation *anim = new QPropertyAnimation(_m_equipRegions[index], "pos");
    anim->setEndValue(newPos);
    anim->setDuration(200);
    _m_equipAnim[index]->addAnimation(anim);
    connect(anim, SIGNAL(finished()), anim, SLOT(deleteLater()));
    anim = new QPropertyAnimation(_m_equipRegions[index], "opacity");
    anim->setEndValue(255);
    anim->setDuration(200);
    _m_equipAnim[index]->addAnimation(anim);
    connect(anim, SIGNAL(finished()), anim, SLOT(deleteLater()));
    _m_equipAnim[index]->start();

    Q_ASSERT(_m_equipBorders[index]);
    if (turnOn) {
        _m_equipBorders[index]->show();
        _m_equipBorders[index]->start();
    } else {
        _m_equipBorders[index]->hide();
        _m_equipBorders[index]->stop();
    }

    _m_isEquipsAnimOn[index] = turnOn;
    _mutexEquipAnim.unlock();
}

void Dashboard::sortHandCards(QList<int>hands)
{
	QList<CardItem *>m_hands;
	foreach(int id, hands){
		foreach(CardItem *h, m_handCards){
			if(h->getId()==id)
				m_hands << h;
		}
	}
    if(m_hands.length()!=m_handCards.length()) return;
	m_handCards = m_hands;
	_adjustCards();
}

void Dashboard::adjustCards(bool playAnimation)
{
    _adjustCards();
    foreach(CardItem *card, m_handCards)
        card->goBack(playAnimation);
}

void Dashboard::refreshHandCardTooltips()
{
    foreach (CardItem *item, m_handCards) {
        if (item) item->refreshTooltip();
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
					
                    CardItem * card_item = CardItem::FindItem(m_handCards, card_id);
                    if (card_item == selected) selected = nullptr;
                    Q_ASSERT(card_item);
                    if (card_item) {
                        m_handCards.removeOne(card_item);
                        card_item->disconnect(this);
                        card_item->deleteLater();
                    }
                    pileNeedAdjust = true;
                }
            }
        }
    } else
        Q_ASSERT(false);
	
    //Q_ASSERT(result.size() == card_ids.size());
    if (place == Player::PlaceHand)
        adjustCards();
    else if (result.size() > 1 || place == Player::PlaceSpecial) {
        QRect rect(0, 0, _dlayout->m_disperseWidth, 0);
        QPointF center(0, 0);
        if (place == Player::PlaceEquip || place == Player::PlaceDelayedTrick) {
            for (int i = 0; i < result.size(); i++)
                center += result[i]->pos();
            center = 1.0 / result.length() * center;
        } else if (place == Player::PlaceSpecial)
            center = mapFromItem(_getAvatarParent(), _dlayout->m_avatarArea.center());
        else
            Q_ASSERT(false);
        rect.moveCenter(center.toPoint());
        _disperseCards(result, rect, Qt::AlignCenter, false, false);
		
        if (pileNeedAdjust)
            adjustCards();
    }
    update();
    return result;
}

void Dashboard::updateAvatar()
{
    PlayerCardContainer::updateAvatar();
    _m_skillDock->update();
}

static bool CompareByNumber(const CardItem *a, const CardItem *b)
{
    return Card::CompareByNumber(a->getCard(), b->getCard());
}

static bool CompareBySuit(const CardItem *a, const CardItem *b)
{
    return Card::CompareBySuit(a->getCard(), b->getCard());
}

static bool CompareByType(const CardItem *a, const CardItem *b)
{
    return Card::CompareByType(a->getCard(), b->getCard());
}

void Dashboard::sortCards()
{
    if (isShowingDialogOptions() || m_handCards.isEmpty()||(m_player&&m_player->property("NotSortHands").toBool())) return;

    QMenu *menu = _m_sort_menu;
    menu->clear();
    menu->setTitle(tr("Sort handcards"));

    QAction *action1 = menu->addAction(tr("Sort by type"));
    action1->setData((int)ByType);

    QAction *action2 = menu->addAction(tr("Sort by suit"));
    action2->setData((int)BySuit);

    QAction *action3 = menu->addAction(tr("Sort by number"));
    action3->setData((int)ByNumber);

    connect(action1, SIGNAL(triggered()), this, SLOT(beginSorting()));
    connect(action2, SIGNAL(triggered()), this, SLOT(beginSorting()));
    connect(action3, SIGNAL(triggered()), this, SLOT(beginSorting()));

    QPointF posf = QCursor::pos();
    menu->popup(QPoint(posf.x(), posf.y()));
}

void Dashboard::beginSorting()
{
    QAction *action = qobject_cast<QAction *>(sender());
    SortType type = ByType;
    if (action)
        type = (SortType)(action->data().toInt());

    switch (type) {
    case ByType: std::sort(m_handCards.begin(), m_handCards.end(), CompareByType); break;
    case BySuit: std::sort(m_handCards.begin(), m_handCards.end(), CompareBySuit); break;
    case ByNumber: std::sort(m_handCards.begin(), m_handCards.end(), CompareByNumber); break;
    default: Q_ASSERT(false);
    }

    adjustCards();
}

void Dashboard::reverseSelection()
{
    if (!view_as_skill) return;

    QList<CardItem *> selected_items;
    foreach(CardItem *item, m_handCards)
        if (item->isSelected()) {
            item->clickItem();
            selected_items << item;
        }
    foreach(CardItem *item, m_handCards)
        if (item->isEnabled() && !selected_items.contains(item))
            item->clickItem();
    adjustCards();
}

void Dashboard::cancelNullification()
{
    QString currentPlayerName = Self != nullptr ? Self->objectName() : QString();
    if (!currentPlayerName.isEmpty() && ClientInstance->m_noNullificationTrickName != ".") {
        if (ClientInstance->m_noNullificationPlayers.contains(currentPlayerName))
            ClientInstance->m_noNullificationPlayers.remove(currentPlayerName);
        else
            ClientInstance->m_noNullificationPlayers.insert(currentPlayerName);
        ClientInstance->m_noNullificationThisTime = ClientInstance->m_noNullificationPlayers.contains(currentPlayerName);
    }
    if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        && Sanguosha->getCurrentCardUsePattern() == "nullification"
        && RoomSceneInstance->isCancelButtonEnabled()) {
        RoomSceneInstance->doCancelButton();
    }
}

void Dashboard::controlNullificationButton(bool show)
{
    if (ClientInstance->getReplayer()) return;
    m_btnNoNullification->setState(ClientInstance->m_noNullificationThisTime
        ? QSanButton::S_STATE_DOWN
        : QSanButton::S_STATE_UP);
    m_btnNoNullification->setVisible(show);
}

void Dashboard::setShefuState()
{
    QMenu *menu = _m_shefu_menu;
    menu->clear();
    menu->setTitle(tr("Shefu"));

    foreach (QString mark_name, m_player->getMarkNames()) {
        if (mark_name.startsWith("Shefu_")) {
            int id = m_player->getMark(mark_name) - 1;
            if (id == -1) continue;
            const Card *c = Sanguosha->getCard(id);
            QString card_name = mark_name.mid(6);
            QString name = QString("%1 [%2]").arg(c->getFullName()).arg(Sanguosha->translate(card_name));
            menu->addAction(G_ROOM_SKIN.getCardSuitPixmap(c->getSuit()), name);
        }
    }

    menu->addSeparator();

    QAction *action1 = menu->addAction(tr("Shefu Ask All"));
    action1->setData((int)RoomScene::ShefuAskAll);
    action1->setCheckable(true);
    action1->setChecked(RoomSceneInstance->m_ShefuAskState == RoomScene::ShefuAskAll);

    QAction *action2 = menu->addAction(tr("Shefu Ask Necessary"));
    action2->setData((int)RoomScene::ShefuAskNecessary);
    action2->setCheckable(true);
    action2->setChecked(RoomSceneInstance->m_ShefuAskState == RoomScene::ShefuAskNecessary);

    QAction *action3 = menu->addAction(tr("Shefu Ask None"));
    action3->setData((int)RoomScene::ShefuAskNone);
    action3->setCheckable(true);
    action3->setChecked(RoomSceneInstance->m_ShefuAskState == RoomScene::ShefuAskNone);

    connect(action1, SIGNAL(triggered()), this, SLOT(changeShefuState()));
    connect(action2, SIGNAL(triggered()), this, SLOT(changeShefuState()));
    connect(action3, SIGNAL(triggered()), this, SLOT(changeShefuState()));

    QPointF posf = QCursor::pos();
    menu->popup(QPoint(posf.x(), posf.y()));
}

void Dashboard::changeShefuState()
{
    QAction *action = qobject_cast<QAction *>(sender());
    Q_ASSERT(action);
    RoomSceneInstance->m_ShefuAskState = (RoomScene::ShefuAskState)(action->data().toInt());
}

void Dashboard::setRenPileState()
{
    QPointF posf = QCursor::pos();
    _m_renpile_menu->popup(QPoint(posf.x(), posf.y()));
}

void Dashboard::disableAllCards()
{
    m_mutexEnableCards.lock();
    foreach(CardItem *card_item, m_handCards)
        card_item->setEnabled(false);
    m_mutexEnableCards.unlock();
}

void Dashboard::enableCards()
{
    m_mutexEnableCards.lock();
    foreach (const QString &pile, m_player->getPileNames()) {
        if (pile == "wooden_ox" || pile.startsWith("&"))
            expandPileCards(pile);
    }
    foreach(CardItem *card_item, m_handCards)
        card_item->setEnabled(card_item->getCard()->isAvailable(m_player));/*
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipCards[i]&&_m_equipSkillBtns[i]) {
            const ViewAsSkill*vs = Sanguosha->getViewAsSkill(_m_equipCards[i]->objectName());
			_m_equipCards[i]->setMarkable(vs&&vs->isEnabledAtPlay(m_player));
            _m_equipCards[i]->setEnabled(_m_equipCards[i]->isMarkable());
			_m_equipSkillBtns[i]->setEnabled(_m_equipCards[i]->isMarkable());

			if(_m_equipCards[i]->isMarkable())
                _m_equipRegions[i]->setOpacity(1.0);
			else
                _m_equipRegions[i]->setOpacity(0.7);
        }
    }*/
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
    adjustCards(true);
    m_mutexEnableCards.unlock();
}

void Dashboard::expandPileCards(const QString &pile_name)
{
    if (_m_pile_expanded.contains(pile_name)) return;
    //_m_pile_expanded << pile_name;
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
        new_name = new_name.split("/").last();
    } else if (new_name.startsWith("%")) {
        new_name = new_name.mid(1);
        foreach(const Player *p, m_player->getAliveSiblings())
            pile += p->getPile(new_name);
    } else
        pile = m_player->getPile(new_name);
    if (pile.isEmpty()) return;
    QList<CardItem *> card_items = _createCards(pile);
    foreach (CardItem *card_item, card_items) {
        card_item->setPos(mapFromScene(card_item->scenePos()));
        card_item->setParentItem(this);
        _addHandCard(card_item, true, Sanguosha->translate(new_name));
    }
    adjustCards();
    _playMoveCardsAnimation(card_items, false);
    update();
    _m_pile_expanded[pile_name] = pile;
}

void Dashboard::retractPileCards(const QString &pile_name)
{
    foreach (int id, _m_pile_expanded.value(pile_name)) {
        CardItem *card_item = CardItem::FindItem(m_handCards, id);
        if (card_item == selected) selected = nullptr;
        Q_ASSERT(card_item);
        if (card_item) {
            m_handCards.removeOne(card_item);
            card_item->disconnect(this);
            delete card_item;
        }
    }
    _m_pile_expanded.remove(pile_name);
    adjustCards();
    update();
}

void Dashboard::retractAllSkillPileCards()
{
    foreach (const QString &pileName, _m_pile_expanded.keys()) {
        //if (pileName == "wooden_ox" || pileName.startsWith("&"))
            retractPileCards(pileName);
    }
}

void Dashboard::onCardItemClicked()
{
    CardItem *card_item = qobject_cast<CardItem *>(sender());
    if (!card_item) return;

    if (view_as_skill) {
        if (card_item->isSelected()) {
            selectCard(card_item, false);
            pendings.removeOne(card_item);
        } else {
            if (view_as_skill->inherits("OneCardViewAsSkill"))
                unselectAll();
            selectCard(card_item, true);
            pendings << card_item;
        }

        updatePending();
    } else {
        if (card_item->isSelected()) {
            unselectAll();
            emit card_selected(nullptr);
        } else {
            unselectAll();
            selectCard(card_item, true);
            selected = card_item;

            emit card_selected(selected->getCard());
        }
    }
}

void Dashboard::updatePending()
{
    if (!view_as_skill) return;
    QList<const Card *> cards, pended;
    foreach(CardItem *item, pendings)
        cards.append(item->getCard());

    if (!view_as_skill->inherits("OneCardViewAsSkill"))
        pended = cards;
    foreach (CardItem *item, m_handCards) {
        if (!item->isSelected() || pendings.isEmpty())
            item->setEnabled(view_as_skill->viewFilter(pended,item->getCard()));
        if (!item->isEnabled())
            animations->effectOut(item);
    }

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipCards[i]) {
            if(!_m_equipCards[i]->isMarked())
				_m_equipCards[i]->setMarkable(view_as_skill->viewFilter(pended,_m_equipCards[i]->getCard()));

			QSanSkillButton *equipSkillButton = _getEquipSkillButton(_m_equipCards[i]);
			if(_m_equipCards[i]->isMarkable()||(equipSkillButton&&equipSkillButton->isEnabled()))
                _m_equipRegions[i]->setOpacity(1.0);
			else
                _m_equipRegions[i]->setOpacity(0.7);
        }
    }

    const Card *new_pending_card = view_as_skill->viewAs(cards);
    if (pending_card != new_pending_card) {
        if (pending_card && !pending_card->parent() && pending_card->isVirtualCard())
            delete pending_card;/*
        if (view_as_skill->objectName().contains("guhuo")&&new_pending_card
            && Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
            foreach (CardItem *item, m_handCards) {
                if (item->getCard() == cards.first()) {
					item->hideFootnote();
                    const SkillCard *guhuo = qobject_cast<const SkillCard *>(new_pending_card);
                    item->setFootnote(Sanguosha->translate(guhuo->getUserString()));
                    item->showFootnote();
                }
            }
        }*/
        pending_card = new_pending_card;
        emit card_selected(pending_card);
    }
}

void Dashboard::clearPendings()
{
    selected = nullptr;
    foreach(CardItem *item, m_handCards)
        selectCard(item, false);
    pendings.clear();
}

void Dashboard::onCardItemDoubleClicked()
{
    if (!Config.EnableDoubleClick) return;
    CardItem *card_item = qobject_cast<CardItem *>(sender());
    if (card_item) {
        if (!view_as_skill) selected = card_item;
        animations->effectOut(card_item);
        emit card_to_use();
    }
}

void Dashboard::onCardItemThrown()
{
    CardItem *card_item = qobject_cast<CardItem *>(sender());
    if (card_item) {
        if (!view_as_skill) selected = card_item;
        emit card_to_use();
    }
}

void Dashboard::onCardItemHover()
{
    QGraphicsItem *card_item = qobject_cast<QGraphicsItem *>(sender());
    if (!card_item) return;

    animations->emphasize(card_item);
}

void Dashboard::onCardItemLeaveHover()
{
    QGraphicsItem *card_item = qobject_cast<QGraphicsItem *>(sender());
    if (!card_item) return;

    animations->effectOut(card_item);
}

void Dashboard::onMarkChanged()
{
    CardItem *card_item = qobject_cast<CardItem *>(sender());

    Q_ASSERT(card_item->isEquipped());

    if (card_item) {
        if (card_item->isMarked()) {
            if (!pendings.contains(card_item)) {
                if (view_as_skill && view_as_skill->inherits("OneCardViewAsSkill"))
                    unselectAll(card_item);
                pendings.append(card_item);
            }
        } else
            pendings.removeOne(card_item);

        updatePending();
    }
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
    qDeleteAll(_m_filterUIElements);
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
    if (isShowingDialogOptions() || m_handCards.isEmpty() || (m_player && m_player->property("NotSortHands").toBool())) return;

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

            QRectF cardBounds = cItem->boundingRect();
            QPointF cardCenter = cardBounds.center();

            QRectF textBounds = label->boundingRect();
            label->setPos(cardCenter.x() - textBounds.width() / 2, cardCenter.y() - textBounds.height() / 2);

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

