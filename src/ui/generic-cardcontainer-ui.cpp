#include "generic-cardcontainer-ui.h"
#include "engine.h"
#include "standard.h"
#include "graphicspixmaphoveritem.h"
#include "roomscene.h"
#include "wrapped-card.h"
#include "timed-progressbar.h"
#include "magatamas-item.h"
#include "rolecombobox.h"
#include "clientstruct.h"
#include "carditem.h"
#include "generaloverview.h"
#include "window.h"
#include "button.h"
#include <QMutexLocker>

using namespace QSanProtocol;

QList<CardItem *> GenericCardContainer::cloneCardItems(QList<int> card_ids)
{
    return _createCards(card_ids);
}

QList<CardItem *> GenericCardContainer::_createCards(QList<int> card_ids)
{
    QList<CardItem *> result;
    foreach (int card_id, card_ids)
        result.append(_createCard(card_id));
    return result;
}

CardItem *GenericCardContainer::_createCard(int card_id)
{
    CardItem *item = new CardItem(Sanguosha->getEngineCard(card_id));
    item->setParentItem(this);
    item->setOpacity(0);
    return item;
}

void GenericCardContainer::_destroyCard()
{
    CardItem *card = (CardItem *)sender();
    card->setVisible(false);
    card->deleteLater();
}

bool GenericCardContainer::_horizontalPosLessThan(const CardItem *card1, const CardItem *card2)
{
    return card1->x() < card2->x();
}

void GenericCardContainer::_disperseCards(QList<CardItem *> &cards, QRectF fillRegion,
    Qt::Alignment align, bool useHomePos, bool keepOrder)
{
    int numCards = cards.length();
    if (numCards<1) return;
    if (!keepOrder&&numCards>1)
		std::sort(cards.begin(), cards.end(), _horizontalPosLessThan);
    double w = G_COMMON_LAYOUT.m_cardNormalWidth, step = qMin(w, (fillRegion.width() - w) / (numCards - 1));
    align &= Qt::AlignHorizontal_Mask;
    for (int i = 0; i < numCards; i++) {
        if (align == Qt::AlignHCenter)
            w = fillRegion.center().x() + step * (i - (numCards - 1) / 2.0);
        else if (align == Qt::AlignLeft)
            w = fillRegion.left() + step * i + cards[i]->boundingRect().width() / 2.0;
        else if (align == Qt::AlignRight)
            w = fillRegion.right() + step * (i - numCards) + cards[i]->boundingRect().width() / 2.0;
        else
            continue;
        if (useHomePos) cards[i]->setHomePos(QPointF(w, fillRegion.center().y()));
        else cards[i]->setPos(QPointF(w, fillRegion.center().y()));
		cards[i]->setZValue(11.0+_m_highestZ*0.01);
		_m_highestZ++;
    }
}

void GenericCardContainer::onAnimationFinished()
{
    QParallelAnimationGroup *animation = qobject_cast<QParallelAnimationGroup *>(sender());
    if (animation) {
        while (animation->animationCount() > 0)
            animation->takeAnimation(0);
        animation->deleteLater();
    }
}

void GenericCardContainer::_playMoveCardsAnimation(QList<CardItem *> &cards, bool destroyCards)
{
    QParallelAnimationGroup *animation = new QParallelAnimationGroup;
    foreach (CardItem *card_item, cards) {
		if (destroyCards)
            connect(card_item, SIGNAL(movement_animation_finished()), this, SLOT(_destroyCard()));
        animation->addAnimation(card_item->getGoBackAnimation(true));
    }

    connect(animation, SIGNAL(finished()), this, SLOT(update()));
    connect(animation, SIGNAL(finished()), this, SLOT(onAnimationFinished()));
    animation->start();
}

void GenericCardContainer::addCardItems(QList<CardItem *> &card_items, const CardsMoveStruct &moveInfo)
{
    foreach (CardItem *card_item, card_items) {
        card_item->setPos(mapFromScene(card_item->scenePos()));
        card_item->setParentItem(this);
    }
    _playMoveCardsAnimation(card_items, _addCardItems(card_items, moveInfo));
}

void PlayerCardContainer::_paintPixmap(QGraphicsPixmapItem *&item, const QRect &rect, const QString &key)
{
    _paintPixmap(item, rect, _getPixmap(key));
}

void PlayerCardContainer::_paintPixmap(QGraphicsPixmapItem *&item, const QRect &rect,
    const QString &key, QGraphicsItem *parent)
{
    _paintPixmap(item, rect, _getPixmap(key), parent);
}

void PlayerCardContainer::_paintPixmap(QGraphicsPixmapItem *&item, const QRect &rect, const QPixmap &pixmap)
{
    _paintPixmap(item, rect, pixmap, _m_groupMain);
}

QPixmap PlayerCardContainer::_getPixmap(const QString &key, const QString &sArg, bool cache)
{
    //Q_ASSERT(key.contains("%1"));
    if (key.contains("%2")) {
        QString rKey = key.arg(getResourceKeyName()).arg(sArg);

        if (G_ROOM_SKIN.isImageKeyDefined(rKey))
            return G_ROOM_SKIN.getPixmap(rKey, QString(), cache); // first try "%1key%2 = ...", %1 = "photo", %2 = sArg

        rKey = key.arg(getResourceKeyName());
        return G_ROOM_SKIN.getPixmap(rKey, sArg, cache); // then try "%1key = ..."
    }
	return G_ROOM_SKIN.getPixmap(key, sArg, cache); // finally, try "key = ..."
}

QPixmap PlayerCardContainer::_getPixmap(const QString &key, bool cache)
{
    if (key.contains("%1") && G_ROOM_SKIN.isImageKeyDefined(key.arg(getResourceKeyName())))
        return G_ROOM_SKIN.getPixmap(key.arg(getResourceKeyName()), QString(), cache);
    return G_ROOM_SKIN.getPixmap(key, QString(), cache);
}

void PlayerCardContainer::_paintPixmap(QGraphicsPixmapItem *&item, const QRect &rect,
    const QPixmap &pixmap, QGraphicsItem *parent)
{
    if (item == nullptr) {
        item = new QGraphicsPixmapItem(parent);
        item->setTransformationMode(Qt::SmoothTransformation);
    }
    item->setPos(rect.x(), rect.y());
    if (pixmap.size() == rect.size()) item->setPixmap(pixmap);
    else item->setPixmap(pixmap.scaled(rect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    item->setParentItem(parent);
}

void PlayerCardContainer::_clearPixmap(QGraphicsPixmapItem *pixmap)
{
    if (pixmap == nullptr) return;
    QPixmap dummy;
    pixmap->setPixmap(dummy);
    pixmap->hide();
}

void PlayerCardContainer::hideProgressBar()
{
    _m_progressBar->hide();
}

void PlayerCardContainer::showProgressBar(Countdown countdown)
{
    _m_progressBar->setCountdown(countdown);
    _m_progressBar->show();
}

QPixmap PlayerCardContainer::getSmallAvatarIcon(const QString &generalName)
{
    return paintByMask(G_ROOM_SKIN.getGeneralPixmap(generalName, QSanRoomSkin::GeneralIconSize(_m_layout->m_smallAvatarSize)));
}

QPixmap PlayerCardContainer::_getAvatarIcon(const QString &heroName)
{
    int avatarSize = m_player->getGeneral2() ? _m_layout->m_primaryAvatarSize : _m_layout->m_avatarSize;
    return G_ROOM_SKIN.getGeneralPixmap(heroName, (QSanRoomSkin::GeneralIconSize)avatarSize);
}

void PlayerCardContainer::updateAvatar()
{
    if (_m_avatarIcon == nullptr) {
        _m_avatarIcon = new GraphicsPixmapHoverItem(this, _getAvatarParent());
        _m_avatarIcon->setTransformationMode(Qt::SmoothTransformation);
        _m_avatarIcon->setFlag(QGraphicsItem::ItemStacksBehindParent);
    }
    const General *general = nullptr;
    if (m_player) {
        general = m_player->getAvatarGeneral();
		_m_screenNameItem->setVisible(Self != m_player);
		_m_layout->m_screenNameFont.paintText(_m_screenNameItem, _m_layout->m_screenNameArea, Qt::AlignCenter, m_player->screenName());
    }
	QGraphicsPixmapItem *avatarIconTmp = _m_avatarIcon;
    if (general) {
        QString name = m_player->property("avatarIcon").toString();
        if (name.isEmpty()) name = general->objectName();
		
        QPixmap avatarIcon;
		if(m_player->property("avatarIcon2").toString().isEmpty()) avatarIcon = _getAvatarIcon(name);
		else avatarIcon = G_ROOM_SKIN.getGeneralPixmap(name, QSanRoomSkin::GeneralIconSize(_m_layout->m_primaryAvatarSize));
        _paintPixmap(avatarIconTmp, _m_layout->m_avatarArea, avatarIcon, _getAvatarParent());
        // this is just avatar general, perhaps game has not started yet.
        if (m_player->getGeneral()) {
            _paintPixmap(_m_kingdomIcon, _m_layout->m_kingdomIconArea, G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_KINGDOM_ICON, m_player->getKingdom()), _getAvatarParent());
            QString key = inherits("Photo") ? QSanRoomSkin::S_SKIN_KEY_KINGDOM_COLOR_MASK : QSanRoomSkin::S_SKIN_KEY_DASHBOARD_KINGDOM_COLOR_MASK;
            _paintPixmap(_m_kingdomColorMaskIcon, _m_layout->m_kingdomMaskArea, G_ROOM_SKIN.getPixmap(key, m_player->getKingdom()), _getAvatarParent());
            _paintPixmap(_m_handCardBg, _m_layout->m_handCardArea, _getPixmap(QSanRoomSkin::S_SKIN_KEY_HANDCARDNUM, m_player->getKingdom()), _getAvatarParent());
			if(name==general->objectName()) name = general->getBriefName();
			else name = Sanguosha->translate(name);
            _m_layout->m_avatarNameFont.paintText(_m_avatarNameItem, _m_layout->m_avatarNameArea, Qt::AlignLeft | Qt::AlignJustify, name);
        } else
            _paintPixmap(_m_handCardBg, _m_layout->m_handCardArea, _getPixmap(QSanRoomSkin::S_SKIN_KEY_HANDCARDNUM, QSanRoomSkin::S_SKIN_KEY_DEFAULT_SECOND), _getAvatarParent());
    } else {
        _paintPixmap(avatarIconTmp, _m_layout->m_avatarArea, QSanRoomSkin::S_SKIN_KEY_BLANK_GENERAL, _getAvatarParent());
        _clearPixmap(_m_kingdomColorMaskIcon);
        _clearPixmap(_m_kingdomIcon);
        _paintPixmap(_m_handCardBg, _m_layout->m_handCardArea, _getPixmap(QSanRoomSkin::S_SKIN_KEY_HANDCARDNUM, QSanRoomSkin::S_SKIN_KEY_DEFAULT_SECOND), _getAvatarParent());
        _m_avatarArea->setToolTip("");
    }
    _m_avatarIcon->show();
    _adjustComponentZValues();
}

QPixmap PlayerCardContainer::paintByMask(QPixmap source)
{
    QPixmap tmp = G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_GENERAL_CIRCLE_MASK, QString::number(_m_layout->m_circleImageSize), true);
    if (tmp.height() <= 1 && tmp.width() <= 1) return source;
    QPainter p(&tmp);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.drawPixmap(0, 0, _m_layout->m_smallAvatarArea.width(), _m_layout->m_smallAvatarArea.height(), source);
    return tmp;
}

