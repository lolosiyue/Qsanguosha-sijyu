#include "boot-video-item.h"

#include <QAudioOutput>
#include <QImage>
#include <QMatrix4x4>
#include <QMediaPlayer>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPropertyAnimation>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTextureMaterial>
#include <QVideoFrame>
#include <QtQuick/qsgtexture_platform.h>
#include <atomic>
#include <cstring>

namespace {

// One decoded frame. YUV420P and NV12 keep their 8-bit planes and are converted by a
// shader; any other format arrives as an RGB image.
struct BootVideoPicture
{
    QByteArray planes[3];
    QSize sizes[3];
    // 0 when the picture is an image.
    int planeCount = 0;
    // NV12: the second plane holds both chroma channels.
    bool interleaved = false;
    QMatrix4x4 colorMatrix;
    QImage image;
};

QMatrix4x4 yuvToRgb(const QVideoFrameFormat &format)
{
    float kr = 0.2126f, kb = 0.0722f;
    if (format.colorSpace() == QVideoFrameFormat::ColorSpace_BT601
        || (format.colorSpace() == QVideoFrameFormat::ColorSpace_Undefined && format.frameHeight() < 720)) {
        kr = 0.299f;
        kb = 0.114f;
    } else if (format.colorSpace() == QVideoFrameFormat::ColorSpace_BT2020) {
        kr = 0.2627f;
        kb = 0.0593f;
    }
    const float kg = 1 - kr - kb;
    const bool full = format.colorRange() == QVideoFrameFormat::ColorRange_Full;
    const float ys = full ? 1.0f : 255.0f / 219.0f;
    const float yo = full ? 0.0f : 16.0f / 255.0f;
    const float cs = full ? 1.0f : 255.0f / 224.0f;
    const float rv = 2 * (1 - kr) * cs;
    const float bu = 2 * (1 - kb) * cs;
    const float gu = -2 * kb * (1 - kb) / kg * cs;
    const float gv = -2 * kr * (1 - kr) / kg * cs;
    return QMatrix4x4(ys, 0, rv, -ys * yo - rv * 0.5f,
                      ys, gu, gv, -ys * yo - (gu + gv) * 0.5f,
                      ys, bu, 0, -ys * yo - bu * 0.5f,
                      0, 0, 0, 1);
}

bool copyPlanes(const QVideoFrame &source, BootVideoPicture &picture)
{
    const QVideoFrameFormat::PixelFormat format = source.pixelFormat();
    const bool nv12 = format == QVideoFrameFormat::Format_NV12;
    if (!nv12 && format != QVideoFrameFormat::Format_YUV420P)
        return false;
    QVideoFrame frame(source);
    if (!frame.map(QVideoFrame::ReadOnly))
        return false;
    const QSize luma = frame.size();
    const QSize chroma((luma.width() + 1) / 2, (luma.height() + 1) / 2);
    picture.planeCount = nv12 ? 2 : 3;
    picture.interleaved = nv12;
    for (int i = 0; i < picture.planeCount; ++i) {
        const QSize size = i == 0 ? luma : chroma;
        const int rowBytes = size.width() * (nv12 && i == 1 ? 2 : 1);
        const uchar *bits = frame.bits(i);
        const int stride = frame.bytesPerLine(i);
        QByteArray &plane = picture.planes[i];
        plane.resize(qsizetype(rowBytes) * size.height());
        for (int row = 0; row < size.height(); ++row)
            std::memcpy(plane.data() + qsizetype(row) * rowBytes, bits + qsizetype(row) * stride, rowBytes);
        picture.sizes[i] = size;
    }
    frame.unmap();
    picture.colorMatrix = yuvToRgb(frame.surfaceFormat());
    return true;
}

class BootPlaneMaterial : public QSGMaterial
{
public:
    BootPlaneMaterial() { setFlag(Blending); }

    QSGMaterialType *type() const override
    {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override
    {
        return this == other ? 0 : (this < other ? -1 : 1);
    }

    QSGTexture *planes[3] = {};
    QMatrix4x4 colorMatrix;
    bool interleaved = false;
};

class BootPlaneShader : public QSGMaterialShader
{
public:
    BootPlaneShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/QSanguosha/Home/shaders/boot-video.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/QSanguosha/Home/shaders/boot-video.frag.qsb"));
    }

    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *) override
    {
        const auto *material = static_cast<BootPlaneMaterial *>(newMaterial);
        char *data = state.uniformData()->data();
        std::memcpy(data, state.combinedMatrix().constData(), 64);
        std::memcpy(data + 64, material->colorMatrix.constData(), 64);
        const float opacity = state.opacity();
        const float interleaved = material->interleaved ? 1.0f : 0.0f;
        std::memcpy(data + 128, &opacity, 4);
        std::memcpy(data + 132, &interleaved, 4);
        return true;
    }

