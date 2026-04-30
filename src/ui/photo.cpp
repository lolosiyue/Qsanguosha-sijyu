#include "photo.h"
//#include "clientplayer.h"
//#include "settings.h"
#include "carditem.h"
#include "engine.h"
#include "graphicspixmaphoveritem.h"
//#include "standard.h"
//#include "client.h"
//#include "playercarddialog.h"
#include "rolecombobox.h"
//#include "skin-bank.h"
#include "sprite.h"

#include "pixmapanimation.h"

using namespace QSanProtocol;

// skins that remain to be extracted:
// equips
// mark
// emotions
// hp
// seatNumber
// death logo
// kingdom mask and kingdom icon (decouple from player)
// make layers (drawing order) configurable

Photo::Photo() : PlayerCardContainer(), m_giftHighlighted(false), m_giftHighlightFrame(nullptr)
{
    _m_mainFrame = nullptr;
    m_player = nullptr;
    _m_focusFrame = nullptr;
    _m_onlineStatusItem = nullptr;
    _m_layout = &G_PHOTO_LAYOUT;
    _m_frameType = S_FRAME_NO_FRAME;
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setTransform(QTransform::fromTranslate(-G_PHOTO_LAYOUT.m_normalWidth / 2, -G_PHOTO_LAYOUT.m_normalHeight / 2), true);
    _m_skillNameItem = new QGraphicsPixmapItem(_m_groupMain);

    emotion_item = new Sprite(_m_groupMain);

    _m_duanchangMask = new QGraphicsRectItem(_m_groupMain);
    _m_duanchangMask->setRect(boundingRect());
    _m_duanchangMask->setZValue(3);
    _m_duanchangMask->setOpacity(0.4);
    _m_duanchangMask->hide();
    QBrush duanchang_brush(G_PHOTO_LAYOUT.m_duanchangMaskColor);
    _m_duanchangMask->setBrush(duanchang_brush);

    _createControls();
    repaintAll();
}

Photo::~Photo()
{
    if (emotion_item) {
        delete emotion_item;
        emotion_item = nullptr;
    }
    if (m_giftHighlightFrame) {
        delete m_giftHighlightFrame;
        m_giftHighlightFrame = nullptr;
    }
}

