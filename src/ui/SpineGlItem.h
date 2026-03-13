/******************************************************************************
 * SpineGlItem.h
 * QGraphicsItem-based Spine animation renderer using OpenGL.
 *
 * Renders Spine animations directly into the QGraphicsScene (which uses
 * QOpenGLWidget as viewport), providing hardware-accelerated fullscreen
 * dynamic special effects.
 *
 * Usage:
 *   SpineGlItem *item = new SpineGlItem(scene);
 *   item->loadSpine("为君担忧/XingXiang", "animation");
 *   item->play();
 *****************************************************************************/

#ifndef SPINE_GL_ITEM_H
#define SPINE_GL_ITEM_H

#include <QGraphicsItem>
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QHash>
#include <QRectF>
#include <memory>
#include <functional>

// Full spine-cpp includes needed for inheritance and types used in this header
#include <spine/TextureLoader.h>
#include <spine/SpineString.h>

// Forward declarations – spine-cpp types (definitions not needed here)
namespace spine {
    class Atlas;
    class SkeletonData;
    class Skeleton;
    class AnimationState;
    class AnimationStateData;
    class SkeletonBinary;
    class SkeletonClipping;
}

/// Qt-based texture loader for Spine atlas pages.
class QtSpineTextureLoader : public spine::TextureLoader {
public:
    QtSpineTextureLoader();
    ~QtSpineTextureLoader() override;

    void load(void *&textureHandle, const spine::String &path) override;
    void unload(void *textureHandle) override;

    /// Get QOpenGLTexture from opaque handle.
    static QOpenGLTexture *getTexture(void *handle);

private:
    QList<QOpenGLTexture *> _textures;
};

/// A fullscreen (or sized) QGraphicsItem that renders a Spine animation
/// via OpenGL in the QGraphicsView's OpenGL viewport.
class SpineGlItem : public QObject, public QGraphicsItem
{
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)

public:
    explicit SpineGlItem(QGraphicsItem *parent = nullptr);
    ~SpineGlItem() override;

    // ─── Loading ────────────────────────────────────────────────
    /// Load a Spine animation from .atlas + .skel files.
    /// @param basePath  Path prefix (e.g. "为君担忧/XingXiang") without extension.
    /// @param animationName  Name of the animation track to queue (empty = first).
    /// @return true if loading succeeded.
    bool loadSpine(const QString &basePath, const QString &animationName = QString());

    /// Load from explicit file paths.
    bool loadSpineFiles(const QString &atlasPath, const QString &skelPath,
                        const QString &animationName = QString());

    /// Optional runtime version hint: "3.5.35", "3.7", "3.8", "4.0", "4.1".
    /// Empty = auto detect from skeleton header.
    void setRuntimeVersionHint(const QString &version) { _runtimeVersionHint = version; }
    QString runtimeVersionHint() const { return _runtimeVersionHint; }

    // ─── Playback ───────────────────────────────────────────────
    void play(bool loop = false);
    void pause();
    void resume();
    void stop();
    bool isPlaying() const { return _playing; }

    /// Set animation by name.  trackIndex=0 for main track.
    void setAnimation(int trackIndex, const QString &name, bool loop);
    void addAnimation(int trackIndex, const QString &name, bool loop, float delay = 0);

    // ─── Transform / Display ────────────────────────────────────
    /// Set the render area (scene coordinates).
    void setRenderRect(const QRectF &rect);
    QRectF renderRect() const { return _renderRect; }

    void setSpineScale(float scale);
    float spineScale() const { return _spineScale; }

    void setSpinePosition(const QPointF &pos);
    QPointF spinePosition() const { return _spinePos; }

    /// Fit animation to fill the scene.
    void fitToScene();

    // ─── Tween / Motion ─────────────────────────────────────────
    /// Smoothly move the spine position to `target` over `durationMs` milliseconds.
    void moveTo(const QPointF &target, int durationMs);

    /// Smoothly scale the spine to `targetScale` over `durationMs` milliseconds.
    void scaleTo(float targetScale, int durationMs);

    /// Smoothly change opacity to `targetOpacity` (0.0–1.0) over `durationMs` milliseconds.
    void fadeTo(qreal targetOpacity, int durationMs);

    /// Set opacity (0.0–1.0) immediately.
    void setSpineOpacity(qreal opacity);
    qreal spineOpacity() const { return _opacity; }

    /// Whether any tween (move/scale/fade) is currently active.
    bool isTweening() const;

    /// Cancel all active tweens.
    void cancelTweens();

    /// Mirror the skeleton horizontally.
    void setFlipX(bool flip);
    bool flipX() const { return _flipX; }

    // ─── Animation list query ───────────────────────────────────
    /// Describes one animation track found in the skeleton data.
    struct AnimationInfo {
        QString name;
        float   duration; // seconds
    };

    /// Return cached list of all animation names and durations.
    /// Available after a successful loadSpine / loadSpineFiles.
    QList<AnimationInfo> availableAnimations() const { return _cachedAnimations; }

    /// Find a specific animation by name (returns nullptr-equivalent if not found).
    bool hasAnimation(const QString &name) const;

    /// Lookup duration of an animation by name, returns -1 if not found.
    float animationDurationByName(const QString &name) const;

    // ─── QGraphicsItem overrides ────────────────────────────────
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    /// Get the duration of the current animation (seconds).
    float animationDuration() const;