    void updateSampledImage(RenderState &state, int binding, QSGTexture **texture,
                            QSGMaterial *newMaterial, QSGMaterial *) override
    {
        auto *material = static_cast<BootPlaneMaterial *>(newMaterial);
        // NV12 has no third plane; that binding is never read.
        QSGTexture *plane = material->planes[binding - 1] ? material->planes[binding - 1]
                                                          : material->planes[1];
        plane->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        *texture = plane;
    }
};

QSGMaterialShader *BootPlaneMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new BootPlaneShader;
}

// An 8-bit (one or two channel) OpenGL texture that receives one plane per frame.
class BootPlaneTexture
{
public:
    ~BootPlaneTexture()
    {
        delete m_texture;
        // Without a current context the window is gone and took the texture with it.
        if (m_id && QOpenGLContext::currentContext())
            QOpenGLContext::currentContext()->functions()->glDeleteTextures(1, &m_id);
    }

    QSGTexture *upload(QQuickWindow *window, const QByteArray &data, const QSize &size, bool twoChannels)
    {
        QOpenGLFunctions *gl = QOpenGLContext::currentContext()->functions();
        const GLenum format = twoChannels ? GL_RG : GL_RED;
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (!m_id || size != m_size) {
            delete m_texture;
            if (!m_id)
                gl->glGenTextures(1, &m_id);
            gl->glBindTexture(GL_TEXTURE_2D, m_id);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, twoChannels ? GL_RG8 : GL_R8, size.width(), size.height(),
                             0, format, GL_UNSIGNED_BYTE, data.constData());
            m_size = size;
            m_texture = QNativeInterface::QSGOpenGLTexture::fromNative(m_id, window, size);
            m_texture->setFiltering(QSGTexture::Linear);
            m_texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            m_texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
        } else {
            gl->glBindTexture(GL_TEXTURE_2D, m_id);
            gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width(), size.height(),
                                format, GL_UNSIGNED_BYTE, data.constData());
        }
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        return m_texture;
    }

private:
    GLuint m_id = 0;
    QSize m_size;
    QSGTexture *m_texture = nullptr;
};

}

// The newest picture, passed from the decoder thread to the render thread.
struct BootVideoFrames
{
    QMutex mutex;
    std::shared_ptr<const BootVideoPicture> picture;
    quint64 serial = 0;
    // Frames of the current clip; the serial never restarts, so the node never misses one.
    int clipFrames = 0;
    // Cleared by the render thread when it cannot draw planes; images are used then.
    std::atomic<bool> planesUsable{true};
};

namespace {

class BootVideoNode : public QSGGeometryNode
{
public:
    BootVideoNode(QQuickWindow *window, std::shared_ptr<BootVideoFrames> frames)
        : m_window(window)
        , m_frames(std::move(frames))
        , m_geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4)
    {
        setFlag(UsePreprocess);
        setGeometry(&m_geometry);
        // Black until the first frame arrives.
        QImage black(1, 1, QImage::Format_RGB32);
        black.fill(Qt::black);
        showImage(black);

        QOpenGLContext *context = QOpenGLContext::currentContext();
        const bool planes = m_window->rendererInterface()->graphicsApi() == QSGRendererInterface::OpenGL
            && context && (context->format().majorVersion() >= 3
                           || context->hasExtension(QByteArrayLiteral("GL_ARB_texture_rg")));
        if (!planes)
            m_frames->planesUsable = false;
    }

    ~BootVideoNode() override { delete m_imageTexture; }

    void setArea(const QRectF &area)
    {
        m_area = area;
        fit();
    }

