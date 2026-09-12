#include "pixmapanimation.h"
#include "skin-bank.h"

const int PixmapAnimation::S_DEFAULT_INTERVAL = 50;

PixmapAnimation::PixmapAnimation(QGraphicsScene *)
    : QGraphicsItem(nullptr)
    , _m_timerId(0)
    , current(0)
    , off_x(0)
    , off_y(0)
{
}

void PixmapAnimation::advance(int phase)
{
    // No frames means "finished playing" is meaningless - do not emit finished()
    // here, otherwise an empty item would fake one finished playback merely by entering the scene.
    if (frames.isEmpty())
        return;
    if (phase) current++;
    if (current >= frames.size()) {
        current = 0;
        emit finished();
    }
    update();
}

void PixmapAnimation::setPath(const QString &path)
{
    frames.clear();
    current = 0;

    // The do-while used to insert a frame even when frame 0 did not exist, and
    // getPixmapFromFileName() returns a 1x1 placeholder (not null) when the file is
    // missing, so valid() was always true - every caller's "do not play when assets
    // are missing" branches (GetPixmapAnimation returning nullptr,
    // _createEquipBorderAnimations clearing the pointer) never ran at all. Only after switching to while do those fallbacks actually take effect.
    //
    // With assets present the behavior is identical: the loop condition was the same QFile::exists() all along.
    int i = 0;
    QString pic_path = QString("%1%2%3").arg(path).arg(i++).arg(".png");
    while (QFile::exists(pic_path)) {
        frames << G_ROOM_SKIN.getPixmapFromFileName(pic_path, true);
        pic_path = QString("%1%2%3").arg(path).arg(i++).arg(".png");
    }
}

void PixmapAnimation::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    // Empty frames mean missing assets. An item with valid() == false should never
    // enter the scene, but paint() / boundingRect() must never at() into an empty list regardless.
    if (frames.isEmpty() || current < 0 || current >= frames.size())
        return;
    painter->drawPixmap(0, 0, frames.at(current));
}

QRectF PixmapAnimation::boundingRect() const
{
    if (frames.isEmpty() || current < 0 || current >= frames.size())
        return QRectF();
    return frames.at(current).rect();
}

bool PixmapAnimation::valid()
{
    return !frames.isEmpty();
}

void PixmapAnimation::timerEvent(QTimerEvent *)
{
    advance(1);
}

void PixmapAnimation::start(bool permanent, int interval)
{
    if (frames.isEmpty())
        return;
    if (_m_timerId != 0)
        killTimer(_m_timerId);
    _m_timerId = startTimer(interval);
    if (!permanent) connect(this, SIGNAL(finished()), this, SLOT(deleteLater()));
}

void PixmapAnimation::stop()
{
    // GetPixmapAnimation() and preStart() call startTimer() directly, not via
    // start(), so _m_timerId is not necessarily that timer; stopping without a
    // prior start is also a normal path (e.g. an equip frame switching from "on" to
    // "off" before ever being on). killTimer(0) is a no-op, but _m_timerId used to be uninitialized and would kill a garbage id.
    if (_m_timerId != 0) {
        killTimer(_m_timerId);
        _m_timerId = 0;
    }
}

void PixmapAnimation::preStart()
{
    if (frames.isEmpty())
        return;
    this->show();
    if (_m_timerId != 0)
        killTimer(_m_timerId);
    _m_timerId = this->startTimer(S_DEFAULT_INTERVAL);
}

PixmapAnimation *PixmapAnimation::GetPixmapAnimation(QGraphicsItem *parent, const QString &emotion)
{
    // The code below reads parent->boundingRect() to center; with no parent there is nothing to do.
    if (parent == nullptr)
        return nullptr;

    PixmapAnimation *pma = new PixmapAnimation();
    pma->setPath(QString("image/system/emotion/%1/").arg(emotion));
    if (pma->valid()) {
        if (emotion == "no-success") {
            pma->moveBy(pma->boundingRect().width() * 0.25, pma->boundingRect().height() * 0.25);
            pma->setScale(0.5);
        } else if (emotion == "success") {
            pma->moveBy(pma->boundingRect().width() * 0.1,
                pma->boundingRect().height() * 0.1);
            pma->setScale(0.8);
        } else if (emotion.contains("double_sword"))
            pma->moveBy(13, -20);
        else if (emotion.contains("fan") || emotion.contains("guding_blade"))
            pma->moveBy(0, -20);
        else if (emotion.contains("/spear"))
            pma->moveBy(-20, -20);

        pma->moveBy((parent->boundingRect().width() - pma->boundingRect().width()) / 2,
            (parent->boundingRect().height() - pma->boundingRect().height()) / 2);

        pma->setParentItem(parent);
        pma->setZValue(22);
        if (emotion.contains("weapon")) {
            pma->hide();
            QTimer::singleShot(600, pma, SLOT(preStart()));
        } else
            pma->start(true, S_DEFAULT_INTERVAL);

        connect(pma, SIGNAL(finished()), pma, SLOT(deleteLater()));
        return pma;
    } else {
        delete pma;
        return nullptr;
    }
}

QPixmap PixmapAnimation::GetFrameFromCache(const QString &filename)
{
    QPixmap pixmap;
    if (!QPixmapCache::find(filename, &pixmap)) {
        if (pixmap.load(filename))
            QPixmapCache::insert(filename, pixmap);
    }
    return pixmap;
}

int PixmapAnimation::GetFrameCount(const QString &emotion)
{
    QString path = QString("image/system/emotion/%1/").arg(emotion);
    QDir dir(path);
    dir.setNameFilters(QStringList("*.png"));
    return dir.entryList(QDir::Files | QDir::NoDotAndDotDot).count();
}

