#include "pointer-effect-overlay.h"
#include "settings.h"
#include "ui-rng.h"
#include "effects/effects-policy.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QMainWindow>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygon>
#include <QQuickWindow>
#include <QtMath>

namespace {
const qreal kPi = 3.14159265358979323846;
const qreal kEffectScale = 0.5;
const qreal kEffectOpacity = 1.0;
const qreal kDurationScale = 1.0;
const qreal kFragmentScale = 1.2;
const qreal kTrailDurationMs = 300.0;
const qreal kPixelsPerUnit = 720.0;
const int kMaxTrailPoints = 80;
const int kMaxMoveParticles = 24;
const int kMaxMoveSpawnPerStep = 6;

qreal randomRange(qreal minimum, qreal maximum)
{
    return minimum + UiRng::generateDouble() * (maximum - minimum);
}

QPointF randomPointInTriangle(qreal scale)
{
    const qreal root = qSqrt(UiRng::generateDouble());
    const qreal a = 1.0 - root;
    const qreal b = root * (1.0 - UiRng::generateDouble());
    const qreal c = 1.0 - a - b;
    return QPointF((a * 0.099028 + b * -0.146066 + c * 0.099028) * scale,
                   (a * 0.134899 + c * -0.134899) * scale);
}

QImage tintCopy(const QImage &src, const QRect &srcRect, const QColor &tint, qreal emission)
{
    QImage piece = src.copy(srcRect).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qreal tr = tint.redF() * emission;
    const qreal tg = tint.greenF() * emission;
    const qreal tb = tint.blueF() * emission;
    for (int y = 0; y < piece.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(piece.scanLine(y));
        for (int x = 0; x < piece.width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            if (a == 0)
                continue;
            line[x] = qRgba(qBound(0, int(qRed(px) * tr), 255),
                            qBound(0, int(qGreen(px) * tg), 255),
                            qBound(0, int(qBlue(px) * tb), 255),
                            a);
        }
    }
    return piece;
}

QPixmap bakeTintedPixmap(const QImage &src, const QColor &tint, qreal emission)
{
    if (src.isNull())
        return {};
    return QPixmap::fromImage(tintCopy(src, src.rect(), tint, emission));
}

QImage makeCircleMask()
{
    const int size = 128;
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QRadialGradient gradient(size * 0.5, size * 0.5, size * 0.5);
    gradient.setColorAt(0.0, QColor(255, 255, 255, 255));
    gradient.setColorAt(0.72, QColor(255, 255, 255, 230));
    gradient.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(image.rect());
    return image;
}

QImage makeRingMask()
{
    const int width = 256;
    const int height = 128;
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, width, 0);
    gradient.setColorAt(0.0, QColor(255, 255, 255, 0));
    gradient.setColorAt(0.5, QColor(255, 255, 255, 255));
    gradient.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(image.rect(), gradient);
    return image;
}

QImage makeTriangleMask()
{
    const int width = 256;
    const int height = 128;
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    const QPoint down[3] = {
        QPoint(width / 4, height - 8),
        QPoint(16, 8),
        QPoint(width / 2 - 16, 8)
    };
    const QPoint up[3] = {
        QPoint(width * 3 / 4, 8),
        QPoint(width / 2 + 16, height - 8),
        QPoint(width - 16, height - 8)
    };
    painter.drawPolygon(down, 3);
    painter.drawPolygon(up, 3);
    return image;
}

QImage makeCursorArrow()
{
    const int size = 32;
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPolygon arrow;
    arrow << QPoint(3, 3) << QPoint(3, 27) << QPoint(9, 21)
          << QPoint(14, 29) << QPoint(18, 27) << QPoint(12, 18) << QPoint(21, 18);
    QLinearGradient fill(3, 3, 18, 24);
    fill.setColorAt(0.0, QColor(120, 214, 250));
    fill.setColorAt(1.0, QColor(47, 142, 196));
    painter.setBrush(fill);
    painter.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(arrow);
    return image;
}