void Photo::refresh(bool killed)
{
    PlayerCardContainer::refresh(killed);
    if(!m_player) return;
	if(m_player->getState()=="online"){
		if(_m_onlineStatusItem)
			_m_onlineStatusItem->hide();
	}else{
		QRect rect = G_PHOTO_LAYOUT.m_onlineStatusArea;
        QImage image(rect.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.fillRect(QRect(0, 0, rect.width(), rect.height()), G_PHOTO_LAYOUT.m_onlineStatusBgColor);
        G_PHOTO_LAYOUT.m_onlineStatusFont.paintText(&painter, QRect(QPoint(0, 0), rect.size()), Qt::AlignCenter, Sanguosha->translate(m_player->getState()));
        QPixmap pixmap = QPixmap::fromImage(image);
        _paintPixmap(_m_onlineStatusItem, rect, pixmap, _m_groupMain);
        _layBetween(_m_onlineStatusItem, _m_mainFrame, _m_chainIcon);
        if (!_m_onlineStatusItem->isVisible()) _m_onlineStatusItem->show();
	}
}

QRectF Photo::boundingRect() const
{
    return QRect(0, 0, G_PHOTO_LAYOUT.m_normalWidth, G_PHOTO_LAYOUT.m_normalHeight);
}

void Photo::repaintAll(bool all)
{
    resetTransform();
    setTransform(QTransform::fromTranslate(-G_PHOTO_LAYOUT.m_normalWidth / 2, -G_PHOTO_LAYOUT.m_normalHeight / 2), true);
    _paintPixmap(_m_mainFrame, G_PHOTO_LAYOUT.m_mainFrameArea, QSanRoomSkin::S_SKIN_KEY_MAINFRAME);
    setFrame(_m_frameType);
    hideSkillName(); // @todo: currently we don't adjust skillName's position for simplicity,
    // consider repainting it instead of hiding it in the future.
    PlayerCardContainer::repaintAll(all);
	_adjustComponentZValues();
}

void Photo::_adjustComponentZValues(bool killed)
{
    PlayerCardContainer::_adjustComponentZValues(killed);
    //_layBetween(emotion_item, _m_chainIcon, _m_roleComboBox);
    _layBetween(_m_skillNameItem, _m_chainIcon, _m_roleComboBox);
    // Place mainFrame between smallAvatarArea and faceTurnedIcon so it sits
    // well below equips (~4 Z-units gap) instead of right next to them.
    // When face-up faceTurnedIcon is hidden, so the frame and deputy avatar
    // behind it are both visible.  When face-down the mask correctly covers them.
    _layBetween(_m_mainFrame, _m_smallAvatarArea, _m_faceTurnedIcon);
    // Deputy avatar + circle: place just above mainFrame.
    // They end up ~4 Z-units below the lowest equip region, eliminating
    // the intermittent overlap that the old 0.2-gap approach caused.
    if (_m_smallAvatarIcon && _m_mainFrame
        && !(m_player && m_player->getGeneral2Name().contains("zuoci") && _m_huashenAnimation != nullptr)) {
        double z = _m_mainFrame->zValue();
        _m_smallAvatarIcon->setZValue(z + 0.2);
        if (_m_circleItem)
            _m_circleItem->setZValue(z + 0.3);
    }
    _m_progressBarItem->setZValue(_m_groupMain->zValue() + 1);
}

void Photo::setEmotion(const QString &emotion, bool permanent)
{
    if (emotion == ".") {
        hideEmotion();
        return;
    }

    QString path = QString("image/system/emotion/%1.png").arg(emotion);
    if (QFile::exists(path)) {
        QPixmap pixmap = PixmapAnimation::GetFrameFromCache(path);
        emotion_item->setPixmap(pixmap);
        emotion_item->setPos((G_PHOTO_LAYOUT.m_normalWidth - pixmap.width()) / 2, (G_PHOTO_LAYOUT.m_normalHeight - pixmap.height()) / 2);
        _layBetween(emotion_item, _m_chainIcon, _m_roleComboBox);

        QPropertyAnimation *appear = new QPropertyAnimation(emotion_item, "opacity");
        appear->setStartValue(0.0);
        if (permanent) {
            appear->setEndValue(1.0);
            appear->setDuration(500);
        } else {
            appear->setKeyValueAt(0.25, 1.0);
            appear->setKeyValueAt(0.75, 1.0);
            appear->setEndValue(0.0);
            appear->setDuration(2000);
        }
        appear->start(QAbstractAnimation::DeleteWhenStopped);
    } else
        PixmapAnimation::GetPixmapAnimation(this, emotion);
}

void Photo::tremble()
{
    QPropertyAnimation *vibrate = new QPropertyAnimation(this, "x");
    static qreal offset = 20;

    vibrate->setKeyValueAt(0.5, x() - offset);
    vibrate->setEndValue(x());

    vibrate->setEasingCurve(QEasingCurve::OutInBounce);

    vibrate->start(QAbstractAnimation::DeleteWhenStopped);
}

void Photo::showSkillName(const QString &skill_name)
{
    G_PHOTO_LAYOUT.m_skillNameFont.paintText(_m_skillNameItem,G_PHOTO_LAYOUT.m_skillNameArea,Qt::AlignLeft,Sanguosha->translate(skill_name));
    _m_skillNameItem->show();
    QTimer::singleShot(1111, this, SLOT(hideSkillName()));
}

void Photo::hideSkillName()
{
    _m_skillNameItem->hide();
}

void Photo::hideEmotion()
{
    QPropertyAnimation *disappear = new QPropertyAnimation(emotion_item, "opacity");
    disappear->setStartValue(1.0);
    disappear->setEndValue(0.0);
    disappear->setDuration(500);
    disappear->start(QAbstractAnimation::DeleteWhenStopped);
}

void Photo::updateDuanchang()
{
    _m_duanchangMask->setVisible(m_player&&m_player->getMark("@duanchang")>0);
}

const ClientPlayer *Photo::getPlayer() const
{
    return m_player;
}

void Photo::speak(const QString &)
{
}

QList<CardItem *> Photo::removeCardItems(const QList<int> &card_ids, Player::Place place)
{
    QList<CardItem *> result;
    if (place == Player::PlaceHand || place == Player::PlaceSpecial) {
        result = _createCards(card_ids);
        updateHandcardNum();
    } else if (place == Player::PlaceEquip)
        result = removeEquips(card_ids);
    else if (place == Player::PlaceDelayedTrick)
        result = removeDelayedTricks(card_ids);

    // if it is just one card from equip or judge area, we'd like to keep them
    // to start from the equip/trick icon.
    if (place == Player::PlaceHand || place == Player::PlaceSpecial || result.size() > 1)
        _disperseCards(result, G_PHOTO_LAYOUT.m_cardMoveRegion, Qt::AlignCenter, false, false);

    update();
    return result;
}

bool Photo::_addCardItems(QList<CardItem *> &card_items, const CardsMoveStruct &moveInfo)
{
    _disperseCards(card_items, G_PHOTO_LAYOUT.m_cardMoveRegion, Qt::AlignCenter, true, false);

    foreach(CardItem *card_item, card_items)
        card_item->setHomeOpacity(0);
    if (moveInfo.to_place == Player::PlaceEquip) {
        addEquips(card_items);
    } else if (moveInfo.to_place == Player::PlaceDelayedTrick) {
        addDelayedTricks(card_items);
    } else if (moveInfo.to_place==Player::PlaceHand||moveInfo.to_place==Player::PlaceSpecial) {
        updateHandcardNum();
		return true;
    }
    return false;
}

void Photo::addEquips(QList<CardItem *> &equips)
{
    PlayerCardContainer::addEquips(equips);
    _adjustComponentZValues();
}

void Photo::setFrame(FrameType type)
{
    _m_frameType = type;
    if (type == S_FRAME_NO_FRAME) {
        if (_m_focusFrame) {
            if (_m_saveMeIcon && _m_saveMeIcon->isVisible())
                setFrame(S_FRAME_SOS);
            else if (m_player->getPhase() != Player::NotActive)
                setFrame(S_FRAME_PLAYING);
            else
                _m_focusFrame->hide();
        }
    } else {
        _paintPixmap(_m_focusFrame, G_PHOTO_LAYOUT.m_focusFrameArea,
            _getPixmap(QSanRoomSkin::S_SKIN_KEY_FOCUS_FRAME, QString::number(type)),
            _m_groupMain);
        _layBetween(_m_focusFrame, _m_avatarArea, _m_mainFrame);
        _m_focusFrame->show();
    }
    update();
}

void Photo::updatePhase()
{
    PlayerCardContainer::updatePhase();
    if (m_player->getPhase() != Player::NotActive)
        setFrame(S_FRAME_PLAYING);
    else
        setFrame(S_FRAME_NO_FRAME);
}

void Photo::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
}

