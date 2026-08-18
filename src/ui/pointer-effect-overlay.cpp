#include "pointer-effect-overlay.h"
#include "settings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QMainWindow>
#include <QPainter>
#include <QPen>
#include <QPolygon>
#include <QPolygonF>
#include <QRandomGenerator>
#include <QTransform>
#include <QtMath>

namespace {
const qreal kPi = 3.14159265358979323846;
const qreal kEffectScale = 0.5;
const qreal kEffectOpacity = 1.0;
const qreal kDurationScale = 1.0;
const qreal kFragmentScale = 1.2;
const qreal kTrailDurationMs = 300.0;
const qreal kPixelsPerUnit = 720.0;
const int kMaxTrailPoints = 320;

qreal randomRange(qreal minimum, qreal maximum)
{
    return minimum + QRandomGenerator::global()->generateDouble() * (maximum - minimum);
}

QPointF randomPointInTriangle(qreal scale)
{
    const qreal root = qSqrt(QRandomGenerator::global()->generateDouble());
    const qreal a = 1.0 - root;
    const qreal b = root * (1.0 - QRandomGenerator::global()->generateDouble());
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

QImage makeTrailMask()
{
    const int size = 128;
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QRadialGradient gradient(size * 0.35, size * 0.5, size * 0.65);
    gradient.setColorAt(0.0, QColor(255, 255, 255, 255));
    gradient.setColorAt(0.45, QColor(255, 255, 255, 140));
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
    arrow << QPoint(2, 2) << QPoint(2, 28) << QPoint(8, 22)
          << QPoint(14, 30) << QPoint(18, 28) << QPoint(12, 18) << QPoint(22, 18);
    painter.setPen(QPen(QColor(20, 30, 70), 1.4));
    QLinearGradient fill(2, 2, 20, 24);
    fill.setColorAt(0.0, QColor(210, 235, 255));
    fill.setColorAt(1.0, QColor(180, 170, 230));
    painter.setBrush(fill);
    painter.drawPolygon(arrow);
    return image;
}

QImage makeBuiltinMask(const QString &fileName)
{
    if (fileName.contains(QLatin1String("Circle")))
        return makeCircleMask();
    if (fileName.contains(QLatin1String("Ring")))
        return makeRingMask();
    if (fileName.contains(QLatin1String("Trail")))
        return makeTrailMask();
    if (fileName.contains(QLatin1String("Triangle")))
        return makeTriangleMask();
    if (fileName.contains(QLatin1String("MousePoint")))
        return makeCursorArrow();
    return QImage();
}
}

PointerEffectOverlay::PointerEffectOverlay(QWidget *host)
    : QWidget(host, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus
              | Qt::WindowTransparentForInput)
    , m_host(host)
    , m_prevButtons(Qt::NoButton)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    // 使用獨立 Tool 視窗：FitView 的 QOpenGLWidget viewport 不能再疊 GL widget，
    // 且一般 QWidget 子控件會被 native GL 視窗蓋住。

    m_circle = loadAsset(QStringLiteral("FX_TEX_Circle_01.png"), true);
    m_ring = loadAsset(QStringLiteral("FX_TEX_Grad_Ring3.png"), false);
    m_trail = loadAsset(QStringLiteral("FX_TEX_Trail_03.png"), true);
    m_triangle = loadAsset(QStringLiteral("FX_TEX_Triangle_02_1.png"), false);

    const QImage cursorImage = loadAsset(QStringLiteral("PCIcon_MousePoint.png"));
    if (!cursorImage.isNull()) {
        m_baCursor = QCursor(QPixmap::fromImage(cursorImage), 2, 2);
        m_hasBaCursor = true;
    }

    m_clock.start();
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &PointerEffectOverlay::onFrame);

    if (m_host)
        m_host->installEventFilter(this);

    setActive(Config.EnablePointerEffect);
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
    setGeometry(geo);

    bool dialogOpen = false;
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget *widget : topLevels) {
        if (widget == m_host || widget == this || !widget->isVisible())
            continue;
        if (qobject_cast<QDialog *>(widget)) {
            dialogOpen = true;
            break;
        }
    }

    const bool hostVisible = m_host->isVisible() && !m_host->isMinimized() && !dialogOpen;
    if (m_active && hostVisible) {
        if (!isVisible())
            show();
    } else if (isVisible()) {
        hide();
    }
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
    const bool want = Config.EnablePointerEffect;
    if (want != m_active)
        setActive(want);
    if (!m_active)
        return;

    syncToHost();
    if (!isVisible()) {
        applyCursor(false);
        m_prevButtons = Qt::NoButton;
        m_hasLastTrailPos = false;
        return;
    }

    const qreal now = m_clock.elapsed();
    const QPoint local = mapFromGlobal(QCursor::pos());
    const bool inside = rect().contains(local);

    applyCursor(inside);

    if (inside) {
        const QPointF pos(local);
        const Qt::MouseButtons buttons = QGuiApplication::mouseButtons();
        const Qt::MouseButtons pressed = buttons & ~m_prevButtons;
        if (pressed)
            spawnClick(pos, now);
        m_prevButtons = buttons;
        updateTrail(pos, now);
    } else {
        m_prevButtons = Qt::NoButton;
        m_hasLastTrailPos = false;
    }

    pruneExpired(now);
    const bool content = hasVisibleContent(now);
    if (content || m_lastHadContent)
        update();
    m_lastHadContent = content;
}