    // Runs on the render thread, with its OpenGL context current, for every frame drawn.
    void preprocess() override
    {
        std::shared_ptr<const BootVideoPicture> picture;
        {
            QMutexLocker lock(&m_frames->mutex);
            if (m_frames->serial == m_serial)
                return;
            m_serial = m_frames->serial;
            picture = m_frames->picture;
        }
        if (picture->planeCount > 0) {
            for (int i = 0; i < 3; ++i) {
                m_planeMaterial.planes[i] = i < picture->planeCount
                    ? m_planes[i].upload(m_window, picture->planes[i], picture->sizes[i],
                                         picture->interleaved && i == 1)
                    : nullptr;
            }
            m_planeMaterial.colorMatrix = picture->colorMatrix;
            m_planeMaterial.interleaved = picture->interleaved;
            m_source = picture->sizes[0];
            setMaterial(&m_planeMaterial);
            markDirty(DirtyMaterial);
        } else {
            showImage(picture->image);
        }
        fit();
    }

private:
    void showImage(const QImage &image)
    {
        QSGTexture *old = m_imageTexture;
        m_imageTexture = m_window->createTextureFromImage(image);
        m_imageTexture->setFiltering(QSGTexture::Linear);
        m_imageMaterial.setTexture(m_imageTexture);
        delete old;
        m_source = image.size();
        setMaterial(&m_imageMaterial);
        markDirty(DirtyMaterial);
    }

    // Fill the item and crop the overflow, like VideoOutput.PreserveAspectCrop.
    void fit()
    {
        if (m_area.isEmpty() || m_source.isEmpty())
            return;
        const qreal scale = qMax(m_area.width() / m_source.width(), m_area.height() / m_source.height());
        const qreal shownWidth = m_area.width() / scale / m_source.width();
        const qreal shownHeight = m_area.height() / scale / m_source.height();
        QSGGeometry::updateTexturedRectGeometry(&m_geometry, m_area,
            QRectF((1 - shownWidth) / 2, (1 - shownHeight) / 2, shownWidth, shownHeight));
        markDirty(DirtyGeometry);
    }

    QQuickWindow *m_window;
    std::shared_ptr<BootVideoFrames> m_frames;
    QSGGeometry m_geometry;
    QSGTextureMaterial m_imageMaterial;
    QSGTexture *m_imageTexture = nullptr;
    BootPlaneMaterial m_planeMaterial;
    BootPlaneTexture m_planes[3];
    quint64 m_serial = 0;
    QSize m_source;
    QRectF m_area;
};

}

BootVideoItem::BootVideoItem(QQuickItem *parent)
    : QQuickItem(parent)
    , m_frames(std::make_shared<BootVideoFrames>())
    , m_player(new QMediaPlayer(this))
    , m_audio(new QAudioOutput(this))
{
    setFlag(ItemHasContents);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoSink(&m_sink);
    // The decoder thread emits this. Copying the planes there keeps the GUI and render
    // threads free; only formats the shader cannot take are converted on the CPU.
    connect(&m_sink, &QVideoSink::videoFrameChanged, this,
        [this, frames = m_frames](const QVideoFrame &frame) {
            auto picture = std::make_shared<BootVideoPicture>();
            if (!frames->planesUsable || !copyPlanes(frame, *picture))
                picture->image = frame.toImage();
            if (picture->planeCount == 0 && picture->image.isNull())
                return;
            bool first;
            {
                QMutexLocker lock(&frames->mutex);
                first = frames->clipFrames++ == 0;
                frames->picture = std::move(picture);
                ++frames->serial;
            }
            if (first)
                QMetaObject::invokeMethod(this, &BootVideoItem::firstFrame, Qt::QueuedConnection);
        }, Qt::DirectConnection);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::InvalidMedia)
            fail();
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, &BootVideoItem::fail);
}

// A broken clip reports both an error and invalid media; pass it on once.
void BootVideoItem::fail()
{
    if (m_failed)
        return;
    m_failed = true;
    emit failed();
}

BootVideoItem::~BootVideoItem()
{
    // Deleting the player joins its decoder threads, so no frame callback outlives
    // this item; ~QObject drops a firstFrame call that is still queued.
    m_sink.disconnect(this);
    delete m_player;
}

void BootVideoItem::play(const QUrl &source, qreal volume)
{
    m_failed = false;
    {
        QMutexLocker lock(&m_frames->mutex);
        m_frames->clipFrames = 0;
    }
    m_audio->setVolume(volume);
    m_player->setSource(source);
    m_player->play();
}

bool BootVideoItem::hasFrame() const
{
    QMutexLocker lock(&m_frames->mutex);
    return m_frames->clipFrames > 0;
}

void BootVideoItem::fadeOutAudio(int durationMs)
{
    auto *fade = new QPropertyAnimation(m_audio, "volume", this);
    fade->setDuration(durationMs);
    fade->setEndValue(0.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void BootVideoItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

QSGNode *BootVideoItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<BootVideoNode *>(oldNode);
    if (!node)
        node = new BootVideoNode(window(), m_frames);
    node->setArea(boundingRect());
    return node;
}
