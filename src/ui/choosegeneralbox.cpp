/*
 * Copyright (c) 2013-2015 - Mogara
 * Ported from QSanguosha-For-Hegemony-xxyheaven/src/ui/choosegeneralbox.cpp.
 * This file is part of QSanguosha-Hegemony, distributed under the GNU General
 * Public License, version 3 or later, WITHOUT ANY WARRANTY. See LICENSE.
 */
#include "choosegeneralbox.h"

#include "button.h"
#include "choosegeneraldialog.h"
#include "engine.h"
#include "general.h"
#include "server-info.h"
#include "skin-bank.h"
#include "timed-progressbar.h"
#include "client.h"
#include "effects/effects-policy.h"

#include <QAbstractAnimation>
#include <QApplication>
#include <QFontMetrics>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <algorithm>

GeneralCardItem::GeneralCardItem(const QString &generalName)
    : CardItem(generalName)
{
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemIsFocusable);
    setFlag(ItemIsMovable);
    // The box owns settling: CardItem's release handler must not restart it.
    setAutoBack(false);
}

void GeneralCardItem::moveToHome(const QPointF &home)
{
    const bool sameTarget = homePos() == home;
    setHomePos(home);
    if (!G_EFFECTS.animationsEnabled()) {
        goBack(false, false);
        return;
    }
    // Leave stationary cards and an already-running move to this seat alone.
    if (sameTarget && m_currentAnimation && m_currentAnimation->state() == QAbstractAnimation::Running)
        return;
    if (pos() == home) {
        if (m_currentAnimation) m_currentAnimation->stop();
        return;
    }
    getGoBackAnimation(false, false, 140)->start();
}

void GeneralCardItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (!m_choiceEnabled) return;
    // Grabbing a moving card takes control immediately, without snapping it home.
    if (m_currentAnimation) m_currentAnimation->stop();
    m_dragging = false;
    CardItem::mousePressEvent(event);
}

void GeneralCardItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!m_choiceEnabled) return;
    // Use the platform's screen-pixel threshold, independent of the box scale.
    if (!m_dragging && (event->screenPos() - event->buttonDownScreenPos(Qt::LeftButton)).manhattanLength()
        < QApplication::startDragDistance()) return;
    m_dragging = true;
    cancelTouchPreview();
    setPos(mapToParent(event->pos()) - transform().map(event->buttonDownPos(Qt::LeftButton)));
    event->accept();
}

void GeneralCardItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (!m_choiceEnabled) return;
    if (m_dragging) {
        m_dragging = false;
        cancelTouchPreview();
        emit released();
        event->accept();
    } else CardItem::mouseReleaseEvent(event); // Keep click and touch-preview handling.
    moveToHome(homePos());
}

void GeneralCardItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const QRect rect = G_COMMON_LAYOUT.m_cardMainArea;
    if (!m_choiceEnabled) painter->setOpacity(0.4);
    painter->drawPixmap(rect, G_ROOM_SKIN.getGeneralPixmap(objectName(), QSanRoomSkin::S_GENERAL_ICON_SIZE_CARD));
    if (m_hasCompanion) {
        // Use translated text instead of the donor's fixed Chinese bitmap label.
        const QRect label(rect.left(), rect.bottom() - 22, rect.width(), 23);
        painter->fillRect(label, QColor(25, 20, 10, 225));
        painter->setPen(QColor(255, 220, 120));
        QFont font = UiConfig.SmallFont;
        font.setPixelSize(13);
        painter->setFont(font);
        painter->drawText(label, Qt::AlignCenter, tr("Companion"));
    }
    if (hasFocus()) {
        painter->setOpacity(1);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 220, 120), 3));
        painter->drawRect(rect.adjusted(1, 1, -1, -1));
    }
    painter->restore();
}

void GeneralCardItem::setChoiceEnabled(bool enabled)
{
    if (m_choiceEnabled == enabled) return;
    m_choiceEnabled = enabled;
    setFrozen(!enabled);
    setFlag(ItemIsMovable, enabled);
    update();
}

void GeneralCardItem::showCompanion()
{
    if (m_hasCompanion) return;
    m_hasCompanion = true;
    update();
}

void GeneralCardItem::hideCompanion()
{
    if (!m_hasCompanion) return;
    m_hasCompanion = false;
    update();
}

