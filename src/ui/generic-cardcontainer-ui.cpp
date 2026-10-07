#include "generic-cardcontainer-ui.h"
#include "engine.h"
#include "oracle_helper.h"
#include "general-info-card.h"
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
#include "heroskincontainer.h"
#include "effects/effects-completion.h"
#include "effects/effects-policy.h"
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QMutexLocker>
#include <QTimer>

using namespace QSanProtocol;

EquipPixmapItem::EquipPixmapItem(QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
}

void EquipPixmapItem::setPixmap(const QPixmap &pixmap)
{
    prepareGeometryChange();
    m_pixmap = pixmap;
    update();
}

static QSizeF equipPixmapLogicalSize(const QPixmap &pixmap)
{
    // deviceIndependentSize() arrived in Qt 6.2. Qt 5.6 width()/height() are device
    // pixels, so divide by dpr or an XP equip slot grows with the supersample factor.
    const qreal ratio = pixmap.devicePixelRatio();
    if (ratio <= 1.0)
        return QSizeF(pixmap.size());
    return QSizeF(pixmap.width(), pixmap.height()) / ratio;
}

QRectF EquipPixmapItem::boundingRect() const
{
    return QRectF(QPointF(0, 0), equipPixmapLogicalSize(m_pixmap));
}

void EquipPixmapItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (m_pixmap.isNull())
        return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawPixmap(QPointF(0, 0), m_pixmap);
}

void PlayerCardContainer::setApplicationSuspended(bool suspended, bool offline)
{
    if (_m_progressBar)
        _m_progressBar->setApplicationSuspended(suspended, offline);
}

namespace {

qint64 currentCardMoveMonitorMs()
{
    static QElapsedTimer timer;
    static bool started = false;
    if (!started) {
        timer.start();
        started = true;
    }
    return timer.elapsed();
}

qint64 g_skipCardMoveAnimUntilMs = 0;

bool shouldSkipCardMoveAnimation()
{
    // NONE profile is not "set duration to 0": a zero-duration animation emits
    // finished() synchronously inside start(), letting _destroyCard() reenter
    // before the group is assembled. Instead, take the pre-existing skip branch: cards jump straight to final position and opacity.
    return !G_EFFECTS.animationsEnabled()
        || Config.value("NoCardMoveAnim", false).toBool()
        || currentCardMoveMonitorMs() < g_skipCardMoveAnimUntilMs;
}

void updateAdaptiveCardMoveAnimationState(QObject *animation)
{
    if (animation == nullptr || Config.value("NoCardMoveAnim", false).toBool())
        return;

    const qint64 startedAtMs = animation->property("cardMoveAnimStartedAt").toLongLong();
    const int expectedDurationMs = animation->property("cardMoveAnimExpectedDuration").toInt();
    if (startedAtMs <= 0 || expectedDurationMs <= 0)
        return;

    const qint64 elapsedMs = currentCardMoveMonitorMs() - startedAtMs;
    const int lagTriggerMs = qMax(expectedDurationMs * 2, expectedDurationMs + 500);
    if (elapsedMs >= lagTriggerMs)
        g_skipCardMoveAnimUntilMs = currentCardMoveMonitorMs() + 5000;
}

int getEquipPrimarySlot(const Card *card, const ClientPlayer *player)
{
    if (card == nullptr)
        return 0;

    if (player != nullptr) {
        QList<int> real_slots = player->getEquipRealSlots(card->getEffectiveId());
        if (!real_slots.isEmpty())
            return real_slots.first();
    }

    const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
    return equip ? equip->location() : 0;
}

QList<int> getEquipDisplaySearchOrder(int primary_slot)
{
    QList<int> order;
    order << primary_slot;

    if (primary_slot == 0) {
        static const int weapon_fallback[] = { 1, 4, 2, 3, 0 };
        for (int idx = 0; idx < int(sizeof(weapon_fallback) / sizeof(weapon_fallback[0])); ++idx) {
            int slot = weapon_fallback[idx];
            if (!order.contains(slot))
                order << slot;
        }
    }

    static const int default_fallback[] = { 0, 1, 4, 2, 3 };
    for (int idx = 0; idx < int(sizeof(default_fallback) / sizeof(default_fallback[0])); ++idx) {
        int slot = default_fallback[idx];
        if (!order.contains(slot))
            order << slot;
    }

    for (int slot = 0; slot < S_EQUIP_AREA_LENGTH; ++slot) {
        if (!order.contains(slot))
            order << slot;
    }

    return order;
}

bool equipSlotNarrow(const QSanRoomSkin::PlayerCardContainerLayout *layout, int slot)
{
    if (layout == nullptr)
        return slot == 2 || slot == 3;
    const int width = layout->m_equipAreas[slot].width();
    return width > 0 ? width < 80 : (slot == 2 || slot == 3);
}

QList<int> equipOverflowCandidates(const ClientPlayer *player,
                                   const QSanRoomSkin::PlayerCardContainerLayout *layout,
                                   int logical, bool cardIsHorse, bool abolishedOnly,
                                   const QSet<int> &reserved)
{
    QList<int> abolished, horses, wide, narrow;
    foreach (int candidate, getEquipDisplaySearchOrder(logical)) {
        if (candidate == logical || reserved.contains(candidate))
            continue;
        if (candidate < 0 || candidate >= S_EQUIP_AREA_LENGTH)
            continue;
        const bool gone = player == nullptr || !player->hasEquipArea(candidate);
        if (gone) {
            abolished << candidate;
            continue;
        }
        if (abolishedOnly)
            continue;
        if (cardIsHorse && (candidate == 2 || candidate == 3))
            horses << candidate;
        else if (!equipSlotNarrow(layout, candidate))
            wide << candidate;
        else
            narrow << candidate;
    }
    QList<int> order;
    order << abolished;
    if (cardIsHorse)
        order << horses;
    order << wide << narrow;
    QList<int> unique;
    foreach (int slot, order) {
        if (!unique.contains(slot))
            unique << slot;
    }
    return unique;
}

struct EquipVisualPlan
{
    QMap<int, int> displayById;
    QMap<int, int> shadowOwnerId;
    QMap<int, QList<int>> sharedIds;
    QMap<int, int> extraEmptyLogical;
    QMap<int, int> logicalByVisual;
    QMap<int, int> equipsOfType;
    QSet<int> coveredLive;
    QSet<int> reserved;
};

struct EquipVisualRow
{
    int id = -1;
    int primary = 0;
    QList<int> realSlots;
    bool horse = false;
};

void reserveEquipShadows(EquipVisualPlan &plan, const EquipVisualRow &row, int display)
{
    if (!row.realSlots.contains(display))
        return;
    foreach (int slot, row.realSlots) {
        if (slot == display || plan.reserved.contains(slot))
            continue;
        if (slot < 0 || slot >= S_EQUIP_AREA_LENGTH)
            continue;
        plan.reserved.insert(slot);
        plan.shadowOwnerId.insert(slot, row.id);
        plan.logicalByVisual.insert(slot, slot);
    }
}

int shareEquipVisual(const EquipVisualPlan &plan, int logical)
{
    for (int visual = 0; visual < S_EQUIP_AREA_LENGTH; ++visual) {
        if (plan.shadowOwnerId.contains(visual))
            continue;
        if (plan.logicalByVisual.value(visual, -1) == logical)
            return visual;
    }
    return qBound(0, logical, S_EQUIP_AREA_LENGTH - 1);
}

EquipVisualPlan planRealEquipVisuals(const ClientPlayer *player,
                                      const QSanRoomSkin::PlayerCardContainerLayout *layout,
                                      const QList<int> &cardIds)
{
    EquipVisualPlan plan;
    if (player == nullptr)
        return plan;

    QList<EquipVisualRow> rows;
    foreach (int cardId, cardIds) {
        const Card *card = Sanguosha->getCard(cardId);
        if (card == nullptr)
            continue;
        EquipVisualRow row;
        row.id = card->getEffectiveId();
        row.primary = getEquipPrimarySlot(card, player);
        row.realSlots = player->getEquipRealSlots(row.id);
        if (row.realSlots.isEmpty())
            row.realSlots << row.primary;
        row.horse = qobject_cast<const Horse *>(card->getRealCard()) != nullptr;
        rows << row;
        plan.equipsOfType[row.primary] = plan.equipsOfType.value(row.primary) + 1;
    }

    QList<EquipVisualRow> overflow;
    foreach (const EquipVisualRow &row, rows) {
        if (row.primary >= 0 && row.primary < S_EQUIP_AREA_LENGTH && !plan.reserved.contains(row.primary)) {
            plan.displayById.insert(row.id, row.primary);
            plan.reserved.insert(row.primary);
            plan.logicalByVisual.insert(row.primary, row.primary);
            reserveEquipShadows(plan, row, row.primary);
        } else {
            overflow << row;
        }
    }

    foreach (const EquipVisualRow &row, overflow) {
        int chosen = -1;
        foreach (int candidate, equipOverflowCandidates(player, layout, row.primary, row.horse, false, plan.reserved)) {
            chosen = candidate;
            break;
        }
        if (chosen < 0) {
            const int share = shareEquipVisual(plan, row.primary);
            plan.sharedIds[share] << row.id;
            if (!plan.logicalByVisual.contains(share))
                plan.logicalByVisual.insert(share, row.primary);
            continue;
        }
        plan.displayById.insert(row.id, chosen);
        plan.reserved.insert(chosen);
        plan.logicalByVisual.insert(chosen, row.primary);
        if (player->hasEquipArea(chosen) && chosen != row.primary)
            plan.coveredLive.insert(chosen);
        reserveEquipShadows(plan, row, chosen);
    }
    return plan;
}

void placeEmptyEquipAreas(EquipVisualPlan &plan, const ClientPlayer *player,
                           const QSanRoomSkin::PlayerCardContainerLayout *layout)
{
    if (player == nullptr)
        return;
    for (int slot = 0; slot < S_EQUIP_AREA_LENGTH; ++slot) {
        const int areas = player->getEquipArea(slot);
        const int placeholder = (player->hasEquipArea(slot) && !plan.reserved.contains(slot)) ? 1 : 0;
        if (placeholder > 0)
            plan.reserved.insert(slot);
        const int shadowRepresents = (plan.shadowOwnerId.contains(slot) && player->hasEquipArea(slot)) ? 1 : 0;
        int extra = areas - plan.equipsOfType.value(slot) - placeholder - shadowRepresents;
        while (extra > 0) {
            int display = -1;
            foreach (int candidate, equipOverflowCandidates(player, layout, slot, slot == 2 || slot == 3, true, plan.reserved)) {
                display = candidate;
                break;
            }
            if (display < 0)
                break;
            plan.extraEmptyLogical.insert(display, slot);
            plan.logicalByVisual.insert(display, slot);
            plan.reserved.insert(display);
            --extra;
        }
    }
}

QMap<int, int> getEquipDisplaySlotsById(const ClientPlayer *player,
                                         const QSanRoomSkin::PlayerCardContainerLayout *layout)
{
    EquipVisualPlan plan = planRealEquipVisuals(player, layout, player ? player->getEquipsId() : QList<int>());
    QMap<int, int> displaySlots = plan.displayById;
    for (auto it = plan.sharedIds.constBegin(); it != plan.sharedIds.constEnd(); ++it) {
        foreach (int id, it.value())
            displaySlots.insert(id, it.key());
    }
    return displaySlots;
}

}

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

