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
    // 冇 frame 就冇「播完」可言 —— 唔可以喺呢度 emit finished(),
    // 否則一個空 item 淨係入到 scene 就會扮播完一次。
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

    // do-while 會喺 frame 0 都唔存在嗰陣照塞一格入去,而
    // getPixmapFromFileName() 缺檔案時回嘅係一張 1x1 佔位圖(唔係 null),
    // 所以 valid() 以前永遠都係 true —— 全部 caller 嘅「缺資產就唔好播」
    // 分支（GetPixmapAnimation 回 nullptr、_createEquipBorderAnimations
    // 清指標）根本從來冇行過。改成 while 之後嗰啲 fallback 先至真係生效。
    //
    // 資產齊嗰陣行為完全一樣:loop 條件本來就係同一個 QFile::exists()。
    int i = 0;
    QString pic_path = QString("%1%2%3").arg(path).arg(i++).arg(".png");
    while (QFile::exists(pic_path)) {
        frames << G_ROOM_SKIN.getPixmapFromFileName(pic_path, true);
        pic_path = QString("%1%2%3").arg(path).arg(i++).arg(".png");
    }
}

void PixmapAnimation::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    // frames 空咗代表資產缺失。valid() 為 false 嘅 item 唔應該入到 scene,
    // 但 paint()／boundingRect() 一律唔可以喺空 list 上面 at()。
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
    // GetPixmapAnimation() 同 preStart() 都係直接 startTimer(),唔經 start(),
    // 所以 _m_timerId 未必係嗰個 timer;而未 start 過就 stop 亦係正常路徑
    // （例如裝備框由「著」變返「熄」之前根本未著過）。killTimer(0) 係 no-op,
    // 但以前 _m_timerId 根本未初始化,會殺一個垃圾 id。
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
    // 下面要讀 parent->boundingRect() 嚟置中,冇 parent 就冇嘢可以做。
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