ChooseGeneralBox::ChooseGeneralBox()
{
    // Button's scale overload shrinks its surface only; measure its actual font.
    const QString label = tr("Confirm pair");
    const QFontMetrics metrics(UiConfig.SmallFont);
    const int labelWidth = qMax(metrics.horizontalAdvance(label), metrics.boundingRect(label).width());
    confirm = new Button(label, QSizeF(qMax(144, labelWidth + 32), qMax(36, metrics.height() + 12)));
    setFlag(ItemIsMovable, false);
    setFlag(ItemIsFocusable);
    setObjectName(QStringLiteral("hegemonyChooseGeneralBox"));
    confirm->setParentItem(this);
    confirm->setFlag(ItemIsFocusable);
    connect(confirm, &Button::clicked, this, &ChooseGeneralBox::reply);
    for (int slot = 0; slot < 2; ++slot) {
        const QString text = slot == 0 ? tr("Free choose head...") : tr("Free choose deputy...");
        auto *button = new Button(text, QSizeF(qMax(144, metrics.horizontalAdvance(text) + 32),
                                               qMax(36, metrics.height() + 12)));
        freeChooseButtons[slot] = button;
        button->setParentItem(this);
        button->setFlag(ItemIsFocusable);
        connect(button, &Button::clicked, this, [this, slot]() { freeChoose(slot); });
        button->hide();
    }
    hide();
}

qreal ChooseGeneralBox::splitLineY() const
{
    qreal y = top_blank_width + G_COMMON_LAYOUT.m_cardNormalHeight + card_bottom_to_split_line;
    if (general_number > 5) y += card_to_center_line + G_COMMON_LAYOUT.m_cardNormalHeight;
    return y;
}

QPointF ChooseGeneralBox::seatPosition(int slot) const
{
    const qreal offset = card_to_center_line + G_COMMON_LAYOUT.m_cardNormalWidth / 2.0;
    return QPointF(boundingRect().center().x() + (slot == 0 ? -offset : offset),
        splitLineY() + split_line_to_card_seat + G_COMMON_LAYOUT.m_cardNormalHeight / 2.0);
}

QRectF ChooseGeneralBox::boundingRect() const
{
    const int firstRow = qMax(2, general_number < 6 ? general_number : (general_number + 1) / 2);
    const qreal controlsWidth = m_freeChooseEnabled
        ? freeChooseButtons[0]->boundingRect().width() + freeChooseButtons[1]->boundingRect().width() + 12
        : confirm->boundingRect().width();
    const qreal width = qMax(firstRow * G_COMMON_LAYOUT.m_cardNormalWidth
        + (firstRow - 1) * card_to_center_line + left_blank_width * 2.0,
        qMax(controlsWidth, confirm->boundingRect().width()) + left_blank_width * 2);
    return QRectF(0, 0, width, splitLineY() + split_line_to_card_seat
        + G_COMMON_LAYOUT.m_cardNormalHeight
        + qMax(qreal(bottom_blank_width), confirm->boundingRect().height() + 74)
        + (m_freeChooseEnabled ? freeChooseButtons[0]->boundingRect().height() + 12 : 0));
}

void ChooseGeneralBox::paintLayout(QPainter *painter)
{
    painter->save();
    painter->setPen(QColor(150, 140, 120));
    painter->drawLine(QPointF(left_blank_width, splitLineY()),
                      QPointF(boundingRect().right() - left_blank_width, splitLineY()));
    const QSizeF size(G_COMMON_LAYOUT.m_cardNormalWidth + 4, G_COMMON_LAYOUT.m_cardNormalHeight + 4);
    const QPixmap seat = G_ROOM_SKIN.getPixmap(QSanRoomSkin::S_SKIN_KEY_CHOOSE_GENERAL_BOX_DEST_SEAT);
    for (int slot = 0; slot < 2; ++slot) {
        const QRect rect = QRectF(seatPosition(slot) - QPointF(size.width() / 2, size.height() / 2), size).toRect();
        if (seat.isNull()) {
            painter->setBrush(QColor(20, 20, 20, 150));
            painter->drawRoundedRect(rect, 4, 4);
        } else painter->drawPixmap(rect, seat);
        G_COMMON_LAYOUT.m_chooseGeneralBoxDestSeatFont.paintText(painter, rect, Qt::AlignCenter,
            slot == 0 ? tr("Head general") : tr("Deputy general"));
    }
    painter->setPen(Qt::white);
    QFont helpFont = UiConfig.SmallFont;
    helpFont.setPixelSize(12);
    painter->setFont(helpFont);
    painter->drawText(QRectF(10, boundingRect().bottom() - 64, boundingRect().width() - 20, 36),
        Qt::AlignCenter | Qt::TextWordWrap, tr("Arrows/Tab: focus; Enter/Space: activate; Esc/Backspace: undo; X: swap"));
    painter->restore();
}