void PointerEffectOverlay::setActive(bool active)
{
    if (m_active == active && m_timer.isActive() == active)
        return;

    m_active = active;
    m_trailPoints.clear();
    m_clickEffects.clear();
    m_moveParticles.clear();
    m_hasLastTrailPos = false;
    m_emissionCarry = 0;
    m_prevButtons = Qt::NoButton;
    m_lastHadContent = true;

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
    if (inside && m_active && m_hasBaCursor) {
        if (!m_cursorOverridden) {
            QApplication::setOverrideCursor(m_baCursor);
            m_cursorOverridden = true;
        }
    } else if (m_cursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
}

void PointerEffectOverlay::spawnClick(const QPointF &pos, qreal now)
{
    ClickEffect effect;
    effect.position = pos;
    effect.startedAt = now;
    for (int i = 0; i < 2; ++i) {
        MeshParticle mesh;
        mesh.startSize = randomRange(0.12, 0.14);
        mesh.initialRotation = randomRange(0, kPi * 2);
        mesh.rotationBlend = QRandomGenerator::global()->generateDouble();
        effect.meshParticles.append(mesh);
    }
    for (int i = 0; i < 4; ++i)
        effect.triangles.append(createTriangle(pos, now, false));
    m_clickEffects.append(effect);
}

PointerEffectOverlay::TriangleParticle PointerEffectOverlay::createTriangle(
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
    particle.alternateFrame = QRandomGenerator::global()->bounded(2) == 1;
    return particle;
}

void PointerEffectOverlay::updateTrail(const QPointF &pos, qreal now)
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
    if (distance < 0.01 * pixels)
        return;

    m_trailPoints.append({pos, now});
    const qreal expected = m_emissionCarry + distance / pixels * 5.0;
    const int count = qMin(int(qFloor(expected)), 64);
    m_emissionCarry = expected - qFloor(expected);
    for (int i = 0; i < count; ++i) {
        const qreal t = qreal(i + 1) / qreal(count + 1);
        const QPointF spawn(m_lastTrailPos.x() + delta.x() * t,
                            m_lastTrailPos.y() + delta.y() * t);
        m_moveParticles.append(createTriangle(spawn, now, true));
    }
    m_lastTrailPos = pos;
}

void PointerEffectOverlay::pruneExpired(qreal now)
{
    const qreal cutoff = now - kTrailDurationMs;
    while (!m_trailPoints.isEmpty() && m_trailPoints.first().time < cutoff)
        m_trailPoints.removeFirst();
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

bool PointerEffectOverlay::hasVisibleContent(qreal now) const
{
    Q_UNUSED(now);
    return !m_trailPoints.isEmpty() || !m_clickEffects.isEmpty() || !m_moveParticles.isEmpty();
}

void PointerEffectOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);

    const qreal now = m_clock.elapsed();
    drawTrail(painter, now);
    drawClickEffects(painter, now);
    drawTriangles(painter, now, m_moveParticles);
    for (const ClickEffect &effect : m_clickEffects)
        drawTriangles(painter, now, effect.triangles);
}