void GenericCardContainer::updateContainer()
{
    update();
}

void GenericCardContainer::onAnimationFinished()
{
    QParallelAnimationGroup *animation = qobject_cast<QParallelAnimationGroup *>(sender());
    if (animation) {
        updateAdaptiveCardMoveAnimationState(animation);
        while (animation->animationCount() > 0)
            animation->takeAnimation(0);
        animation->deleteLater();
    }
}

void GenericCardContainer::_playMoveCardsAnimation(QList<CardItem *> &cards, bool destroyCards)
{
    if (shouldSkipCardMoveAnimation()) {
        G_EFFECTS.note(VisualEffectsPolicy::AnimationsSkipped);
        foreach (CardItem *card_item, cards) {
            card_item->goBack(false);
            card_item->setOpacity(card_item->getHomeOpacity());
            if (destroyCards) {
                card_item->setVisible(false);
                card_item->deleteLater();
            }
        }
        updateContainer();
        return;
    }

    QParallelAnimationGroup *animation = new QParallelAnimationGroup;
    foreach (CardItem *card_item, cards) {
		if (destroyCards)
            connect(card_item, SIGNAL(movement_animation_finished()), this, SLOT(_destroyCard()));
        // Duration is not scaled here: getGoBackAnimation() is the single scaling
        // point; scaling both sides would scale twice (REDUCED 600ms would become 54ms).
        animation->addAnimation(card_item->getGoBackAnimation(true));
    }

    animation->setProperty("cardMoveAnimStartedAt", currentCardMoveMonitorMs());
    // Adaptive-delay detection compares against what will actually play, so the scaled value is required here.
    animation->setProperty("cardMoveAnimExpectedDuration",
        G_EFFECTS.scaledDuration(Config.S_MOVE_CARD_ANIMATION_DURATION));
    connect(animation, SIGNAL(finished()), this, SLOT(updateContainer()));
    connect(animation, SIGNAL(finished()), this, SLOT(onAnimationFinished()));
    G_EFFECTS.note(VisualEffectsPolicy::AnimationsStarted);
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
    bool isPhoto = inherits("Photo");
    bool isDualGeneral = m_player && m_player->getGeneral2() != nullptr;
    
    if (isPhoto) {
        return G_ROOM_SKIN.getGeneralPixmapForPhoto(heroName, (QSanRoomSkin::GeneralIconSize)avatarSize, isDualGeneral);
    } else {
        return G_ROOM_SKIN.getGeneralPixmap(heroName, (QSanRoomSkin::GeneralIconSize)avatarSize);
    }
}

void PlayerCardContainer::updateAvatar()
{
    _allZAdjusted = false;
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
        _m_avatarIcon->setGeneralImage(avatarIcon, _m_layout->m_avatarArea.size());
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
    updateGeneralIndicators();
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
        _m_smallAvatarIcon->setFlag(QGraphicsItem::ItemStacksBehindParent, false);
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
        _m_smallAvatarArea->setToolTip(QString());
        _m_smallAvatarIcon->setToolTip(QString());
    } else {
		QString tooltip;
		const General *general2 = m_player ? m_player->getGeneral2() : nullptr;
		if (general2)
			tooltip = GeneralInfoCard::forGeneral(general2);
		else
			tooltip = Sanguosha->translate(name);
		QGraphicsPixmapItem *smallAvatarIconTmp = _m_smallAvatarIcon;
        _paintPixmap(smallAvatarIconTmp, _m_layout->m_smallAvatarArea, paintByMask(G_ROOM_SKIN.getGeneralPixmap(name, QSanRoomSkin::GeneralIconSize(_m_layout->m_smallAvatarSize))), _getAvatarParent());
        _paintPixmap(_m_circleItem, _m_layout->m_circleArea, QString(QSanRoomSkin::S_SKIN_KEY_GENERAL_CIRCLE_IMAGE).arg(_m_layout->m_circleImageSize), _getAvatarParent());
		if(m_player->getGeneral2()) name = m_player->getGeneral2()->getBriefName();
		else name = Sanguosha->translate(name);
        _m_layout->m_smallAvatarNameFont.paintText(_m_smallAvatarNameItem, _m_layout->m_smallAvatarNameArea, Qt::AlignLeft | Qt::AlignJustify, name);
        _m_smallAvatarArea->setToolTip(tooltip);
        _m_smallAvatarIcon->setToolTip(tooltip);
        _m_smallAvatarIcon->show();
    }
    _paintGeneralIndicators();
    _allZAdjusted = false;
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
			// Some private-pile buttons are dropped after the turn while their marks remain; rebuild them once.
			foreach (const QString &markName, m_player->getMarkNames()) {
				if (markName.startsWith("&") && m_player->getMark(markName) > 0)
					updateMark(markName, m_player->getMark(markName));
			}
		}
	}else
        _clearPixmap(_m_phaseIcon);
    _layoutStatusItems();
}

void PlayerCardContainer::paintHp(int hp, int maxHp)
{
    _m_hpBox->setHp(hp);
    _m_hpBox->setMaxHp(maxHp);
    _m_hpBox->update();
}

void PlayerCardContainer::updateHp()
{
    //Q_ASSERT(_m_hpBox && _m_saveMeIcon && m_player);
    paintHp(m_player->getHp(), m_player->getMaxHp());
    _m_saveMeIcon->setVisible(m_player->isAlive()&&m_player->hasFlag("Global_Dying"));
    _updateEquips();
    updateHandcardNum();
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
			QGraphicsProxyWidget *proxy = _m_privatePiles.take(pile_name);
			if (proxy->widget())
				proxy->widget()->deleteLater();
			proxy->setWidget(nullptr);
			if (proxy->scene())
				proxy->scene()->removeItem(proxy);
			proxy->deleteLater();
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

        disconnect(button, &QPushButton::clicked, this, &PlayerCardContainer::showPile);
        connect(button, &QPushButton::clicked, this, &PlayerCardContainer::showPile);
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

void PlayerCardContainer::showPile()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (button) {
        QList<int> card_ids = m_player->getPile(button->objectName());
        if (card_ids.isEmpty() || card_ids.contains(-1)) return;
        RoomSceneInstance->showPile(card_ids, button->objectName());
    }
}

void PlayerCardContainer::updateGeneralPile(const QString &pile_name)
{
    ClientPlayer *player = (ClientPlayer *)sender();
    if (!player) player = m_player;
    if (!player) return;

    QStringList generals = player->getGeneralPile(pile_name);

    if (generals.isEmpty()) {
        if (_m_privatePiles.contains(pile_name)) {
			QGraphicsProxyWidget *proxy = _m_privatePiles.take(pile_name);
			if (proxy->widget())
				proxy->widget()->deleteLater();
			proxy->setWidget(nullptr);
			if (proxy->scene())
				proxy->scene()->removeItem(proxy);
			proxy->deleteLater();
        }
    } else {
        QPushButton *button;
        if (_m_privatePiles.contains(pile_name)) {
            button = (QPushButton *)_m_privatePiles[pile_name]->widget();
            if(button->menu()) button->menu()->deleteLater();
        } else {
            button = new QPushButton;
            button->setObjectName(pile_name);
            button->setProperty("general_pile", "true");
            button->setStyleSheet("background-color:darkblue; color:white;");
            button->setFixedSize(60, 30);
            _m_privatePiles[pile_name] = new QGraphicsProxyWidget(_getPileParent());
            _m_privatePiles[pile_name]->setObjectName(pile_name);
            _m_privatePiles[pile_name]->setZValue(-2.0);
            _m_privatePiles[pile_name]->setFlag(QGraphicsItem::ItemIsMovable, false);
            _m_privatePiles[pile_name]->setFlag(QGraphicsItem::ItemIsSelectable, false);
            _m_privatePiles[pile_name]->setAcceptHoverEvents(false);
            _m_privatePiles[pile_name]->setWidget(button);
        }

        QMenu* menu = new QMenu(button);

        QString text = Sanguosha->translate(pile_name);
        text.append(QString("(%1)").arg(generals.length()));
        button->setText(text);
        menu->setProperty("general_pile", "true");
        // QMenu hides action tooltips by default, so the general cards never showed.
        menu->setToolTipsVisible(true);

        foreach(const QString &general_name, generals) {
            const General *general = Sanguosha->getGeneral(general_name);
            if (general) {
                QAction *action = menu->addAction(QIcon(G_ROOM_SKIN.getGeneralPixmap(general_name, QSanRoomSkin::S_GENERAL_ICON_SIZE_TINY)),
                                                Sanguosha->translate(general_name));
                action->setToolTip(GeneralInfoCard::forGeneral(general));
                action->setData(general_name);
            }
        }

        if(generals.length() > 0)
            button->setMenu(menu);
        else {
            delete menu;
            button->setMenu(NULL);
        }
    }

    QStringList treasureNames;
    foreach(const Card *e, player->getEquips())
        treasureNames << e->objectName();

    QList<QGraphicsProxyWidget *> widgets_treasure, widgets_general, widgets_card;
    foreach (QGraphicsProxyWidget *widget, _m_privatePiles.values()) {
        QPushButton *btn = (QPushButton *)widget->widget();
        QString pile_name = widget->objectName();

        if (treasureNames.contains(pile_name)) {
            widgets_treasure << widget;
        } else if (btn && btn->property("general_pile").toBool()) {
            widgets_general << widget;
        } else {
            widgets_card << widget;
        }
    }

    QList<QGraphicsProxyWidget *> sorted_widgets = widgets_treasure + widgets_general + widgets_card;

    for (int i = 0; i < sorted_widgets.length(); i++) {
        sorted_widgets[i]->setPos(_m_layout->m_privatePileStartPos + i * _m_layout->m_privatePileStep);
    }
}