void PlayerCardContainer::updateSmallAvatar()
{
    updateAvatar();

    if (_m_smallAvatarIcon == nullptr) {
        _m_smallAvatarIcon = new GraphicsPixmapHoverItem(this, _getAvatarParent());
        _m_smallAvatarIcon->setTransformationMode(Qt::SmoothTransformation);
        _m_smallAvatarIcon->setFlag(QGraphicsItem::ItemStacksBehindParent);
    }

    QString name;
    if (m_player){
		name = m_player->getGeneral2Name();
		if (name.isEmpty()) name = m_player->property("avatarIcon2").toString();
	}
    if (name.isEmpty()) {
        _clearPixmap(_m_smallAvatarIcon);
        _clearPixmap(_m_circleItem);
        _m_layout->m_smallAvatarNameFont.paintText(_m_smallAvatarNameItem, _m_layout->m_smallAvatarNameArea, Qt::AlignLeft | Qt::AlignJustify, name);
        _m_smallAvatarArea->setToolTip(name);
    } else {
		QGraphicsPixmapItem *smallAvatarIconTmp = _m_smallAvatarIcon;
        _paintPixmap(smallAvatarIconTmp, _m_layout->m_smallAvatarArea, paintByMask(G_ROOM_SKIN.getGeneralPixmap(name, QSanRoomSkin::GeneralIconSize(_m_layout->m_smallAvatarSize))), _getAvatarParent());
        _paintPixmap(_m_circleItem, _m_layout->m_circleArea, QString(QSanRoomSkin::S_SKIN_KEY_GENERAL_CIRCLE_IMAGE).arg(_m_layout->m_circleImageSize), _getAvatarParent());
		if(m_player->getGeneral2()) name = m_player->getGeneral2()->getBriefName();
		else name = Sanguosha->translate(name);
        _m_layout->m_smallAvatarNameFont.paintText(_m_smallAvatarNameItem, _m_layout->m_smallAvatarNameArea, Qt::AlignLeft | Qt::AlignJustify, name);
        _m_smallAvatarIcon->show();
    }
    _adjustComponentZValues();
}

void PlayerCardContainer::updatePhase()
{
	if(m_player&&m_player->isAlive()){
		if(m_player->getPhase() < Player::NotActive){
			QRect phaseArea = _m_layout->m_phaseArea.getTranslatedRect(_getPhaseParent()->boundingRect().toRect());
			_paintPixmap(_m_phaseIcon, phaseArea, _getPixmap(QSanRoomSkin::S_SKIN_KEY_PHASE, QString::number(m_player->getPhase()), true), _getPhaseParent());
			_m_phaseIcon->show();
		}else{
			if (_m_progressBar) _m_progressBar->hide();
			if (_m_phaseIcon) _m_phaseIcon->hide();
		}
	}else
        _clearPixmap(_m_phaseIcon);
}

void PlayerCardContainer::updateHp()
{
    //Q_ASSERT(_m_hpBox && _m_saveMeIcon && m_player);
    _m_hpBox->setHp(m_player->getHp());
    _m_hpBox->setMaxHp(m_player->getMaxHp());
    _m_hpBox->update();
    _m_saveMeIcon->setVisible(m_player->isAlive()&&m_player->hasFlag("Global_Dying"));
    _updateEquips();
    updateHandcardNum();
}

static bool CompareByNumber(const Card *card1, const Card *card2)
{
    return card1->getNumber() < card2->getNumber();
}

void PlayerCardContainer::updatePile(const QString &pile_name)
{
    if (!m_player) return;

    QStringList treasureNames;
	foreach(const Card *e, m_player->getEquips())
		treasureNames << e->objectName();

    QList<int> pile = m_player->getPile(pile_name);
    if (pile.isEmpty()) {
        if (_m_privatePiles.contains(pile_name)) {
			_m_privatePiles[pile_name]->widget()->deleteLater();
			_m_privatePiles[pile_name]->setWidget(nullptr);
            delete _m_privatePiles[pile_name];
            _m_privatePiles.remove(pile_name);
        }
    } else {
        // retrieve menu and create a new pile if necessary
        QPushButton *button;
        if (_m_privatePiles.contains(pile_name)) {
            button = (QPushButton *)_m_privatePiles[pile_name]->widget();
			if(button->menu()) button->menu()->deleteLater();
        } else {
            button = new QPushButton;
            button->setObjectName(pile_name);
            if (treasureNames.contains(pile_name))
                button->setProperty("treasure", "true");
            else {
                button->setProperty("private_pile", "true");
                button->setStyleSheet("background-color:black");
            }
            _m_privatePiles[pile_name] = new QGraphicsProxyWidget(_getPileParent());
            _m_privatePiles[pile_name]->setObjectName(pile_name);
            _m_privatePiles[pile_name]->setWidget(button);
        }

        button->setText(QString("%1(%2)").arg(Sanguosha->translate(pile_name)).arg(pile.length()));

        //Sort the cards in pile by number can let players know what is in this pile more clear.
        //If someone has "buqu", we can got which card he need or which he hate easier.
        QList<const Card *> cards;
        foreach (int id, pile){
            const Card *card = Sanguosha->getEngineCard(id);
			if(card) cards << card;
		}
        if(cards.isEmpty()){
			button->setMenu(nullptr);
		}else{
			QMenu* menu = new QMenu(button);
			std::sort(cards.begin(), cards.end(), CompareByNumber);
			foreach(const Card *card, cards)
				menu->addAction(G_ROOM_SKIN.getCardSuitPixmap(card->getSuit()), card->getFullName());
			if (treasureNames.contains(pile_name)) menu->setProperty("treasure", "true");
			else menu->setProperty("private_pile", "true");
			button->setMenu(menu);
		}
    }

    QList<QGraphicsProxyWidget *> widgets, widgets_p;
    foreach (QGraphicsProxyWidget *widget, _m_privatePiles.values()) {
        if (treasureNames.contains(widget->objectName())) widgets << widget;
        else widgets_p << widget;
    }
    widgets << widgets_p;
    for (int i = 0; i < widgets.length(); i++) {
        //widgets[i]->resize(_m_layout->m_privatePileButtonSize);
        widgets[i]->setPos(_m_layout->m_privatePileStartPos + i * _m_layout->m_privatePileStep);
    }
}

void PlayerCardContainer::updateMark(const QString &mark_name, int mark_num)
{
    /*ClientPlayer *player = (ClientPlayer *)sender();
    if (!player) player = m_player;
    if (!player) return;*/

    if (mark_num==0) {
        if (_m_privatePiles.contains(mark_name)) {
			_m_privatePiles[mark_name]->widget()->deleteLater();
			_m_privatePiles[mark_name]->setWidget(nullptr);
            delete _m_privatePiles[mark_name];
            _m_privatePiles.remove(mark_name);
        }
    } else {
        QPushButton *button = new QPushButton;
		button->setObjectName(mark_name);
		button->setProperty("private_pile", "true");
		//button->setStyleSheet("background-color:transparent");//把标记背景变透明
        if (_m_privatePiles.contains(mark_name)){
			_m_privatePiles[mark_name]->widget()->deleteLater();
			_m_privatePiles[mark_name]->setWidget(nullptr);
		}else{
			_m_privatePiles[mark_name] = new QGraphicsProxyWidget(_getPileParent());
			_m_privatePiles[mark_name]->setObjectName(mark_name);
		}

        QStringList new_mark;
		QString text, dest, arg, arg2, arg3;
        foreach (QString name, mark_name.mid(1).split("+")) {
            if (name.startsWith("#")) {
				dest = name.mid(1);
				if (name.endsWith("Clear") || name.endsWith("-Keep"))
					dest = dest.split("-").first();
				else if (name.endsWith("_lun"))
					dest.remove("_lun");
            }else if (name.startsWith("arg:")) {
                arg = name.mid(QString("arg:").length());
				if (name.endsWith("Clear") || name.endsWith("-Keep"))
					arg = arg.split("-").first();
				else if (name.endsWith("_lun"))
					arg.remove("_lun");
            }else if (name.startsWith("arg2:")) {
                arg2 = name.mid(QString("arg2:").length());
				if (name.endsWith("Clear") || name.endsWith("-Keep"))
					arg2 = arg2.split("-").first();
				else if (name.endsWith("_lun"))
					arg2.remove("_lun");
            }else if (name.startsWith("arg3:")) {
                arg3 = name.mid(QString("arg3:").length());
				if (name.endsWith("Clear") || name.endsWith("-Keep"))
					arg3 = arg3.split("-").first();
				else if (name.endsWith("_lun"))
					arg3.remove("_lun");
            } else if (name.contains("sys_")) {
                continue; 
            }else if (name.endsWith("Clear") || name.endsWith("-Keep")) {
                name = name.split("-").first();
                new_mark.append(name);
                text.append(Sanguosha->translate(name));
            } else if (name.endsWith("_lun")) {
                name.remove("_lun");
                new_mark.append(name);
                text.append(Sanguosha->translate(name));
            } else {
                new_mark.append(name);
                text.append(Sanguosha->translate(name));
            }
        }

        if (mark_num != 1)
            text.append(QString("[%1]").arg(mark_num));
        button->setText(text);

        if (mark_name.endsWith("+#tuoyu"))
            button->setToolTip(Sanguosha->translate(":tuoyuarea"));
        else if(new_mark.length()>0){
			text = Sanguosha->translate(":&" + new_mark.join("+"));
			if (text.contains(":&")) {
				if (!dest.isEmpty()) {
					text = Sanguosha->translate(":&commonmarktooltip");
					text.replace("%dest", ClientInstance->getPlayerName(dest));
					button->setToolTip(text);
				}
			} else {
				if (!dest.isEmpty()) text.replace("%dest", ClientInstance->getPlayerName(dest));
				if (!arg3.isEmpty()) text.replace("%arg3", ClientInstance->getPlayerName(arg3));
				if (!arg2.isEmpty()) text.replace("%arg2", ClientInstance->getPlayerName(arg2));
				if (!arg.isEmpty()) text.replace("%arg", ClientInstance->getPlayerName(arg));
				if (text.contains("%src+1")) text.replace("%src+1", QString::number(mark_num+1));
				else text.replace("%src", QString::number(mark_num));
				button->setToolTip(text);
			}
		}
		_m_privatePiles[mark_name]->setWidget(button);
    }

    QList<QGraphicsProxyWidget *> widgets = _m_privatePiles.values();
    for (int i = 0; i < widgets.length(); i++) {
        //widgets[i]->resize(_m_layout->m_privatePileButtonSize);
        widgets[i]->setPos(_m_layout->m_privatePileStartPos + i * _m_layout->m_privatePileStep);
    }
}