void PointerEffectOverlay::drawTrail(QPainter &painter, qreal now)
{
    if (m_trail.isNull() || m_trailPoints.size() < 2)
        return;

    QVector<QPointF> pts;
    QVector<qreal> ages;
    pts.reserve(m_trailPoints.size());
    ages.reserve(m_trailPoints.size());
    QPointF previous;
    bool hasPrevious = false;
    for (const TrailPoint &point : m_trailPoints) {
        if (hasPrevious) {
            const QPointF d = point.pos - previous;
            if (d.x() * d.x() + d.y() * d.y() < 0.0625)
                continue;
        }
        pts.append(point.pos);
        ages.append(qBound(0.0, (now - point.time) / kTrailDurationMs, 1.0));
        previous = point.pos;
        hasPrevious = true;
    }
    if (pts.size() < 2)
        return;

    // 無 HDR Bloom，加寬帶狀網格以接近原特效可見寬度
    const qreal coreWidth = qMax(0.5, 0.005 * kPixelsPerUnit * kEffectScale);
    const qreal halfWidth = coreWidth * 4.0;
    const QRect src(0, 0, m_trail.width(), m_trail.height());

    for (int i = 0; i < pts.size() - 1; ++i) {
        const QPointF off0 = trailOffset(pts, i, halfWidth);
        const QPointF off1 = trailOffset(pts, i + 1, halfWidth);
        QPolygonF quad;
        quad << pts[i] + off0 << pts[i] - off0 << pts[i + 1] - off1 << pts[i + 1] + off1;
        const QPolygonF srcPoly = QPolygonF(QRectF(src));
        QTransform transform;
        if (!QTransform::quadToQuad(srcPoly, quad, transform))
            continue;
        const qreal age = (ages[i] + ages[i + 1]) * 0.5;
        qreal fade = 1.0 - age;
        fade = fade * fade * (3.0 - 2.0 * fade);
        const QColor color = trailColor(age);
        const QImage slice = tintCopy(m_trail, src, color, 2.4);
        painter.save();
        painter.setTransform(transform, true);
        painter.setCompositionMode(QPainter::CompositionMode_Plus);
        painter.setOpacity(kEffectOpacity * fade);
        painter.drawImage(src, slice);
        painter.restore();
    }
}

void PointerEffectOverlay::drawClickEffects(QPainter &painter, qreal now)
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
            const QColor blue = QColor::fromRgbF(0.24056602, 0.39061815, 1.0);
            const QColor color = flashProgress < blueTime
                ? lerpColor(Qt::white, blue, flashProgress / blueTime)
                : blue;
            drawSprite(painter, m_circle, m_circle.rect(), effect.position,
                       QSizeF(diameter * 1.34, diameter * 1.34), 0, color,
                       fade * kEffectOpacity, 2.0);
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
            drawSprite(painter, m_ring, m_ring.rect(), effect.position,
                       QSizeF(scale, scale), rotation, meshTriColor(progress),
                       opacity, 3.2);
        }
    }
}

void PointerEffectOverlay::drawTriangles(QPainter &painter, qreal now,
                                         const QVector<TriangleParticle> &particles)
{
    if (m_triangle.isNull())
        return;

    const int halfW = qMax(1, m_triangle.width() / 2);
    for (const TriangleParticle &particle : particles) {
        const qreal simulatedAge = (now - particle.startedAt) / 1000.0 / kDurationScale;
        const qreal progress = simulatedAge / particle.lifetime;
        if (progress < 0.0 || progress > 1.0)
            continue;

        const qreal pixels = kPixelsPerUnit * kEffectScale;
        const QPointF position = particle.origin
            + (particle.shapeOffset + particle.direction * (particle.speed * simulatedAge)) * pixels;
        const qreal size = particle.size * pixels * 0.3078824 * triangleSizeCurve(progress)
            * kFragmentScale;
        const qreal opacity = kEffectOpacity * triangleOpacity(progress);
        if (opacity <= 0.01 || size <= 0.5)
            continue;

        const QRect src = particle.alternateFrame
            ? QRect(halfW, 0, m_triangle.width() - halfW, m_triangle.height())
            : QRect(0, 0, halfW, m_triangle.height());
        drawSprite(painter, m_triangle, src, position, QSizeF(size, size), 0,
                   triangleColor(progress), opacity, 1.86);
    }
}

void PointerEffectOverlay::drawSprite(QPainter &painter, const QImage &image, const QRectF &src,
                                      const QPointF &center, const QSizeF &size, qreal rotationRad,
                                      const QColor &tint, qreal opacity, qreal emission)
{
    if (image.isNull() || opacity <= 0.0 || size.width() <= 0.0 || size.height() <= 0.0)
        return;

    const QImage tinted = tintCopy(image, src.toRect(), tint, emission);
    painter.save();
    painter.translate(center);
    painter.rotate(qRadiansToDegrees(rotationRad));
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.setOpacity(qBound(0.0, opacity, 1.0));
    painter.drawImage(QRectF(-size.width() * 0.5, -size.height() * 0.5, size.width(), size.height()),
                      tinted);
    painter.restore();
}

QImage PointerEffectOverlay::loadAsset(const QString &fileName, bool luminanceAsAlpha)
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