void PlayerCardContainer::updateMark(const QString &mark_name, int mark_num)
{
    // A QML-bound mark is drawn by QmlTableLayer instead of a pile button.
    if (Sanguosha->isQmlMark(mark_name))
        return;
    /*ClientPlayer *player = (ClientPlayer *)sender();
    if (!player) player = m_player;
    if (!player) return;*/

    if (mark_num==0) {
        if (_m_privatePiles.contains(mark_name)) {
			QGraphicsProxyWidget *proxy = _m_privatePiles.take(mark_name);
			if (proxy->widget())
				proxy->widget()->deleteLater();
			proxy->setWidget(nullptr);
			if (proxy->scene())
				proxy->scene()->removeItem(proxy);
			proxy->deleteLater();
        }
    } else {
        QPushButton *button = new QPushButton;
		button->setObjectName(mark_name);
		button->setProperty("private_pile", "true");

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
        _allZAdjusted = false;
    return;
}

void PlayerCardContainer::paintHandcardNum(int handcardNum, int hp, int maxCards, bool hideHandcardNum, bool hasPlayer)
{
    QString num = "0";
    QRect area = _m_layout->m_handCardArea;
    const int extraW = 50;
    QRect wideArea(area.x() - extraW, area.y(), area.width() + extraW * 2, area.height());
    QRect innerRect(0, 0, wideArea.width(), wideArea.height());

    QImage image(wideArea.width(), wideArea.height(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter imagePainter(&image);

    if (hasPlayer) {

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

    imagePainter.end();

    _m_handCardNumText->setPixmap(QPixmap::fromImage(image));

    if (_m_handCardNumText->parentItem() != this) {
        _m_handCardNumText->setParentItem(this);
        _m_handCardNumText->setZValue(100);
    }
    _m_handCardNumText->setPos(mapFromItem(_getAvatarParent(), QPointF(wideArea.x(), wideArea.y())));
    // Hand count is hidden from other players while Fengbi is active.
    _m_handCardNumText->setVisible(!hideHandcardNum);

}

void PlayerCardContainer::updateHandcardNum()
{
    paintHandcardNum(m_player ? m_player->getHandcardNum() : 0,
        m_player ? m_player->getHp() : 0, m_player ? m_player->uiState().handMax : 0,
        m_player && m_player != Self && m_player->hasSkill("inovation_fengbi"), m_player != nullptr);

    if (!m_player) return;
    int limitBase = m_player->getHp();
    const PlayerUIState &uiState = m_player->uiState();
    const int maxCards = uiState.handMax;
    if (maxCards != limitBase) {
        QStringList tooltipInfo;
        const QStringList &mc_tag = uiState.maxCardsSkills;
        bool hasFixedMaxCards = false;

        foreach (const QString &entry, mc_tag) {
            if (!entry.contains("^")) continue;
            QStringList parts = entry.split("^");
            QString valueStr = parts.size() > 1 ? parts[1] : QString();
            if (valueStr.startsWith("F")) {
                hasFixedMaxCards = true;
                break;
            }
        }

        tooltipInfo << Sanguosha->translate("UI_MC_HandMax").arg(maxCards);
        if (!hasFixedMaxCards) {
            tooltipInfo << Sanguosha->translate("UI_MC_BaseHp").arg(qMax(limitBase, 0));
        }
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
                            // A reason may contain '_'; parse the source player only from the final component.
                            QString real_reason = data;
                            QString source_objname;
                            int last_sep = data.lastIndexOf('_');
                            if (last_sep > 0) {
                                QString candidate = data.mid(last_sep + 1);
                                if (ClientInstance->getPlayer(candidate) != nullptr) {
                                    source_objname = candidate;
                                    real_reason = data.left(last_sep);
                                }
                            }

                            QString sign = val > 0 ? "+" : "";
                            QString translatedReason = Sanguosha->translate(real_reason);
                            
                            QString source = Sanguosha->translate("UI_MC_GlobalSource");
                            if (!source_objname.isEmpty()) {
                                ClientPlayer *src_player = ClientInstance->getPlayer(source_objname);
                                if (src_player) {
                                    source = src_player->getLogName();
                                }
                            }

                            tooltipInfo << Sanguosha->translate("UI_MC_SkillEntry")
                                           .arg(translatedReason).arg(sign).arg(val).arg(source);
                        }
                    }
                }

                int base_extra = m_player->getMark("ExtraBfMaxCards") + m_player->getMark("ExtraBfMaxCards-Clear");
                if (base_extra != 0) {
                    QString sign = base_extra > 0 ? "+" : "";
                    tooltipInfo << Sanguosha->translate("UI_MC_OtherExtra").arg(sign).arg(base_extra);
                }
                break;
            }
        }

        // Read skill hand-limit adjustments from the server tag without calling Lua.
        foreach (const QString &entry, mc_tag) {
            if (!entry.contains("^")) continue;
            QStringList parts = entry.split("^");
            QString skillName = Sanguosha->translate(parts[0]);
            QString valueStr  = parts.size() > 1 ? parts[1] : "";
            QString srcName   = parts.size() > 2 ? parts[2] : QString();
            QString sourceDisplay = Sanguosha->translate("UI_MC_SelfSource");

            if (!srcName.isEmpty()) {
                ClientPlayer *src_player = ClientInstance->getPlayer(srcName);
                sourceDisplay = src_player ? src_player->getLogName() : srcName;
            }

            if (valueStr.startsWith("F")) {
                int fixed = valueStr.mid(1).toInt();
                tooltipInfo << Sanguosha->translate("UI_MC_FixedEntry")
                               .arg(skillName).arg(fixed).arg(sourceDisplay);
            } else {
                int extra = valueStr.toInt();
                QString sign = extra > 0 ? "+" : "";
                tooltipInfo << Sanguosha->translate("UI_MC_SkillEntry")
                               .arg(skillName).arg(sign).arg(extra).arg(sourceDisplay);
            }
        }

        _m_handCardNumText->setToolTip(tooltipInfo.join("\n"));
        
    } else {
        _m_handCardNumText->setToolTip(QString());
    }

    if (m_handcardWindow && m_handcardWindow->isVisible()) {
        updateHandcardViewer();
    }
}

void PlayerCardContainer::updateMarks()
{
    if (!_m_markItem) return;
    QRect parentRect = _getMarkParent()->boundingRect().toRect();
    QSize markSize = _m_markItem->boundingRect().size().toSize();
    QRect newRect = _m_layout->m_markTextArea.getTranslatedRect(parentRect, markSize);
    if (inherits("Photo"))
        _m_markItem->setPos(newRect.topLeft());
    else
        _m_markItem->setPos(newRect.left(), newRect.top() + newRect.height() / 2);
    _layoutStatusItems();
    _updateEquips();
}

