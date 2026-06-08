#ifndef PLAYERCARDBOX_H
#define PLAYERCARDBOX_H

#include "graphicsbox.h"
#include "card.h"
#include "player.h"

class ClientPlayer;
class QGraphicsProxyWidget;
class QSanCommandProgressBar;
class QSanButton;

class PlayerCardBox : public GraphicsBox
{
    Q_OBJECT

public:
    explicit PlayerCardBox();

    void chooseCard(const QString &reason, const ClientPlayer *player,
               const QString &flags = "hej", bool handcardVisible = false,
               Card::HandlingMethod method = Card::MethodNone,
               const QList<int> &disabledIds = QList<int>(),
               bool canCancel = false);
    void globalchooseCard(const ClientPlayer *player, const QString &reason, const QString &flags,
        bool handcardVisible, const QList<int> &disabledIds, const QList<int> &handcards);
    void clear();
    void setfalse();
    void reset();
    void global_click();
    QList<CardItem *> items;

protected:
    QRectF boundingRect() const;
    void paintLayout(QPainter *painter);

private:
    void paintArea(const QString &name, QPainter *painter);
    int getRowCount(const int &cardNumber) const;
    void updateNumbers(const int &cardNumber);
    void arrangeCards(const QList<const Card *> &cards, const QPoint &topLeft, bool is_globalchoose = false);

    const ClientPlayer *player;
    QString flags;
    bool handcardVisible;
    bool canCancel;
    Card::HandlingMethod method;
    QList<int> disabledIds;
    QList<int> handcards;
    QList<int> equipSlots;

    QGraphicsProxyWidget *progressBarItem;
    QSanCommandProgressBar *progressBar;
    QSanButton *cancelButton;

    QList<QRect> nameRects;

    int rowCount;
    int intervalsBetweenAreas;
    int intervalsBetweenRows;
    int maxCardsInOneRow;

    static const int maxCardNumberInOneRow;

    static const int verticalBlankWidth;
    static const int placeNameAreaWidth;
    static const int intervalBetweenNameAndCard;
    static const int topBlankWidth;
    static const int bottomBlankWidth;
    static const int intervalBetweenAreas;
    static const int intervalBetweenRows;
    static const int intervalBetweenCards;

public slots:
    void reply();
    void cancel();

signals:
    void global_choose(const ClientPlayer *player, int id);
};

#endif