void PlayerCardContainer::updateDrankState()
{
    if (m_player->getMark("drank") > 0)
        _m_avatarArea->setBrush(G_PHOTO_LAYOUT.m_drankMaskColor);
    else
        _m_avatarArea->setBrush(Qt::NoBrush);
}

void PlayerCardContainer::updateDuanchang()
{
    return;
}

void PlayerCardContainer::updateHandcardNum()
{
    QString num = "0";
    QRect area = _m_layout->m_handCardArea;
    const int extraW = 50;
    QRect wideArea(area.x() - extraW, area.y(), area.width() + extraW * 2, area.height());
    QRect innerRect(0, 0, wideArea.width(), wideArea.height());

    QImage image(wideArea.width(), wideArea.height(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter imagePainter(&image);

    if (m_player) {
        int handcardNum = m_player->getHandcardNum();
        int hp = m_player->getHp();
        int maxCards = m_player->getTag("UI_Hand_Max").isValid() ? m_player->getTag("UI_Hand_Max").toInt() : qMax(hp, 0);

        int W = wideArea.width(), H = wideArea.height();
        int midW = W / 10;
        int seg  = (W - midW) / 2;
        QRect leftZone (0,          0, seg,  H);
        QRect midZone  (seg,        0, midW, H);
        QRect rightZone(seg + midW, 0, seg,  H);

        IQSanComponentSkin::QSanShadowTextFont limitFont = _m_layout->m_handCardFont;
        if (maxCards != hp)
            limitFont.m_color = (maxCards > hp) ? QColor(0, 255, 0) : QColor(255, 0, 0);

        _m_layout->m_handCardFont.paintText(&imagePainter, leftZone,  Qt::AlignRight  | Qt::AlignVCenter, QString::number(handcardNum));
        _m_layout->m_handCardFont.paintText(&imagePainter, midZone,   Qt::AlignCenter, "/");
        limitFont.paintText               (&imagePainter, rightZone, Qt::AlignLeft   | Qt::AlignVCenter, QString::number(maxCards));
    } else {
        _m_layout->m_handCardFont.paintText(&imagePainter, innerRect, Qt::AlignCenter, num);
    }

    _m_handCardNumText->setPixmap(QPixmap::fromImage(image));

    if (_m_handCardNumText->parentItem() != this) {
        _m_handCardNumText->setParentItem(this);
        _m_handCardNumText->setZValue(100);
    }
    _m_handCardNumText->setPos(mapFromItem(_getAvatarParent(), QPointF(wideArea.x(), wideArea.y())));
    _m_handCardNumText->setVisible(true);

    if (!m_player) return;
    int limitBase = m_player->getHp();
    int maxCards = m_player->getTag("UI_Hand_Max").isValid() ? m_player->getTag("UI_Hand_Max").toInt() : qMax(limitBase, 0);
    if (maxCards != limitBase) {
        QStringList tooltipInfo;
        tooltipInfo << tr("手牌上限: %1").arg(maxCards);
        foreach (const MaxCardsSkill *mc_skill, Sanguosha->getMaxCardsSkills()) {
            if (mc_skill && mc_skill->objectName() == "gamerulemaxcards") {
                foreach (const QString &mark_name, m_player->getMarkNames()) {
                    if (mark_name.startsWith("ExtraBfMaxCards_")) {
                        QString data = mark_name.mid(16); 
                        if (data.endsWith("-Clear")) {
                            data.chop(6);
                        }
                        int val = m_player->getMark(mark_name);
                        if (val != 0) {
                            QStringList parts = data.split("_");
                            QString real_reason = parts.at(0);
                            QString source_objname = parts.length() > 1 ? parts.at(1) : "";
                            
                            QString sign = val > 0 ? "+" : "";
                            QString translatedReason = Sanguosha->translate(real_reason);
                            
                            QString source = tr("全局規則/系統");
                            if (!source_objname.isEmpty()) {
                                ClientPlayer *src_player = ClientInstance->getPlayer(source_objname);
                                if (src_player) {
                                    source = src_player->screenName();
                                }
                            }

                            tooltipInfo << QString("%1: %2%3 (來源: %4)")
                                           .arg(translatedReason).arg(sign).arg(val).arg(source);
                        }
                    }
                }

                int base_extra = m_player->getMark("ExtraBfMaxCards") + m_player->getMark("ExtraBfMaxCards-Clear");
                if (base_extra != 0) {
                    QString sign = base_extra > 0 ? "+" : "";
                    tooltipInfo << QString("其他額外效果: %1%2").arg(sign).arg(base_extra);
                }
                break;
            }
        }

        // 從 Server 推播的 Tag 讀取各技能對手牌上限的影響（零 Lua 呼叫）
        QStringList mc_tag = m_player->getTag("UI_MC_Skills").toStringList();
        foreach (const QString &entry, mc_tag) {
            if (!entry.contains("^")) continue;
            QStringList parts = entry.split("^");
            QString skillName = Sanguosha->translate(parts[0]);
            QString valueStr  = parts.size() > 1 ? parts[1] : "";
            QString srcName   = parts.size() > 2 ? parts[2] : QString();
            QString sourceDisplay = tr("自身/系統");

            if (!srcName.isEmpty()) {
                ClientPlayer *src_player = ClientInstance->getPlayer(srcName);
                sourceDisplay = src_player ? src_player->screenName() : srcName;
            }

            if (valueStr.startsWith("F")) {
                int fixed = valueStr.mid(1).toInt();
                tooltipInfo << QString("%1: 固定為 %2 (來源: %3)")
                               .arg(skillName).arg(fixed).arg(sourceDisplay);
            } else {
                int extra = valueStr.toInt();
                QString sign = extra > 0 ? "+" : "";
                tooltipInfo << QString("%1: %2%3 (來源: %4)")
                               .arg(skillName).arg(sign).arg(extra).arg(sourceDisplay);
            }
        }

        _m_handCardNumText->setToolTip(tooltipInfo.join("\n"));
        
    } else {
        _m_handCardNumText->setToolTip(QString());
    }
}

void PlayerCardContainer::updateMarks()
{
    if (!_m_markItem) return;
    QRect parentRect = _getMarkParent()->boundingRect().toRect();
    QSize markSize = _m_markItem->boundingRect().size().toSize();
    QRect newRect = _m_layout->m_markTextArea.getTranslatedRect(parentRect, markSize);
    if (_m_layout == &G_PHOTO_LAYOUT)
        _m_markItem->setPos(newRect.topLeft());
    else
        _m_markItem->setPos(newRect.left(), newRect.top() + newRect.height() / 2);
    _updateEquips();
}

void PlayerCardContainer::_updateEquips()
{
    if(!m_player) return;
    
    QMap<int, const Card *> occupied_slots;
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (_m_equipCards[i]) {
            const Card *card = _m_equipCards[i]->getCard();
            if (!card) continue;
            QList<int> real_slots = m_player->getEquipRealSlots(card->getEffectiveId());
            foreach (int slot, real_slots) {
                if (slot != i) occupied_slots[slot] = card;
            }
        }
    }

    QMap<int, Card *> simulated_equips;
    QMap<int, QString> simulated_equip_skills;
    QMap<int, QList<int> > simulated_equip_real_slots;

    auto try_add_simulated_equip = [&](Card *ec, const QString &skill_name) {
        const EquipCard *equip = qobject_cast<const EquipCard *>(ec);
        if (!equip) {
            ec->deleteLater();
            return;
        }

        QList<int> real_slots = equip->getOccupyLocations();
        int display_slot = equip->location();
        if (!real_slots.isEmpty()) display_slot = real_slots.first();

        if (simulated_equips.contains(display_slot) || _m_equipCards[display_slot]) {
            ec->deleteLater();
            return;
        }

        foreach (int slot, real_slots) {
            if (_m_equipCards[slot] || simulated_equips.contains(slot)) {
                ec->deleteLater();
                return;
            }
            if (occupied_slots.contains(slot)) {
                ec->deleteLater();
                return;
            }
        }

        simulated_equips[display_slot] = ec;
        simulated_equip_skills[display_slot] = skill_name;
        simulated_equip_real_slots[display_slot] = real_slots;

        foreach (int slot, real_slots) {
            if (slot != display_slot) occupied_slots[slot] = ec;
        }
    };

    QStringList property_equips = m_player->property("View_As_Equips_List").toString().split("+", QString::SkipEmptyParts);
    foreach (QString eq_name, property_equips) {
        Card *ec = Sanguosha->cloneCard(eq_name);
        if (ec) try_add_simulated_equip(ec, QString());
    }
    QStringList vae_tag = m_player->getTag("UI_VAE_Skills").toStringList();
    foreach (QString entry, vae_tag) {
        QStringList parts = entry.split("^"); // 記得用 ^ 切割
        if (parts.length() >= 2) {
            Card *ec = Sanguosha->cloneCard(parts[0]);
            if (ec) try_add_simulated_equip(ec, parts[1]);
        }
    }

    // 從 Server 推播的 Tag 讀取距離修正（零 Lua 呼叫）
    int off_dist = m_player->getTag("UI_Off_Dist").toInt();
    QStringList off_skills = m_player->getTag("UI_Off_Skills").toStringList();
    int def_dist = m_player->getTag("UI_Def_Dist").toInt();
    QStringList def_skills = m_player->getTag("UI_Def_Skills").toStringList();

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        bool is_def = (i == 2);
        bool is_off = (i == 3);
        int skill_dist = is_def ? def_dist : (is_off ? off_dist : 0);
        QStringList skills = is_def ? def_skills : off_skills;

        QPixmap pixmap;
        QString tooltip;
        float opacity = 1.0;
        bool has_card_image = false;

        if (_m_equipCards[i]) {
            const Card *card = _m_equipCards[i]->getCard();
            pixmap = _getEquipPixmap(card);
            tooltip = card ? card->getDescription() : QString();
            opacity = 1.0;
            has_card_image = true;
        } 
        else if (simulated_equips.contains(i)) {
            const Card *card = simulated_equips[i];
            QString skill_name = simulated_equip_skills[i];
            pixmap = _getEquipPixmap(card);

            QString skillText;
            if (skill_name.isEmpty()) skillText = Sanguosha->translate("skill_transform");
            else skillText = Sanguosha->translate("skill_transform_from").arg(Sanguosha->translate(skill_name));

            tooltip = QString("<b>【%1】</b> (%2)<br/>%3")
                          .arg(Sanguosha->translate(card->objectName()))
                          .arg(skillText)
                          .arg(card->getDescription());
            opacity = 0.8;
            has_card_image = true;
        } 
        else if (occupied_slots.contains(i)) {
            const Card *occupying_card = occupied_slots[i];
            pixmap = _getEquipPixmap(occupying_card);
            tooltip = Sanguosha->translate("equip_slot_occupied").arg(Sanguosha->translate(occupying_card->objectName()));
            opacity = 0.35;
            has_card_image = true;
        }

        if (has_card_image) {
            if (skill_dist != 0) {
                QPainter painter(&pixmap);
                QString skill_val_str = (skill_dist > 0 ? "+" : "") + QString::number(skill_dist);
                QRect overlayArea(0, 0, pixmap.width() - 5, pixmap.height());
                
                if (skill_dist > 0) {
                    _m_layout->m_equipPointFontRed.paintText(&painter, overlayArea, Qt::AlignRight | Qt::AlignVCenter, skill_val_str);
                } else {
                    _m_layout->m_equipPointFontBlack.paintText(&painter, overlayArea, Qt::AlignRight | Qt::AlignVCenter, skill_val_str);
                }

                QStringList translated_skills;
                foreach (QString sk, skills) translated_skills << Sanguosha->translate(sk);
                
                if (!tooltip.isEmpty()) tooltip += "<br/><hr/>";
                tooltip += QString("<b>附加技能修正：</b>%1 (%2)").arg(translated_skills.join("、")).arg(skill_val_str);
            }

            _m_equipLabel[i]->setPixmap(pixmap);
            _m_equipRegions[i]->setPos(_m_layout->m_equipAreas[i].topLeft());
            _m_equipRegions[i]->setToolTip(tooltip);
            _m_equipRegions[i]->setOpacity(opacity);
            _m_equipRegions[i]->show();
            continue;
        }

        if (m_player->hasEquipArea(i)) {
            if (skill_dist != 0) {
                QPixmap empty_pixmap(_m_layout->m_equipAreas[i].size());
                empty_pixmap.fill(Qt::transparent);
                QPainter painter(&empty_pixmap);
                QString val_str = (skill_dist > 0 ? "+" : "") + QString::number(skill_dist);

                if (skill_dist > 0) {
                    _m_layout->m_equipPointFontRed.paintText(&painter, _m_layout->m_horsePointArea, Qt::AlignLeft | Qt::AlignVCenter, val_str);
                } else {
                    _m_layout->m_equipPointFontBlack.paintText(&painter, _m_layout->m_horsePointArea, Qt::AlignLeft | Qt::AlignVCenter, val_str);
                }
                
                _m_equipLabel[i]->setPixmap(empty_pixmap);
                _m_equipRegions[i]->setPos(_m_layout->m_equipAreas[i].topLeft());
                
                QStringList translated_skills;
                foreach (QString sk, skills) translated_skills << Sanguosha->translate(sk);
                
                QString empty_tooltip = QString("<b>【%1】</b><br/>%2")
                                            .arg(translated_skills.join("、"))
                                            .arg(is_def ? tr("防禦距離 %1").arg(val_str) : tr("進攻距離 %1").arg(val_str));
                
                _m_equipRegions[i]->setToolTip(empty_tooltip);
                _m_equipRegions[i]->setOpacity(1.0);
                _m_equipRegions[i]->show();
            } else {
                _m_equipRegions[i]->setOpacity(0);
            }
        } else {
            _m_equipLabel[i]->setPixmap(_getEquipPixmap(nullptr, QString("equip%1lose").arg(i)));
            _m_equipRegions[i]->setPos(_m_layout->m_equipAreas[i].topLeft());
            _m_equipRegions[i]->setToolTip("");
            _m_equipRegions[i]->setOpacity(1.0);
            _m_equipRegions[i]->show();
        }
    }

    foreach (Card *ec, simulated_equips.values()) ec->deleteLater();
}