void PlayerCardContainer::_updateEquips()
{
    if (!m_player || _m_layout == nullptr)
        return;

    QMap<int, CardItem *> equipItemsById;
    auto considerItem = [&](CardItem *item) {
        if (item == nullptr || item->getCard() == nullptr)
            return;
        equipItemsById.insert(item->getCard()->getEffectiveId(), item);
    };
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i)
        considerItem(_m_equipCards[i]);
    foreach (CardItem *item, _m_extraEquipCards)
        considerItem(item);

    EquipVisualPlan plan = planRealEquipVisuals(m_player, _m_layout, m_player->getEquipsId());
    QMap<int, Card *> simulatedEquips;
    QMap<int, QString> simulatedSkills;
    QMap<int, const Card *> shadowCards;
    foreach (int visual, plan.shadowOwnerId.keys()) {
        if (plan.shadowOwnerId.value(visual) < 0)
            continue;
        const Card *card = Sanguosha->getCard(plan.shadowOwnerId.value(visual));
        if (card != nullptr)
            shadowCards.insert(visual, card);
    }

    auto tryAddSimulated = [&](Card *ec, const QString &skillName) {
        const EquipCard *equip = qobject_cast<const EquipCard *>(ec);
        if (equip == nullptr) {
            ec->deleteLater();
            return;
        }
        QList<int> realSlots = equip->getOccupyLocations();
        int primary = realSlots.isEmpty() ? equip->location() : realSlots.first();
        if (primary < 0 || primary >= S_EQUIP_AREA_LENGTH) {
            ec->deleteLater();
            return;
        }
        const bool horse = qobject_cast<const Horse *>(equip) != nullptr;
        int chosen = plan.reserved.contains(primary) ? -1 : primary;
        if (chosen < 0) {
            foreach (int candidate, equipOverflowCandidates(m_player, _m_layout, primary, horse, false, plan.reserved)) {
                chosen = candidate;
                break;
            }
        }
        if (chosen < 0) {
            ec->deleteLater();
            return;
        }
        foreach (int slot, realSlots) {
            if (slot != chosen && plan.reserved.contains(slot)) {
                ec->deleteLater();
                return;
            }
        }
        simulatedEquips.insert(chosen, ec);
        simulatedSkills.insert(chosen, skillName);
        plan.reserved.insert(chosen);
        plan.logicalByVisual.insert(chosen, primary);
        plan.equipsOfType[primary] = plan.equipsOfType.value(primary) + 1;
        if (m_player->hasEquipArea(chosen) && chosen != primary)
            plan.coveredLive.insert(chosen);
        if (realSlots.contains(chosen)) {
            foreach (int slot, realSlots) {
                if (slot == chosen || plan.reserved.contains(slot) || slot < 0 || slot >= S_EQUIP_AREA_LENGTH)
                    continue;
                plan.reserved.insert(slot);
                plan.shadowOwnerId.insert(slot, -1);
                plan.logicalByVisual.insert(slot, slot);
                shadowCards.insert(slot, ec);
            }
        }
    };

    const QStringList propertyEquips = m_player->property("View_As_Equips_List").toString().split(QStringLiteral("+"), Qt::SkipEmptyParts);
    foreach (const QString &name, propertyEquips) {
        if (Card *ec = Sanguosha->cloneCard(name))
            tryAddSimulated(ec, QString());
    }
    foreach (const QString &entry, m_player->uiState().viewAsEquipSkills) {
        const QStringList parts = entry.split(QStringLiteral("^"));
        if (parts.length() >= 2) {
            if (Card *ec = Sanguosha->cloneCard(parts.at(0)))
                tryAddSimulated(ec, parts.at(1));
        }
    }

    placeEmptyEquipAreas(plan, m_player, _m_layout);

    auto areaName = [](int slot) {
        return Sanguosha->translate(QStringLiteral("EquipArea%1").arg(slot));
    };
    auto logicalOf = [&](int visual) {
        return plan.extraEmptyLogical.contains(visual)
            ? plan.extraEmptyLogical.value(visual)
            : plan.logicalByVisual.value(visual, visual);
    };
    auto decorateTooltip = [&](QString tooltip, int visual) {
        const int logical = logicalOf(visual);
        const bool borrowed = logical != visual;
        const bool covered = plan.coveredLive.contains(visual);
        if (!borrowed && !covered)
            return tooltip;
        QString head = QStringLiteral("<b>%1</b>").arg(areaName(logical));
        if (covered) {
            head += QStringLiteral("<br/>")
                + Sanguosha->translate(QStringLiteral("EquipAreaCovered")).arg(areaName(visual));
        }
        if (!tooltip.isEmpty())
            head += QStringLiteral("<br/>") + tooltip;
        return head;
    };

    const PlayerUIState &uiState = m_player->uiState();
    const int off_dist = uiState.offensiveDistance;
    const QStringList &off_skills = uiState.offensiveSkills;
    const int def_dist = uiState.defensiveDistance;
    const QStringList &def_skills = uiState.defensiveSkills;
    // The horse slot is narrower than weapon and armor in the compact Photo layout; scale the label accordingly.
    const int distFontPx = qBound(8, _m_layout->m_horsePointArea.height(), 14);

    auto paintDistance = [&](QPixmap &pixmap, int skillDist, const QStringList &skills, QString &tooltip, bool centered, bool horseSlot) {
        if (skillDist == 0 || pixmap.isNull())
            return;
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        const QString skillValue = (skillDist > 0 ? QStringLiteral("+") : QString()) + QString::number(skillDist);
        const QRect pointArea = horseSlot ? _m_layout->m_horsePointArea : _m_layout->m_equipPointArea;
        const int logicalHeight = qRound(equipPixmapLogicalSize(pixmap).height());
        const int logicalWidth = qRound(equipPixmapLogicalSize(pixmap).width());
        const QRect overlay = centered
            ? QRect(0, 0, logicalWidth, logicalHeight)
            : QRect(0, 0, qMax(0, pointArea.left() - 3), logicalHeight);
        QFont boldFont;
        boldFont.setPixelSize(distFontPx);
        boldFont.setBold(true);
        painter.setFont(boldFont);
        const QColor mainColor = (skillDist > 0) ? QColor(255, 220, 0) : QColor(255, 80, 80);
        const int align = centered ? (Qt::AlignCenter) : (Qt::AlignRight | Qt::AlignVCenter);
        painter.setPen(QColor(0, 0, 0, 210));
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (dx || dy)
                    painter.drawText(overlay.translated(dx, dy), align, skillValue);
            }
        }
        painter.setPen(mainColor);
        painter.drawText(overlay, align, skillValue);
        QStringList translated;
        foreach (const QString &skill, skills)
            translated << Sanguosha->translate(skill);
        if (!tooltip.isEmpty())
            tooltip += QStringLiteral("<br/><hr/>");
        tooltip += Sanguosha->translate(QStringLiteral("UI_DIST_SkillMod")).arg(translated.join(QStringLiteral("、"))).arg(skillValue);
    };

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        const bool isDef = i == 2;
        const bool isOff = i == 3;
        const int skillDist = isDef ? def_dist : (isOff ? off_dist : 0);
        const QStringList skills = isDef ? def_skills : off_skills;
        _m_equipRowItems[i].clear();

        QList<int> faceIds;
        for (auto it = plan.displayById.constBegin(); it != plan.displayById.constEnd(); ++it) {
            if (it.value() == i)
                faceIds << it.key();
        }
        foreach (int id, plan.sharedIds.value(i)) {
            if (!faceIds.contains(id))
                faceIds << id;
        }

        QPixmap pixmap;
        QString tooltip;
        float opacity = 1.0f;
        bool painted = false;

        if (!faceIds.isEmpty()) {
            if (faceIds.size() == 1) {
                const Card *card = Sanguosha->getCard(faceIds.first());
                pixmap = _getEquipPixmap(card, i);
                tooltip = card ? card->getDescription(m_player) : QString();
                if (CardItem *item = equipItemsById.value(faceIds.first()))
                    _m_equipRowItems[i] << item;
            } else {
                const QSize slotSize = _m_layout->m_equipAreas[i].size();
                const int supersample = getUITextSupersample();
                pixmap = QPixmap(slotSize * supersample);
                pixmap.fill(Qt::transparent);
                pixmap.setDevicePixelRatio(supersample);
                QPainter painter(&pixmap);
                const int gap = 1;
                const int cellWidth = qMax(1, (slotSize.width() - gap * (faceIds.size() - 1)) / faceIds.size());
                for (int part = 0; part < faceIds.size(); ++part) {
                    const Card *card = Sanguosha->getCard(faceIds.at(part));
                    const QPixmap cell = _getEquipPixmap(card, i, QSize(cellWidth, slotSize.height()));
                    painter.drawPixmap(QPointF(part * (cellWidth + gap), 0), cell);
                    if (CardItem *item = equipItemsById.value(faceIds.at(part)))
                        _m_equipRowItems[i] << item;
                    if (!tooltip.isEmpty())
                        tooltip += QStringLiteral("<br/><hr/>");
                    if (card != nullptr)
                        tooltip += QStringLiteral("<b>【%1】</b><br/>%2")
                                       .arg(Sanguosha->translate(card->objectName()), card->getDescription(m_player));
                }
            }
            tooltip = decorateTooltip(tooltip, i);
            painted = true;
        } else if (simulatedEquips.contains(i)) {
            const Card *card = simulatedEquips.value(i);
            const QString skillName = simulatedSkills.value(i);
            pixmap = _getEquipPixmap(card, i);
            const QString skillText = skillName.isEmpty()
                ? Sanguosha->translate(QStringLiteral("skill_transform"))
                : Sanguosha->translate(QStringLiteral("skill_transform_from")).arg(Sanguosha->translate(skillName));
            tooltip = QStringLiteral("<b>【%1】</b> (%2)<br/>%3")
                          .arg(Sanguosha->translate(card->objectName()), skillText, card->getDescription(m_player));
            tooltip = decorateTooltip(tooltip, i);
            opacity = 0.8f;
            painted = true;
        } else if (shadowCards.contains(i)) {
            const Card *card = shadowCards.value(i);
            pixmap = _getEquipPixmap(card, i);
            tooltip = decorateTooltip(card->getDescription(m_player), i);
            opacity = 0.92f;
            painted = true;
        } else if (plan.extraEmptyLogical.contains(i)) {
            pixmap = _paintEquipCaptionRow(i, areaName(plan.extraEmptyLogical.value(i)));
            tooltip = decorateTooltip(QString(), i);
            painted = true;
        }

        if (painted) {
            paintDistance(pixmap, skillDist, skills, tooltip, false, isDef || isOff);
            _m_equipRegions[i]->setPixmap(pixmap);
            _m_equipRegions[i]->setPos(_m_layout->m_equipAreas[i].topLeft());
            _m_equipRegions[i]->setToolTip(tooltip);
            _m_equipRegions[i]->setOpacity(opacity);
            _m_equipRegions[i]->show();
            continue;
        }

        if (m_player->hasEquipArea(i)) {
            if (skillDist != 0) {
                QPixmap emptyPixmap(_m_layout->m_equipAreas[i].size());
                emptyPixmap.fill(Qt::transparent);
                QString emptyTooltip;
                paintDistance(emptyPixmap, skillDist, skills, emptyTooltip, true, isDef || isOff);
                QStringList translated;
                foreach (const QString &skill, skills)
                    translated << Sanguosha->translate(skill);
                const QString value = (skillDist > 0 ? QStringLiteral("+") : QString()) + QString::number(skillDist);
                const QString distLine = isDef
                    ? Sanguosha->translate(QStringLiteral("UI_DIST_Defend")).arg(value)
                    : Sanguosha->translate(QStringLiteral("UI_DIST_Offense")).arg(value);
                emptyTooltip = QStringLiteral("<b>%1</b><br/>%2")
                                   .arg(Sanguosha->translate(QStringLiteral("UI_DIST_SkillTitle")).arg(translated.join(QStringLiteral("、"))),
                                        distLine);
                _m_equipRegions[i]->setPixmap(emptyPixmap);
                _m_equipRegions[i]->setPos(_m_layout->m_equipAreas[i].topLeft());
                _m_equipRegions[i]->setToolTip(emptyTooltip);
                _m_equipRegions[i]->setOpacity(1.0);
                _m_equipRegions[i]->show();
            } else {
                _m_equipRegions[i]->setOpacity(0);
            }
        } else {
            _m_equipRegions[i]->setPixmap(_getEquipPixmap(nullptr, i));
            _m_equipRegions[i]->setPos(_m_layout->m_equipAreas[i].topLeft());
            _m_equipRegions[i]->setToolTip(QString());
            _m_equipRegions[i]->setOpacity(1.0);
            _m_equipRegions[i]->show();
        }
    }

    _m_extraEquipCards.clear();
    QSet<CardItem *> kept;
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) {
        _m_equipCards[i] = _m_equipRowItems[i].isEmpty() ? nullptr : _m_equipRowItems[i].first();
        if (_m_equipCards[i] != nullptr)
            kept.insert(_m_equipCards[i]);
        for (int part = 1; part < _m_equipRowItems[i].size(); ++part) {
            _m_extraEquipCards << _m_equipRowItems[i].at(part);
            kept.insert(_m_equipRowItems[i].at(part));
        }
    }
    for (auto it = equipItemsById.constBegin(); it != equipItemsById.constEnd(); ++it) {
        if (it.value() != nullptr && !kept.contains(it.value()))
            _m_extraEquipCards << it.value();
    }

    foreach (Card *ec, simulatedEquips)
        ec->deleteLater();
}

void PlayerCardContainer::_paintGeneralIndicators()
{
    if (!ServerInfo.EnableHegemony && !_m_headShowLock && !_m_deputyShowLock)
        return;
    // Derive both overlays from the active skin's avatar rectangles so double
    // generals and responsive Dashboard/Photo layouts keep the correct slot.
    const QPixmap lock = _getPixmap(QSanRoomSkin::S_SKIN_KEY_DISABLE_SHOW_LOCK);
    for (int slot = 0; slot < 2; ++slot) {
        QRect area = slot == 0 ? _m_layout->m_avatarArea : _m_layout->m_smallAvatarArea;
        const QRect deputyArea = _m_layout->m_smallAvatarArea;
        // Photo paints the deputy over the right half of the primary image.
        // Keep the head indicator inside the remaining visible half.
        if (slot == 0 && m_player && m_player->getGeneral2()
            && deputyArea.left() > area.left() && area.intersects(deputyArea)
            && deputyArea.top() <= area.top() && deputyArea.bottom() >= area.bottom())
            area.setRight(deputyArea.left() - 1);
        QGraphicsPixmapItem *&lockItem = slot == 0 ? _m_headShowLock : _m_deputyShowLock;
        QGraphicsPixmapItem *&markItem = slot == 0 ? _m_headHiddenMark : _m_deputyHiddenMark;
        if (area.isEmpty()) {
            if (lockItem) lockItem->hide();
            if (markItem) markItem->hide();
            continue;
        }
        const int side = qMax(1, qMin(area.width(), area.height()) / 2);
        const QRect lockArea(area.center().x() - side / 2,
                             area.top() + area.height() / 5, side, side);
        _paintPixmap(lockItem, lockArea, lock, _getAvatarParent());
        lockItem->setAcceptedMouseButtons(Qt::NoButton);
        lockItem->setToolTip(tr("This general cannot be revealed."));

        const int height = qMax(1, qMin(26, area.height() / 5));
        // Dashboard docks grow upward from the avatar bottom and cover a
        // bottom-aligned mark. Keep the private indicator above that skill row.
        const QRect markArea(area.left(), area.top() + 2, area.width(), height);
        QPixmap mark(markArea.size());
        mark.fill(Qt::transparent);
        QPainter painter(&mark);
        painter.fillRect(mark.rect(), QColor(0, 0, 0, 170));
        QFont font = UiConfig.SmallFont;
        font.setPixelSize(qMax(1, height - 6));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor(200, 220, 255));
        painter.drawText(mark.rect(), Qt::AlignCenter, tr("Hidden"));
        painter.end();
        _paintPixmap(markItem, markArea, mark, _getAvatarParent());
        markItem->setAcceptedMouseButtons(Qt::NoButton);
        markItem->setToolTip(tr("No skill on this general is preshown."));
    }
    updateGeneralIndicators();
}

