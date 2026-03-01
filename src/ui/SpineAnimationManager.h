/******************************************************************************
 * SpineAnimationManager.h
 * Convenience manager for triggering Spine fullscreen effects in the game.
 *****************************************************************************/

#ifndef SPINE_ANIMATION_MANAGER_H
#define SPINE_ANIMATION_MANAGER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QWidget>

class SpineEffectWidget;

/**
 * SpineAnimationManager
 *
 * Provides easy-to-use API for triggering Spine-based fullscreen effects.
 * Usage in roomscene:
 *   SpineAnimationManager::instance()->playFullscreen(parentWidget, "为君担忧/BeiJing");
 *
 * The basePath should point to the .atlas/.skel pair (without extension).
 */
class SpineAnimationManager : public QObject
{
    Q_OBJECT
public:
    static SpineAnimationManager *instance();

    /**
     * Play a fullscreen Spine effect overlay on top of parentWidget.
     * @param parentWidget  The widget (e.g. QGraphicsView) to overlay
     * @param basePath      Path to .atlas/.skel without extension,
     *                      e.g. "为君担忧/BeiJing"
     * @param animationName Animation to play (empty = first available)
     * @param loop          Loop the animation
     * @param clickThrough  Allow mouse clicks to pass through
     * @param scale         Spine skeleton scale
     * @param offsetX       X offset from center
     * @param offsetY       Y offset from center
     * @return true if effect was started
     */
    bool playFullscreen(QWidget *parentWidget,
                        const QString &basePath,
                        const QString &animationName = QString(),
                        bool loop = false,
                        bool clickThrough = true,
                        float scale = 1.0f,
                        float offsetX = 0.0f,
                        float offsetY = 0.0f);

    /**
     * Play a fullscreen Spine effect with explicit atlas + skel paths.
     */
    bool playFullscreenFiles(QWidget *parentWidget,
                             const QString &atlasPath,
                             const QString &skelPath,
                             const QString &animationName = QString(),
                             bool loop = false,
                             bool clickThrough = true,
                             float scale = 1.0f,
                             float offsetX = 0.0f,
                             float offsetY = 0.0f);

    /**
     * Stop all active Spine effects on a parent widget.
     */
    void stopAll(QWidget *parentWidget = nullptr);

signals:
    void effectStarted(const QString &basePath);
    void effectFinished(const QString &basePath);

private:
    explicit SpineAnimationManager(QObject *parent = nullptr);
    ~SpineAnimationManager();

    static SpineAnimationManager *s_instance;
    QList<SpineEffectWidget *> _activeEffects;
};

#endif // SPINE_ANIMATION_MANAGER_H