QGraphicsItem *Photo::getMouseClickReceiver()
{
    return this;
}

void Photo::setGiftHighlight(bool highlight)
{
    if (m_giftHighlighted == highlight) return;

    m_giftHighlighted = highlight;

    if (highlight) {
        if (!m_giftHighlightFrame) {
            m_giftHighlightFrame = new QGraphicsPixmapItem(_m_groupMain);

            QPixmap highlightPixmap(G_PHOTO_LAYOUT.m_normalWidth, G_PHOTO_LAYOUT.m_normalHeight);
            highlightPixmap.fill(Qt::transparent);

            QPainter painter(&highlightPixmap);
            painter.setRenderHint(QPainter::Antialiasing);

            QPen pen(QColor(255, 215, 0, 200));
            pen.setWidth(3);
            painter.setPen(pen);
            painter.setBrush(QBrush(QColor(255, 215, 0, 30)));
            painter.drawRoundedRect(2, 2, G_PHOTO_LAYOUT.m_normalWidth - 4,
                                  G_PHOTO_LAYOUT.m_normalHeight - 4, 8, 8);

            m_giftHighlightFrame->setPixmap(highlightPixmap);
            m_giftHighlightFrame->setZValue(_m_groupMain->zValue() + 0.1);
        }
        m_giftHighlightFrame->show();
    } else {
        if (m_giftHighlightFrame) {
            m_giftHighlightFrame->hide();
        }
    }
}

bool Photo::isGiftHighlighted() const
{
    return m_giftHighlighted;
}

void Photo::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_giftHighlighted && event->button() == Qt::LeftButton) {
        emit giftClicked();
        return;
    }

    PlayerCardContainer::mouseReleaseEvent(event);
}