void PlayerCardContainer::updateGeneralIndicators()
{
    if (!_m_layout)
        return;
    const bool active = ServerInfo.EnableHegemony && m_player
        && m_player->getGeneral() && m_player->isAlive();
    for (int slot = 0; slot < 2; ++slot) {
        const bool head = slot == 0;
        const QRect area = head ? _m_layout->m_avatarArea : _m_layout->m_smallAvatarArea;
        const bool present = active && !area.isEmpty() && (head || m_player->getGeneral2());
        QGraphicsPixmapItem *lock = head ? _m_headShowLock : _m_deputyShowLock;
        QGraphicsPixmapItem *mark = head ? _m_headHiddenMark : _m_deputyHiddenMark;
        if (lock)
            lock->setVisible(present && !(head ? m_player->hasShownGeneral() : m_player->hasShownGeneral2())
                && !m_player->disableShow(head).isEmpty());
        // Preshow is private: never infer another player's opt-in from their
        // recipient-filtered skill list. Queued signals observe the full snapshot.
        if (mark)
            mark->setVisible(present && m_player == Self && m_player->isHidden(head));
    }
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
    updateGeneralIndicators();
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
            _m_equipRowItems[i].clear();
			_m_equipRegions[i]->setPixmap(QPixmap(_m_layout->m_equipAreas[i].size()));
			_m_equipRegions[i]->setOpacity(0);
			_m_equipRegions[i]->hide();
		}
        foreach (CardItem *equip, _m_extraEquipCards) {
            bool primary = false;
            for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) {
                if (_m_equipCards[i] == equip)
                    primary = true;
            }
            if (!primary)
                delete equip;
        }
        _m_extraEquipCards.clear();
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
        foreach (const QString &markName, m_player->getMarkNames()) {
            if (markName.startsWith("&"))
                updateMark(markName, m_player->getMark(markName));
        }
		_allZAdjusted = false;
	}

    refresh();
}

void PlayerCardContainer::_createRoleComboBox()
{
    _m_roleComboBox = new RoleComboBox(_getRoleComboBoxParent());
}

const ClientPlayer *PlayerCardContainer::getPlayer() const
{
    return m_player;
}

void PlayerCardContainer::setPlayer(ClientPlayer *player)
{
    ClientPlayer *previous = m_player;
    if (previous != nullptr && previous != player) {
        disconnect(previous, nullptr, this, nullptr);
        if (_m_roleComboBox != nullptr)
            disconnect(previous, nullptr, _m_roleComboBox, nullptr);
        disconnect(previous->getMarkDoc(), SIGNAL(contentsChanged()), this, SLOT(updateMarks()));
    }

    m_player = player;
    if (player) {
        foreach (QGraphicsProxyWidget *widget, _m_privatePiles.values())
            delete widget;
        _m_privatePiles.clear();

        connect(player, SIGNAL(general_changed()), this, SLOT(updateAvatar()));
        connect(player, SIGNAL(general2_changed()), this, SLOT(updateSmallAvatar()));
        connect(player, SIGNAL(state_changed()), this, SLOT(refresh()));
        connect(player, SIGNAL(phase_changed()), this, SLOT(updatePhase()));
        connect(player, SIGNAL(drank_changed()), this, SLOT(updateDrankState()));
        connect(player, SIGNAL(duanchang_invoked()), this, SLOT(updateDuanchang()));
        connect(player, SIGNAL(pile_changed(QString)), this, SLOT(updatePile(QString)));
        connect(player, SIGNAL(general_pile_changed(QString)), this, SLOT(updateGeneralPile(QString)));
        connect(player, SIGNAL(Mark_changed(QString, int)), this, SLOT(updateMark(QString, int)));
        connect(player, SIGNAL(role_changed(QString)), _m_roleComboBox, SLOT(fix(QString)));
        connect(player, SIGNAL(hp_changed()), this, SLOT(updateHp()));
        // Deliver after snapshot/upsert mutations finish, including parent/bind metadata.
        const auto tooltipConnection = Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection);
        const auto coalescedConnection = Qt::ConnectionType(Qt::AutoConnection | Qt::UniqueConnection);
        connect(player, &Player::skill_set_changed, this,
                &PlayerCardContainer::scheduleAvatarTooltipUpdate, coalescedConnection);
        connect(player, &Player::skill_state_changed, this,
                &PlayerCardContainer::scheduleAvatarTooltipUpdate, coalescedConnection);
        connect(player, &Player::gameplay_property_changed, this,
                &PlayerCardContainer::updateGeneralIndicators, tooltipConnection);
        connect(player, &Player::skill_set_changed, this,
                &PlayerCardContainer::updateGeneralIndicators, tooltipConnection);
        connect(player, &Player::skill_state_changed, this,
                &PlayerCardContainer::updateGeneralIndicators, tooltipConnection);

        QTextDocument *textDoc = m_player->getMarkDoc();
        Q_ASSERT(_m_markItem);
        _m_markItem->setDocument(textDoc);
        connect(textDoc, SIGNAL(contentsChanged()), this, SLOT(updateMarks()));

        foreach (const QString &markName, player->getMarkNames()) {
            if (markName.startsWith("&"))
                updateMark(markName, player->getMark(markName));
        }

        if (_m_roleComboBox != nullptr)
            _m_roleComboBox->fix(player->getRole());
    } else if (_m_roleComboBox != nullptr) {
        _m_roleComboBox->fix("unknown");
    }

    if (m_handcardWindow != nullptr && player != nullptr) {
        m_handcardWindow->setTitle(QString("%1%2").arg(player->getLogName()).arg(tr("'s Handcards")));
    }

    if (player != nullptr) {
        updateMarks();
        repaintAll(true);
    } else {
        updateAvatar();
        refresh();
    }
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
        start.translate(step * int(_m_judgeCards.size()));
        const Card *tc = trick->getCard();
        _paintPixmap(item, start, G_ROOM_SKIN.getCardJudgeIconPixmap(tc->objectName()));
        trick->setHomeOpacity(0);
        trick->setHomePos(start.center());
        QString toolTip = Sanguosha->getEngineCard(tc->getId())->getLogName();
		toolTip.append("<br/>").append(tc->getDescription(m_player));
		if(tc->isKindOf("Xumou")) toolTip = "";
        item->setToolTip(buildOracleTooltip(QString(), toolTip));
        _m_judgeCards.append(trick);
        _m_judgeIcons.append(item);
    }
}

namespace {
    // Equip-row names are drawn in clerical script instead of baked skin Equip images.
    QString equipRowFamily()
    {
        static QString chosen;
        if (!chosen.isEmpty())
            return chosen;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QStringList available = QFontDatabase::families();
#else
        QFontDatabase database;
        const QStringList available = database.families();
#endif
        const QStringList preferred = QStringList()
            << QStringLiteral("隶书")
            << QStringLiteral("LiSu")
            << QStringLiteral("STLiti")
            << QStringLiteral("SimSun")
            << QStringLiteral("微软雅黑")
            << QStringLiteral("Microsoft YaHei");
        foreach (const QString &name, preferred) {
            if (available.contains(name)) {
                chosen = name;
                return chosen;
            }
        }
        chosen = QStringLiteral("SimSun");
        return chosen;
    }

    QFont equipRowFont(int pixelSize)
    {
        QFont font;
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
        font.setFamilies(QStringList() << QStringLiteral("隶书")
                                       << QStringLiteral("LiSu")
                                       << QStringLiteral("STLiti")
                                       << QStringLiteral("SimSun")
                                       << QStringLiteral("微软雅黑"));
#else
        // Qt 5.6 has no setFamilies. Pick one installed face; clerical script is often missing on XP.
        font.setFamily(equipRowFamily());
#endif
        font.setPixelSize(qMax(8, pixelSize));
        font.setBold(true);
        font.setStyleStrategy(QFont::PreferAntialias);
        font.setHintingPreference(QFont::PreferNoHinting);
        return font;
    }

    // Tighten tracking, then stretch, only when the name does not fit. Short names keep their width.
    QFont equipNameFontFit(int pixelSize, const QString &text, qreal maxWidth)
    {
        QFont font = equipRowFont(pixelSize);
        if (maxWidth <= 0.0 || text.isEmpty())
            return font;
        if (QFontMetricsF(font).horizontalAdvance(text) <= maxWidth)
            return font;
        qreal spacing = 0.0;
        while (QFontMetricsF(font).horizontalAdvance(text) > maxWidth && spacing > -3.0) {
            spacing -= 0.5;
            font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
        }
        int stretch = 100;
        while (QFontMetricsF(font).horizontalAdvance(text) > maxWidth && stretch > 70) {
            stretch -= 2;
            font.setStretch(stretch);
        }
        return font;
    }

    // Weapon range uses Chinese numerals and falls back to digits outside 0-10.
    QString chineseNumeral(int n)
    {
        static const QString digits[] = {
            QStringLiteral("〇"), QStringLiteral("一"), QStringLiteral("二"),
            QStringLiteral("三"), QStringLiteral("四"), QStringLiteral("五"),
            QStringLiteral("六"), QStringLiteral("七"), QStringLiteral("八"),
            QStringLiteral("九"), QStringLiteral("十")
        };
        if (n >= 0 && n < 11)
            return digits[n];
        return QString::number(n);
    }

