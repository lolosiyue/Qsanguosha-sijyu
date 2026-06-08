#ifndef FIELDCARDTRANSFERBOX_H
#define FIELDCARDTRANSFERBOX_H

#include "carditem.h"
#include "timed-progressbar.h"
#include "graphicsbox.h"

#include <QMap>

class Button;
class QGraphicsDropShadowEffect;

class FieldCardTransferBox : public GraphicsBox
{
    Q_OBJECT

public:
    explicit FieldCardTransferBox();

    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    QRectF boundingRect() const;
    void clear();

public slots:
    void doFieldCardTransferChoose(const ClientPlayer *playerA, const ClientPlayer *playerB, const QString &reason, bool equipArea, bool judgingArea);
    void reply();

private:
    QString reason;
    const ClientPlayer *playerA, *playerB;
    bool equipArea, judgingArea;
    bool buttonstate;

    QMap<int, CardItem *> cardItems;

    int itemCount;

    QList<int> selected;

    static const int cardInterval = 3;

    static const int top_dark_bar = 27;
    static const int top_blank_width = 42;
    static const int bottom_blank_width = 68;
    static const int card_bottom_to_split_line = 23;
    static const int card_to_center_line = 5;
    static const int lord_to_card_center_line = 20;
    static const int left_blank_width = 37;
    static const int split_line_to_card_seat = 15;

    static const int S_DATA_INITIAL_HOME_POS = 9527;

    Button *confirm, *cancel;
    QGraphicsProxyWidget *progress_bar_item;
    QSanCommandProgressBar *progress_bar;

    void createCardItem(const Card *card, bool enabled);
    void adjust();

private slots:
    void onItemClicked();
};

#endif // FIELDCARDTRANSFERBOX_H
