#include "game-view.h"

#include "build-features.h"
#include "pointer-hover-delivery.h"
#include "roomscene.h"
#include "settings.h"
#include "skin-bank.h"
#include "startscene.h"
#if !defined(QSAN_XP_LEGACY)
#include "room-overlay-host.h"
#include "room-window-posture.h"
#endif

#if !QSAN_USE_RASTER_VIEWPORT
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#endif
#include <QGraphicsEffect>
#include <QPainter>
#include <QPixmapCache>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QApplication>
#include <QKeyEvent>

namespace {

bool isGrayscaleMode()
{
    return Config.VisualMode == QLatin1String("grayscale");
}

bool isVisualModeActive()
{
    return isGrayscaleMode() || Config.VisualMode == QLatin1String("highcontrast");
}

// 與 HomeScene 的 MultiEffect 同參數:灰階全去色,高對比 contrast +0.35。
constexpr int kHighContrastPercent = 135;

// CPU 後製:光柵 viewport 整張畫面,以及 GL 版浮在 viewport 上的 QWidget overlay。
class VisualModeEffect final : public QGraphicsEffect
{
public:
    explicit VisualModeEffect(bool grayscale)
        : m_grayscale(grayscale)
    {
    }

protected:
    void draw(QPainter *painter) override
    {
        QPoint offset;
        const QPixmap pixmap = sourcePixmap(Qt::DeviceCoordinates, &offset, QGraphicsEffect::NoPad);
        if (pixmap.isNull())
            return;
        // premultiplied:對比以 a/2 為中心拉伸並夾在 [0, a],半透明邊緣才不會溢色。
        QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < image.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = line[x];
                const int a = qAlpha(pixel);
                if (m_grayscale) {
                    const int luma = (qRed(pixel) * 77 + qGreen(pixel) * 150 + qBlue(pixel) * 29) >> 8;
                    line[x] = qRgba(luma, luma, luma, a);
                } else {
                    const auto stretch = [a](int c) {
                        return qBound(0, (c * 2 - a) * kHighContrastPercent / 200 + a / 2, a);
                    };
                    line[x] = qRgba(stretch(qRed(pixel)), stretch(qGreen(pixel)), stretch(qBlue(pixel)), a);
                }
            }
        }
        painter->save();
        painter->setWorldTransform(QTransform());
        painter->drawImage(offset, image);
        painter->restore();
    }

private:
    bool m_grayscale;
};

} // namespace

#if !QSAN_USE_RASTER_VIEWPORT
// GPU 後製:把 QOpenGLWidget 已畫好的 FBO 複製成貼圖,再用 shader 蓋回去。
class GameViewGlFilter final : public QObject
{
public:
    explicit GameViewGlFilter(QOpenGLWidget *widget)
        : QObject(widget), m_widget(widget)
    {
    }

    // 需在 beginNativePainting/endNativePainting 之間呼叫。
    void apply(bool grayscale)
    {
        QOpenGLContext *context = QOpenGLContext::currentContext();
        if (!context || m_failed)
            return;
        if (m_context != context) {
            // widget 換 top-level 時 context 會重建;刪 GL 物件前要先 makeCurrent。
            m_context = context;
            connect(context, &QOpenGLContext::aboutToBeDestroyed, this, [this]() {
                m_widget->makeCurrent();
                release();
                m_widget->doneCurrent();
            });
        }
        if (!ensureProgram())
            return;
        QOpenGLFunctions *f = context->functions();
        const QSize size = m_widget->size() * m_widget->devicePixelRatioF();
        if (size.isEmpty())
            return;

        f->glActiveTexture(GL_TEXTURE0);
        if (!m_texture)
            f->glGenTextures(1, &m_texture);
        f->glBindTexture(GL_TEXTURE_2D, m_texture);
        if (m_textureSize != size) {
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.width(), size.height(), 0,
                GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            m_textureSize = size;
        }
        f->glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, size.width(), size.height());