    // Crop the card illustration from the main art. On a 400x560 face that is
    // (63,121)-(338,332), with a 2px feathered edge.
    void paintEquipCardArtIcon(QPainter &painter, const QRectF &box, const QString &cardName, int supersample)
    {
        const QPixmap card = G_ROOM_SKIN.getCardMainPixmap(cardName, true);
        if (card.width() < 4 || card.height() < 4)
            return;
        const qreal cardWidth = card.width();
        const qreal cardHeight = card.height();
        QRectF source(cardWidth * (63.0 / 400.0), cardHeight * (121.0 / 560.0),
                      cardWidth * (275.0 / 400.0), cardHeight * (211.0 / 560.0));
        source &= QRectF(0, 0, cardWidth, cardHeight);

        const int width = qMax(1, qRound(box.width() * supersample));
        const int height = qMax(1, qRound(box.height() * supersample));
        QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter imagePainter(&image);
            imagePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            imagePainter.drawPixmap(QRectF(0, 0, width, height), card, source);
        }
        const qreal feather = 2.0 * supersample;
        if (feather >= 1.0) {
            for (int y = 0; y < height; ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < width; ++x) {
                    const qreal dist = qMin(qMin<qreal>(x, width - 1 - x), qMin<qreal>(y, height - 1 - y));
                    if (dist >= feather)
                        continue;
                    const qreal fade = dist / feather;
                    const QRgb color = line[x];
                    line[x] = qRgba(qRound(qRed(color) * fade), qRound(qGreen(color) * fade),
                                    qRound(qBlue(color) * fade), qRound(qAlpha(color) * fade));
                }
            }
        }
        painter.drawImage(box, image);
    }

    void paintEquipRowBg(QPainter &painter, const QRectF &row)
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(row, QColor(217, 212, 190));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0, 179), 1.0));
        painter.drawRect(row.adjusted(0.5, 0.5, -0.5, -0.5));
        painter.restore();
    }

    void boxBlurArgb(QImage &image, int radius, int passes)
    {
        const int width = image.width();
        const int height = image.height();
        if (radius < 1 || width < 2 || height < 2)
            return;
        for (int pass = 0; pass < passes; ++pass) {
            QImage temporary = image;
            for (int y = 0; y < height; ++y) {
                const QRgb *source = reinterpret_cast<const QRgb *>(temporary.constScanLine(y));
                QRgb *destination = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < width; ++x) {
                    int alpha = 0, red = 0, green = 0, blue = 0, count = 0;
                    for (int k = -radius; k <= radius; ++k) {
                        const int xx = x + k;
                        if (xx < 0 || xx >= width)
                            continue;
                        const QRgb color = source[xx];
                        alpha += qAlpha(color);
                        red += qRed(color);
                        green += qGreen(color);
                        blue += qBlue(color);
                        ++count;
                    }
                    destination[x] = qRgba(red / count, green / count, blue / count, alpha / count);
                }
            }
            temporary = image;
            for (int y = 0; y < height; ++y) {
                QRgb *destination = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < width; ++x) {
                    int alpha = 0, red = 0, green = 0, blue = 0, count = 0;
                    for (int k = -radius; k <= radius; ++k) {
                        const int yy = y + k;
                        if (yy < 0 || yy >= height)
                            continue;
                        const QRgb color = reinterpret_cast<const QRgb *>(temporary.constScanLine(yy))[x];
                        alpha += qAlpha(color);
                        red += qRed(color);
                        green += qGreen(color);
                        blue += qBlue(color);
                        ++count;
                    }
                    destination[x] = qRgba(red / count, green / count, blue / count, alpha / count);
                }
            }
        }
    }

    // A soft glow keeps the dark clerical text readable on the beige row.
    void drawGlowText(QPainter &painter, const QRectF &rect, int flags, const QString &text,
                      const QColor &textColor, const QColor &glowColor)
    {
        const qreal deviceRatio = qMax(1.0, painter.device()->devicePixelRatioF());
        const int pad = 8;
        const int width = qMax(1, qRound((rect.width() + 2 * pad) * deviceRatio));
        const int height = qMax(1, qRound((rect.height() + 2 * pad) * deviceRatio));
        QImage glow(width, height, QImage::Format_ARGB32_Premultiplied);
        glow.fill(Qt::transparent);
        {
            QPainter glowPainter(&glow);
            glowPainter.scale(deviceRatio, deviceRatio);
            glowPainter.setFont(painter.font());
            glowPainter.setPen(glowColor);
            glowPainter.drawText(QRectF(pad, pad, rect.width(), rect.height()), flags, text);
        }
        boxBlurArgb(glow, qMax(1, qRound(1.6 * deviceRatio)), 3);
        painter.save();
        const QRectF glowDestination(rect.x() - pad, rect.y() - pad, rect.width() + 2 * pad, rect.height() + 2 * pad);
        for (int i = 0; i < 2; ++i)
            painter.drawImage(glowDestination, glow);
        painter.setPen(textColor);
        painter.drawText(rect, flags, text);
        painter.restore();
    }

    void paintEquipNumber(QPainter &painter, const QRect &pointArea, const QPixmap &numberPixmap,
                          qreal heightScale, qreal xOffset, qreal yOffset)
    {
        if (numberPixmap.isNull() || numberPixmap.width() <= 1 || numberPixmap.height() <= 1)
            return;
        const qreal numberHeight = pointArea.height() * heightScale;
        const qreal numberWidth = numberPixmap.width() * numberHeight / numberPixmap.height();
        const QRectF numberRect(pointArea.x() + xOffset,
                                pointArea.y() + (pointArea.height() - numberHeight) / 2.0 + yOffset,
                                numberWidth, numberHeight);
        painter.drawPixmap(numberRect, numberPixmap, QRectF(numberPixmap.rect()));
    }
}

