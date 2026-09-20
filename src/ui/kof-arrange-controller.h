#ifndef KOF_ARRANGE_CONTROLLER_H
#define KOF_ARRANGE_CONTROLLER_H

#include <QList>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QStringList>

class Button;
class CardItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QSanSelectableItem;
class QKeyEvent;
class KofKeyboardFocus;

// Owns the general draft and the arrangement UI of the 3v3, 1v1 and XMode game
// modes.  A standard eight-player game never builds any of it, so RoomScene
// holds only a pointer and the few calls below.
class KofArrangeController : public QObject
{
    Q_OBJECT

public:
    explicit KofArrangeController(QGraphicsScene *scene, QObject *parent = nullptr);

    // The table centre moves with the layout; the selector box follows it.
    void setTableCenter(const QPointF &center);

    // True once startArrange() has built the arrange UI.  The context menu and
    // changeGeneral() use it to tell the arrange phase from ordinary play.
    void setPendingGeneralChange(CardItem *item);
    bool isArranging() const;

    // Suspending the application drops any touch preview the draft is showing.
    void cancelTouchPreviews();

    // Arrangement ran out of time: take whatever is left and submit it.
    void autoCompleteArrangement();
    bool handleKeyPress(QKeyEvent *event);

public slots:
    void fillGenerals(const QStringList &names);
    void takeGeneral(const QString &who, const QString &name, const QString &rule);
    void recoverGeneral(int index, const QString &name);
    void startGeneralSelection();
    void startArrange(const QString &to_arrange);
    void changeGeneral(const QString &general);

signals:
    // Forwarded from the draft items; RoomScene owns the preview widget.
    void touchPreviewRequested(CardItem *card);

private slots:
    void selectGeneral();
    void toggleArrange();
    void finishArrange();

private:
    void fillGenerals1v1(const QStringList &names);
    void fillGenerals3v3(const QStringList &names);
    void layoutArrangement();
    void focusGeneral(CardItem *item);

    QGraphicsScene *m_scene;
    QPointF m_tableCenter;

    QSanSelectableItem *selector_box;
    QList<CardItem *> general_items, up_generals, down_generals;
    CardItem *to_change;
    QList<QGraphicsRectItem *> arrange_rects;
    QList<CardItem *> arrange_items;
    Button *arrange_button;
    bool m_selectingGeneral = false;
    QPointer<KofKeyboardFocus> m_keyboardFocus;
};

#endif // KOF_ARRANGE_CONTROLLER_H
