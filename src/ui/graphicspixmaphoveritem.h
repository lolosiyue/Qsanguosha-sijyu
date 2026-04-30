#ifndef GRAPHICSPIXMAPHOVERITEM_H
#define GRAPHICSPIXMAPHOVERITEM_H

#include <QObject>
#include <QGraphicsPixmapItem>
#include <QMovie>
#include <QLabel>
#include <QGraphicsProxyWidget>

class PlayerCardContainer;

//自己的头像区不固定显示昵称，而是在鼠标悬停在上面时才显示，
//所以需要为_m_avatarIcon和_m_smallAvatarIcon重写一个类，以便能捕获GraphicsSceneHoverEnter
//和GraphicsSceneHoverLeave事件
//之前尝试使用installSceneEventFilter和setFiltersChildEvents的方法来实现未获得满意效果
class GraphicsPixmapHoverItem : public QObject, public QGraphicsPixmapItem
{
    Q_OBJECT

public:
    explicit GraphicsPixmapHoverItem(PlayerCardContainer *playerCardContainer,
        QGraphicsItem *parent = 0);

    void stopChangeHeroSkinAnimation();
    bool isSkinChangingFinished() const { return 0 == m_timer; }

    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *);

    void setGeneralImage(const QString &imagePath, const QSize &targetSize);
    void setGeneralImage(const QPixmap &pixmap, const QSize &targetSize);
    void stopGifAnimation();
    void startGifAnimation();
    bool isAnimated() const { return m_isAnimated; }

public slots:
    void startChangeHeroSkinAnimation(const QString &generalName);

protected:
    virtual void hoverEnterEvent(QGraphicsSceneHoverEvent *);
    virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *);

    virtual void timerEvent(QTimerEvent *);

private:
    bool isPrimaryAvartarItem() const;
    bool isSecondaryAvartarItem() const;

    static void initSkinChangingFrames();

private:
    PlayerCardContainer *m_playerCardContainer;
    int m_timer;
    int m_val;
    static const int m_max = 100;
    static const int m_step = 1;
    static const int m_interval = 25;
    QPixmap m_heroSkinPixmap;

    static QList<QPixmap> m_skinChangingFrames;
    static int m_skinChangingFrameCount;

    int m_currentSkinChangingFrameIndex;

    QMovie *m_movie;
    QLabel *m_movieLabel;
    QGraphicsProxyWidget *m_proxyWidget;
    bool m_isAnimated;
    QString m_currentImagePath;
    QPixmap m_staticPixmap;
    QString m_targetImagePath;
    QString m_targetGeneralName;

signals:
    void hover_enter();
    void hover_leave();

    void skin_changing_start();
    void skin_changing_finished();
};

#endif // GRAPHICSPIXMAPHOVERITEM_H