void ChooseGeneralBox::chooseGeneral(const QStringList &candidates, const QStringList &legalPairs)
{
    clear();
    if (legalPairs.isEmpty()) return;
    m_legalPairs = legalPairs;
    QStringList generals;
    for (const QString &name : candidates)
        if (Sanguosha->getGeneral(name) && !generals.contains(name)) generals << name;
    const QStringList kingdoms = Sanguosha->getKingdoms();
    std::stable_sort(generals.begin(), generals.end(), [&kingdoms](const QString &a, const QString &b) {
        const General *first = Sanguosha->getGeneral(a), *second = Sanguosha->getGeneral(b);
        if (first->isDoubleKingdoms() != second->isDoubleKingdoms()) return !first->isDoubleKingdoms();
        return kingdoms.indexOf(first->getKingdom()) < kingdoms.indexOf(second->getKingdom());
    });
    prepareGeometryChange();
    m_freeChooseEnabled = ServerInfo.FreeChoose;
    general_number = generals.size();
    title = tr("Choose head and deputy generals");
    const int firstRow = general_number < 6 ? general_number : (general_number + 1) / 2;
    const int cardWidth = G_COMMON_LAYOUT.m_cardNormalWidth;
    const int cardHeight = G_COMMON_LAYOUT.m_cardNormalHeight;
    for (int i = 0; i < general_number; ++i) {
        auto *item = new GeneralCardItem(generals.at(i));
        item->setParentItem(this);
        item->setZValue(general_number - i);
        item->setProperty("source", generals.at(i));
        const bool secondRow = i >= firstRow;
        const int rowSize = secondRow ? general_number - firstRow : firstRow;
        const qreal rowWidth = rowSize * cardWidth + (rowSize - 1) * card_to_center_line;
        const QPointF home((boundingRect().width() - rowWidth) / 2 + cardWidth / 2.0
            + (cardWidth + card_to_center_line) * (secondRow ? i - firstRow : i),
            top_blank_width + cardHeight / 2.0 + (secondRow ? cardHeight + card_to_center_line : 0));
        item->setData(S_DATA_INITIAL_HOME_POS, home);
        item->setHomePos(home);
        item->setPos(home);
        connect(item, &CardItem::clicked, this, &ChooseGeneralBox::_onItemClicked);
        connect(item, &CardItem::released, this, &ChooseGeneralBox::_adjust);
        items << item;
    }
    confirm->setPos(boundingRect().center().x() - confirm->boundingRect().width() / 2,
                    boundingRect().bottom() - 68 - confirm->boundingRect().height());
    const qreal freeWidth = freeChooseButtons[0]->boundingRect().width()
        + freeChooseButtons[1]->boundingRect().width() + 12;
    for (int slot = 0; slot < 2; ++slot) {
        freeChooseButtons[slot]->setVisible(m_freeChooseEnabled);
        freeChooseButtons[slot]->setPos((boundingRect().width() - freeWidth) / 2
            + (slot == 0 ? 0 : freeChooseButtons[0]->boundingRect().width() + 12),
            confirm->y() - freeChooseButtons[slot]->boundingRect().height() - 12);
    }
    m_active = true;
    adjustItems();
    if (ServerInfo.OperationTimeout > 0) {
        progress_bar = new QSanCommandProgressBar;
        progress_bar->setFixedSize(qMax(1, int(boundingRect().width()) - 20), 12);
        progress_bar->setTimerEnabled(true);
        progress_bar->setCountdown(QSanProtocol::S_COMMAND_CHOOSE_GENERAL);
        progress_bar_item = new QGraphicsProxyWidget(this);
        progress_bar_item->setWidget(progress_bar);
        progress_bar_item->setPos(10, boundingRect().bottom() - 20);
        connect(progress_bar, &QSanCommandProgressBar::timedOut, this, &ChooseGeneralBox::reply);
        progress_bar->show();
    }
    show();
    setFocus();
    handleChooseKey(Qt::Key_Tab);
}