void PlayerCardContainer::refresh(bool killed)
{
	if(m_player){
        if (_m_chainIcon) _m_chainIcon->setVisible(m_player->isChained());
        if (_m_faceTurnedIcon) _m_faceTurnedIcon->setVisible(!m_player->faceUp());
        if (_m_actionIcon) _m_actionIcon->setVisible(m_player->hasFlag("actioned"));
        if (_m_saveMeIcon) _m_saveMeIcon->setVisible(m_player->isAlive()&&m_player->hasFlag("Global_Dying"));
        if (_m_deathIcon && !(ServerInfo.GameMode == "04_1v3" && m_player->getGeneralName() != "shenlvbu2" && m_player->getGeneralName() != "shenlvbu3"))
            _m_deathIcon->setVisible(m_player->isDead());
	}else{
        _m_chainIcon->setVisible(false);
        _m_faceTurnedIcon->setVisible(false);
        _m_actionIcon->setVisible(false);
        _m_saveMeIcon->setVisible(false);
	}
    _updateEquips();
    updateHandcardNum();
    _adjustComponentZValues(killed);
}

void PlayerCardContainer::repaintAll(bool all)
{
    _m_avatarArea->setRect(_m_layout->m_avatarArea);
    _m_smallAvatarArea->setRect(_m_layout->m_smallAvatarArea);

    //updateAvatar();
    updateSmallAvatar();
    updatePhase();
    updateMarks();
    _updateProgressBar();
    _updateDeathIcon();
    _updateEquips();
    updateDelayedTricks();

    if (_m_huashenAnimation != nullptr)
        startHuaShen(_m_huashenGeneralName, _m_huashenSkillName);

    _paintPixmap(_m_faceTurnedIcon, _m_layout->m_avatarArea, QSanRoomSkin::S_SKIN_KEY_FACETURNEDMASK,
        _getAvatarParent());
    _paintPixmap(_m_chainIcon, _m_layout->m_chainedIconRegion, QSanRoomSkin::S_SKIN_KEY_CHAIN,
        _getAvatarParent());
    _paintPixmap(_m_saveMeIcon, _m_layout->m_saveMeIconRegion, QSanRoomSkin::S_SKIN_KEY_SAVE_ME_ICON,
        _getAvatarParent());
    _paintPixmap(_m_actionIcon, _m_layout->m_actionedIconRegion, QSanRoomSkin::S_SKIN_KEY_ACTIONED_ICON,
        _getAvatarParent());

    if (m_changePrimaryHeroSKinBtn) {
        m_changePrimaryHeroSKinBtn->setPos(_m_layout->m_changePrimaryHeroSkinBtnPos);
    }
    if (m_changeSecondaryHeroSkinBtn) {
        m_changeSecondaryHeroSkinBtn->setPos(_m_layout->m_changeSecondaryHeroSkinBtnPos);
    }

    if (_m_roleComboBox != nullptr)
        _m_roleComboBox->setPos(_m_layout->m_roleComboBoxPos);

    _m_hpBox->setIconSize(_m_layout->m_magatamaSize);
    _m_hpBox->setOrientation(_m_layout->m_magatamasHorizontal ? Qt::Horizontal : Qt::Vertical);
    _m_hpBox->setBackgroundVisible(_m_layout->m_magatamasBgVisible);
    _m_hpBox->setAnchorEnable(true);
    _m_hpBox->setAnchor(_m_layout->m_magatamasAnchor, _m_layout->m_magatamasAlign);
    _m_hpBox->setImageArea(_m_layout->m_magatamaImageArea);
    _m_hpBox->update();
	if(all){
		updateHp();
		for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
			if(_m_equipCards[i]){
				_m_equipCards[i]->setHomeOpacity(0.0);
				delete _m_equipCards[i];
			}
			_m_equipCards[i] = nullptr;
			_m_equipLabel[i]->setPixmap(QPixmap(_m_layout->m_equipAreas[i].size()));
			_m_equipRegions[i]->setOpacity(0);
			_m_equipRegions[i]->hide();
		}
		QList<CardItem *> card_items = _createCards(m_player->getEquipsId());
		addEquips(card_items);
		for (int i = 0; i < _m_judgeIcons.length(); i++){
			_m_judgeIcons[i]->setOpacity(0);
			delete _m_judgeIcons[i];
		}
		_m_judgeIcons.clear();
		card_items = _createCards(m_player->getJudgingAreaID());
		addDelayedTricks(card_items);
		foreach (QGraphicsProxyWidget *widget, _m_privatePiles.values())
			delete widget;
		_m_privatePiles.clear();
		foreach (QString pn, m_player->getPileNames())
			updatePile(pn);
		_allZAdjusted = false;
	}

    refresh();
}

void PlayerCardContainer::_createRoleComboBox()
{
    _m_roleComboBox = new RoleComboBox(_getRoleComboBoxParent());
}

void PlayerCardContainer::setPlayer(ClientPlayer *player)
{
    m_player = player;
    if (player) {
        connect(player, SIGNAL(general_changed()), this, SLOT(updateAvatar()));
        connect(player, SIGNAL(general2_changed()), this, SLOT(updateSmallAvatar()));
        connect(player, SIGNAL(state_changed()), this, SLOT(refresh()));
        connect(player, SIGNAL(phase_changed()), this, SLOT(updatePhase()));
        connect(player, SIGNAL(drank_changed()), this, SLOT(updateDrankState()));
        connect(player, SIGNAL(duanchang_invoked()), this, SLOT(updateDuanchang()));
        connect(player, SIGNAL(pile_changed(QString)), this, SLOT(updatePile(QString)));
        connect(player, SIGNAL(Mark_changed(QString, int)), this, SLOT(updateMark(QString, int)));
        connect(player, SIGNAL(role_changed(QString)), _m_roleComboBox, SLOT(fix(QString)));
        connect(player, SIGNAL(hp_changed()), this, SLOT(updateHp()));

        QTextDocument *textDoc = m_player->getMarkDoc();
        Q_ASSERT(_m_markItem);
        _m_markItem->setDocument(textDoc);
        connect(textDoc, SIGNAL(contentsChanged()), this, SLOT(updateMarks()));
    }
    updateAvatar();
    refresh();
}