        static const GLfloat quad[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
        f->glBindBuffer(GL_ARRAY_BUFFER, 0);
        f->glViewport(0, 0, size.width(), size.height());
        m_program->bind();
        m_program->setUniformValue("source", 0);
        m_program->setUniformValue("grayscale", grayscale ? 1.0f : 0.0f);
        m_program->setUniformValue("contrast", grayscale ? 1.0f : kHighContrastPercent / 100.0f);
        m_program->enableAttributeArray(0);
        m_program->setAttributeArray(0, GL_FLOAT, quad, 2);
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_program->disableAttributeArray(0);
        m_program->release();
        f->glBindTexture(GL_TEXTURE_2D, 0);
    }

private:
    bool ensureProgram()
    {
        if (m_program)
            return true;
        m_program = new QOpenGLShaderProgram;
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
            "attribute highp vec2 vertex;\n"
            "varying highp vec2 texCoord;\n"
            "void main()\n"
            "{\n"
            "    texCoord = vertex * 0.5 + 0.5;\n"
            "    gl_Position = vec4(vertex, 0.0, 1.0);\n"
            "}\n");
        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
            "uniform sampler2D source;\n"
            "uniform mediump float grayscale;\n"
            "uniform mediump float contrast;\n"
            "varying highp vec2 texCoord;\n"
            "void main()\n"
            "{\n"
            "    mediump vec4 color = texture2D(source, texCoord);\n"
            "    mediump float luma = dot(color.rgb, vec3(0.299, 0.587, 0.114));\n"
            "    mediump vec3 rgb = mix(color.rgb, vec3(luma), grayscale);\n"
            "    rgb = (rgb - 0.5 * color.a) * contrast + 0.5 * color.a;\n"
            "    gl_FragColor = vec4(clamp(rgb, 0.0, color.a), color.a);\n"
            "}\n");
        m_program->bindAttributeLocation("vertex", 0);
        if (m_program->link())
            return true;
        qWarning().noquote() << "Visual mode shader failed:" << m_program->log();
        m_failed = true;
        release();
        return false;
    }

    void release()
    {
        delete m_program;
        m_program = nullptr;
        if (m_texture) {
            if (QOpenGLContext *context = QOpenGLContext::currentContext())
                context->functions()->glDeleteTextures(1, &m_texture);
            m_texture = 0;
        }
        m_textureSize = QSize();
    }

    QOpenGLWidget *m_widget;
    QPointer<QOpenGLContext> m_context;
    QOpenGLShaderProgram *m_program = nullptr;
    GLuint m_texture = 0;
    QSize m_textureSize;
    bool m_failed = false;
};
#endif

FitView::FitView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setSceneRect(UiConfig.Rect);
    setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing
        | QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAlignment(Qt::AlignCenter);
    m_uiScale = qBound<qreal>(1.0, Config.UIScale, 2.0);
#if !QSAN_USE_RASTER_VIEWPORT
    QOpenGLWidget *glWidget = new QOpenGLWidget(this);
    glWidget->setUpdateBehavior(QOpenGLWidget::PartialUpdate);
    setViewport(glWidget);
    m_glFilter = new GameViewGlFilter(glWidget);
#endif
    qsanEnableWidgetPointerHover(this);
    qsanEnableWidgetPointerHover(viewport());
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
#if !defined(QSAN_XP_LEGACY)
    // Main window and diagnostic FitViews share one Android listener. Destroying
    // a secondary view must not detach the main game's posture subscription.
    static QPointer<RoomWindowPosture> sharedPosture;
    if (!sharedPosture) {
        sharedPosture = new RoomWindowPosture(qApp);
        sharedPosture->setObjectName(QStringLiteral("roomWindowPosture"));
    }
    m_posture = sharedPosture;
    m_responsiveEnabled = Config.responsiveUiEnabled();
    m_posture->setResponsivePreview(m_responsiveEnabled);
    connect(&Config, &Settings::uiLayoutChanged, this, [this] {
        setResponsiveRoomEnabled(Config.responsiveUiEnabled());
    });
    connect(m_posture, &RoomWindowPosture::postureChanged, this, [this]() { refit(); });
#endif
    applyVisualMode();
}