void ChooseGeneralBox::fitToTable(const QPointF &center, const QRectF &available)
{
    const QRectF bounds = boundingRect();
    const qreal scale = qMin(qreal(1), qMin(qMax(qreal(1), available.width() - 24) / bounds.width(),
                                          qMax(qreal(1), available.height() - 24) / bounds.height()));
    setScale(scale);
    const QPointF half(bounds.width() * scale / 2, bounds.height() * scale / 2);
    const QPointF fitted(qBound(available.left() + half.x(), center.x(), available.right() - half.x()),
                         qBound(available.top() + half.y(), center.y(), available.bottom() - half.y()));
    setPos(fitted - half);
}

bool ChooseGeneralBox::canPair(const QString &head, const QString &deputy) const
{
    if (m_freeChooseEnabled) {
        const General *first = Sanguosha->getGeneral(head);
        return first && first->canPairForHegemony(Sanguosha->getGeneral(deputy));
    }
    return m_legalPairs.contains(head + "+" + deputy);
}

bool ChooseGeneralBox::canBeHead(const QString &name) const
{
    if (m_freeChooseEnabled) {
        const General *general = Sanguosha->getGeneral(name);
        return general && general->isHegemonySelectable();
    }
    for (const QString &pair : m_legalPairs)
        if (pair.section('+', 0, 0) == name) return true;
    return false;
}

QString ChooseGeneralBox::selectedPair() const
{
    return selected.size() == 2 ? selected.first()->objectName() + "+" + selected.last()->objectName() : QString();
}

QStringList ChooseGeneralBox::candidateNames() const
{
    QStringList names;
    for (GeneralCardItem *item : items) names << item->objectName();
    return names;
}

QStringList ChooseGeneralBox::selectedGenerals() const
{
    QStringList names;
    for (GeneralCardItem *item : selected) names << item->objectName();
    return names;
}

bool ChooseGeneralBox::choiceEnabled(const QString &name) const
{
    for (GeneralCardItem *item : items)
        if (item->objectName() == name) return m_active && item->choiceEnabled();
    return false;
}

void ChooseGeneralBox::selectGeneral(const QString &name, bool select)
{
    for (GeneralCardItem *item : items)
        if (item->objectName() == name && selected.contains(item) != select) { toggleItem(item); return; }
}

void ChooseGeneralBox::resetSelection()
{
    if (!m_active) return;
    selected.clear();
    adjustItems();
}

bool ChooseGeneralBox::canConfirm() const
{
    return m_active && selected.size() == 2
        && canPair(selected.first()->objectName(), selected.last()->objectName());
}

void ChooseGeneralBox::freeChoose(int slot)
{
    if (!freeChooseEnabled() || m_freeChooseDialog || (slot != 0 && slot != 1)
        || (slot == 1 && selected.isEmpty())) return;
    QStringList allowed;
    for (const General *general : Sanguosha->findChildren<const General *>()) {
        const QString name = general->objectName();
        if (slot == 0 ? canBeHead(name) : canPair(selected.first()->objectName(), name)) allowed << name;
    }
    if (allowed.isEmpty()) return; // An empty allowlist means unrestricted in the legacy picker.
    QWidget *parent = scene() && !scene()->views().isEmpty() ? scene()->views().first() : nullptr;
    auto *dialog = new FreeChooseDialog(QString(), parent, FreeChooseDialog::Exclusive, allowed);
    dialog->setWindowTitle(slot == 0 ? tr("Free choose head...") : tr("Free choose deputy..."));
    m_freeChooseDialog = dialog;
    connect(dialog, &FreeChooseDialog::general_chosen, this, [this, dialog, slot](const QString &name) {
        // A replaced/timed-out request must not receive the old picker's result.
        if (m_freeChooseDialog == dialog && m_active) setFreeGeneral(slot, name);
    });
    connect(dialog, &QDialog::finished, this, [this, dialog, slot]() {
        if (m_freeChooseDialog == dialog) {
            m_freeChooseDialog = nullptr;
            if (m_active) {
                if (scene() && !scene()->views().isEmpty()) scene()->views().first()->setFocus();
                focusChoice(freeChooseButtons[slot]);
            }
        }
        dialog->deleteLater();
    });
    dialog->open();
}