QImage styleCursorImage(const QImage &src)
{
    if (src.isNull())
        return makeCursorArrow();

    const QImage src32 = src.convertToFormat(QImage::Format_ARGB32);
    const int w = src32.width();
    const int h = src32.height();
    QImage out(w, h, QImage::Format_ARGB32);
    out.fill(Qt::transparent);

    const QColor innerTop(120, 214, 250);
    const QColor innerBot(47, 142, 196);

    auto alphaAt = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return 0;
        return qAlpha(src32.pixel(x, y));
    };

    for (int y = 0; y < h; ++y) {
        const qreal t = h > 1 ? qreal(y) / qreal(h - 1) : 0;
        const QColor fill(
            int(innerTop.red() + (innerBot.red() - innerTop.red()) * t),
            int(innerTop.green() + (innerBot.green() - innerTop.green()) * t),
            int(innerTop.blue() + (innerBot.blue() - innerTop.blue()) * t));
        for (int x = 0; x < w; ++x) {
            const int a = alphaAt(x, y);
            if (a < 8)
                continue;
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    if (alphaAt(x + dx, y + dy) < 48) {
                        edge = true;
                        break;
                    }
                }
            }
            const QColor c = edge ? Qt::white : fill;
            out.setPixelColor(x, y, QColor(c.red(), c.green(), c.blue(), a));
        }
    }
    return out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage makeBuiltinMask(const QString &fileName)
{
    if (fileName.contains(QLatin1String("Circle")))
        return makeCircleMask();
    if (fileName.contains(QLatin1String("Ring")))
        return makeRingMask();
    if (fileName.contains(QLatin1String("Triangle")))
        return makeTriangleMask();
    if (fileName.contains(QLatin1String("MousePoint")))
        return makeCursorArrow();
    return QImage();
}
}

PointerFxEngine::PointerFxEngine()
    : m_prevButtons(Qt::NoButton)
{
    const QImage circle = loadAsset(QStringLiteral("FX_TEX_Circle_01.png"), true);
    const QImage ring = loadAsset(QStringLiteral("FX_TEX_Grad_Ring3.png"), false);
    const QImage triangle = loadAsset(QStringLiteral("FX_TEX_Triangle_02_1.png"), false);
    m_circlePm = QPixmap::fromImage(circle);
    m_circleBluePm = bakeTintedPixmap(circle, QColor::fromRgbF(0.24056602, 0.39061815, 1.0), 2.0);
    m_ringPm = QPixmap::fromImage(ring);
    m_ringBluePm = bakeTintedPixmap(ring, QColor(76, 167, 255), 3.2);
    m_trianglePm = bakeTintedPixmap(triangle, QColor::fromRgbF(0.3726415, 0.7731873, 1.0), 1.86);

    const QImage cursorImage = loadAsset(QStringLiteral("PCIcon_MousePoint.png"));
    if (!cursorImage.isNull()) {
        m_baCursor = QCursor(QPixmap::fromImage(styleCursorImage(cursorImage)), 2, 2);
        m_hasBaCursor = true;
    }
    m_clock.start();
}

void PointerFxEngine::reset()
{
    m_trailPoints.clear();
    m_clickEffects.clear();
    m_moveParticles.clear();
    m_hasLastTrailPos = false;
    m_emissionCarry = 0;
    m_prevButtons = Qt::NoButton;
}

qreal PointerFxEngine::elapsed() const
{
    return m_clock.elapsed();
}

bool PointerFxEngine::hasBaCursor() const
{
    return m_hasBaCursor;
}

QCursor PointerFxEngine::baCursor() const
{
    return m_baCursor;
}

bool PointerFxEngine::hasContent() const
{
    return hasVisibleContent(m_clock.elapsed());
}

QRect PointerFxEngine::tick(const QPointF &localPos, bool inside, Qt::MouseButtons buttons)
{
    const qreal now = m_clock.elapsed();
    if (inside) {
        const Qt::MouseButtons pressed = buttons & ~m_prevButtons;
        if (pressed)
            spawnClick(localPos, now);
        m_prevButtons = buttons;
        updateTrail(localPos, now);
    } else {
        m_prevButtons = Qt::NoButton;
        m_hasLastTrailPos = false;
    }
    pruneExpired(now);
    return hasVisibleContent(now) ? visibleBounds(now) : QRect();
}

void PointerFxEngine::paint(QPainter &painter)
{
    const qreal now = m_clock.elapsed();
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawTrail(painter, now);
    drawClickEffects(painter, now);
    drawTriangles(painter, now, m_moveParticles);
    for (const ClickEffect &effect : m_clickEffects)
        drawTriangles(painter, now, effect.triangles);
}