QPixmap PlayerCardContainer::_getEquipPixmap(const Card *equip, int slot, const QSize &forcedSize)
{
    // Vector equip row: beige fill, thin black edge, cropped card art, and a clerical name.
    // Dashboard and Photo both crop the card art. A Photo horse shows +1/-1 and omits the name.
    const int supersample = getUITextSupersample();
    const QSize naturalSize = _m_layout->m_equipAreas[slot].size();
    const QSize slotSize = forcedSize.isValid() ? forcedSize : naturalSize;
    QPixmap equipIcon(slotSize * supersample);
    equipIcon.fill(Qt::transparent);
    equipIcon.setDevicePixelRatio(supersample);

    const bool cardIsHorse = equip != nullptr && qobject_cast<const Horse *>(equip->getRealCard()) != nullptr;
    const bool subCell = forcedSize.isValid() && naturalSize.width() > forcedSize.width() + 2;
    const bool foreignOnHorse = equip != nullptr && (slot == 2 || slot == 3) && !cardIsHorse;
    if (equip != nullptr && (subCell || foreignOnHorse)) {
        QPainter painter(&equipIcon);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool isDashboard = getResourceKeyName() == QSanRoomSkin::S_SKIN_KEY_DASHBOARD;
        if (isDashboard) {
            painter.fillRect(QRectF(0, 0, slotSize.width(), slotSize.height()), QColor(0, 0, 0));
            const qreal inset = qMax(1.0, slotSize.height() * 0.08);
            painter.fillRect(QRectF(0, inset, qMax(1.0, slotSize.width() - inset), qMax(1.0, slotSize.height() - inset * 2.0)),
                             QColor(217, 212, 190));
        } else {
            paintEquipRowBg(painter, QRectF(0, 1, slotSize.width(), qMax(1.0, slotSize.height() - 2.0)));
        }
        const qreal iconHeight = qMax(1.0, slotSize.height() - 4.0);
        const qreal iconWidth = qMin(iconHeight * 275.0 / 211.0, slotSize.width() * 0.42);
        const QRectF iconBox(2, (slotSize.height() - iconHeight) / 2.0, iconWidth, iconHeight);
        paintEquipCardArtIcon(painter, iconBox, equip->objectName(), supersample);
        const qreal textLeft = iconBox.right() + 2.0;
        const QRectF textRect(textLeft, 0, qMax(8.0, slotSize.width() - textLeft - 1.0), slotSize.height());
        const QColor textColor(38, 30, 16);
        QString shown;
        if (cardIsHorse) {
            const Horse *horse = qobject_cast<const Horse *>(equip->getRealCard());
            const int correct = horse ? horse->getCorrect(m_player) : 0;
            shown = (correct > 0 ? QStringLiteral("+") : QString()) + QString::number(correct);
        } else if (const Weapon *weapon = qobject_cast<const Weapon *>(equip->getRealCard())) {
            shown = chineseNumeral(weapon->getRange(m_player)) + QStringLiteral(" ")
                + Sanguosha->translate(equip->objectName());
        } else {
            shown = Sanguosha->translate(equip->objectName());
        }
        painter.setPen(textColor);
        painter.setFont(equipNameFontFit(qMax(8, qRound(slotSize.height() * 0.7)), shown, textRect.width()));
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, shown);
        return equipIcon;
    }

    const bool isHorse = slot == 2 || slot == 3;
    const bool isDashboard = getResourceKeyName() == QSanRoomSkin::S_SKIN_KEY_DASHBOARD;
    QRect suitArea = isHorse ? _m_layout->m_horseSuitArea : _m_layout->m_equipSuitArea;
    QRect pointArea = isHorse ? _m_layout->m_horsePointArea : _m_layout->m_equipPointArea;

    auto paintAbolished = [&](QPainter &painter) {
        const QColor textColor(38, 30, 16);
        const QString abolished = Sanguosha->translate(QStringLiteral("EquipAreaX"));
        if (isDashboard) {
            const qreal scale = slotSize.width() / 298.0;
            const QRectF outer(0, 0, slotSize.width(), slotSize.height());
            painter.fillRect(outer, QColor(0, 0, 0));
            const QRectF inner = outer.adjusted(0.0, 4.0 * scale, -4.0 * scale, -4.0 * scale);
            painter.fillRect(inner, QColor(217, 212, 190));
            const int fontPx = qMax(8, qRound(inner.height() * 0.84) - 3);
            const QColor glowColor(255, 253, 228, 255);
            const QRectF rangeRect(60.0 * scale + 2.0, inner.top(), (100.0 - 60.0) * scale, inner.height());
            painter.setFont(equipRowFont(fontPx));
            drawGlowText(painter, rangeRect, Qt::AlignVCenter | Qt::AlignLeft,
                         QStringLiteral("×"), textColor, glowColor);
            const qreal nameLeft = 100.0 * scale;
            const QRectF nameRect(nameLeft, inner.top(),
                                  qMax<qreal>(8.0, pointArea.x() - 2.0 - nameLeft), inner.height());
            painter.setFont(equipNameFontFit(fontPx, abolished, nameRect.width()));
            drawGlowText(painter, nameRect, Qt::AlignVCenter | Qt::AlignLeft, abolished, textColor, glowColor);
            return;
        }

        const QRectF equipPanel = QRectF(_m_layout->m_equipImageArea).adjusted(0, 2, -3, -2);
        const QRectF panelRect = isHorse
            ? QRectF(0, equipPanel.top(), slotSize.width(), equipPanel.height())
            : equipPanel;
        paintEquipRowBg(painter, panelRect);
        const qreal iconHeight = panelRect.height();
        const qreal iconWidth = iconHeight * (275.0 / 211.0);
        const QRectF iconBox(panelRect.left(), panelRect.top(), iconWidth, iconHeight);
        const qreal prefixLeft = iconBox.right() + 3.0;
        const qreal prefixWidth = panelRect.height() * 1.3;
        const qreal nameLeft = prefixLeft + prefixWidth + 2.0;
        const QRectF prefixRect(prefixLeft, panelRect.top(), prefixWidth, panelRect.height());
        painter.setPen(textColor);
        painter.setFont(equipRowFont(qRound(panelRect.height() * 0.84)));
        painter.drawText(prefixRect, Qt::AlignVCenter | Qt::AlignHCenter, QStringLiteral("×"));
        const QRectF nameRect(nameLeft, panelRect.top(),
                              qMax<qreal>(8.0, suitArea.left() - 1.0 - nameLeft), panelRect.height());
        painter.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, abolished);
    };

    if (!equip) {
        QPainter painter(&equipIcon);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setRenderHint(QPainter::Antialiasing);
        paintAbolished(painter);
        return equipIcon;
    }

    QPainter painter(&equipIcon);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPixmap numberPixmap = G_ROOM_SKIN.getCardNumberPixmap(equip->getNumber(), !equip->isRed());
    if (isDashboard) {
        // Dashboard layout scales from a 298x50 baseline. A 149-wide slot uses a factor near 0.5.
        const qreal scale = slotSize.width() / 298.0;
        const QRectF outer(0, 0, slotSize.width(), slotSize.height());
        painter.fillRect(outer, QColor(0, 0, 0));
        const QRectF inner = outer.adjusted(0.0, 4.0 * scale, -4.0 * scale, -4.0 * scale);
        painter.fillRect(inner, QColor(217, 212, 190));

        const qreal iconHeight = inner.height();
        const qreal iconWidth = iconHeight * 275.0 / 211.0;
        const QRectF iconBox(16.0 * scale - 4.0, inner.top(), iconWidth, iconHeight);
        paintEquipCardArtIcon(painter, iconBox, equip->objectName(), supersample);

        const int fontPx = qMax(8, qRound(inner.height() * 0.84) - 3);
        const QColor textColor(38, 30, 16);
        const QColor glowColor(255, 253, 228, 255);
        const QRectF rangeRect(60.0 * scale + 2.0, inner.top(), (100.0 - 60.0) * scale, inner.height());
        if (const Horse *horse = qobject_cast<const Horse *>(equip->getRealCard())) {
            const int correct = horse->getCorrect(m_player);
            const QString distance = (correct > 0 ? QStringLiteral("+") : QString()) + QString::number(correct);
            auto horseFont = _m_layout->m_equipPointFontBlack;
            horseFont.m_spacing -= 4;
            horseFont.m_fontSize -= QSize(1, 1);
            painter.drawPixmap(rangeRect.topLeft() - QPointF(0, 1),
                               horseFont.paintTextToQPixmap(rangeRect.size().toSize(),
                                                            Qt::AlignVCenter | Qt::AlignLeft, distance));
        } else if (const Weapon *weapon = qobject_cast<const Weapon *>(equip->getRealCard())) {
            painter.setFont(equipRowFont(fontPx));
            drawGlowText(painter, rangeRect, Qt::AlignVCenter | Qt::AlignLeft,
                         chineseNumeral(weapon->getRange(m_player)), textColor, glowColor);
        }

        const qreal nameLeft = 100.0 * scale;
        const QRectF nameRect(nameLeft, inner.top(),
                              qMax<qreal>(8.0, pointArea.x() - 2.0 - nameLeft), inner.height());
        const QString equipName = Sanguosha->translate(equip->objectName());
        painter.setFont(equipNameFontFit(fontPx, equipName, nameRect.width()));
        drawGlowText(painter, nameRect, Qt::AlignVCenter | Qt::AlignLeft, equipName, textColor, glowColor);

        suitArea.translate(-2, -1);
        QRectF suitRect(suitArea);
        suitRect.adjust(suitRect.width() * 0.005, suitRect.height() * 0.005,
                        -suitRect.width() * 0.005, -suitRect.height() * 0.005);
        const QPixmap suitPixmap = G_ROOM_SKIN.getCardSuitPixmap(equip->getSuit());
        painter.drawPixmap(suitRect, suitPixmap, QRectF(suitPixmap.rect()));
        paintEquipNumber(painter, pointArea, numberPixmap, 0.891,
                         -6.0 + (isHorse ? 0.0 : -1.0), 3.0);
        return equipIcon;
    }

    // Photo uses a horizontal strip. Horse slots are half width and show only the distance.
    const QRectF equipPanel = QRectF(_m_layout->m_equipImageArea).adjusted(0, 2, -3, -2);
    const QRectF panelRect = isHorse
        ? QRectF(0, equipPanel.top(), slotSize.width(), equipPanel.height())
        : equipPanel;
    paintEquipRowBg(painter, panelRect);

    const qreal iconHeight = panelRect.height();
    const qreal iconWidth = iconHeight * (275.0 / 211.0);
    const QRectF iconBox(panelRect.left(), panelRect.top(), iconWidth, iconHeight);
    paintEquipCardArtIcon(painter, iconBox, equip->objectName(), supersample);

    const qreal prefixLeft = iconBox.right() + 3.0;
    const qreal prefixWidth = panelRect.height() * 1.3;
    const qreal nameLeft = prefixLeft + prefixWidth + 2.0;
    const qreal contentRight = suitArea.left() - 1.0;
    const QRectF prefixRect(prefixLeft, panelRect.top(), prefixWidth, panelRect.height());
    if (const Horse *horse = qobject_cast<const Horse *>(equip->getRealCard())) {
        const int correct = horse->getCorrect(m_player);
        const QString distance = (correct > 0 ? QStringLiteral("+") : QString()) + QString::number(correct);
        auto horseFont = _m_layout->m_equipPointFontBlack;
        horseFont.m_fontSize -= QSize(3, 3);
        horseFont.m_spacing -= 4;
        if (correct < 0)
            horseFont.m_spacing += 2;
        painter.drawPixmap(prefixRect.topLeft() - QPointF(0, 1),
                           horseFont.paintTextToQPixmap(prefixRect.size().toSize(),
                                                        Qt::AlignVCenter | Qt::AlignLeft, distance));
    } else if (const Weapon *weapon = qobject_cast<const Weapon *>(equip->getRealCard())) {
        painter.setPen(QColor(38, 30, 16));
        painter.setFont(equipRowFont(qRound(panelRect.height() * 0.84)));
        painter.drawText(prefixRect, Qt::AlignVCenter | Qt::AlignHCenter,
                         chineseNumeral(weapon->getRange(m_player)));
    }

    if (!isHorse) {
        const QRectF nameRect(nameLeft, panelRect.top(),
                              qMax<qreal>(8.0, contentRight - nameLeft), panelRect.height());
        painter.setPen(QColor(38, 30, 16));
        painter.setFont(equipRowFont(qRound(panelRect.height() * 0.84)));
        painter.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, Sanguosha->translate(equip->objectName()));
    }

    QRectF suitRect(suitArea);
    suitRect.adjust(suitRect.width() * 0.05, suitRect.height() * 0.05,
                    -suitRect.width() * 0.05, -suitRect.height() * 0.05);
    const QPixmap suitPixmap = G_ROOM_SKIN.getCardSuitPixmap(equip->getSuit());
    painter.drawPixmap(suitRect, suitPixmap, QRectF(suitPixmap.rect()));
    pointArea.translate(2, 0);
    paintEquipNumber(painter, pointArea, numberPixmap, 0.9504,
                     -2.0 + (isHorse ? 0.0 : -1.0), isHorse ? 3.0 : 2.0);
    return equipIcon;
}

QPixmap PlayerCardContainer::_paintEquipCaptionRow(int slot, const QString &label)
{
    // Same beige equip row as a real card. An extra empty area keeps its own name
    // and does not reuse the abolished "× 已废除" row.
    const int supersample = getUITextSupersample();
    const QSize slotSize = _m_layout->m_equipAreas[slot].size();
    QPixmap icon(slotSize * supersample);
    icon.fill(Qt::transparent);
    icon.setDevicePixelRatio(supersample);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const bool isDashboard = getResourceKeyName() == QSanRoomSkin::S_SKIN_KEY_DASHBOARD;
    const QColor textColor(38, 30, 16);
    if (isDashboard) {
        const qreal scale = slotSize.width() / 298.0;
        const QRectF outer(0, 0, slotSize.width(), slotSize.height());
        painter.fillRect(outer, QColor(0, 0, 0));
        const QRectF inner = outer.adjusted(0.0, 4.0 * scale, -4.0 * scale, -4.0 * scale);
        painter.fillRect(inner, QColor(217, 212, 190));
        const qreal nameLeft = 16.0 * scale;
        const QRectF nameRect(nameLeft, inner.top(), qMax(8.0, inner.right() - 4.0 - nameLeft), inner.height());
        const int fontPx = qMax(8, qRound(inner.height() * 0.84) - 3);
        painter.setFont(equipNameFontFit(fontPx, label, nameRect.width()));
        drawGlowText(painter, nameRect, Qt::AlignVCenter | Qt::AlignLeft, label, textColor, QColor(255, 253, 228));
        return icon;
    }

    const QRectF panel(0, 1, slotSize.width(), qMax(1.0, slotSize.height() - 3.0));
    paintEquipRowBg(painter, panel);
    const QRectF nameRect(4, panel.top(), qMax(8.0, panel.width() - 6.0), panel.height());
    painter.setPen(textColor);
    painter.setFont(equipNameFontFit(qMax(8, qRound(panel.height() * 0.84)), label, nameRect.width()));
    painter.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, label);
    return icon;
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

        int index = getEquipPrimarySlot(card, m_player);
        if (m_player != nullptr && card != nullptr) {
            QMap<int, int> display_slots = getEquipDisplaySlotsById(m_player, _m_layout);
            if (display_slots.contains(card->getEffectiveId()))
                index = display_slots.value(card->getEffectiveId());
        }

        connect(equip, SIGNAL(mark_changed()), this, SLOT(_onEquipSelectChanged()));

        QPointF equipAreaCenter = _m_layout->m_equipAreas[index].center();
        QPointF homePos = mapFromItem(_getEquipParent(), equipAreaCenter);
        equip->setHomePos(homePos);
        equip->setHomeOpacity(0.0);
        if (_m_equipCards[index] != nullptr && _m_equipCards[index] != equip)
            _m_extraEquipCards << equip;
        else
            _m_equipCards[index] = equip;
        QString description = card->getDescription(m_player);
        _m_equipRegions[index]->setToolTip(description);
		
        QPixmap pixmap = _getEquipPixmap(card, index);
        _m_equipRegions[index]->setPixmap(pixmap);
		
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
        _allZAdjusted = false;
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
    for (int index = _m_extraEquipCards.size() - 1; index >= 0; --index) {
        CardItem *equip = _m_extraEquipCards.at(index);
        if (equip == nullptr || equip->getCard() == nullptr
            || !cardIds.contains(equip->getCard()->getEffectiveId()))
            continue;
        equip->setHomeOpacity(0.0);
        result.append(equip);
        _m_extraEquipCards.removeAt(index);
    }
    _updateEquips();
    return result;
}