void ChooseGeneralBox::setFreeGeneral(int slot, const QString &name)
{
    if (!freeChooseEnabled() || (slot == 0 ? !canBeHead(name)
        : selected.isEmpty() || !canPair(selected.first()->objectName(), name))) return;
    GeneralCardItem *choice = nullptr;
    for (GeneralCardItem *item : items) if (item->objectName() == name) { choice = item; break; }
    if (!choice) {
        choice = new GeneralCardItem(name);
        choice->setParentItem(this);
        choice->setProperty("free_choice", true);
        choice->setHomePos(seatPosition(slot));
        choice->setPos(seatPosition(slot));
        connect(choice, &CardItem::clicked, this, &ChooseGeneralBox::_onItemClicked);
        connect(choice, &CardItem::released, this, &ChooseGeneralBox::_adjust);
        items << choice;
    }
    if (slot == 0) {
        GeneralCardItem *deputy = selected.size() == 2 ? selected.last() : nullptr;
        selected = {choice};
        if (deputy && canPair(name, deputy->objectName())) selected << deputy;
    } else {
        selected = {selected.first(), choice};
    }
    adjustItems(); // Choosing a free general edits the draft; Confirm still commits both seats.
}

void ChooseGeneralBox::removeSelection(GeneralCardItem *item)
{
    selected.removeOne(item);
    // A deputy-only card cannot become the head when the former head is removed.
    if (selected.size() == 1 && !canBeHead(selected.first()->objectName())) selected.clear();
}

void ChooseGeneralBox::toggleItem(GeneralCardItem *item)
{
    if (!m_active || !item || !item->choiceEnabled()) return;
    if (selected.contains(item)) removeSelection(item);
    else if (selected.isEmpty() && canBeHead(item->objectName())) selected << item;
    else if (selected.size() == 1 && canPair(selected.first()->objectName(), item->objectName())) selected << item;
    adjustItems();
}

void ChooseGeneralBox::_onItemClicked()
{
    toggleItem(qobject_cast<GeneralCardItem *>(sender()));
}

void ChooseGeneralBox::_adjust()
{
    auto *item = qobject_cast<GeneralCardItem *>(sender());
    if (!m_active || !item || !item->choiceEnabled()) return;
    if (selected.contains(item) && item->y() <= splitLineY()) removeSelection(item);
    else if (selected.size() == 2 && canPair(selected.last()->objectName(), selected.first()->objectName())
        && ((selected.first() == item && item->x() > boundingRect().center().x())
            || (selected.last() == item && item->x() < boundingRect().center().x())))
        selected.swapItemsAt(0, 1);
    else if (!selected.contains(item) && item->y() > splitLineY()) {
        if (selected.isEmpty() && canBeHead(item->objectName())) selected << item;
        else if (selected.size() == 1 && canPair(selected.first()->objectName(), item->objectName())) selected << item;
    }
    // Invalid drops return to their previous home without changing the draft.
    adjustItems();
}

void ChooseGeneralBox::_initializeItems()
{
    for (GeneralCardItem *item : items) item->setChoiceEnabled(canBeHead(item->objectName()));
    confirm->setEnabled(false);
}

void ChooseGeneralBox::adjustItems()
{
    // Only the two current free choices need graphics items; do not expand the
    // dealt pool into thousands of portraits or keep discarded choices alive.
    for (GeneralCardItem *item : QList<GeneralCardItem *>(items)) {
        if (!item->property("free_choice").toBool() || selected.contains(item)) continue;
        items.removeOne(item);
        item->setChoiceEnabled(false);
        disconnect(item, nullptr, this, nullptr);
        item->hide();
        item->deleteLater();
    }
    bool headHasCompanion = false;
    if (selected.isEmpty()) _initializeItems();
    for (GeneralCardItem *item : items) {
        const int slot = selected.indexOf(item);
        item->moveToHome(slot >= 0 ? seatPosition(slot) : item->data(S_DATA_INITIAL_HOME_POS).toPointF());
        if (!selected.isEmpty())
            item->setChoiceEnabled(slot >= 0 || (selected.size() == 1
                && canPair(selected.first()->objectName(), item->objectName())));
        item->hideCompanion();
        if (selected.isEmpty()) {
            for (GeneralCardItem *other : items) {
                if (canPair(item->objectName(), other->objectName())
                    && Sanguosha->getGeneral(item->objectName())->isCompanionWith(other->objectName()))
                    item->showCompanion();
            }
        } else if (selected.size() == 1 && item != selected.first() && item->choiceEnabled()
            && Sanguosha->getGeneral(selected.first()->objectName())->isCompanionWith(item->objectName())) {
            item->showCompanion();
            headHasCompanion = true;
        }
    }
    if (headHasCompanion) selected.first()->showCompanion();
    if (selected.size() == 2 && Sanguosha->getGeneral(selected.first()->objectName())->isCompanionWith(selected.last()->objectName()))
        for (GeneralCardItem *item : selected) item->showCompanion();
    confirm->setEnabled(canConfirm());
    freeChooseButtons[0]->setEnabled(freeChooseEnabled());
    freeChooseButtons[1]->setEnabled(freeChooseEnabled() && !selected.isEmpty());
    update();
    emit draftChanged();
}

