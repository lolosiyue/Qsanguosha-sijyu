#ifndef _CHOOSE_GENERAL_BOX_H
#define _CHOOSE_GENERAL_BOX_H

#include "carditem.h"
#include "graphicsbox.h"
#include "timed-progressbar.h"

#include <QTimer>

class Button;
class CardContainer;

class GeneralCardItem : public CardItem
{
    Q_OBJECT

public:
    friend class ChooseGeneralBox;
    void showCompanion();
    void hideCompanion();
    inline bool hasCompanionShown() const { return m_hasCompanion; }

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    explicit GeneralCardItem(const QString &generalName);
    bool m_hasCompanion;
};

class ChooseGeneralBox : public GraphicsBox
{
    Q_OBJECT

public:
    explicit ChooseGeneralBox();
    void paintLayout(QPainter *painter) override;
    QRectF boundingRect() const override;
    void clear();

public slots:
    void chooseGeneral(const QStringList &generals, bool viewOnly = false, bool singleResult = false, const QString &reason = QString());
    void reply();
    void adjustItems();

private:
    int m_generalNumber;
    bool m_singleResult;
    bool m_viewOnly;
    QList<GeneralCardItem *> m_items, m_selected;
    static const int top_dark_bar = 27;
    static const int top_blank_width = 42;
    static const int bottom_blank_width = 68;
    static const int card_bottom_to_split_line = 23;
    static const int card_to_center_line = 5;
    static const int left_blank_width = 37;
    static const int split_line_to_card_seat = 15;
    static const int S_DATA_INITIAL_HOME_POS = 9527;

    Button *m_confirmButton;
    QGraphicsProxyWidget *m_progressBarItem;
    QSanCommandProgressBar *m_progressBar;

    void _initializeItems();
    void _showAllCompanions();
    void _hideAllCompanions();

private slots:
    void _adjust();
    void _onItemClicked();
};

#endif
