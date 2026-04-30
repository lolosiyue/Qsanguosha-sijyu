#include "graphicspixmaphoveritem.h"
#include "generic-cardcontainer-ui.h"
#include "pixmapanimation.h"
#include "engine.h"
#include "settings.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QFile>
#include <QRegExp>
#include <QFileInfo>

const char *CHANGE_SKIN_EMOTION_NAME = "skin_changing";

QList<QPixmap> GraphicsPixmapHoverItem::m_skinChangingFrames;
int GraphicsPixmapHoverItem::m_skinChangingFrameCount = 0;

GraphicsPixmapHoverItem::GraphicsPixmapHoverItem(PlayerCardContainer *playerCardContainer, QGraphicsItem *parent)
    : QGraphicsPixmapItem(parent), m_playerCardContainer(playerCardContainer), m_timer(0), m_val(0),
    m_currentSkinChangingFrameIndex(-1),
    m_movie(nullptr), m_movieLabel(nullptr), m_proxyWidget(nullptr), m_isAnimated(false),
    m_targetImagePath(), m_targetGeneralName()
{
    setAcceptHoverEvents(true);

    if (m_skinChangingFrames.isEmpty()) {
        initSkinChangingFrames();
    }
}

void GraphicsPixmapHoverItem::hoverEnterEvent(QGraphicsSceneHoverEvent *)
{
    emit hover_enter();
}

void GraphicsPixmapHoverItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    emit hover_leave();
}

/*!
    \internal

    Highlights \a item as selected.

    NOTE: This function is a duplicate of qt_graphicsItem_highlightSelected() in
          qgraphicssvgitem.cpp!
*/
static void qt_graphicsItem_highlightSelected(
    QGraphicsItem *item, QPainter *painter, const QStyleOptionGraphicsItem *option)
{
    const QRectF murect = painter->transform().mapRect(QRectF(0, 0, 1, 1));
    if (qFuzzyIsNull(qMax(murect.width(), murect.height())))
        return;

    const QRectF mbrect = painter->transform().mapRect(item->boundingRect());
    if (qMin(mbrect.width(), mbrect.height()) < qreal(1.0))
        return;

    qreal itemPenWidth;
    switch (item->type()) {
        case QGraphicsEllipseItem::Type:
            itemPenWidth = static_cast<QGraphicsEllipseItem *>(item)->pen().widthF();
            break;
        case QGraphicsPathItem::Type:
            itemPenWidth = static_cast<QGraphicsPathItem *>(item)->pen().widthF();
            break;
        case QGraphicsPolygonItem::Type:
            itemPenWidth = static_cast<QGraphicsPolygonItem *>(item)->pen().widthF();
            break;
        case QGraphicsRectItem::Type:
            itemPenWidth = static_cast<QGraphicsRectItem *>(item)->pen().widthF();
            break;
        case QGraphicsSimpleTextItem::Type:
            itemPenWidth = static_cast<QGraphicsSimpleTextItem *>(item)->pen().widthF();
            break;
        case QGraphicsLineItem::Type:
            itemPenWidth = static_cast<QGraphicsLineItem *>(item)->pen().widthF();
            break;
        default:
            itemPenWidth = 1.0;
    }
    const qreal pad = itemPenWidth / 2;

    const qreal penWidth = 0; // cosmetic pen

    const QColor fgcolor = option->palette.windowText().color();
    const QColor bgcolor( // ensure good contrast against fgcolor
        fgcolor.red()   > 127 ? 0 : 255,
        fgcolor.green() > 127 ? 0 : 255,
        fgcolor.blue()  > 127 ? 0 : 255);

    painter->setPen(QPen(bgcolor, penWidth, Qt::SolidLine));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(item->boundingRect().adjusted(pad, pad, -pad, -pad));

    painter->setPen(QPen(option->palette.windowText(), 0, Qt::DashLine));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(item->boundingRect().adjusted(pad, pad, -pad, -pad));
}

void GraphicsPixmapHoverItem::paint(QPainter *painter,
    const QStyleOptionGraphicsItem *option, QWidget *)
{
    if (pixmap().isNull()) {
        return;
    }

    //此处不能使用static来定义tempPix，因为普通界面和全幅界面使用的图片尺寸不一样
    QPixmap tempPix(pixmap().size());
    tempPix.fill(Qt::transparent);

    QPainter tempPainter(&tempPix);
    tempPainter.setRenderHint(QPainter::SmoothPixmapTransform,
        (transformationMode() == Qt::SmoothTransformation));

    if (m_val > 0) {
        tempPainter.drawPixmap(offset(), m_heroSkinPixmap);

        double percent = 1 - (double)m_val / (double)m_max;
        QRectF rect = QRectF(offset().x(), offset().y(),
            boundingRect().width(), percent * boundingRect().height());

        tempPainter.setClipRect(rect);
        tempPainter.drawPixmap(offset(), pixmap());

        tempPainter.setClipRect(boundingRect());
        tempPainter.drawPixmap(rect.left() - 9, rect.bottom() - 25,
            m_skinChangingFrames[m_currentSkinChangingFrameIndex]);

        //由于可能需要在外部额外处理tempPix，因此必须调用tempPainter.end()
        //来释放tempPainter对tempPix的控制权，否则会出现异常
        tempPainter.end();
        if (isSecondaryAvartarItem()) {
            tempPix = m_playerCardContainer->paintByMask(tempPix);
        }
    }
    else {
        tempPainter.drawPixmap(offset(), pixmap());
    }

    if (option->state & QStyle::State_Selected) {
        qt_graphicsItem_highlightSelected(this, &tempPainter, option);
    }

    painter->drawPixmap(0, 0, tempPix);
}