PointerEffectOverlay::PointerEffectOverlay(QWidget *host)
    : QWidget(host, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus
              | Qt::WindowTransparentForInput)
    , m_host(host)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    // 使用獨立 Tool 視窗：FitView 的 QOpenGLWidget viewport 不能再疊 GL widget，
    // 且一般 QWidget 子控件會被 native GL 視窗蓋住。

    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &PointerEffectOverlay::onFrame);

    if (m_host)
        m_host->installEventFilter(this);

    syncToHost();
}

PointerEffectOverlay::~PointerEffectOverlay()
{
    applyCursor(false);
}

void PointerEffectOverlay::syncToHost()
{
    if (!m_host)
        return;

    QMainWindow *mainWindow = qobject_cast<QMainWindow *>(m_host);
    QWidget *area = (mainWindow && mainWindow->centralWidget())
        ? mainWindow->centralWidget()
        : m_host;
    const QRect geo(area->mapToGlobal(QPoint(0, 0)), area->size());
    if (geo != m_syncedGeo) {
        m_syncedGeo = geo;
        setGeometry(geo);
    }

    const bool dialogOpen = QApplication::activeModalWidget() != nullptr;

    const bool hostVisible = m_pageEnabled && m_host->isVisible()
        && !m_host->isMinimized() && !dialogOpen;
    if (m_active && hostVisible) {
        if (!isVisible())
            show();
    } else if (isVisible()) {
        hide();
    }
}

void PointerEffectOverlay::setPageEnabled(bool enabled)
{
    if (m_pageEnabled == enabled)
        return;
    m_pageEnabled = enabled;
    if (!enabled) {
        applyCursor(false);
        m_fx.reset();
        if (isVisible())
            hide();
    }
    onFrame();
}

bool PointerEffectOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::WindowStateChange:
            syncToHost();
            break;
        case QEvent::Hide:
            hide();
            applyCursor(false);
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PointerEffectOverlay::onFrame()
{
    const bool want = (Config.EnablePointerEffect && G_EFFECTS.animationsEnabled()) && m_pageEnabled;
    if (want != m_active)
        setActive(want);
    if (!m_active)
        return;

    syncToHost();
    if (!isVisible()) {
        applyCursor(false);
        m_fx.reset();
        return;
    }

    const QPoint local = mapFromGlobal(QCursor::pos());
    const bool inside = rect().contains(local);
    applyCursor(inside);
    const QRect dirty = m_fx.tick(QPointF(local), inside, QGuiApplication::mouseButtons());
    const QRect toPaint = dirty.united(m_lastPainted).intersected(rect());
    if (!toPaint.isEmpty())
        update(toPaint.adjusted(-4, -4, 4, 4));
    m_lastPainted = dirty.intersected(rect());
    m_lastHadContent = !dirty.isEmpty();
}

void PointerEffectOverlay::setActive(bool active)
{
    if (m_active == active && m_timer.isActive() == active)
        return;

    m_active = active;
    m_fx.reset();
    m_lastHadContent = true;
    m_lastPainted = QRect();

    if (active) {
        m_timer.start();
        syncToHost();
    } else {
        m_timer.stop();
        applyCursor(false);
        hide();
        update();
    }
}