void FitView::applyVisualMode()
{
    const bool active = isVisualModeActive();
#if QSAN_USE_RASTER_VIEWPORT
    // overlay 是 viewport 的子 widget,一併被這層效果處理。
    viewport()->setGraphicsEffect(active ? new VisualModeEffect(isGrayscaleMode()) : nullptr);
#elif !defined(QSAN_XP_LEGACY)
    // GL viewport 由 drawForeground 後製;overlay 是另外合成的 QWidget,要自己套。
    if (m_overlay)
        m_overlay->setGraphicsEffect(active ? new VisualModeEffect(isGrayscaleMode()) : nullptr);
#endif
    viewport()->update();
}

void FitView::drawForeground(QPainter *painter, const QRectF &rect)
{
    QGraphicsView::drawForeground(painter, rect);
#if !QSAN_USE_RASTER_VIEWPORT
    if (!isVisualModeActive())
        return;
    painter->beginNativePainting();
    m_glFilter->apply(isGrayscaleMode());
    painter->endNativePainting();
#endif
}

bool FitView::event(QEvent *event)
{
    // QWidget consumes Tab before keyPressEvent. Route native gameplay keys
    // here so the table stays operable without opening the widget action panel.
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        if (auto *room = qobject_cast<RoomScene *>(scene()))
            if (room->handleNativeKey(static_cast<QKeyEvent *>(event))) return true;
    }
    return QGraphicsView::event(event);
}

bool FitView::viewportEvent(QEvent *event)
{
    if (qsanForwardPointerHoverAsMouseMove(viewport(), event))
        return true;
    return QGraphicsView::viewportEvent(event);
}

void FitView::setScene(QGraphicsScene *next)
{
    QGraphicsView::setScene(next);
    qsanEnableWidgetPointerHover(viewport());
#if !defined(QSAN_XP_LEGACY)
    if (!qobject_cast<RoomScene *>(next)) {
        // Rotation is an application preference; returning home keeps it enabled.
        if (m_posture) m_posture->setResponsivePreview(m_responsiveEnabled);
        delete m_overlay;
        m_overlay = nullptr;
        m_overlayRoom = nullptr;
        m_hasPreviousProfile = false;
    }
#endif
    refit();
}

void FitView::setStableSafeAreaMargins(const QMargins &margins)
{
    if (m_stableSafeMargins == margins)
        return;
    m_stableSafeMargins = margins;
    QTimer::singleShot(0, this, &FitView::refit);
}

void FitView::showPlayerInspector()
{
#if !defined(QSAN_XP_LEGACY)
    if (auto *room = qobject_cast<RoomScene *>(scene())) {
        ensureRoomOverlay(room);
        m_overlay->inspectPlayer(QString());
    }
#endif
}

void FitView::setResponsiveRoomEnabled(bool enabled)
{
#if !defined(QSAN_XP_LEGACY)
    m_responsiveEnabled = enabled;
    Config.setResponsiveUiEnabled(enabled);
    if (m_posture) m_posture->setResponsivePreview(enabled);
    m_hasPreviousProfile = false;
    if (m_overlay && m_overlay->responsiveEnabled() != enabled)
        m_overlay->setResponsiveEnabled(enabled);
    refit();
#else
    Q_UNUSED(enabled);
#endif
}

#if !defined(QSAN_XP_LEGACY)
void FitView::ensureRoomOverlay(RoomScene *room)
{
    if (m_overlay && m_overlayRoom == room)
        return;
    delete m_overlay;
    m_hasPreviousProfile = false;
    m_overlayRoom = room;
    m_overlay = new RoomOverlayHost(viewport());
    if (m_posture) m_posture->setResponsivePreview(m_responsiveEnabled);
    m_overlay->setResponsiveEnabled(m_responsiveEnabled);
    room->attachOverlay(m_overlay);
    connect(room, &RoomScene::seatCountChanged, this, &FitView::refit, Qt::QueuedConnection);
    connect(m_overlay, &RoomOverlayHost::responsiveEnabledChanged, this,
            [this](bool enabled) { setResponsiveRoomEnabled(enabled); });
    connect(m_overlay, &RoomOverlayHost::layoutPreferencesChanged, this, [this]() { refit(); });
    connect(room, &RoomScene::responsiveGeometryChanged, m_overlay, [this, room]() {
        if (m_overlay && (m_responsiveEnabled || room->largeRoomRequired()))
            m_overlay->setLayoutResult(room->responsiveLayout());
    });
    connect(room, &QObject::destroyed, m_overlay, &QObject::deleteLater);
    applyVisualMode();
    m_overlay->show();
}
#endif

