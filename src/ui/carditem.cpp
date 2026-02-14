#include "carditem.h"
#include "engine.h"
//#include "skill.h"
//#include "clientplayer.h"
//#include "settings.h"
#include "skin-bank.h"

void CardItem::_initialize()
{
    setFlag(QGraphicsItem::ItemIsMovable);
    m_opacityAtHome = 1.0;
    m_currentAnimation = nullptr;
    _m_width = G_COMMON_LAYOUT.m_cardNormalWidth;
    _m_height = G_COMMON_LAYOUT.m_cardNormalHeight;
    _m_isUnknownGeneral = false;
    _m_showFootnote = true;
    m_isSelected = false;
    m_isShiny = false;
    auto_back = true;
    frozen = false;
    resetTransform();
    setTransform(QTransform::fromTranslate(-_m_width / 2, -_m_height / 2), true);
    m_cardId = Card::S_UNKNOWN_CARD_ID;
	setObjectName("unknown");
}

CardItem::CardItem(const Card *card)
{
    _initialize();
    m_isShiny = qrand() < (RAND_MAX + 1L) / 4096;
    setCard(card);
    setAcceptHoverEvents(true);
}

CardItem::CardItem(const QString &general_name)
{
    _initialize();
    changeGeneral(general_name);
}

QRectF CardItem::boundingRect() const
{
    return G_COMMON_LAYOUT.m_cardFrameArea;
}

void CardItem::setCard(const Card *card)
{
    if (!card) return;
	m_cardId = card->getId();
	const Card *c = Sanguosha->getCard(m_cardId);
	if(c!=nullptr){
		if(c->isKindOf("Xumou")) return;
		if(card->objectName().contains("_zhizhe_")) card = c;
	}
	setObjectName(card->objectName());
	QString description = card->getDescription();
	if (m_isShiny) description = QString("<font color=#FF0000>%1</font>").arg(description);
	setToolTip(description);
}

void CardItem::setEnabled(bool enabled)
{
    QSanSelectableItem::setEnabled(enabled);
}

CardItem::~CardItem()
{
    m_animationMutex.lock();
    if (m_currentAnimation != nullptr) {
        delete m_currentAnimation;
        m_currentAnimation = nullptr;
    }
    m_animationMutex.unlock();
}

void CardItem::changeGeneral(const QString &general_name)
{
    setObjectName(general_name);
    const General *general = Sanguosha->getGeneral(general_name);
    if (general) {
        setToolTip(general->getSkillDescription(true));
    } else {
        _m_isUnknownGeneral = true;
        setToolTip("");
    }
}

const Card *CardItem::getCard() const
{
    return Sanguosha->getCard(m_cardId);
}

void CardItem::setHomePos(QPointF home_pos)
{
    this->home_pos = home_pos;
}

QPointF CardItem::homePos() const
{
    return home_pos;
}

void CardItem::goBack(bool playAnimation, bool doFade)
{
    if (playAnimation) {
        getGoBackAnimation(doFade);
        if (m_currentAnimation != nullptr)
            m_currentAnimation->start();
    } else {
        m_animationMutex.lock();
        if (m_currentAnimation != nullptr) {
            m_currentAnimation->stop();
            delete m_currentAnimation;
            m_currentAnimation = nullptr;
        }
        setPos(homePos());
        m_animationMutex.unlock();
    }
}

QAbstractAnimation *CardItem::getGoBackAnimation(bool doFade, bool smoothTransition, int duration)
{
    m_animationMutex.lock();
    if (m_currentAnimation != nullptr) {
        m_currentAnimation->stop();
        delete m_currentAnimation;
    }
    QPropertyAnimation *goback = new QPropertyAnimation(this, "pos");
    goback->setEasingCurve(QEasingCurve::OutQuad);
    goback->setDuration(duration);
    goback->setEndValue(home_pos);
	m_currentAnimation = goback;

    if (doFade) {
        QParallelAnimationGroup *group = new QParallelAnimationGroup;
        QPropertyAnimation *disappear = new QPropertyAnimation(this, "opacity");
        double middleOpacity = qMax(opacity(), m_opacityAtHome);
        if (middleOpacity == 0) middleOpacity = 1.0;
        disappear->setEndValue(m_opacityAtHome);
        if (!smoothTransition) {
            disappear->setKeyValueAt(0.2, middleOpacity);
            disappear->setKeyValueAt(0.8, middleOpacity);
            disappear->setDuration(duration);
        }

        group->addAnimation(goback);
        group->addAnimation(disappear);

        m_currentAnimation = group;
    }
    m_animationMutex.unlock();
    connect(m_currentAnimation, SIGNAL(finished()), this, SIGNAL(movement_animation_finished()));
    connect(m_currentAnimation, SIGNAL(destroyed()), this, SLOT(currentAnimationDestroyed()));
    return m_currentAnimation;
}