void PointerEffectOverlay::applyCursor(bool inside)
{
    if (inside && m_active && m_fx.hasBaCursor()) {
        if (!m_cursorOverridden) {
            QApplication::setOverrideCursor(m_fx.baCursor());
            m_cursorOverridden = true;
        }
    } else if (m_cursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
}

void PointerFxEngine::spawnClick(const QPointF &pos, qreal now)
{
    ClickEffect effect;
    effect.position = pos;
    effect.startedAt = now;
    for (int i = 0; i < 2; ++i) {
        MeshParticle mesh;
        mesh.startSize = randomRange(0.12, 0.14);
        mesh.initialRotation = randomRange(0, kPi * 2);
        mesh.rotationBlend = UiRng::generateDouble();
        effect.meshParticles.append(mesh);
    }
    for (int i = 0; i < 4; ++i)
        effect.triangles.append(createTriangle(pos, now, false));
    m_clickEffects.append(effect);
}

PointerFxEngine::TriangleParticle PointerFxEngine::createTriangle(
    const QPointF &pos, qreal now, bool movement)
{
    const qreal angle = randomRange(0, kPi * 2);
    TriangleParticle particle;
    particle.origin = pos;
    particle.direction = QPointF(qCos(angle), qSin(angle));
    particle.shapeOffset = movement
        ? randomPointInTriangle(0.15)
        : particle.direction * randomRange(0.09, 0.098);
    particle.startedAt = now;
    particle.lifetime = movement ? randomRange(0.2, 0.4) : randomRange(0.6, 0.7);
    particle.speed = movement ? randomRange(0.067, 0.1) : randomRange(0.09, 0.13);
    particle.size = randomRange(0.1, 0.2);
    particle.alternateFrame = UiRng::bounded(2) == 1;
    return particle;
}

void PointerFxEngine::updateTrail(const QPointF &pos, qreal now)
{
    const qreal pixels = kPixelsPerUnit * kEffectScale;
    if (!m_hasLastTrailPos) {
        m_lastTrailPos = pos;
        m_hasLastTrailPos = true;
        m_trailPoints.append({pos, now});
        return;
    }

    const QPointF delta = pos - m_lastTrailPos;
    const qreal distance = qHypot(delta.x(), delta.y());
    if (distance < 0.02 * pixels)
        return;

    m_trailPoints.append({pos, now});
    const qreal expected = m_emissionCarry + distance / pixels * 2.0;
    const int count = qMin(int(qFloor(expected)), kMaxMoveSpawnPerStep);
    m_emissionCarry = expected - qFloor(expected);
    for (int i = 0; i < count; ++i) {
        const qreal t = qreal(i + 1) / qreal(count + 1);
        const QPointF spawn(m_lastTrailPos.x() + delta.x() * t,
                            m_lastTrailPos.y() + delta.y() * t);
        m_moveParticles.append(createTriangle(spawn, now, true));
    }
    while (m_moveParticles.size() > kMaxMoveParticles)
        m_moveParticles.removeFirst();
    m_lastTrailPos = pos;
}

void PointerFxEngine::pruneExpired(qreal now)
{
    const qreal cutoff = now - kTrailDurationMs;
    int first = 0;
    while (first < m_trailPoints.size() && m_trailPoints[first].time < cutoff)
        ++first;
    if (first > 0)
        m_trailPoints.erase(m_trailPoints.begin(), m_trailPoints.begin() + first);
    if (m_trailPoints.size() > kMaxTrailPoints)
        m_trailPoints.erase(m_trailPoints.begin(),
                            m_trailPoints.begin() + (m_trailPoints.size() - kMaxTrailPoints));

    for (int i = m_clickEffects.size() - 1; i >= 0; --i) {
        if (now - m_clickEffects[i].startedAt > 700.0 * kDurationScale)
            m_clickEffects.removeAt(i);
    }
    for (int i = m_moveParticles.size() - 1; i >= 0; --i) {
        if (now - m_moveParticles[i].startedAt > m_moveParticles[i].lifetime * 1000.0 * kDurationScale)
            m_moveParticles.removeAt(i);
    }
}

bool PointerFxEngine::hasVisibleContent(qreal now) const
{
    Q_UNUSED(now);
    return !m_trailPoints.isEmpty() || !m_clickEffects.isEmpty() || !m_moveParticles.isEmpty();
}

QPointF PointerFxEngine::trianglePosition(const TriangleParticle &particle, qreal now) const
{
    const qreal simulatedAge = (now - particle.startedAt) / 1000.0 / kDurationScale;
    const qreal pixels = kPixelsPerUnit * kEffectScale;
    return particle.origin
        + (particle.shapeOffset + particle.direction * (particle.speed * simulatedAge)) * pixels;
}

QRect PointerFxEngine::visibleBounds(qreal now) const
{
    QRect bounds;
    auto include = [&bounds](const QPointF &pos, qreal radius) {
        bounds = bounds.united(QRect(int(pos.x() - radius), int(pos.y() - radius),
                                     int(radius * 2) + 2, int(radius * 2) + 2));
    };
    for (const TrailPoint &point : m_trailPoints)
        include(point.pos, 36);
    for (const ClickEffect &effect : m_clickEffects) {
        include(effect.position, 96);
        for (const TriangleParticle &particle : effect.triangles)
            include(trianglePosition(particle, now), 40);
    }
    for (const TriangleParticle &particle : m_moveParticles)
        include(trianglePosition(particle, now), 40);
    return bounds;
}

void PointerEffectOverlay::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(event->rect(), Qt::transparent);
    m_fx.paint(painter);
}