void FitView::setSafeAreaMargins(const QMargins &margins)
{
    if (viewportMargins() == margins)
        return;
    // These margins already include system/keyboard occlusion. RoomLayoutEngine
    // receives the remaining viewport, so it must not subtract the insets again.
    setViewportMargins(margins);
    refit();
}

void FitView::setUiScale(qreal scale)
{
    m_uiScale = qBound<qreal>(1.0, scale, 2.0);
#if !defined(QSAN_XP_LEGACY)
    if (m_responsiveEnabled) {
        refit();
        return;
    }
#endif
    if (auto *roomScene = qobject_cast<RoomScene *>(scene())) {
        roomScene->applyUiElementScale(m_uiScale);
        roomScene->refreshTouchTargets(transform().m11());
    }
}

void FitView::refit()
{
    fitCurrentScene(viewport()->size());
}

void FitView::setBackgroundBrush(bool centerAsOrigin)
{
    if (!scene())
        return;

    const QSize targetSize = viewport()->size();
    QTransform transform;
    if (centerAsOrigin)
        transform.translate(-targetSize.width() / 2.0, -targetSize.height() / 2.0);
    QPixmap source;
    const bool portrait = Config.responsiveUiEnabled() && targetSize.height() > targetSize.width();
    const QString path = portrait ? Config.value(QStringLiteral("UI/PortraitBackgroundImage"),
        QStringLiteral("image/system/portrait/portrait-background.svg")).toString() : Config.BackgroundImage;
    const QString sourceKey = QStringLiteral("qsan-background:") + path;
    if (!QPixmapCache::find(sourceKey, &source)) {
        source.load(path);
        if (source.isNull() && portrait)
            source.load(QStringLiteral("image/system/portrait/portrait-background.svg"));
        if (!source.isNull())
            QPixmapCache::insert(sourceKey, source);
    }
    if (portrait && source.isNull()) {
        scene()->setBackgroundBrush(QColor(QStringLiteral("#344f65")));
        return;
    }
    if (portrait && !source.isNull()) {
        source = source.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        source = source.copy((source.width() - targetSize.width()) / 2,
            (source.height() - targetSize.height()) / 2, targetSize.width(), targetSize.height());
    }
    QBrush brush(scaledPixmapForDevice(source, targetSize, devicePixelRatioF()));
    brush.setTransform(transform);
    scene()->setBackgroundBrush(brush);
}

void FitView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    fitCurrentScene(viewport()->size());
}