QList<CardItem *> PlayerCardContainer::equipCardItems() const
{
    QList<CardItem *> items;
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) {
        if (!_m_equipRowItems[i].isEmpty()) {
            foreach (CardItem *item, _m_equipRowItems[i]) {
                if (item != nullptr && !items.contains(item))
                    items << item;
            }
        } else if (_m_equipCards[i] != nullptr && !items.contains(_m_equipCards[i])) {
            items << _m_equipCards[i];
        }
    }
    foreach (CardItem *item, _m_extraEquipCards) {
        if (item != nullptr && !items.contains(item))
            items << item;
    }
    return items;
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
    // Keep the avatar visible without creating a repaint loop when animated effects are disabled.


    if (G_EFFECTS.animationsEnabled()) {
        G_EFFECTS.note(VisualEffectsPolicy::AnimationsStarted);
        _m_huashenAnimation->start();
    } else {
        G_EFFECTS.note(VisualEffectsPolicy::AnimationsSkipped);
        if (_m_huashenItem)
            _m_huashenItem->setOpacity(1.0);
    }

    _paintPixmap(_m_extraSkillBg, _m_layout->m_extraSkillArea, QSanRoomSkin::S_SKIN_KEY_EXTRA_SKILL_BG, _getAvatarParent());
    _m_layout->m_extraSkillFont.paintText(_m_extraSkillText, _m_layout->m_extraSkillTextArea, Qt::AlignCenter, Sanguosha->translate(skillName).left(2));
    if (!skillName.isEmpty()) {
        _m_extraSkillBg->show();
        _m_extraSkillText->show();
        {
			SafeLuaMutex &lua_mutex = Sanguosha->getLuaMutex();
				if (lua_mutex.tryLock(50)) {
					const Skill *s = Sanguosha->getSkill(skillName);
					_m_extraSkillBg->setToolTip(s ? s->getDescription(m_player) : QString());
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
		if (HeroSkinContainer::hasSkin(general))
			heroSKinBtn->show();
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

void PlayerCardContainer::scheduleAvatarTooltipUpdate()
{
    // Rebuild once for a batch of snapshots or skill events; queuing one call per emit would trigger dozens of rebuilds.
    if (m_avatarTooltipPending)
        return;
    m_avatarTooltipPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_avatarTooltipPending = false;
        updateAvatarTooltip();
    });
}

void PlayerCardContainer::updateAvatarTooltip()
{
    if (!m_player) {
        if (_m_avatarArea) _m_avatarArea->setToolTip(QString());
        if (_m_avatarIcon) _m_avatarIcon->setToolTip(QString());
        if (_m_smallAvatarArea) _m_smallAvatarArea->setToolTip(QString());
        if (_m_smallAvatarIcon) _m_smallAvatarIcon->setToolTip(QString());
        return;
    }
    if (m_player) {
        // One description pass feeds both avatars; the deputy card only changes focus.
        const GeneralInfoCard::AvatarCards cards
            = GeneralInfoCard::forPlayerAvatars(m_player, m_player->getSkillDescription(Self));
        _m_avatarArea->setToolTip(cards.head);
        if (_m_avatarIcon)
            _m_avatarIcon->setToolTip(cards.head);

        QString deputyTooltip = cards.deputy;
        if (deputyTooltip.isEmpty() && m_player->property("avatarIcon2").toString() != "")
            deputyTooltip = Sanguosha->translate(m_player->property("avatarIcon2").toString());

        _m_smallAvatarArea->setToolTip(deputyTooltip);
        if (_m_smallAvatarIcon)
            _m_smallAvatarIcon->setToolTip(deputyTooltip);
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
    m_handcardWindow = nullptr;
    m_handcardContainer = nullptr;
    m_totalText = nullptr;
    m_emptyText = nullptr;
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

    // Always recompute because avatar composition can change at runtime
    // (e.g. changeHero adds/removes secondary hero).
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
    _layUnder(_m_headShowLock);
    _layUnder(_m_deputyShowLock);
    _layUnder(_m_headHiddenMark);
    _layUnder(_m_deputyHiddenMark);
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
    for (int i = S_EQUIP_AREA_LENGTH - 1; i >= 0; i--)
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
    _layUnder(_m_avatarIcon);
    _layUnder(_m_dynamicBgItem);
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
        _m_equipRegions[i] = new EquipPixmapItem(_getEquipParent());
        _m_equipRegions[i]->setPixmap(QPixmap(_m_layout->m_equipAreas[i].size()));
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
    _paintPixmap(_m_deathIcon, deathArea, G_ROOM_SKIN.getPixmapFromFileName(m_player->getDeathPixmapPath()), _getDeathIconParent());
    _m_deathIcon->setZValue(11);
}

void PlayerCardContainer::killPlayer()
{
    bool hideDeathIcon = false;
    QString hideFrom = m_player->property("HideDeathIconFrom").toString();
    if (!hideFrom.isEmpty()) {
        QString myRole = Self->getRole();
        QString myName = Self->objectName();
        hideDeathIcon = hideFrom.contains(myRole) || hideFrom.contains(myName);
    }
    if (!hideDeathIcon)
        hideDeathIcon = m_player->property("HideDeathIcon").toBool();
    _m_roleComboBox->fix(hideDeathIcon ? "unknown" : m_player->getRole());
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
    } else if (hideDeathIcon)
        _m_deathIcon->hide();
    else
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

bool PlayerCardContainer::changeVotes(int delta)
{
    if (delta != 1 && delta != -1)
        return false;
    const int current = isSelected() ? qMax(1, _m_votesGot) : 0;
    const int next = current + delta;
    // Removing a vote remains possible after a target becomes unavailable.
    if (next < 0 || (delta > 0 && (!canBeSelected() || next > _m_maxVotes)))
        return false;
    const bool selectedBefore = isSelected();
    _m_votesGot = next;
    setSelected(next > 0);
    if (selectedBefore == isSelected())
        emit selected_changed();
    updateVotes();
    return true;
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
    // Fengbi also hides cached known cards and the viewer's total/back-card count.
    if (m_player->hasSkill("inovation_fengbi")) {
        if (m_handcardWindow) m_handcardWindow->hide();
        return;
    }

    if (m_handcardWindow) {
        updateHandcardViewer();
        m_handcardWindow->setZValue(20);
        m_handcardWindow->show();
        return;
    }

    QString title = QString("%1%2").arg(m_player->getLogName()).arg(tr("'s Handcards"));
    Window *handcardWindow = new Window(title, QSize(800, 500));
    m_handcardWindow = handcardWindow;

    connect(handcardWindow, &QObject::destroyed, this, [this]() {
        m_handcardWindow = nullptr;
        m_handcardContainer = nullptr;
        m_totalText = nullptr;
        m_emptyText = nullptr;
    });

    handcardWindow->setZValue(20);
    RoomSceneInstance->addItem(handcardWindow);

    QRectF sceneRect = RoomSceneInstance->sceneRect();
    QPointF centerPos = sceneRect.center();
    QRectF windowRect = handcardWindow->boundingRect();
    handcardWindow->setPos(centerPos.x() - windowRect.width() / 2,
                           centerPos.y() - windowRect.height() / 2);

    m_totalText = new QGraphicsTextItem(handcardWindow);
    m_totalText->setDefaultTextColor(Qt::white);
    m_totalText->setFont(UiConfig.SmallFont);
    m_totalText->setPos(25, 20);

    m_handcardContainer = new QGraphicsRectItem(0, 0, 770, 400, handcardWindow);
    m_handcardContainer->setPos(15, 50);
    m_handcardContainer->setFlag(QGraphicsItem::ItemClipsChildrenToShape);
    m_handcardContainer->setPen(Qt::NoPen);

    handcardWindow->addCloseButton(tr("Close"));
    handcardWindow->appear();

    updateHandcardViewer();
}

void PlayerCardContainer::updateHandcardViewer()
{
    if (!m_handcardWindow || !m_handcardContainer || !m_player) return;
    // Skill acquisition refreshes handcard UI; revoke an already-open viewer too.
    if (m_player != Self && m_player->hasSkill("inovation_fengbi")) {
        m_handcardWindow->hide();
        return;
    }

    m_handcardWindow->setTitle(QString("%1%2").arg(m_player->getLogName()).arg(tr("'s Handcards")));

    int total_handcard_num = m_player->getHandcardNum();

    if (m_totalText) {
        m_totalText->setPlainText(QString("%1%2").arg(tr("Total: ")).arg(total_handcard_num));
    }

    const bool canSeeExactHandcards = Self != nullptr && (Self == m_player || Self->canSeeHandcard(m_player));

    QList<const Card *> known_cards;
    if (canSeeExactHandcards) {
        QList<const Card *> handCards = m_player->getHandcards();
        if (!handCards.isEmpty()) {
            foreach (const Card *card, handCards) {
                if (card) known_cards << card;
            }
        } else {
            known_cards = m_player->getKnownCards();
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

    QList<QGraphicsItem *> existingItems = m_handcardContainer->childItems();
    int existingCount = existingItems.size();
    int itemIndex = 0;

    auto getOrMakeItem = [&]() -> QGraphicsPixmapItem* {
        if (itemIndex < existingCount) {
            QGraphicsPixmapItem* item = qgraphicsitem_cast<QGraphicsPixmapItem*>(existingItems[itemIndex]);
            item->show();
            itemIndex++;
            return item;
        } else {
            QGraphicsPixmapItem* newItem = new QGraphicsPixmapItem(m_handcardContainer);
            itemIndex++;
            return newItem;
        }
    };

    foreach (const Card *card, sorted_known) {
        QGraphicsPixmapItem *cardItem = getOrMakeItem();

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
        cardItem->setToolTip(buildOracleTooltip(QString(), card->getDescription(m_player)));

        count++;
        x += cardWidth + horizontalSpacing;
        if (count % cardsPerRow == 0) {
            x = startX;
            y += cardHeight + verticalSpacing;
        }
    }

    int unknown_count = qMax(0, total_handcard_num - known_cards.size());
    QPixmap unknownPixmap = G_ROOM_SKIN.getCardMainPixmap("unknown").scaled(cardWidth, cardHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    for (int i = 0; i < unknown_count; i++) {
        QGraphicsPixmapItem *cardItem = getOrMakeItem();
        cardItem->setPixmap(unknownPixmap);
        cardItem->setPos(x, y);
        cardItem->setToolTip(buildOracleTooltip(QString(), tr("Unknown Card")));

        count++;
        x += cardWidth + horizontalSpacing;
        if (count % cardsPerRow == 0) {
            x = startX;
            y += cardHeight + verticalSpacing;
        }
    }

    while (itemIndex < existingCount) {
        existingItems[itemIndex]->hide();
        itemIndex++;
    }

    if (total_handcard_num == 0) {
        if (!m_emptyText) {
            m_emptyText = new QGraphicsTextItem(m_handcardWindow);
            m_emptyText->setDefaultTextColor(Qt::white);
            QFont font = UiConfig.BigFont;
            font.setPointSize(14);
            m_emptyText->setFont(font);
            m_emptyText->setPos(250, 160);
        }
        m_emptyText->show();
    } else if (m_emptyText) {
        m_emptyText->hide();
    }
}


//  Dynamic skin background

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
        _m_dynamicBgItem->setFlag(QGraphicsItem::ItemStacksBehindParent);
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