void PointerFxEngine::drawTrail(QPainter &painter, qreal now)
{
    if (m_trailPoints.size() < 2)
        return;

    QPainterPath buckets[4];
    int prevBucket = -1;
    QPointF prevPos;
    bool hasPrev = false;
    for (const TrailPoint &point : m_trailPoints) {
        if (hasPrev) {
            const QPointF d = point.pos - prevPos;
            if (d.x() * d.x() + d.y() * d.y() < 0.0625)
                continue;
        }
        const qreal age = qBound(0.0, (now - point.time) / kTrailDurationMs, 1.0);
        const int bucket = qBound(0, int(age * 4.0), 3);
        if (!hasPrev || bucket != prevBucket)
            buckets[bucket].moveTo(hasPrev ? prevPos : point.pos);
        buckets[bucket].lineTo(point.pos);
        prevPos = point.pos;
        prevBucket = bucket;
        hasPrev = true;
    }

    const qreal coreWidth = qMax(0.5, 0.005 * kPixelsPerUnit * kEffectScale);
    const qreal widths[] = {coreWidth * 16.0, coreWidth * 9.0, coreWidth * 3.5};
    const qreal opacities[] = {0.22, 0.5, 1.0};
    const qreal bucketAge[] = {0.12, 0.38, 0.62, 0.88};

    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    for (int layer = 0; layer < 3; ++layer) {
        QPen pen(Qt::white, widths[layer], Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        for (int bucket = 0; bucket < 4; ++bucket) {
            if (buckets[bucket].elementCount() < 2)
                continue;
            qreal fade = 1.0 - bucketAge[bucket];
            fade = fade * fade * (3.0 - 2.0 * fade);
            QColor color = trailColor(bucketAge[bucket]);
            color.setAlphaF(qBound(0.0, kEffectOpacity * opacities[layer] * fade, 1.0));
            pen.setColor(color);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(buckets[bucket]);
        }
    }
}

void PointerFxEngine::drawClickEffects(QPainter &painter, qreal now)
{
    for (const ClickEffect &effect : m_clickEffects) {
        const qreal flashProgress = (now - effect.startedAt) / (200.0 * kDurationScale);
        if (flashProgress >= 0.0 && flashProgress <= 1.0) {
            const qreal curve = ringSizeCurve(flashProgress);
            const qreal diameter = 0.12 * 2.0 * curve * kPixelsPerUnit * kEffectScale;
            const qreal fade = flashProgress <= 0.1088
                ? 1.0
                : 1.0 - (flashProgress - 0.1088) / 0.8912;
            const qreal blueTime = 7903.0 / 65535.0;
            const qreal mix = flashProgress < blueTime ? flashProgress / blueTime : 1.0;
            const QSizeF size(diameter * 1.34, diameter * 1.34);
            if (1.0 - mix > 0.01)
                drawSprite(painter, m_circlePm, m_circlePm.rect(), effect.position,
                           size, 0, fade * (1.0 - mix) * kEffectOpacity);
            if (mix > 0.01)
                drawSprite(painter, m_circleBluePm, m_circleBluePm.rect(), effect.position,
                           size, 0, fade * mix * kEffectOpacity);
        }

        for (const MeshParticle &particle : effect.meshParticles) {
            const qreal progress = (now - effect.startedAt) / (600.0 * kDurationScale);
            if (progress < 0.0 || progress > 1.0)
                continue;
            const qreal scale = particle.startSize * meshTriSizeCurve(progress)
                * kPixelsPerUnit * kEffectScale;
            const qreal rotation = particle.initialRotation
                - meshTriRotationDelta(progress, particle.rotationBlend);
            const qreal threshold = meshTriDissolveThreshold(progress);
            const qreal opacity = kEffectOpacity * 0.92 * (1.0 - qBound(0.0, threshold, 1.0));
            if (opacity <= 0.01)
                continue;
            const qreal mix = progress <= 0.1118
                ? 0.0
                : (progress >= 0.5 ? 1.0 : (progress - 0.1118) / 0.3882);
            const QSizeF size(scale, scale);
            if (1.0 - mix > 0.01)
                drawSprite(painter, m_ringPm, m_ringPm.rect(), effect.position,
                           size, rotation, opacity * (1.0 - mix));
            if (mix > 0.01)
                drawSprite(painter, m_ringBluePm, m_ringBluePm.rect(), effect.position,
                           size, rotation, opacity * mix);
        }
    }
}

void PointerFxEngine::drawTriangles(QPainter &painter, qreal now,
                                         const QVector<TriangleParticle> &particles)
{
    if (m_trianglePm.isNull())
        return;

    const int halfW = qMax(1, m_trianglePm.width() / 2);
    for (const TriangleParticle &particle : particles) {
        const qreal simulatedAge = (now - particle.startedAt) / 1000.0 / kDurationScale;
        const qreal progress = simulatedAge / particle.lifetime;
        if (progress < 0.0 || progress > 1.0)
            continue;

        const QPointF position = trianglePosition(particle, now);
        const qreal size = particle.size * kPixelsPerUnit * kEffectScale * 0.3078824
            * triangleSizeCurve(progress) * kFragmentScale;
        const qreal opacity = kEffectOpacity * triangleOpacity(progress);
        if (opacity <= 0.01 || size <= 0.5)
            continue;

        const QRect src = particle.alternateFrame
            ? QRect(halfW, 0, m_trianglePm.width() - halfW, m_trianglePm.height())
            : QRect(0, 0, halfW, m_trianglePm.height());
        drawSprite(painter, m_trianglePm, src, position, QSizeF(size, size), 0, opacity);
    }
}

void PointerFxEngine::drawSprite(QPainter &painter, const QPixmap &pixmap, const QRectF &src,
                                      const QPointF &center, const QSizeF &size, qreal rotationRad,
                                      qreal opacity)
{
    if (pixmap.isNull() || opacity <= 0.0 || size.width() <= 0.0 || size.height() <= 0.0)
        return;

    painter.save();
    painter.translate(center);
    painter.rotate(qRadiansToDegrees(rotationRad));
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.setOpacity(qBound(0.0, opacity, 1.0));
    painter.drawPixmap(QRectF(-size.width() * 0.5, -size.height() * 0.5, size.width(), size.height()),
                       pixmap, src);
    painter.restore();
}

QImage PointerFxEngine::loadAsset(const QString &fileName, bool luminanceAsAlpha)
{
    QImage image(assetPath(fileName));
    if (image.isNull())
        return makeBuiltinMask(fileName);

    // 可選外部貼圖：白圖+不透明黑底須把亮度寫進 alpha，否則 Plus 會留下黑方塊
    image = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = line[x];
            const int alpha = luminanceAsAlpha
                ? qMax(qRed(px), qMax(qGreen(px), qBlue(px)))
                : qAlpha(px);
            line[x] = qRgba(255, 255, 255, alpha);
        }
    }
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QString PointerFxEngine::assetPath(const QString &fileName)
{
    const QString relative = QStringLiteral("image/system/pointer/") + fileName;
    if (QFile::exists(relative))
        return relative;
    const QString beside = QCoreApplication::applicationDirPath() + QLatin1Char('/') + relative;
    if (QFile::exists(beside))
        return beside;
    return relative;
}

