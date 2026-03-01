/******************************************************************************
 * SpineEffectWidget.h
 * QOpenGLWidget-based fullscreen overlay for playing Spine dynamic effects
 * on top of the game window (similar to EmbeddedQmlLoader but for Spine).
 *
 * Used for "出框" (breakout-frame) fullscreen special effects such as
 * 为君担忧 (XingXiang / BeiJing spine animations).
 *****************************************************************************/

#ifndef SPINE_EFFECT_WIDGET_H
#define SPINE_EFFECT_WIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

// Forward declarations
namespace spine {
    class Atlas;
    class SkeletonData;
    class Skeleton;
    class AnimationState;
    class AnimationStateData;
    class SkeletonClipping;
}
class QtSpineTextureLoader;

/// A transparent QOpenGLWidget overlay that renders a Spine animation
/// on top of any parent widget (typically the game's QGraphicsView).
/// Supports click-through and auto-close on animation completion.
class SpineEffectWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit SpineEffectWidget(QWidget *parent = nullptr);
    ~SpineEffectWidget() override;

    /// Load and play a Spine effect.
    /// @param basePath  e.g. "为君担忧/XingXiang" (without extension)
    /// @param animationName  The animation name to play (empty = first available)
    /// @param loop  Whether to loop
    /// @param autoClose  Automatically close widget when animation ends
    /// @return true on success
    bool playEffect(const QString &basePath,
                    const QString &animationName = QString(),
                    bool loop = false,
                    bool autoClose = true);

    /// Load from explicit file paths.
    bool playEffectFiles(const QString &atlasPath, const QString &skelPath,
                         const QString &animationName = QString(),
                         bool loop = false, bool autoClose = true);

    /// Enable click-through (mouse events pass to underlying widgets).
    void setClickThrough(bool enabled);
    bool isClickThrough() const { return _clickThrough; }

    /// Manually stop the effect.
    void stopEffect();

    /// Set the Spine skeleton scale.
    void setSpineScale(float scale);

    /// Set position of the skeleton origin (relative to widget center by default).
    void setSpineOffset(float x, float y);

signals:
    /// Emitted when the animation completes (non-looping) or is stopped.
    void effectFinished();

    /// Emitted on Spine events.
    void spineEvent(const QString &eventName);

    /// Emitted if loading fails.
    void effectError(const QString &error);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private slots:
    void onFrameTimer();
    void onAutoClose();

private:
    bool loadSpineData(const QString &atlasPath, const QString &skelPath);
    void cleanupSpine();
    void renderSkeleton();

    // Spine data
    std::unique_ptr<QtSpineTextureLoader>      _textureLoader;
    std::unique_ptr<spine::Atlas>              _atlas;
    spine::SkeletonData                        *_skeletonData;
    std::unique_ptr<spine::Skeleton>           _skeleton;
    std::unique_ptr<spine::AnimationStateData> _animStateData;
    std::unique_ptr<spine::AnimationState>     _animState;
    std::unique_ptr<spine::SkeletonClipping>   _clipper;

    // GL
    QOpenGLShaderProgram *_shader;

    // Timing
    QTimer       _frameTimer;
    QElapsedTimer _elapsed;
    float         _lastTime;

    // Config
    bool  _clickThrough;
    bool  _autoClose;
    bool  _playing;
    float _spineScale;
    float _spineOffsetX;
    float _spineOffsetY;

    // Vertex structure (same as SpineGlItem)
    struct Vertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };
    QVector<Vertex> _vertices;
    QVector<GLuint> _indices;
};

#endif // SPINE_EFFECT_WIDGET_H
