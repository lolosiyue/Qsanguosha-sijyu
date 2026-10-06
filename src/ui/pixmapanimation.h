#ifndef _PIXMAP_ANIMATION_H
#define _PIXMAP_ANIMATION_H

#include <QGraphicsPixmapItem>

class PixmapAnimation : public QObject, public QGraphicsItem
{
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)

public:
    PixmapAnimation(QGraphicsScene *scene = 0);

    QRectF boundingRect() const;
    void advance(int phase);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    void timerEvent(QTimerEvent *e);

    void setPath(const QString &path);
    bool valid();

    void start(bool permanent = true, int interval = 50);
    void stop();

    static PixmapAnimation *GetPixmapAnimation(QGraphicsItem *parent, const QString & emotion);
    static QPixmap GetFrameFromCache(const QString &filename);
    static int GetFrameCount(const QString &emotion);
    // Emotion art comes from the "emotion" theme slot: the folder holding one animation's
    // frames (ending in '/'), taken whole from one theme pack or the default art.
    static QString EmotionDirectory(const QString &emotion);
    // The file of a single-image emotion, such as the pindian question mark.
    static QString EmotionFile(const QString &emotion);
    // Predecode common emotion frames off-thread into QPixmapCache; context destruction cancels the work.
    static void PrewarmEmotions(QObject *context, const QStringList &emotions);

    static const int S_DEFAULT_INTERVAL;

signals:
    void finished();
    void frame_loaded();

public slots:
    void preStart();

private:
    int _m_timerId;
    QString path;
    QList<QPixmap> frames;
    int current, off_x, off_y;
};

#endif