signals:
    /// Emitted when a non-looping animation finishes.
    void animationFinished();

    /// Emitted on each Spine event.
    void spineEvent(const QString &eventName);

    /// Emitted when loading fails.
    void loadError(const QString &error);

    /// Emitted when all tweens finish.
    void tweenFinished();

private slots:
    void onTimer();

private:
    void initGL();
    void cleanupGL();
    void renderSpine(QPainter *painter);
    void updateSkeleton(float deltaSeconds);
    void updateTweens(float deltaSeconds);
    void buildAnimationCache();

    // ─── Spine data ─────────────────────────────────────────────
    std::unique_ptr<QtSpineTextureLoader> _textureLoader;
    std::unique_ptr<spine::Atlas>             _atlas;
    spine::SkeletonData                      *_skeletonData;
    std::unique_ptr<spine::Skeleton>          _skeleton;
    std::unique_ptr<spine::AnimationStateData> _animStateData;
    std::unique_ptr<spine::AnimationState>    _animState;
    std::unique_ptr<spine::SkeletonClipping>  _clipper;

    // ─── GL resources ───────────────────────────────────────────
    QOpenGLShaderProgram *_shader;
    QOpenGLBuffer         _vbo;
    QOpenGLBuffer         _ibo;           ///< Index buffer object (ElementArray)
    bool                  _glInitialized;
    int                   _vboCapacity;   ///< Current VBO allocation in vertex count
    int                   _iboCapacity;   ///< Current IBO allocation in index count

    // ─── State ──────────────────────────────────────────────────
    QRectF          _renderRect;
    float           _spineScale;
    QPointF         _spinePos;
    bool            _playing;
    bool            _loop;
    QElapsedTimer   _elapsed;
    QTimer          _timer;
    float           _lastTime;
    QString         _pendingAnim;
    QString         _runtimeVersionHint;
    qreal           _opacity;
    bool            _flipX;

    // ─── Tween state ────────────────────────────────────────────
    struct Tween {
        bool   active;
        float  elapsed;
        float  duration;     // seconds
        // Move
        QPointF startPos, endPos;
        // Scale
        float   startScale, endScale;
        // Opacity
        qreal   startOpacity, endOpacity;
        Tween() : active(false), elapsed(0), duration(0),
                  startScale(1), endScale(1),
                  startOpacity(1), endOpacity(1) {}
    };
    Tween _tweenMove;
    Tween _tweenScale;
    Tween _tweenFade;

    // ─── Animation cache ────────────────────────────────────────
    QList<AnimationInfo> _cachedAnimations;
    QHash<QString, float> _animDurationMap;

    // ─── Vertex buffer data ─────────────────────────────────────
    struct Vertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };
    QVector<Vertex> _vertices;
    QVector<GLuint> _indices;
    int _vertexCount;   ///< Actual vertex count this frame (avoids clear+append)
    int _indexCount;    ///< Actual index count this frame
};

#endif // SPINE_GL_ITEM_H
