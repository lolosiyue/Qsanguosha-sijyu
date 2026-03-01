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

    /// Fit animation to fill the scene.
    void fitToScene();

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

private slots:
    void onTimer();

private:
    void initGL();
    void cleanupGL();
    void renderSpine(QPainter *painter);
    void updateSkeleton(float deltaSeconds);

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
    bool                  _glInitialized;

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

    // ─── Vertex buffer data ─────────────────────────────────────
    struct Vertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };
    QVector<Vertex> _vertices;
    QVector<GLuint> _indices;
};

#endif // SPINE_GL_ITEM_H
