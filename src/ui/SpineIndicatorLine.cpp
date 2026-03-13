/******************************************************************************
 * SpineIndicatorLine.cpp
 *
 * Indicator-line sub-effect: beam + hit-burst, each backed by SpineGlItem.
 *****************************************************************************/

#include "SpineIndicatorLine.h"
#include "SpineGlItem.h"

#include <QGraphicsScene>
#include <QtMath>
#include <QTimer>

// ═══════════════════════════════════════════════════════════════════════════
//  Construction / destruction
// ═══════════════════════════════════════════════════════════════════════════

SpineIndicatorLine::SpineIndicatorLine(QGraphicsScene *scene, QObject *parent)
    : QObject(parent)
    , _scene(scene)
    , _beamItem(nullptr)
    , _burstItem(nullptr)
    , _estimatedDuration(1.0f)
    , _finished(false)
    , _burstPending(false)
{
}

SpineIndicatorLine::~SpineIndicatorLine()
{
    if (_beamItem) {
        _beamItem->stop();
        if (_scene)
            _scene->removeItem(_beamItem);
        delete _beamItem;
        _beamItem = nullptr;
    }
    if (_burstItem) {
        _burstItem->stop();
        if (_scene)
            _scene->removeItem(_burstItem);
        delete _burstItem;
        _burstItem = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Launch
// ═══════════════════════════════════════════════════════════════════════════

void SpineIndicatorLine::launch(const QString &skelName,
                                 const QString &runtimeVersion,
                                 const QPointF &start,
                                 const QPointF &end,
                                 double rotationDeg,
                                 float speed)
{
    if (!_scene || skelName.isEmpty()) {
        _finished = true;
        emit finished();
        return;
    }

    _beamItem = new SpineGlItem();
    _beamItem->setZValue(10001);

    if (!runtimeVersion.isEmpty())
        _beamItem->setRuntimeVersionHint(runtimeVersion);

    QString fullPath = _assetPrefix + skelName;
    if (!_beamItem->loadSpine(fullPath)) {
        qWarning("[SpineIndicatorLine] Failed to load beam skeleton: %s",
                 qPrintable(fullPath));
        delete _beamItem;
        _beamItem = nullptr;
        _finished = true;
        emit finished();
        return;
    }

    // Position at start point
    _beamItem->setSpinePosition(start);

    // Apply rotation via QGraphicsItem transform
    _beamItem->setRotation(rotationDeg);

    // Get animation duration to calculate travel time
    float dur = _beamItem->animationDuration();
    if (dur <= 0) dur = 1.0f;
    _estimatedDuration = dur / speed;

    // Calculate travel speed factor for tween
    double dx = end.x() - start.x();
    double dy = end.y() - start.y();
    double distance = qSqrt(dx * dx + dy * dy);

    // Adjust scale if distance is short
    if (_scene) {
        QRectF sr = _scene->sceneRect();
        double halfHeight = sr.height() / 2.0;
        if (distance < halfHeight) {
            _beamItem->setSpineScale(_beamItem->spineScale() * 0.6f);
        }
    }

    _scene->addItem(_beamItem);
    _beamItem->play(false);

    // Move the beam from start to end over the travel time
    int travelMs = static_cast<int>(_estimatedDuration * 500); // factor 0.5 like JS
    _beamItem->moveTo(end, travelMs);

    // Connect finish (if no burst pending, finish when beam animation ends)
    connect(_beamItem, &SpineGlItem::animationFinished,
            this, &SpineIndicatorLine::onBeamAnimationFinished);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Burst effect
// ═══════════════════════════════════════════════════════════════════════════

void SpineIndicatorLine::spawnBurstEffect(const QString &effectSkelName,
                                            const QString &runtimeVersion,
                                            const QPointF &position)
{
    if (!_scene || effectSkelName.isEmpty()) return;

    _burstPending = true;

    _burstItem = new SpineGlItem();
    _burstItem->setZValue(10002);

    if (!runtimeVersion.isEmpty())
        _burstItem->setRuntimeVersionHint(runtimeVersion);

    QString fullPath = _assetPrefix + effectSkelName;
    if (!_burstItem->loadSpine(fullPath)) {
        qWarning("[SpineIndicatorLine] Failed to load burst skeleton: %s",
                 qPrintable(fullPath));
        delete _burstItem;
        _burstItem = nullptr;
        _burstPending = false;
        return;
    }

    _burstItem->setSpinePosition(position);
    _scene->addItem(_burstItem);
    _burstItem->play(false);

    connect(_burstItem, &SpineGlItem::animationFinished,
            this, &SpineIndicatorLine::onBurstAnimationFinished);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Cleanup
// ═══════════════════════════════════════════════════════════════════════════

void SpineIndicatorLine::onBeamAnimationFinished()
{
    // Remove beam from scene
    if (_beamItem) {
        _beamItem->stop();
        if (_scene)
            _scene->removeItem(_beamItem);
        delete _beamItem;
        _beamItem = nullptr;
    }

    // If no burst was requested, we are done
    if (!_burstPending && !_burstItem) {
        _finished = true;
        emit finished();
    }
}

void SpineIndicatorLine::onBurstAnimationFinished()
{
    if (_burstItem) {
        _burstItem->stop();
        if (_scene)
            _scene->removeItem(_burstItem);
        delete _burstItem;
        _burstItem = nullptr;
    }

    _burstPending = false;

    // If beam already finished (or was never created), we are fully done
    if (!_beamItem) {
        _finished = true;
        emit finished();
    }
}

void SpineIndicatorLine::scheduleCleanup(int delayMs)
{
    QTimer::singleShot(delayMs, this, [this]() {
        _finished = true;
        emit finished();
    });
}