void GraphicsPixmapHoverItem::timerEvent(QTimerEvent *)
{
    ++m_currentSkinChangingFrameIndex;
    if (m_currentSkinChangingFrameIndex >= m_skinChangingFrameCount) {
        m_currentSkinChangingFrameIndex = 0;
    }

    m_val += m_step;
    if (m_val >= m_max) {
        stopChangeHeroSkinAnimation();

        if (!m_targetImagePath.isEmpty()) {
            QSize targetSize = boundingRect().size().toSize();
            setGeneralImage(m_targetImagePath, targetSize);
            m_targetImagePath.clear();
            m_targetGeneralName.clear();
        }
        return;
    }

    update();
}

void GraphicsPixmapHoverItem::startChangeHeroSkinAnimation(const QString &generalName)
{
    if (m_timer != 0) {
        return;
    }

    emit skin_changing_start();

    m_targetGeneralName = generalName;

    QString actualGeneralName = generalName;
    const General *general = Sanguosha->getGeneral(generalName);
    if (general && !general->getImage().isEmpty()) {
        actualGeneralName = general->getImage();
    }

    QString basePath = "image/fullskin/generals/full/";
    m_targetImagePath = basePath + actualGeneralName + ".jpg";

    QString actualGn = Sanguosha->getResourceAlias("heroskin", generalName);
    int skin_index = Config.value(QString("HeroSkin/%1").arg(generalName), 0).toInt();
    if (skin_index > 0) {
        m_targetImagePath = "image/heroskin/fullskin/generals/full/"
                          + actualGn + "_" + QString::number(skin_index) + ".jpg";
    }

    if (m_isAnimated && m_movie) {
        m_movie->stop();
    }
    if (m_proxyWidget) {
        m_proxyWidget->hide();
    }

    if (pixmap().isNull() && !m_staticPixmap.isNull()) {
        setPixmap(m_staticPixmap);
    }
    QGraphicsPixmapItem::show();

    if (m_playerCardContainer) {
        QPixmap staticPixmap(m_targetImagePath);
        if (!staticPixmap.isNull()) {
            m_heroSkinPixmap = staticPixmap;
            QSize itemSize = boundingRect().size().toSize();
            if (m_heroSkinPixmap.size() != itemSize) {
                m_heroSkinPixmap = m_heroSkinPixmap.scaled(itemSize, Qt::IgnoreAspectRatio,
                                                           Qt::SmoothTransformation);
            }
        } else {
            if (isPrimaryAvartarItem()) {
                m_heroSkinPixmap = m_playerCardContainer->_getAvatarIcon(generalName);
            } else {
                m_heroSkinPixmap = m_playerCardContainer->getSmallAvatarIcon(generalName);
            }
        }
        m_timer = startTimer(m_interval);
    }
}

void GraphicsPixmapHoverItem::stopChangeHeroSkinAnimation()
{
    if (m_timer != 0) {
        killTimer(m_timer);
        m_timer = 0;
    }
    m_val = 0;
    m_currentSkinChangingFrameIndex = -1;

    emit skin_changing_finished();
}