QString PointerEffectOverlay::assetPath(const QString &fileName)
{
    const QString relative = QStringLiteral("image/system/pointer/") + fileName;
    if (QFile::exists(relative))
        return relative;
    const QString beside = QCoreApplication::applicationDirPath() + QLatin1Char('/') + relative;
    if (QFile::exists(beside))
        return beside;
    return relative;
}

QColor PointerEffectOverlay::lerpColor(const QColor &from, const QColor &to, qreal t)
{
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(lerp(from.redF(), to.redF(), t),
                            lerp(from.greenF(), to.greenF(), t),
                            lerp(from.blueF(), to.blueF(), t),
                            lerp(from.alphaF(), to.alphaF(), t));
}

qreal PointerEffectOverlay::lerp(qreal from, qreal to, qreal t)
{
    return from + (to - from) * qBound(0.0, t, 1.0);
}

qreal PointerEffectOverlay::smoothStep(qreal value)
{
    value = qBound(0.0, value, 1.0);
    return value * value * (3.0 - 2.0 * value);
}

qreal PointerEffectOverlay::cubicHermite(qreal from, qreal to, qreal outgoing, qreal incoming,
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

qreal PointerEffectOverlay::ringSizeCurve(qreal progress)
{
    return progress <= 0.2139
        ? cubicHermite(0.325836, 0.715977, 2.400473, 0.911574, progress / 0.2139, 0.2139)
        : cubicHermite(0.715977, 1, 0.911574, 0, (progress - 0.2139) / 0.7861, 0.7861);
}

qreal PointerEffectOverlay::meshTriSizeCurve(qreal progress)
{
    if (progress <= 0.00721)
        return 0.4205;
    if (progress <= 0.2139)
        return cubicHermite(0.420509, 0.715977, 2.400473, 0.911574,
                            (progress - 0.00721) / 0.20669, 0.20669);
    return cubicHermite(0.715977, 1, 0.911574, 0, (progress - 0.2139) / 0.7861, 0.7861);
}

qreal PointerEffectOverlay::meshTriRotationDelta(qreal progress, qreal blend)
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

qreal PointerEffectOverlay::meshTriDissolveThreshold(qreal progress)
{
    return progress <= 0.2
        ? cubicHermite(1, 0, 0, 0, progress / 0.2, 0.2)
        : cubicHermite(0, 1, 2.4249368, 0.27735636, (progress - 0.2) / 0.8, 0.8);
}

QColor PointerEffectOverlay::meshTriColor(qreal progress)
{
    if (progress <= 0.1118)
        return Qt::white;
    if (progress <= 0.5)
        return lerpColor(Qt::white, QColor(76, 167, 255), (progress - 0.1118) / 0.3882);
    return QColor(76, 167, 255);
}

qreal PointerEffectOverlay::triangleSizeCurve(qreal progress)
{
    return progress <= 0.15445
        ? smoothStep(progress / 0.15445)
        : 1.0 - smoothStep((progress - 0.15445) / 0.84555);
}

qreal PointerEffectOverlay::triangleOpacity(qreal progress)
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

QColor PointerEffectOverlay::triangleColor(qreal progress)
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

QColor PointerEffectOverlay::trailColor(qreal progress)
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

QPointF PointerEffectOverlay::trailOffset(const QVector<QPointF> &pts, int index, qreal halfWidth)
{
    auto directionAt = [&pts](int from, int to) {
        QPointF d = pts[to] - pts[from];
        const qreal len = qHypot(d.x(), d.y());
        if (len < 0.0001)
            return QPointF(0, 1);
        return d / len;
    };
    const QPointF prevDir = index > 0 ? directionAt(index - 1, index) : directionAt(0, 1);
    const QPointF nextDir = index < pts.size() - 1 ? directionAt(index, index + 1) : prevDir;
    const QPointF prevNormal(-prevDir.y(), prevDir.x());
    const QPointF nextNormal(-nextDir.y(), nextDir.x());
    if (index == 0)
        return nextNormal * halfWidth;
    if (index == pts.size() - 1)
        return prevNormal * halfWidth;

    QPointF miter = prevNormal + nextNormal;
    const qreal miterLen2 = miter.x() * miter.x() + miter.y() * miter.y();
    if (miterLen2 < 0.0001)
        return nextNormal * halfWidth;
    miter /= qSqrt(miterLen2);
    const qreal denominator = qMax(0.35, qAbs(QPointF::dotProduct(miter, nextNormal)));
    const qreal length = qMin(halfWidth / denominator, halfWidth * 2.0);
    return miter * length;
}
