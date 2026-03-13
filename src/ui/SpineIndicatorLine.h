/******************************************************************************
 * SpineIndicatorLine.h
 *
 * A self-contained sub-effect that renders a Spine-animated "indicator line"
 * (beam / projectile) travelling from an attacker to a target, followed by
 * an optional hit-burst explosion at the destination.
 *
 * Each instance owns its own SpineGlItem(s) and schedules timed destruction
 * via the controller.
 *****************************************************************************/

#ifndef SPINE_INDICATOR_LINE_H
#define SPINE_INDICATOR_LINE_H

#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>

class SpineGlItem;
class QGraphicsScene;

class SpineIndicatorLine : public QObject
{
    Q_OBJECT

public:
    explicit SpineIndicatorLine(QGraphicsScene *scene, QObject *parent = nullptr);
    ~SpineIndicatorLine() override;

    /// Set the asset path prefix (e.g. "assets/dynamic/").
    void setAssetPrefix(const QString &prefix) { _assetPrefix = prefix; }

    /// Launch the beam from `start` to `end`.
    /// @param skelName        Spine skeleton base name for the beam.
    /// @param runtimeVersion  Spine runtime version hint.
    /// @param start           Scene position of the attacker.
    /// @param end             Scene position of the target.
    /// @param rotationDeg     Rotation angle in degrees.
    /// @param speed           Travel speed multiplier (1.0 = normal).
    void launch(const QString &skelName,
                const QString &runtimeVersion,
                const QPointF &start,
                const QPointF &end,
                double rotationDeg,
                float speed = 1.0f);

    /// Spawn a burst effect at the given position (called when beam reaches target).
    void spawnBurstEffect(const QString &effectSkelName,
                          const QString &runtimeVersion,
                          const QPointF &position);

    /// Estimated total duration of the beam animation (seconds).
    float estimatedDuration() const { return _estimatedDuration; }

    /// Whether the indicator line has finished playing.
    bool isFinished() const { return _finished; }

signals:
    /// Emitted when all effects (beam + burst) have completed.
    void finished();

private slots:
    void onBeamAnimationFinished();
    void onBurstAnimationFinished();

private:
    void scheduleCleanup(int delayMs);

    QGraphicsScene *_scene;
    QString         _assetPrefix;

    SpineGlItem    *_beamItem;
    SpineGlItem    *_burstItem;

    float           _estimatedDuration;
    bool            _finished;
    bool            _burstPending;
};

#endif // SPINE_INDICATOR_LINE_H