QColor PointerFxEngine::lerpColor(const QColor &from, const QColor &to, qreal t)
{
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(lerp(from.redF(), to.redF(), t),
                            lerp(from.greenF(), to.greenF(), t),
                            lerp(from.blueF(), to.blueF(), t),
                            lerp(from.alphaF(), to.alphaF(), t));
}

qreal PointerFxEngine::lerp(qreal from, qreal to, qreal t)
{
    return from + (to - from) * qBound(0.0, t, 1.0);
}

qreal PointerFxEngine::smoothStep(qreal value)
{
    value = qBound(0.0, value, 1.0);
    return value * value * (3.0 - 2.0 * value);
}

qreal PointerFxEngine::cubicHermite(qreal from, qreal to, qreal outgoing, qreal incoming,
                                         qreal normalizedTime, qreal duration)
{
    const qreal time = qBound(0.0, normalizedTime, 1.0);
    const qreal squared = time * time;
    const qreal cubed = squared * time;
    return qBound(0.0,
                  (2 * cubed - 3 * squared + 1) * from
                      + (cubed - 2 * squared + time) * outgoing * duration
                      + (-2 * cubed + 3 * squared) * to
                      + (cubed - squared) * incoming * duration,
                  1.0);
}

qreal PointerFxEngine::ringSizeCurve(qreal progress)
{
    return progress <= 0.2139
        ? cubicHermite(0.325836, 0.715977, 2.400473, 0.911574, progress / 0.2139, 0.2139)
        : cubicHermite(0.715977, 1, 0.911574, 0, (progress - 0.2139) / 0.7861, 0.7861);
}