QList<CardItem *> PlayerCardContainer::removeDelayedTricks(const QList<int> &cardIds)
{
    QList<CardItem *> result;
    foreach (int card_id, cardIds) {
        CardItem *item = CardItem::FindItem(_m_judgeCards, card_id);
        if(!item) continue;
        int index = _m_judgeCards.indexOf(item);
        QRect start = _m_layout->m_delayedTrickFirstRegion;
        QPoint step = _m_layout->m_delayedTrickStep;
        start.translate(step * index);
        item->setOpacity(0);
        item->setPos(start.center());
        _m_judgeCards.removeAt(index);
        delete _m_judgeIcons.takeAt(index);
        result.append(item);
    }
    updateDelayedTricks();
    return result;
}

void PlayerCardContainer::updateDelayedTricks()
{
	for (int i = 0; i < _m_judgeIcons.length(); i++) {
        QRect start = _m_layout->m_delayedTrickFirstRegion;
        QPoint step = _m_layout->m_delayedTrickStep;
        start.translate(step * i);
        _m_judgeIcons[i]->setPos(start.topLeft());
    }
    if(!m_player) return;
	if(m_player->hasJudgeArea()){
		if(_m_judgeCards.isEmpty()){
			for (int i = 0; i < _m_judgeIcons.length(); i++){
				_m_judgeIcons[i]->setOpacity(0);
				delete _m_judgeIcons[i];
			}
			_m_judgeIcons.clear();
		}
	}else{
		for (int i = 0; i < _m_judgeCards.length(); i++)
			delete _m_judgeCards[i];
		_m_judgeCards.clear();
		for (int i = 0; i < _m_judgeIcons.length(); i++){
			_m_judgeIcons[i]->setOpacity(0);
			delete _m_judgeIcons[i];
		}
		_m_judgeIcons.clear();
		QRect start = _m_layout->m_delayedTrickFirstRegion;
		QGraphicsPixmapItem *item = new QGraphicsPixmapItem(_getDelayedTrickParent());
		_paintPixmap(item, start, G_ROOM_SKIN.getCardJudgeIconPixmap(QString("Judgelose")));
		item->setOpacity(1);
		_m_judgeIcons.append(item);
	}
}

void PlayerCardContainer::addDelayedTricks(QList<CardItem *> &tricks)
{
    foreach (CardItem *trick, tricks) {
        QGraphicsPixmapItem *item = new QGraphicsPixmapItem(_getDelayedTrickParent());
        QRect start = _m_layout->m_delayedTrickFirstRegion;
        QPoint step = _m_layout->m_delayedTrickStep;
        start.translate(step * _m_judgeCards.size());
        const Card *tc = trick->getCard();
        _paintPixmap(item, start, G_ROOM_SKIN.getCardJudgeIconPixmap(tc->objectName()));
        trick->setHomeOpacity(0);
        trick->setHomePos(start.center());
        QString toolTip = Sanguosha->getEngineCard(tc->getId())->getLogName();
		toolTip.append("<br/>").append(tc->getDescription());
		if(tc->isKindOf("Xumou")) toolTip = "";
        item->setToolTip(toolTip);
        _m_judgeCards.append(trick);
        _m_judgeIcons.append(item);
    }
}

QPixmap PlayerCardContainer::_getEquipPixmap(const Card *equip, const QString &arg)
{
    QPixmap equipIcon(_m_layout->m_equipAreas[0].size());
    equipIcon.fill(Qt::transparent);
    QPainter painter(&equipIcon);
	if(equip){
		const Card *realCard = Sanguosha->getEngineCard(equip->getEffectiveId());
        if (realCard == nullptr) realCard = equip;
		if (realCard->objectName().contains("_zhizhe_")) realCard = equip;
		// icon / background
		QRect imageArea = _m_layout->m_equipImageArea;
		QRect suitArea = _m_layout->m_equipSuitArea;
		QRect pointArea = _m_layout->m_equipPointArea;
		if (equip->isKindOf("Horse")){
			imageArea = _m_layout->m_horseImageArea;
			suitArea = _m_layout->m_horseSuitArea;
			pointArea = _m_layout->m_horsePointArea;
		}
		painter.drawPixmap(imageArea, _getPixmap(QSanRoomSkin::S_SKIN_KEY_EQUIP_ICON, equip->objectName()));
		// equip suit
		painter.drawPixmap(suitArea, G_ROOM_SKIN.getCardSuitPixmap(equip->getSuit()));
		// equip point
		if (realCard->isRed()) {
			_m_layout->m_equipPointFontRed.paintText(&painter,
				pointArea, Qt::AlignLeft | Qt::AlignCenter,
				equip->getNumberString());
		} else {
			_m_layout->m_equipPointFontBlack.paintText(&painter,
				pointArea, Qt::AlignLeft | Qt::AlignCenter,
				equip->getNumberString());
		}
	}else if(!arg.isEmpty()){
		// icon / background
		QRect imageArea = _m_layout->m_equipImageArea;
		QRect suitArea = _m_layout->m_equipSuitArea;
		QRect pointArea = _m_layout->m_equipPointArea;
		if (arg.contains("2")||arg.contains("3")){
			imageArea = _m_layout->m_horseImageArea;
			suitArea = _m_layout->m_horseSuitArea;
			pointArea = _m_layout->m_horsePointArea;
		}
		painter.drawPixmap(imageArea, _getPixmap(QSanRoomSkin::S_SKIN_KEY_EQUIP_ICON, arg));
		// equip suit
		painter.drawPixmap(suitArea, G_ROOM_SKIN.getCardSuitPixmap(Card::NoSuit));
		// equip point
		_m_layout->m_equipPointFontBlack.paintText(&painter,
			pointArea, Qt::AlignLeft | Qt::AlignVCenter, "");
	}
    return equipIcon;
}

void PlayerCardContainer::setFloatingArea(QRect rect)
{
    _m_floatingAreaRect = rect;
    QPixmap dummy(rect.size());
    dummy.fill(Qt::transparent);
    _m_floatingArea->setPixmap(dummy);
    _m_floatingArea->setPos(rect.topLeft());
    if (_getMarkParent() == _m_floatingArea) updateMarks();
    if (_getPhaseParent() == _m_floatingArea) updatePhase();
    if (_getProgressBarParent() == _m_floatingArea) _updateProgressBar();
}

void PlayerCardContainer::addEquips(QList<CardItem *> &equips)
{
    foreach (CardItem *equip, equips) {
        const Card *card = equip->getCard();
        const EquipCard *equipCard = qobject_cast<const EquipCard *>(card->getRealCard());

        int index = equipCard ? equipCard->location() : 0;
        if (m_player && card) {
            QList<int> real_slots = m_player->getEquipRealSlots(card->getEffectiveId());
            if (!real_slots.isEmpty()) index = real_slots.first();
        }

        if (m_player && equipCard && _m_equipCards[index] != nullptr) {
            const Card *existing = _m_equipCards[index]->getCard();
            const EquipCard *existingEquip = existing ? qobject_cast<const EquipCard *>(existing->getRealCard()) : nullptr;
            if (existingEquip && existingEquip->location() == equipCard->location()) {
                int alternate_index = -1;
                static const int search_order[] = { 1, 4, 2, 3, 0 };
                for (int j = 0; j < 5; j++) {
                    int candidate = search_order[j];
                    if (candidate == index) continue;
                    if (!m_player->hasEquipArea(candidate) && _m_equipCards[candidate] == nullptr) {
                        alternate_index = candidate;
                        break;
                    }
                }
                if (alternate_index >= 0) index = alternate_index;
            }
        }

        connect(equip, SIGNAL(mark_changed()), this, SLOT(_onEquipSelectChanged()));

        QPointF equipAreaCenter = _m_layout->m_equipAreas[index].center();
        QPointF homePos = mapFromItem(_getEquipParent(), equipAreaCenter);
        equip->setHomePos(homePos);
        equip->setHomeOpacity(0.0);
        _m_equipCards[index] = equip;
        QString description = card->getDescription();
        _m_equipRegions[index]->setToolTip(description);
		
        QPixmap pixmap = _getEquipPixmap(card);
        _m_equipLabel[index]->setPixmap(pixmap);
        _m_equipLabel[index]->setFixedSize(pixmap.size());
		
        _mutexEquipAnim.lock();
        _m_equipRegions[index]->setPos(_m_layout->m_equipAreas[index].topLeft()+QPoint(_m_layout->m_equipAreas[index].width()/2,0));
        _m_equipRegions[index]->setOpacity(0);
        _m_equipRegions[index]->show();
        _m_equipAnim[index]->stop();
        _m_equipAnim[index]->clear();
		
        QPropertyAnimation *anim = new QPropertyAnimation(_m_equipRegions[index], "pos");
        anim->setEndValue(_m_layout->m_equipAreas[index].topLeft());
        anim->setDuration(200);
        _m_equipAnim[index]->addAnimation(anim);
        connect(anim, SIGNAL(finished()), anim, SLOT(deleteLater()));
		
        anim = new QPropertyAnimation(_m_equipRegions[index], "opacity");
        anim->setEndValue(255);
        anim->setDuration(200);
        _m_equipAnim[index]->addAnimation(anim);
        connect(anim, SIGNAL(finished()), anim, SLOT(deleteLater()));
		
        _m_equipAnim[index]->start();
        _mutexEquipAnim.unlock();/*
		
        const Skill *skill = Sanguosha->getSkill(card->objectName());
        if (skill) emit add_equip_skill(skill, true);*/
    }
    _updateEquips();
}