void CardItem::currentAnimationDestroyed()
{
    QObject *ca = sender();
    if (m_currentAnimation == ca)
        m_currentAnimation = nullptr;
}

void CardItem::showAvatar(const QString &general_name)
{
    _m_avatarName = general_name;
}

void CardItem::hideAvatar()
{
    _m_avatarName = "";
}

void CardItem::setAutoBack(bool auto_back)
{
    this->auto_back = auto_back;
}

bool CardItem::isEquipped() const
{
    const Card *card = getCard();
    Q_ASSERT(card);
    return Self->hasEquip(card);
}

void CardItem::setFrozen(bool is_frozen)
{
    frozen = is_frozen;
}

CardItem *CardItem::FindItem(const QList<CardItem *> &items, int card_id)
{
    foreach (CardItem *item, items) {
        if (item->getCard()) {
			if (item->getCard()->getId() == card_id)
				return item;
        }else if(card_id == Card::S_UNKNOWN_CARD_ID)
			return item;
    }
    return nullptr;
}

const int CardItem::_S_CLICK_JITTER_TOLERANCE = 1600;
const int CardItem::_S_MOVE_JITTER_TOLERANCE = 200;

void CardItem::mousePressEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    if (frozen) return;
    _m_lastMousePressScenePos = mapToParent(mouseEvent->pos());
}

void CardItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    if (frozen) return;

    QPointF totalMove = mapToParent(mouseEvent->pos()) - _m_lastMousePressScenePos;
    if (totalMove.x() * totalMove.x() + totalMove.y() * totalMove.y() < _S_MOVE_JITTER_TOLERANCE)
        emit clicked();
    else
        emit released();

    if (auto_back) {
        goBack(true, false);
    }
}

void CardItem::mouseMoveEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    if (!(flags() & QGraphicsItem::ItemIsMovable)) return;
    QPointF newPos = mapToParent(mouseEvent->pos());
    QPointF totalMove = newPos - _m_lastMousePressScenePos;
    if (totalMove.x() * totalMove.x() + totalMove.y() * totalMove.y() >= _S_CLICK_JITTER_TOLERANCE) {
        QPointF down_pos = mouseEvent->buttonDownPos(Qt::LeftButton);
        setPos(newPos - this->transform().map(down_pos));
    }
}

void CardItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    if (frozen) return;

    if (hasFocus()) {
        event->accept();
        emit double_clicked();
    } else
        emit toggle_discards();
}

void CardItem::hoverEnterEvent(QGraphicsSceneHoverEvent *)
{
    emit enter_hover();
}

void CardItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    emit leave_hover();
}

void CardItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    if (!isEnabled()) {
        painter->fillRect(G_COMMON_LAYOUT.m_cardMainArea, QColor(100, 100, 100, 255 * opacity()));
        painter->setOpacity(0.7 * opacity());
    }

    const Card *card = Sanguosha->getEngineCard(m_cardId);
    const Card *card_now = Sanguosha->getCard(m_cardId, false);
	bool zhizhe = card_now && card->objectName().contains("_zhizhe_");
    if (_m_isUnknownGeneral)
        painter->drawPixmap(G_COMMON_LAYOUT.m_cardMainArea, G_ROOM_SKIN.getPixmap(QString("generalCardBack"), QString(), true));
    else{
        if (zhizhe)
            painter->drawPixmap(G_COMMON_LAYOUT.m_cardMainArea, G_ROOM_SKIN.getCardMainPixmap(card_now->objectName(), true));
        else
            painter->drawPixmap(G_COMMON_LAYOUT.m_cardMainArea, G_ROOM_SKIN.getCardMainPixmap(objectName(), true));
	}
    if (card&&!(card_now&&card_now->isKindOf("Xumou"))) {
		painter->drawPixmap(G_COMMON_LAYOUT.m_cardSuitArea, G_ROOM_SKIN.getCardSuitPixmap(card->getSuit()));
		painter->drawPixmap(G_COMMON_LAYOUT.m_cardNumberArea, G_ROOM_SKIN.getCardNumberPixmap(card->getNumber(), card->isBlack()));
		QStringList footnotes;
        if (zhizhe) {
			footnotes << Sanguosha->translate(card_now->getSkillName());
			QString description = card_now->getDescription();
			if (m_isShiny) description = QString("<font color=#FF0000>%1</font>").arg(description);
			setToolTip(description);
        }
		QString info = card->property("YingBianEffects").toString();
		if (!info.isEmpty()) footnotes << Sanguosha->translate(":" + info);
		foreach (QString tag, card->property("CharTag").toStringList())
			footnotes << Sanguosha->translate(":" + tag);
        if (!footnotes.isEmpty()) {
			setYingbiannote(footnotes.join(","));
            painter->drawImage(G_COMMON_LAYOUT.m_cardYingbianArea, _m_yingbiannoteImage);
        }

        // Deal with stupid QT...
        if (_m_showFootnote)
			painter->drawImage(G_COMMON_LAYOUT.m_cardFootnoteArea, _m_footnoteImage);
		else if(card_now){
            footnotes.clear();
            foreach (QString tip, card_now->getTips())
                footnotes << Sanguosha->translate(tip);
            if (!(zhizhe||card_now->getSkillName().isEmpty())) {
				info = Sanguosha->translate(card_now->objectName());
				info.append(Sanguosha->translate(card_now->getSuitString()));
				info.append(card_now->getNumberString());
				footnotes << info;
            }

            if (!footnotes.isEmpty()) {
                setFootnote(footnotes.join(","));
                // Deal with stupid QT...
                painter->drawImage(G_COMMON_LAYOUT.m_cardFootnoteArea, _m_footnoteImage);
            }
		}
    }

    if (!_m_avatarName.isEmpty())
        painter->drawPixmap(G_COMMON_LAYOUT.m_cardAvatarArea, G_ROOM_SKIN.getCardAvatarPixmap(_m_avatarName));

    if (m_isShiny) {
        static QBrush painter_brush(QColor(255, 215, 0, 64));
        painter->setBrush(painter_brush);
        painter->drawRect(G_COMMON_LAYOUT.m_cardMainArea);
    }
}

void CardItem::setFootnote(const QString &desc)
{
    const IQSanComponentSkin::QSanShadowTextFont &font = G_COMMON_LAYOUT.m_cardFootnoteFont;
    QRect rect = G_COMMON_LAYOUT.m_cardFootnoteArea;
    rect.moveTopLeft(QPoint(0, 0));
    _m_footnoteImage = QImage(rect.size(), QImage::Format_ARGB32);
    _m_footnoteImage.fill(Qt::transparent);
    QPainter painter(&_m_footnoteImage);
    font.paintText(&painter, QRect(QPoint(0, 0), rect.size()),
        (Qt::AlignmentFlag)((int)Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWrapAnywhere), desc);
}

void CardItem::setYingbiannote(const QString &desc)
{
    const IQSanComponentSkin::QSanShadowTextFont &font = G_COMMON_LAYOUT.m_cardFootnoteFont;
    QRect rect = G_COMMON_LAYOUT.m_cardYingbianArea;
    rect.moveTopLeft(QPoint(0, 0));
    _m_yingbiannoteImage = QImage(rect.size(), QImage::Format_ARGB32);
    _m_yingbiannoteImage.fill(Qt::transparent);
    QPainter painter(&_m_yingbiannoteImage);
    font.paintText(&painter, QRect(QPoint(0, 0), rect.size()),
        (Qt::AlignmentFlag)((int)Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWrapAnywhere), desc);
}