void GraphicsPixmapHoverItem::initSkinChangingFrames()
{
    m_skinChangingFrameCount = PixmapAnimation::GetFrameCount(CHANGE_SKIN_EMOTION_NAME);
    for (int i = 0; i < m_skinChangingFrameCount; ++i) {
        QString fileName = QString("image/system/emotion/%1/%2.png")
            .arg(CHANGE_SKIN_EMOTION_NAME).arg(QString::number(i));

        QPixmap framePixmap = G_ROOM_SKIN.getPixmapFromFileName(fileName);
        m_skinChangingFrames << framePixmap.scaled(framePixmap.width() + 15,
            framePixmap.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
}

bool GraphicsPixmapHoverItem::isPrimaryAvartarItem() const
{
    return (this == m_playerCardContainer->getAvartarItem());
}

bool GraphicsPixmapHoverItem::isSecondaryAvartarItem() const
{
    return (this == m_playerCardContainer->getSmallAvartarItem());
}

void GraphicsPixmapHoverItem::setGeneralImage(const QString &imagePath, const QSize &targetSize)
{
    m_currentImagePath = imagePath;

    QString gifPath = imagePath;
    bool hasGif = false;

    if (!imagePath.toLower().endsWith(".gif")) {
        gifPath.replace(QRegExp("\\.(jpg|png)$", Qt::CaseInsensitive), ".gif");

        if (gifPath.contains("/heroskin/")) {
            if (gifPath.contains("/full/") && !gifPath.contains("/full/gif/")) {
                QString gifPathInSubdir = gifPath;
                gifPathInSubdir.replace("/full/", "/full/gif/");
                if (QFile::exists(gifPathInSubdir)) {
                    gifPath = gifPathInSubdir;
                    hasGif = true;
                } else if (QFile::exists(gifPath)) {
                    hasGif = true;
                }
            }
        }
        else if (gifPath.contains("/full/") && !gifPath.contains("/full/gif/")) {
            QString gifPathInSubdir = gifPath;
            gifPathInSubdir.replace("/full/", "/full/gif/");
            if (QFile::exists(gifPathInSubdir)) {
                gifPath = gifPathInSubdir;
                hasGif = true;
            } else if (QFile::exists(gifPath)) {
                hasGif = true;
            }
        } else if (QFile::exists(gifPath)) {
            hasGif = true;
        }
    } else {
        hasGif = QFile::exists(gifPath);
    }

    QString generalName = QFileInfo(imagePath).baseName();
    generalName = generalName.section("_", 0, -2);

    QString animatedAlias = Sanguosha->getResourceAlias("animatedgeneral", generalName);
    if (animatedAlias != generalName) {
        QString aliasGifPath = QFileInfo(gifPath).absoluteDir().absolutePath() + "/" + animatedAlias;
        if (animatedAlias.contains("/") || animatedAlias.contains("\\")) {
            if (QFile::exists(animatedAlias)) {
                gifPath = animatedAlias;
                hasGif = true;
            }
        } else if (QFile::exists(aliasGifPath)) {
            gifPath = aliasGifPath;
            hasGif = true;
        }
    }

    QPixmap staticPixmap(imagePath);
    if (!staticPixmap.isNull()) {
        if (targetSize.width() > 0 && targetSize.height() > 0
            && staticPixmap.size() != targetSize) {
            staticPixmap = staticPixmap.scaled(targetSize, Qt::IgnoreAspectRatio,
                                               Qt::SmoothTransformation);
        }
        setPixmap(staticPixmap);
        m_staticPixmap = staticPixmap;
    }

    if (hasGif && Config.value("EnableAnimatedGenerals", true).toBool()) {
        if (!m_movie) {
            m_movie = new QMovie(gifPath, QByteArray(), this);
        } else {
            m_movie->stop();
            m_movie->setFileName(gifPath);
        }

        if (m_movie->isValid()) {
            m_isAnimated = true;

            if (targetSize.width() > 0 && targetSize.height() > 0) {
                m_movie->setScaledSize(targetSize);
            }

            if (!m_movieLabel) {
                m_movieLabel = new QLabel();
                m_movieLabel->setStyleSheet("QLabel { background-color: transparent; }");
                m_movieLabel->setAttribute(Qt::WA_TranslucentBackground, true);
            }

            m_movieLabel->setMovie(m_movie);
            if (targetSize.width() > 0 && targetSize.height() > 0) {
                m_movieLabel->setFixedSize(targetSize);
            }

            if (!m_proxyWidget) {
                if (scene()) {
                    m_proxyWidget = scene()->addWidget(m_movieLabel);
                    m_proxyWidget->setParentItem(this);
                    m_proxyWidget->setPos(0, 0);
                    m_proxyWidget->setZValue(-1);
                }
            } else {
                m_proxyWidget->setWidget(m_movieLabel);
                m_proxyWidget->setPos(0, 0);
            }

            m_movie->start();
            m_proxyWidget->show();
            setPixmap(QPixmap());
            return;
        }
    }

    m_isAnimated = false;
    if (m_movie) {
        m_movie->stop();
    }
    if (m_proxyWidget) {
        m_proxyWidget->hide();
    }
    QGraphicsPixmapItem::show();
}

void GraphicsPixmapHoverItem::stopGifAnimation()
{
    if (m_movie) {
        m_movie->stop();
    }
    if (m_proxyWidget) {
        m_proxyWidget->hide();
    }
    if (!m_staticPixmap.isNull()) {
        setPixmap(m_staticPixmap);
        QGraphicsPixmapItem::show();
    }
}

void GraphicsPixmapHoverItem::setGeneralImage(const QPixmap &pixmap, const QSize &targetSize)
{
    QPixmap scaledPixmap = pixmap;
    if (targetSize.width() > 0 && targetSize.height() > 0
        && pixmap.size() != targetSize) {
        scaledPixmap = pixmap.scaled(targetSize, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
    }

    setPixmap(scaledPixmap);
    m_staticPixmap = scaledPixmap;
    m_isAnimated = false;

    if (m_movie) {
        m_movie->stop();
    }
    if (m_proxyWidget) {
        m_proxyWidget->hide();
    }
    QGraphicsPixmapItem::show();
}

void GraphicsPixmapHoverItem::startGifAnimation()
{
    if (m_isAnimated && m_movie && m_movie->isValid()) {
        if (m_proxyWidget) {
            m_proxyWidget->show();
        }
        m_movie->start();
        setPixmap(QPixmap());
    }
}