QList<CardItem *> PlayerCardContainer::removeEquips(const QList<int> &cardIds)
{
    QList<CardItem *> result;/*
    foreach (int card_id, cardIds) {
		CardItem *equip = nullptr;
		int index = -1;
		for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
			if(_m_equipCards[i]&&_m_equipCards[i]->getId()==card_id){
				equip = _m_equipCards[i];
				index = i;
				break;
			}
		}
		if(index < 0) continue;
        equip->setHomeOpacity(0.0);
        equip->setPos(_m_layout->m_equipAreas[index].center());
        result.append(equip);
        _m_equipCards[index] = nullptr;
        _mutexEquipAnim.lock();
        _m_equipAnim[index]->stop();
        _m_equipAnim[index]->clear();
        QPropertyAnimation *anim = new QPropertyAnimation(_m_equipRegions[index], "pos");
        anim->setEndValue(_m_layout->m_equipAreas[index].topLeft()+QPoint(_m_layout->m_equipAreas[index].width()/2,0));
        anim->setDuration(200);
        _m_equipAnim[index]->addAnimation(anim);
        connect(anim, SIGNAL(finished()), anim, SLOT(deleteLater()));
        anim = new QPropertyAnimation(_m_equipRegions[index], "opacity");
        anim->setEndValue(0);
        anim->setDuration(200);
        _m_equipAnim[index]->addAnimation(anim);
        connect(anim, SIGNAL(finished()), anim, SLOT(deleteLater()));
        _m_equipAnim[index]->start();
        _mutexEquipAnim.unlock();
        const Skill *skill = Sanguosha->getSkill(equip->objectName());
        if (skill != nullptr) emit remove_equip_skill(skill->objectName());
    }*/
	for (int index = 0; index < S_EQUIP_AREA_LENGTH; index++) {
		if (_m_equipCards[index] && _m_equipCards[index]->getCard()
			&& cardIds.contains(_m_equipCards[index]->getCard()->getEffectiveId())) {
			_m_equipCards[index]->setPos(_m_layout->m_equipAreas[index].center());
			_m_equipCards[index]->setHomeOpacity(0.0);
			result.append(_m_equipCards[index]);
			_mutexEquipAnim.lock();
			_m_equipAnim[index]->stop();
			_m_equipAnim[index]->clear();
			
			QPropertyAnimation *anim = new QPropertyAnimation(_m_equipRegions[index], "pos");
			if(m_player->hasEquipArea(index))
				anim->setEndValue(_m_layout->m_equipAreas[index].topLeft()+QPoint(_m_layout->m_equipAreas[index].width()/2,0));
			anim->setDuration(200);
			_m_equipAnim[index]->addAnimation(anim);
			connect(anim, SIGNAL(finished()), anim, SLOT(deleteLater()));
			
			anim = new QPropertyAnimation(_m_equipRegions[index], "opacity");
			if(m_player->hasEquipArea(index))
				anim->setEndValue(0);
			anim->setDuration(200);
			_m_equipAnim[index]->addAnimation(anim);
			connect(anim, SIGNAL(finished()), anim, SLOT(deleteLater()));
			
			_m_equipAnim[index]->start();
			_mutexEquipAnim.unlock();/*
			
			if (Sanguosha->getSkill(result.last()->objectName()))
				emit remove_equip_skill(result.last()->objectName());*/
			_m_equipCards[index] = nullptr;
		}
	}
    _updateEquips();
    return result;
}

void PlayerCardContainer::startHuaShen(QString generalName, QString skillName)
{
    if(!m_player) return;

    _m_huashenSkillName = skillName;
    _m_huashenGeneralName = generalName;

    bool second_zuoci = m_player->getGeneral2Name().contains("zuoci");
    int avatarSize = second_zuoci ? _m_layout->m_smallAvatarSize : (m_player->getGeneral2() ? _m_layout->m_primaryAvatarSize : _m_layout->m_avatarSize);
    QPixmap pixmap = G_ROOM_SKIN.getGeneralPixmap(generalName, (QSanRoomSkin::GeneralIconSize)avatarSize);

    QRect animRect = second_zuoci ? _m_layout->m_smallAvatarArea : _m_layout->m_avatarArea;
    if (pixmap.size() != animRect.size())
        pixmap = pixmap.scaled(animRect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (second_zuoci)
        pixmap = paintByMask(pixmap);

    stopHuaShen();
    _m_huashenAnimation = G_ROOM_SKIN.createHuaShenAnimation(pixmap, animRect.topLeft(), _getAvatarParent(), _m_huashenItem);
    _m_huashenAnimation->start();

    _paintPixmap(_m_extraSkillBg, _m_layout->m_extraSkillArea, QSanRoomSkin::S_SKIN_KEY_EXTRA_SKILL_BG, _getAvatarParent());
    _m_layout->m_extraSkillFont.paintText(_m_extraSkillText, _m_layout->m_extraSkillTextArea, Qt::AlignCenter, Sanguosha->translate(skillName).left(2));
    if (!skillName.isEmpty()) {
        _m_extraSkillBg->show();
        _m_extraSkillText->show();
        {
            SafeLuaMutex &lua_mutex = Sanguosha->getLuaMutex();
            if (lua_mutex.tryLock(50)) {
                _m_extraSkillBg->setToolTip(Sanguosha->getSkill(skillName)->getDescription(m_player));
                lua_mutex.unlock();
            }
        }
    }
    _adjustComponentZValues();
}

void PlayerCardContainer::stopHuaShen()
{
    if (_m_huashenAnimation != nullptr) {
        _m_huashenAnimation->stop();
        _m_huashenAnimation->deleteLater();
        delete _m_huashenItem;
        _m_huashenItem = nullptr;
        _m_huashenAnimation = nullptr;
        _clearPixmap(_m_extraSkillBg);
        _clearPixmap(_m_extraSkillText);
    }
}

void PlayerCardContainer::onAvatarHoverEnter()
{
    if(!m_player) return;
	updateAvatarTooltip();

    if (_m_screenNameItem&&Self==m_player)
		_m_screenNameItem->setVisible(true);

	QString general = m_player->getGeneralName();
	GraphicsPixmapHoverItem *avatarItem = _m_avatarIcon;
	QSanButton *heroSKinBtn = m_changePrimaryHeroSKinBtn;

	if (sender() == _m_avatarIcon) {
		m_changeSecondaryHeroSkinBtn->hide();
	}else{
		avatarItem = _m_smallAvatarIcon;
		general = m_player->getGeneral2Name();
		heroSKinBtn = m_changeSecondaryHeroSkinBtn;

		m_changePrimaryHeroSKinBtn->hide();
	}

	if (general!=""&&avatarItem->isSkinChangingFinished()) {
		if(Config.value("HeroSkin/"+general, 0).toInt()>0)
			heroSKinBtn->show();
		else{
			Config.beginGroup("HeroSkin");
			Config.setValue(general, 1);
			Config.endGroup();
			QPixmap pixmap = G_ROOM_SKIN.getCardMainPixmap(general);
			Config.beginGroup("HeroSkin");
			Config.remove(general);
			Config.endGroup();
			if(pixmap.width()>1 || pixmap.height()>1)
				heroSKinBtn->show();
		}
    }
}

void PlayerCardContainer::onAvatarHoverLeave()
{
	if (_m_screenNameItem&&Self==m_player)
		_m_screenNameItem->setVisible(false);
	QSanButton *heroSKinBtn = m_changePrimaryHeroSKinBtn;
	if (sender() == _m_smallAvatarIcon) heroSKinBtn = m_changeSecondaryHeroSkinBtn;
	if (heroSKinBtn->isMouseInside()) return;
	heroSKinBtn->hide();
	doAvatarHoverLeave();
}

void PlayerCardContainer::updateAvatarTooltip()
{
    if (m_player) {
        QString description = m_player->getSkillDescription();
        _m_avatarArea->setToolTip(description);
        if (m_player->getGeneral2()||m_player->property("avatarIcon2").toString()!="")
            _m_smallAvatarArea->setToolTip(description);
    }
}

PlayerCardContainer::PlayerCardContainer()
{
    _m_layout = nullptr;
    _m_avatarArea = _m_smallAvatarArea = nullptr;
    _m_avatarNameItem = _m_smallAvatarNameItem = nullptr;
    _m_avatarIcon = nullptr;
    _m_smallAvatarIcon = nullptr;
	_m_circleItem = nullptr;
    _m_screenNameItem = nullptr;
    _m_chainIcon = _m_faceTurnedIcon = nullptr;
    _m_handCardBg = _m_handCardNumText = nullptr;
    _m_kingdomColorMaskIcon = _m_deathIcon = nullptr;
    _m_actionIcon = nullptr;
    _m_kingdomIcon = nullptr;
    _m_saveMeIcon = nullptr;
    _m_phaseIcon = nullptr;
    _m_markItem = nullptr;
    _m_roleComboBox = nullptr;
    m_player = nullptr;
    _m_selectedFrame = nullptr;
    _m_dynamicBgItem = nullptr;

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        _m_equipCards[i] = nullptr;
        _m_equipRegions[i] = nullptr;
        _m_equipAnim[i] = nullptr;
        _m_equipLabel[i] = nullptr;
    }
    _m_huashenItem = nullptr;
    _m_huashenAnimation = nullptr;
    _m_extraSkillBg = nullptr;
    _m_extraSkillText = nullptr;

    _m_floatingArea = nullptr;
    _m_votesGot = 0;
    _m_maxVotes = 1;
    _m_votesItem = nullptr;
    _m_distanceItem = nullptr;
    _m_groupMain = new QGraphicsPixmapItem(this);
    _m_groupMain->setFlag(ItemHasNoContents);
    _m_groupMain->setPos(0, 0);
    _m_groupDeath = new QGraphicsPixmapItem(this);
    _m_groupDeath->setFlag(ItemHasNoContents);
    _m_groupDeath->setPos(0, 0);
    _allZAdjusted = false;
    m_changePrimaryHeroSKinBtn = nullptr;
    m_changeSecondaryHeroSkinBtn = nullptr;
    m_primaryHeroSkinContainer = nullptr;
    m_secondaryHeroSkinContainer = nullptr;
}

void PlayerCardContainer::hideAvatars()
{
    if (_m_avatarIcon) _m_avatarIcon->hide();
    if (_m_smallAvatarIcon) _m_smallAvatarIcon->hide();
}

void PlayerCardContainer::_layUnder(QGraphicsItem *item)
{
    //_lastZ--;
    //Q_ASSERT((unsigned long)item != 0xcdcdcdcd);
    if (item) item->setZValue(--_lastZ);
    else _allZAdjusted = false;
}

bool PlayerCardContainer::_startLaying()
{
    if (_allZAdjusted) return false;
    _allZAdjusted = true;
    _lastZ = -1;
    return true;
}

void PlayerCardContainer::_layBetween(QGraphicsItem *middle, QGraphicsItem *item1, QGraphicsItem *item2)
{
    if (middle && item1 && item2)
        middle->setZValue((item1->zValue() + item2->zValue()) / 2.0);
    else
        _allZAdjusted = false;
}