void FitView::fitCurrentScene(const QSize &viewportSize)
{
    if (!scene() || viewportSize.isEmpty())
        return;

    resetTransform();

    if (auto *roomScene = qobject_cast<RoomScene *>(scene())) {
#if !defined(QSAN_XP_LEGACY)
        if (m_fitting)
            return;
        m_fitting = true;
        ensureRoomOverlay(roomScene);
        m_overlay->setGeometry(viewport()->rect());
        RoomLayoutEngine::ResponsiveInput input;
        input.availableRect = QRectF(QPointF(0, 0), QSizeF(viewportSize));
        // The profile basis excludes system insets but not the IME. Both rects
        // share viewport-local coordinates, so a keyboard cannot select a new profile.
        const QMargins actual = viewportMargins();
        input.stableRect = QRectF(QPointF(0, 0), QSizeF(
            viewportSize.width() + actual.left() + actual.right()
                - m_stableSafeMargins.left() - m_stableSafeMargins.right(),
            viewportSize.height() + actual.top() + actual.bottom()
                - m_stableSafeMargins.top() - m_stableSafeMargins.bottom()));
        input.headerHeight = 56.0;
        input.previousProfile = m_previousProfile;
        input.hasPreviousProfile = m_hasPreviousProfile;
        input.inspectorRequested = m_overlay->inspectorRequested();
        input.logVisible = m_overlay->logVisible();
        input.chatVisible = m_overlay->chatVisible();
        input.handedness = m_overlay->handedness();
        input.firstVisibleSeat = m_overlay->firstVisibleSeat();
        if (m_posture) {
            const auto posture = m_posture->value();
            input.fold.posture = posture.mode == RoomWindowPosture::Mode::Book
                ? RoomLayoutEngine::FoldPosture::Book
                : posture.mode == RoomWindowPosture::Mode::Tabletop
                ? RoomLayoutEngine::FoldPosture::Tabletop : RoomLayoutEngine::FoldPosture::None;
            input.fold.bounds = posture.bounds.translated(-viewport()->mapTo(window(), QPoint(0, 0)));
            // WindowManager reports full-window folds, including system insets.
            // Clip to the safe viewport while retaining a zero-width crease.
            const QRectF hinge = input.fold.bounds;
            if (hinge.right() >= input.stableRect.left() && hinge.left() <= input.stableRect.right()
                && hinge.bottom() >= input.stableRect.top() && hinge.top() <= input.stableRect.bottom()) {
                const qreal left = qMax(hinge.left(), input.stableRect.left());
                const qreal top = qMax(hinge.top(), input.stableRect.top());
                input.fold.bounds = QRectF(left, top,
                    qMax<qreal>(0.0, qMin(hinge.right(), input.stableRect.right()) - left),
                    qMax<qreal>(0.0, qMin(hinge.bottom(), input.stableRect.bottom()) - top));
            }
            input.fold.separating = posture.separating;
            input.fold.occluding = posture.occluding;
        }
        // Ordinary landscape restores the original GUI, even after portrait rotation.
        bool largeRoom = false;
#if !defined(Q_OS_ANDROID)
        largeRoom = roomScene->largeRoomRequired();
#endif
        input.largeRoom = largeRoom;
        const bool responsiveRoom = largeRoom || (m_responsiveEnabled
            && (input.stableRect.height() > input.stableRect.width()
                || input.fold.posture != RoomLayoutEngine::FoldPosture::None
                || input.fold.separating || input.fold.occluding));
        roomScene->setResponsiveLayout(input, responsiveRoom);
#endif
        const QRectF newSceneRect(QPointF(0, 0), QSizeF(viewportSize));
        roomScene->adjustItems(QSizeF(viewportSize));
        setSceneRect(roomScene->sceneRect());
        if (newSceneRect != roomScene->sceneRect())
            fitInView(roomScene->sceneRect(), Qt::KeepAspectRatio);
        roomScene->applyUiElementScale(m_uiScale);
        roomScene->refreshTouchTargets(transform().m11());
        setBackgroundBrush(false);
#if !defined(QSAN_XP_LEGACY)
        if (responsiveRoom) {
            const auto &layout = roomScene->responsiveLayout();
            m_previousProfile = layout.profile;
            m_hasPreviousProfile = layout.valid;
            m_overlay->setLayoutResult(layout);
        } else {
            // Legacy table keeps its exact geometry; Inspector is an on-demand drawer.
            RoomLayoutEngine::ResponsiveResult overlayLayout;
            overlayLayout.valid = true;
            overlayLayout.mainRect = newSceneRect;
            overlayLayout.profile = RoomLayoutEngine::Profile::LegacyLandscape;
            if (m_overlay->inspectorRequested()) {
                const qreal width = qMin<qreal>(360.0, viewportSize.width());
                overlayLayout.inspectorRect = QRectF(viewportSize.width() - width, 0,
                                                      width, viewportSize.height());
            }
            m_overlay->setLayoutResult(overlayLayout);
        }
        m_overlay->raise();
        m_fitting = false;
#endif
        return;
    }

    if (auto *startScene = qobject_cast<StartScene *>(scene())) {
        const QRectF newSceneRect(-viewportSize.width() / 2.0,
            -viewportSize.height() / 2.0, viewportSize.width(), viewportSize.height());
        startScene->setSceneRect(newSceneRect);
        setSceneRect(startScene->sceneRect());
        if (newSceneRect != startScene->sceneRect())
            fitInView(startScene->sceneRect(), Qt::KeepAspectRatio);
    }
    setBackgroundBrush(true);
}