void ChooseGeneralBox::focusChoice(QGraphicsItem *item)
{
    if (item) item->setFocus(Qt::OtherFocusReason);
    confirm->setGlow(confirm->hasFocus() ? 5 : 0);
    for (Button *button : freeChooseButtons) button->setGlow(button->hasFocus() ? 5 : 0);
    for (GeneralCardItem *card : items) card->update();
}

bool ChooseGeneralBox::handleChooseKey(int key)
{
    if (!m_active || !isVisible()) return false;
    if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
        // Activating a card remains reversible even after both seats are filled.
        // Only explicit focus on the confirmation button submits the pair.
        if (confirm->hasFocus() && confirm->isEnabled()) reply();
        else if (freeChooseButtons[0]->hasFocus()) freeChoose(0);
        else if (freeChooseButtons[1]->hasFocus()) freeChoose(1);
        else for (GeneralCardItem *item : items) if (item->hasFocus()) { toggleItem(item); break; }
    } else if (key == Qt::Key_X) {
        if (selected.size() == 2 && canPair(selected.last()->objectName(), selected.first()->objectName())) {
            selected.swapItemsAt(0, 1);
            adjustItems();
        }
    } else if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
        if (!selected.isEmpty()) {
            GeneralCardItem *removed = selected.last();
            for (GeneralCardItem *item : selected) if (item->hasFocus()) { removed = item; break; }
            removeSelection(removed);
            adjustItems();
            // Disabling Confirm can drop scene focus. Return to the pool, skipping
            // a removed deputy-only card if it cannot be chosen as the new head.
            GeneralCardItem *next = removed->choiceEnabled() && !selected.contains(removed) ? removed : nullptr;
            if (!next) for (GeneralCardItem *item : items)
                if (item->choiceEnabled() && !selected.contains(item)) { next = item; break; }
            if (!next && !selected.isEmpty()) next = selected.first();
            focusChoice(next);
        }
    } else if (key == Qt::Key_Tab || key == Qt::Key_Backtab || key == Qt::Key_Left
               || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) {
        QList<QGraphicsItem *> focusable;
        // Traverse the visible pool, then the head/deputy seats and Confirm.
        for (GeneralCardItem *item : items)
            if (item->choiceEnabled() && !selected.contains(item)) focusable << item;
        for (GeneralCardItem *item : selected) focusable << item;
        for (Button *button : freeChooseButtons)
            if (button->isVisible() && button->isEnabled()) focusable << button;
        if (confirm->isEnabled()) focusable << confirm;
        if (!focusable.isEmpty()) {
            int index = -1;
            for (int i = 0; i < focusable.size(); ++i) if (focusable.at(i)->hasFocus()) index = i;
            const bool back = key == Qt::Key_Backtab || key == Qt::Key_Left || key == Qt::Key_Up;
            index = index < 0 ? (back ? focusable.size() - 1 : 0)
                : (index + (back ? -1 : 1) + focusable.size()) % focusable.size();
            focusChoice(focusable.at(index));
        }
    } else return false;
    return true;
}

void ChooseGeneralBox::reply()
{
    if (!m_active || m_legalPairs.isEmpty()) return;
    const QString draft = selectedPair();
    const QString answer = canConfirm() ? draft : m_legalPairs.first();
    clear();
    ClientInstance->onPlayerChooseGeneral(answer);
}

void ChooseGeneralBox::clear()
{
    m_active = false;
    if (m_freeChooseDialog) {
        QDialog *dialog = m_freeChooseDialog;
        m_freeChooseDialog = nullptr;
        dialog->reject();
    }
    for (Button *button : freeChooseButtons) button->hide();
    if (progress_bar) {
        disconnect(progress_bar, nullptr, this, nullptr);
        progress_bar->hide();
        progress_bar_item->hide();
        progress_bar_item->deleteLater(); // The proxy owns the countdown widget.
        progress_bar_item = nullptr;
        progress_bar = nullptr;
    }
    for (GeneralCardItem *item : items) {
        disconnect(item, nullptr, this, nullptr);
        item->hide();
        item->deleteLater(); // A clicked/released CardItem may still be on its event stack.
    }
    items.clear();
    selected.clear();
    m_legalPairs.clear();
    confirm->setEnabled(false);
    clearFocus();
    disappear();
}