qreal PointerFxEngine::meshTriSizeCurve(qreal progress)
{
    if (progress <= 0.00721)
        return 0.4205;
    if (progress <= 0.2139)
        return cubicHermite(0.420509, 0.715977, 2.400473, 0.911574,
                            (progress - 0.00721) / 0.20669, 0.20669);
    return cubicHermite(0.715977, 1, 0.911574, 0, (progress - 0.2139) / 0.7861, 0.7861);
}

qreal PointerFxEngine::meshTriRotationDelta(qreal progress, qreal blend)
{
    auto angularVelocity = [blend](qreal p) {
        const qreal first = p <= 0.149 ? 1.0 : lerp(1.0, 0.4556, smoothStep((p - 0.149) / 0.851));
        const qreal second = p <= 0.1587
            ? 0.7988
            : lerp(0.7988, -0.06509, smoothStep((p - 0.1587) / 0.8413));
        return lerp(first, second, blend) * 11.1701069;
    };
    const qreal step = progress * 0.6 / 12.0;
    qreal sum = 0;
    for (int i = 0; i < 12; ++i)
        sum += angularVelocity(progress * (i + 0.5) / 12.0) * step;
    return sum;
}

qreal PointerFxEngine::meshTriDissolveThreshold(qreal progress)
{
    return progress <= 0.2
        ? cubicHermite(1, 0, 0, 0, progress / 0.2, 0.2)
        : cubicHermite(0, 1, 2.4249368, 0.27735636, (progress - 0.2) / 0.8, 0.8);
}

QColor PointerFxEngine::meshTriColor(qreal progress)
{
    if (progress <= 0.1118)
        return Qt::white;
    if (progress <= 0.5)
        return lerpColor(Qt::white, QColor(76, 167, 255), (progress - 0.1118) / 0.3882);
    return QColor(76, 167, 255);
}

qreal PointerFxEngine::triangleSizeCurve(qreal progress)
{
    return progress <= 0.15445
        ? smoothStep(progress / 0.15445)
        : 1.0 - smoothStep((progress - 0.15445) / 0.84555);
}

qreal PointerFxEngine::triangleOpacity(qreal progress)
{
    static const qreal times[] = {0, 0.2882, 0.3647, 0.4706, 0.5734, 0.6676, 0.7561, 0.8529, 1};
    static const qreal values[] = {1, 1, 0, 1, 0, 1, 0, 1, 1};
    for (int i = 1; i < 9; ++i) {
        if (progress <= times[i])
            return lerp(values[i - 1], values[i],
                        (progress - times[i - 1]) / (times[i] - times[i - 1]));
    }
    return 1;
}

QColor PointerFxEngine::triangleColor(qreal progress)
{
    static const qreal times[] = {
        11951.0 / 65535.0, 18504.0 / 65535.0, 30262.0 / 65535.0,
        43369.0 / 65535.0, 54163.0 / 65535.0
    };
    const QColor colors[] = {
        Qt::white,
        QColor::fromRgbF(0.3726415, 0.7731873, 1),
        QColor::fromRgbF(0.3725490, 0.7725491, 1),
        QColor::fromRgbF(0.3529412, 0.7294118, 0.9450981),
        QColor::fromRgbF(0.3725490, 0.7725491, 1)
    };
    const qreal visibleTransitionEnd = 0.55;
    qreal sampled = progress;
    if (progress > times[0] && progress <= visibleTransitionEnd) {
        sampled = lerp(times[0], times[1], (progress - times[0]) / (visibleTransitionEnd - times[0]));
    } else if (progress > visibleTransitionEnd) {
        sampled = lerp(times[1], 1.0, (progress - visibleTransitionEnd) / (1.0 - visibleTransitionEnd));
    }

    QColor gradient = colors[0];
    if (sampled >= times[4]) {
        gradient = colors[4];
    } else {
        for (int i = 1; i < 5; ++i) {
            if (sampled > times[i])
                continue;
            gradient = lerpColor(colors[i - 1], colors[i],
                                 (sampled - times[i - 1]) / (times[i] - times[i - 1]));
            break;
        }
    }
    const qreal startColor = 0.53773582;
    return QColor::fromRgbF(gradient.redF() * startColor,
                            gradient.greenF() * startColor,
                            gradient.blueF() * startColor);
}

