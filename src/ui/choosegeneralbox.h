/*
 * Copyright (c) 2013-2015 - Mogara
 * Ported from QSanguosha-For-Hegemony-xxyheaven/src/ui/choosegeneralbox.h.
 * This file is part of QSanguosha-Hegemony, distributed under the GNU General
 * Public License, version 3 or later, WITHOUT ANY WARRANTY. See LICENSE.
 */
#ifndef CHOOSEGENERALBOX_H
#define CHOOSEGENERALBOX_H

#include "carditem.h"
#include "graphicsbox.h"
#include <QPointer>

class Button;
class QDialog;
class QGraphicsProxyWidget;
class QSanCommandProgressBar;

class GeneralCardItem : public CardItem
{
    Q_OBJECT
public:
    explicit GeneralCardItem(const QString &generalName);
    void showCompanion();
    void hideCompanion();
    void setChoiceEnabled(bool enabled);
    void moveToHome(const QPointF &home);
    bool choiceEnabled() const { return m_choiceEnabled; }

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    bool m_hasCompanion = false;
    bool m_choiceEnabled = true;
    bool m_dragging = false;
};

class ChooseGeneralBox : public GraphicsBox
{
    Q_OBJECT
public:
    explicit ChooseGeneralBox();
    QRectF boundingRect() const override;
    void chooseGeneral(const QStringList &candidates, const QStringList &legalPairs);
    void clear();
    void fitToTable(const QPointF &center, const QRectF &available);
    bool handleChooseKey(int key);
    QString selectedPair() const;
    QStringList candidateNames() const;
    QStringList selectedGenerals() const;
    bool choiceEnabled(const QString &name) const;
    void selectGeneral(const QString &name, bool select);
    void resetSelection();
    bool canConfirm() const;
    bool freeChooseEnabled() const { return m_active && m_freeChooseEnabled; }
    void freeChoose(int slot);

signals:
    void draftChanged();

public slots:
    void reply();
    void adjustItems();

protected:
    void paintLayout(QPainter *painter) override;

private slots:
    void _adjust();
    void _onItemClicked();

private:
    void _initializeItems();
    void removeSelection(GeneralCardItem *item);
    void toggleItem(GeneralCardItem *item);
    void focusChoice(QGraphicsItem *item);
    bool canBeHead(const QString &name) const;
    bool canPair(const QString &head, const QString &deputy) const;
    void setFreeGeneral(int slot, const QString &name);
    qreal splitLineY() const;
    QPointF seatPosition(int slot) const;

    // Keep XXY's two-row pool and two ordered destination seats. Legality comes
    // from the server instead of duplicating its kingdom/ban-pair rules here.
    QList<GeneralCardItem *> items;
    QList<GeneralCardItem *> selected;
    QStringList m_legalPairs;
    int general_number = 0;
    bool m_active = false;
    bool m_freeChooseEnabled = false;
    QPointer<QDialog> m_freeChooseDialog;
    Button *freeChooseButtons[2];
    Button *confirm;
    QGraphicsProxyWidget *progress_bar_item = nullptr;
    QSanCommandProgressBar *progress_bar = nullptr;
    static constexpr int top_blank_width = 42;
    static constexpr int bottom_blank_width = 110;
    static constexpr int card_bottom_to_split_line = 23;
    static constexpr int card_to_center_line = 5;
    static constexpr int left_blank_width = 37;
    static constexpr int split_line_to_card_seat = 15;
    static constexpr int S_DATA_INITIAL_HOME_POS = 9527;
};

#endif