void PlayerCardContainer::_adjustComponentZValues(bool killed)
{
    // all components use negative zvalues to ensure that no other generated
    // cards can be under us.

    // layout
    //if (!_startLaying()) return;
    if (_allZAdjusted) return;
    _allZAdjusted = true;
    _lastZ = -1;

    _layUnder(_m_floatingArea);
    _layUnder(_m_distanceItem);
    _layUnder(_m_votesItem);
    if (!killed) {
        foreach(QGraphicsItem *pile, _m_privatePiles.values())
            _layUnder(pile);
    }
    foreach(QGraphicsItem *judge, _m_judgeIcons)
        _layUnder(judge);
    _layUnder(_m_markItem);
    _layUnder(_m_progressBarItem);
    _layUnder(_m_roleComboBox);
    _layUnder(_m_chainIcon);
    _layUnder(_m_hpBox);
    //_layUnder(_m_handCardNumText);
    _layUnder(_m_handCardBg);
    _layUnder(_m_actionIcon);
    _layUnder(_m_saveMeIcon);
    _layUnder(_m_phaseIcon);
    _layUnder(_m_smallAvatarNameItem);
    _layUnder(_m_avatarNameItem);
    _layUnder(_m_kingdomIcon);
    _layUnder(_m_kingdomColorMaskIcon);
    _layUnder(_m_screenNameItem);
    for (int i = S_EQUIP_AREA_LENGTH; i > 0; i--)
        _layUnder(_m_equipRegions[i]);
    _layUnder(_m_selectedFrame);
    _layUnder(_m_extraSkillText);
    _layUnder(_m_extraSkillBg);
    _layUnder(_m_faceTurnedIcon);
    _layUnder(_m_smallAvatarArea);
    _layUnder(_m_avatarArea);
    _layUnder(_m_circleItem);
    bool second_zuoci = m_player && m_player->getGeneral2Name().contains("zuoci");
    if (!second_zuoci)
        _layUnder(_m_smallAvatarIcon);
    if (!killed)
        _layUnder(_m_huashenItem);
    if (second_zuoci)
        _layUnder(_m_smallAvatarIcon);
    _layUnder(_m_dynamicBgItem);
    _layUnder(_m_avatarIcon);
}

void PlayerCardContainer::updateRole(const QString &role)
{
    _m_roleComboBox->fix(role);
}

void PlayerCardContainer::_updateProgressBar()
{
    QGraphicsItem *parent = _getProgressBarParent();
    if (parent == nullptr) return;
    _m_progressBar->setOrientation(_m_layout->m_isProgressBarHorizontal ? Qt::Horizontal : Qt::Vertical);
    QRectF newRect = _m_layout->m_progressBarArea.getTranslatedRect(parent->boundingRect().toRect());
    _m_progressBar->setFixedHeight(newRect.height());
    _m_progressBar->setFixedWidth(newRect.width());
    _m_progressBarItem->setParentItem(parent);
    _m_progressBarItem->setPos(newRect.left(), newRect.top());
}

void PlayerCardContainer::_createControls()
{
    _m_floatingArea = new QGraphicsPixmapItem(_m_groupMain);

    _m_screenNameItem = new QGraphicsPixmapItem(_getAvatarParent());

    _m_avatarArea = new QGraphicsRectItem(_m_layout->m_avatarArea, _getAvatarParent());
    _m_avatarArea->setPen(Qt::NoPen);
    _m_avatarNameItem = new QGraphicsPixmapItem(_getAvatarParent());

    _m_smallAvatarArea = new QGraphicsRectItem(_m_layout->m_smallAvatarArea, _getAvatarParent());
    _m_smallAvatarArea->setPen(Qt::NoPen);
    _m_smallAvatarNameItem = new QGraphicsPixmapItem(_getAvatarParent());

    _m_extraSkillText = new QGraphicsPixmapItem(_getAvatarParent());
    _m_extraSkillText->hide();

    _m_handCardNumText = new QGraphicsPixmapItem(_getAvatarParent());
    _m_handCardNumText->setZValue(22);

    _m_hpBox = new MagatamasBoxItem(_getAvatarParent());

    // Now set up progress bar
    _m_progressBar = new QSanCommandProgressBar;
    _m_progressBar->setAutoHide(true);
    _m_progressBar->hide();
    _m_progressBarItem = new QGraphicsProxyWidget(_getProgressBarParent());
    _m_progressBarItem->setWidget(_m_progressBar);
    _updateProgressBar();

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        _m_equipLabel[i] = new QLabel;
        _m_equipLabel[i]->setStyleSheet("QLabel { background-color: transparent; }");
        _m_equipLabel[i]->setPixmap(QPixmap(_m_layout->m_equipAreas[i].size()));
        _m_equipRegions[i] = new QGraphicsProxyWidget();
        _m_equipRegions[i]->setWidget(_m_equipLabel[i]);
        _m_equipRegions[i]->setPos(_m_layout->m_equipAreas[i].topLeft());
        _m_equipRegions[i]->setParentItem(_getEquipParent());
        _m_equipRegions[i]->hide();
        _m_equipAnim[i] = new QParallelAnimationGroup(this);
    }

    _m_markItem = new QGraphicsTextItem(_getMarkParent());
    _m_markItem->setDefaultTextColor(Qt::white);

    m_changePrimaryHeroSKinBtn = new QSanButton("player_container", "change-heroskin", _getAvatarParent());
    m_changePrimaryHeroSKinBtn->hide();
    connect(m_changePrimaryHeroSKinBtn, SIGNAL(clicked()), this, SLOT(showHeroSkinList()));
    connect(m_changePrimaryHeroSKinBtn, SIGNAL(clicked_mouse_outside()), this, SLOT(heroSkinBtnMouseOutsideClicked()));

    m_changeSecondaryHeroSkinBtn = new QSanButton("player_container", "change-heroskin", _getAvatarParent());
    m_changeSecondaryHeroSkinBtn->hide();
    connect(m_changeSecondaryHeroSkinBtn, SIGNAL(clicked()), this, SLOT(showHeroSkinList()));
    connect(m_changeSecondaryHeroSkinBtn, SIGNAL(clicked_mouse_outside()), this, SLOT(heroSkinBtnMouseOutsideClicked()));

    _createRoleComboBox();
    repaintAll();

    connect(_m_avatarIcon, SIGNAL(hover_enter()), this, SLOT(onAvatarHoverEnter()));
    connect(_m_avatarIcon, SIGNAL(hover_leave()), this, SLOT(onAvatarHoverLeave()));
    connect(_m_smallAvatarIcon, SIGNAL(hover_enter()), this, SLOT(onAvatarHoverEnter()));
    connect(_m_smallAvatarIcon, SIGNAL(hover_leave()), this, SLOT(onAvatarHoverLeave()));
}

void PlayerCardContainer::showHeroSkinList()
{
    if (m_player) {
        if (sender() == m_changePrimaryHeroSKinBtn) {
            showHeroSkinListHelper(m_player->getGeneral(), _m_avatarIcon, m_primaryHeroSkinContainer);
        }else {
            showHeroSkinListHelper(m_player->getGeneral2(), _m_smallAvatarIcon, m_secondaryHeroSkinContainer);
        }
    }
}

void PlayerCardContainer::showHeroSkinListHelper(const General *general,
    GraphicsPixmapHoverItem */*avatarIcon*/,
    HeroSkinContainer *&/*heroSkinContainer*/)
{
    if (!general) return;

	int skin_index = Config.value("HeroSkin/"+general->objectName(), 0).toInt();
	skin_index++;
	Config.beginGroup("HeroSkin");
	Config.setValue(general->objectName(), skin_index);
	Config.endGroup();
	QPixmap pixmap = G_ROOM_SKIN.getCardMainPixmap(general->objectName());
	if(pixmap.width()<=1 && pixmap.height()<=1){
		Config.beginGroup("HeroSkin");
		Config.remove(general->objectName());
		Config.endGroup();
	}
	updateSmallAvatar();
    /*
	if (heroSkinContainer==nullptr) {
        heroSkinContainer = RoomSceneInstance->findHeroSkinContainer(general->objectName());
    }

    if (heroSkinContainer==nullptr) {
        heroSkinContainer = new HeroSkinContainer(general->objectName(), general->getKingdom());

        connect(heroSkinContainer, SIGNAL(skin_changed(const QString &)),
            avatarIcon, SLOT(startChangeHeroSkinAnimation(const QString &)));

        RoomSceneInstance->addHeroSkinContainer(m_player, heroSkinContainer);
        RoomSceneInstance->addItem(heroSkinContainer);

        heroSkinContainer->setPos(getHeroSkinContainerPosition());
        RoomSceneInstance->bringToFront(heroSkinContainer);
    }

    heroSkinContainer->show();

    heroSkinContainer->bringToTopMost();*/
}

void PlayerCardContainer::heroSkinBtnMouseOutsideClicked()
{
	QSanButton *heroSKinBtn = m_changeSecondaryHeroSkinBtn;
	if (sender() == m_changePrimaryHeroSKinBtn)
		heroSKinBtn = m_changePrimaryHeroSKinBtn;

	QGraphicsItem *parent = heroSKinBtn->parentItem();
	if (parent && !parent->isUnderMouse()) {
		heroSKinBtn->hide();
    }
}

void PlayerCardContainer::_updateDeathIcon()
{
    if (!m_player || m_player->isAlive()) return;
    QRect deathArea = _m_layout->m_deathIconRegion.getTranslatedRect(_getDeathIconParent()->boundingRect().toRect());
    _paintPixmap(_m_deathIcon, deathArea, QPixmap(m_player->getDeathPixmapPath()), _getDeathIconParent());
    _m_deathIcon->setZValue(11);
}

void PlayerCardContainer::killPlayer()
{
    _m_roleComboBox->fix(m_player->getRole());
	_m_roleComboBox->setEnabled(m_player->property("RestPlayer").toBool());
    _updateDeathIcon();
    //_m_saveMeIcon->hide();
    if (_m_votesItem) _m_votesItem->hide();
    if (_m_distanceItem) _m_distanceItem->hide();
    if (!m_player->property("RestPlayer").toBool()) {
        QGraphicsColorizeEffect *effect = new QGraphicsColorizeEffect();
        effect->setColor(_m_layout->m_deathEffectColor);
        effect->setStrength(1.0);
        _m_groupMain->setGraphicsEffect(effect);
    }
    refresh(true);
    if (ServerInfo.GameMode == "04_1v3" && !m_player->isLord()) {
        _m_deathIcon->hide();
        _m_votesGot = 6;
        updateVotes(false, true);
    } else
        _m_deathIcon->show();
}