QColor PointerFxEngine::trailColor(qreal progress)
{
    const qreal firstTime = 1349.0 / 65535.0;
    const qreal secondTime = 27563.0 / 65535.0;
    const QColor bright = QColor::fromRgbF(0, 0.39058137, 1);
    const QColor dim = QColor::fromRgbF(0, 0.09486991, 0.28235295);
    if (progress <= firstTime)
        return bright;
    if (progress <= secondTime)
        return lerpColor(bright, dim, (progress - firstTime) / (secondTime - firstTime));
    return lerpColor(dim, Qt::black, (progress - secondTime) / (1.0 - secondTime));
}

HomePointerFxItem::HomePointerFxItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
    setOpaquePainting(false);
    setFillColor(Qt::transparent);
    setAntialiasing(true);
    setMipmap(false);
    // NVIDIA：FramebufferObject + Plus 混合會在 nvoglv64 對 nullptr 讀取 (0xC0000005)
    setRenderTarget(QQuickPaintedItem::Image);
    // Keep the software Image target at half of the 1920x1080 design canvas.
    setTextureSize(QSize(960, 540));
    m_timer.setInterval(32);
    connect(&m_timer, &QTimer::timeout, this, &HomePointerFxItem::onFrame);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *win) {
        if (win) {
            m_timer.start();
        } else {
            m_timer.stop();
            applyCursor(false);
            m_fx.reset();
            m_lastPainted = QRect();
            m_hadContent = false;
        }
    });
    if (window())
        m_timer.start();
}

HomePointerFxItem::~HomePointerFxItem()
{
    applyCursor(false);
}

void HomePointerFxItem::paint(QPainter *painter)
{
    if (!painter || width() < 1 || height() < 1)
        return;
    m_fx.paint(*painter);
}

void HomePointerFxItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemVisibleHasChanged && !value.boolValue) {
        applyCursor(false);
        m_fx.reset();
    }
    QQuickPaintedItem::itemChange(change, value);
}

void HomePointerFxItem::onFrame()
{
    if (!window())
        return;
    const bool want = (Config.EnablePointerEffect && G_EFFECTS.animationsEnabled()) && isVisible() && width() > 0 && height() > 0;
    if (!want) {
        applyCursor(false);
        if (m_hadContent) {
            m_fx.reset();
            m_hadContent = false;
            const QRect dirty = m_lastPainted;
            m_lastPainted = QRect();
            if (!dirty.isEmpty())
                update(dirty.adjusted(-4, -4, 4, 4));
        }
        return;
    }

    const QPointF pos = mapFromGlobal(QCursor::pos());
    const bool inside = QRectF(0, 0, width(), height()).contains(pos);
    applyCursor(inside);
    const QRect dirty = m_fx.tick(pos, inside, QGuiApplication::mouseButtons());
    const bool content = !dirty.isEmpty();
    const QRect itemRect = QRectF(0, 0, width(), height()).toAlignedRect();
    const QRect toPaint = dirty.united(m_lastPainted).intersected(itemRect);
    if (!toPaint.isEmpty())
        update(toPaint.adjusted(-4, -4, 4, 4));
    m_lastPainted = dirty.intersected(itemRect);
    m_hadContent = content;
}

void HomePointerFxItem::applyCursor(bool inside)
{
    if (inside && (Config.EnablePointerEffect && G_EFFECTS.animationsEnabled()) && m_fx.hasBaCursor()) {
        if (!m_cursorOverridden) {
            QApplication::setOverrideCursor(m_fx.baCursor());
            m_cursorOverridden = true;
        }
    } else if (m_cursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
}