void PlayerCardContainer::revivePlayer()
{
    _m_votesGot = 0;
    _m_groupMain->setGraphicsEffect(nullptr);
	_m_roleComboBox->setEnabled(true);
    Q_ASSERT(_m_deathIcon);
    _m_deathIcon->hide();
    refresh();
}

void PlayerCardContainer::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (!_m_handCardBg || !m_player || m_player == Self)
        return;

    if (_m_handCardBg->sceneBoundingRect().contains(event->scenePos())) {
        showHandcardViewer();
        event->accept();
    }
}

void PlayerCardContainer::updateVotes(bool need_select, bool display_1)
{
    if ((need_select && !isSelected()) || _m_votesGot < 1 || (!display_1 && _m_votesGot == 1))
        _clearPixmap(_m_votesItem);
    else {
        _paintPixmap(_m_votesItem, _m_layout->m_votesIconRegion,
            _getPixmap(QSanRoomSkin::S_SKIN_KEY_VOTES_NUMBER, QString::number(_m_votesGot)),
            _getAvatarParent());
        _m_votesItem->setZValue(1);
        _m_votesItem->show();
    }
}

void PlayerCardContainer::updateReformState()
{
    _m_votesGot--;
    updateVotes(false, true);
}

void PlayerCardContainer::showDistance()
{
    bool isNull = (_m_distanceItem == nullptr);
    _paintPixmap(_m_distanceItem, _m_layout->m_votesIconRegion,
        _getPixmap(QSanRoomSkin::S_SKIN_KEY_VOTES_NUMBER, QString::number(Self->distanceTo(m_player))),
        _getAvatarParent());
    _m_distanceItem->setZValue(2.1);
    if (Self->inMyAttackRange(m_player)) {
        _m_distanceItem->setGraphicsEffect(nullptr);
    } else {
        QGraphicsColorizeEffect *effect = new QGraphicsColorizeEffect();
        effect->setColor(_m_layout->m_deathEffectColor);
        effect->setStrength(1.0);
        _m_distanceItem->setGraphicsEffect(effect);
    }
    if (_m_distanceItem->isVisible() && !isNull)
        _m_distanceItem->hide();
    else
        _m_distanceItem->show();
}

void PlayerCardContainer::updateScreenName(const QString &screenName)
{
    if (_m_screenNameItem){
		_m_screenNameItem->setVisible(Self != m_player);
        _m_layout->m_screenNameFont.paintText(_m_screenNameItem, _m_layout->m_screenNameArea, Qt::AlignCenter, screenName);
	}
}

void PlayerCardContainer::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem *item = getMouseClickReceiver();
    if (item != nullptr && item->isUnderMouse() && isEnabled() && (flags() & QGraphicsItem::ItemIsSelectable)) {
        if (event->button() == Qt::RightButton)
            setSelected(false);
        else if (event->button() == Qt::LeftButton) {
            _m_votesGot++;
            setSelected(_m_votesGot <= _m_maxVotes);
            if (_m_votesGot > 1) emit selected_changed();
        }
        updateVotes();
    }
}

void PlayerCardContainer::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *)
{
    if (Config.EnableDoubleClick)
        RoomSceneInstance->doOkButton();
}

QVariant PlayerCardContainer::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSelectedHasChanged) {
        if (!value.toBool()) {
            _m_votesGot = 0;
            _clearPixmap(_m_selectedFrame);
            _m_selectedFrame->hide();
        } else {
            _paintPixmap(_m_selectedFrame, _m_layout->m_focusFrameArea,
                _getPixmap(QSanRoomSkin::S_SKIN_KEY_SELECTED_FRAME, true),
                _getFocusFrameParent());
            _m_selectedFrame->show();
        }
        updateVotes();
        emit selected_changed();
    } else if (change == ItemEnabledHasChanged) {
        _m_votesGot = 0;
        emit enable_changed();
    }

    return QGraphicsObject::itemChange(change, value);
}

void PlayerCardContainer::_onEquipSelectChanged()
{
}

bool PlayerCardContainer::canBeSelected()
{
    QGraphicsItem *item1 = getMouseClickReceiver();
    return item1 && isEnabled() && (flags() & QGraphicsItem::ItemIsSelectable);
}

void PlayerCardContainer::showHandcardViewer()
{
    if (!m_player || m_player == Self) return;

    QList<const Card *> known_cards;
    int total_handcard_num = m_player->getHandcardNum();

    if (Self->canSeeHandcard(m_player)) {
        foreach (int id, m_player->handCards()) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (card) known_cards << card;
        }
    } else {
        known_cards = m_player->getKnownCards();
    }

    QList<const Card *> sorted_known = known_cards;
    std::sort(sorted_known.begin(), sorted_known.end(), [](const Card *a, const Card *b) {
        if (a->getSuit() != b->getSuit())
            return a->getSuit() < b->getSuit();
        return a->getNumber() < b->getNumber();
    });

    QString title = QString("%1%2").arg(m_player->getLogName()).arg(tr("'s Handcards"));
    Window *handcardWindow = new Window(title, QSize(800, 500));
    handcardWindow->setZValue(20);
    RoomSceneInstance->addItem(handcardWindow);

    QRectF sceneRect = RoomSceneInstance->sceneRect();
    QPointF centerPos = sceneRect.center();
    QRectF windowRect = handcardWindow->boundingRect();
    handcardWindow->setPos(centerPos.x() - windowRect.width() / 2,
                           centerPos.y() - windowRect.height() / 2);

    QGraphicsRectItem *cardContainer = new QGraphicsRectItem(15, 50, 770, 400, handcardWindow);
    cardContainer->setFlag(QGraphicsItem::ItemClipsChildrenToShape);
    cardContainer->setPen(Qt::NoPen);

    QGraphicsTextItem *totalText = new QGraphicsTextItem(handcardWindow);
    totalText->setPlainText(QString("%1%2").arg(tr("Total: ")).arg(total_handcard_num));
    totalText->setDefaultTextColor(Qt::white);
    totalText->setFont(Config.SmallFont);
    totalText->setPos(25, 20);

    const int cardWidth = 93;
    const int cardHeight = 130;
    const int cardsPerRow = 7;
    const int horizontalSpacing = 15;
    const int verticalSpacing = 20;
    const int startX = 20;
    const int startY = 10;

    int x = startX;
    int y = startY;
    int count = 0;

    foreach (const Card *card, sorted_known) {
        QGraphicsPixmapItem *cardItem = new QGraphicsPixmapItem(cardContainer);

        QPixmap cardPixmap(cardWidth, cardHeight);
        cardPixmap.fill(Qt::transparent);
        QPainter painter(&cardPixmap);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

        painter.drawPixmap(G_COMMON_LAYOUT.m_cardMainArea, G_ROOM_SKIN.getCardMainPixmap(card->objectName()));
        painter.drawPixmap(G_COMMON_LAYOUT.m_cardSuitArea, G_ROOM_SKIN.getCardSuitPixmap(card->getSuit()));
        painter.drawPixmap(G_COMMON_LAYOUT.m_cardNumberArea, G_ROOM_SKIN.getCardNumberPixmap(card->getNumber(), card->isBlack()));
        painter.end();

        cardItem->setPixmap(cardPixmap);
        cardItem->setPos(x, y);
        cardItem->setToolTip(card->getDescription());

        count++;
        x += cardWidth + horizontalSpacing;
        if (count % cardsPerRow == 0) {
            x = startX;
            y += cardHeight + verticalSpacing;
        }
    }

    int unknown_count = qMax(0, total_handcard_num - known_cards.size());
    for (int i = 0; i < unknown_count; i++) {
        QGraphicsPixmapItem *cardItem = new QGraphicsPixmapItem(cardContainer);
        QPixmap unknownPixmap = G_ROOM_SKIN.getCardMainPixmap("unknown");
        cardItem->setPixmap(unknownPixmap.scaled(cardWidth, cardHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        cardItem->setPos(x, y);
        cardItem->setToolTip(tr("Unknown Card"));

        count++;
        x += cardWidth + horizontalSpacing;
        if (count % cardsPerRow == 0) {
            x = startX;
            y += cardHeight + verticalSpacing;
        }
    }

    if (total_handcard_num == 0) {
        QGraphicsTextItem *emptyText = new QGraphicsTextItem(cardContainer);
        emptyText->setPlainText(tr("This player has no handcards"));
        emptyText->setDefaultTextColor(Qt::white);
        QFont font = Config.BigFont;
        font.setPointSize(14);
        emptyText->setFont(font);
        emptyText->setPos(250, 160);
    }

    handcardWindow->addCloseButton(tr("Close"));
    handcardWindow->appear();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Dynamic skin background
// ═══════════════════════════════════════════════════════════════════════════

void PlayerCardContainer::setDynamicBackground(const QString &imagePath)
{
    if (imagePath.isEmpty()) {
        clearDynamicBackground();
        return;
    }

    QPixmap bgPixmap(imagePath);
    if (bgPixmap.isNull()) {
        qWarning("[PlayerCardContainer] Cannot load dynamic background: %s",
                 qPrintable(imagePath));
        clearDynamicBackground();
        return;
    }

    // Paint the background into the avatar area, parented under the avatar parent
    QGraphicsItem *parent = _getAvatarParent();
    if (!_m_dynamicBgItem) {
        _m_dynamicBgItem = new QGraphicsPixmapItem(parent);
        _m_dynamicBgItem->setTransformationMode(Qt::SmoothTransformation);
    }

    // Scale the pixmap to fit the avatar area
    QRect avatarRect = _m_layout->m_avatarArea;
    QPixmap scaled = bgPixmap.scaled(avatarRect.size(), Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
    // Center-crop if necessary
    if (scaled.size() != avatarRect.size()) {
        int x = (scaled.width() - avatarRect.width()) / 2;
        int y = (scaled.height() - avatarRect.height()) / 2;
        scaled = scaled.copy(x, y, avatarRect.width(), avatarRect.height());
    }
    _m_dynamicBgItem->setPixmap(scaled);
    _m_dynamicBgItem->setPos(avatarRect.topLeft());
    _m_dynamicBgItem->show();

    // Force Z-ordering refresh so the bg item sits just above _m_avatarIcon
    _allZAdjusted = false;
    _adjustComponentZValues();
}

void PlayerCardContainer::clearDynamicBackground()
{
    if (_m_dynamicBgItem) {
        _m_dynamicBgItem->hide();
        _m_dynamicBgItem->setPixmap(QPixmap());
    }
}

