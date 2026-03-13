/******************************************************************************
 * SpineGlItem.cpp
 * QGraphicsItem-based Spine animation renderer using OpenGL.
 *****************************************************************************/

#include "SpineGlItem.h"

#include <QPainter>
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QtMath>
#include <cstring>
#include <cstddef>
#include <limits>

// spine-cpp includes
#include <spine/spine.h>

// ═══════════════════════════════════════════════════════════════════════════
//  QtSpineTextureLoader
// ═══════════════════════════════════════════════════════════════════════════

QtSpineTextureLoader::QtSpineTextureLoader() {}

QtSpineTextureLoader::~QtSpineTextureLoader() {
    qDeleteAll(_textures);
    _textures.clear();
}

void QtSpineTextureLoader::load(void *&textureHandle, const spine::String &path) {
    QString qpath = QString::fromUtf8(path.buffer());

    // Try application-relative path first
    if (!QFile::exists(qpath)) {
        QString appDir = QCoreApplication::applicationDirPath() + "/" + qpath;
        if (QFile::exists(appDir))
            qpath = appDir;
    }

    if (!QFile::exists(qpath)) {
        qWarning("[TexLoader] FATAL: texture file not found: '%s'", qPrintable(qpath));
        textureHandle = nullptr;
        return;
    }

    QImage image(qpath);
    if (image.isNull()) {
        qWarning("[TexLoader] Failed to load image: '%s'", qPrintable(qpath));
        textureHandle = nullptr;
        return;
    }

    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        qWarning("[TexLoader] No current OpenGL context — texture creation may fail");

    // Must be called within a valid OpenGL context
    QOpenGLTexture *tex = new QOpenGLTexture(image, QOpenGLTexture::DontGenerateMipMaps);
    tex->setMinificationFilter(QOpenGLTexture::Linear);
    tex->setMagnificationFilter(QOpenGLTexture::Linear);
    tex->setWrapMode(QOpenGLTexture::ClampToEdge);

    _textures.append(tex);
    textureHandle = tex;
}

void QtSpineTextureLoader::unload(void *textureHandle) {
    QOpenGLTexture *tex = static_cast<QOpenGLTexture *>(textureHandle);
    if (tex) {
        _textures.removeAll(tex);
        delete tex;
    }
}

QOpenGLTexture *QtSpineTextureLoader::getTexture(void *handle) {
    return static_cast<QOpenGLTexture *>(handle);
}

// ═══════════════════════════════════════════════════════════════════════════
//  SpineGlItem
// ═══════════════════════════════════════════════════════════════════════════

SpineGlItem::SpineGlItem(QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , _skeletonData(nullptr)
    , _shader(nullptr)
    , _vbo(QOpenGLBuffer::VertexBuffer)
    , _ibo(QOpenGLBuffer::IndexBuffer)
    , _glInitialized(false)
    , _vboCapacity(0)
    , _iboCapacity(0)
    , _spineScale(1.0f)
    , _playing(false)
    , _loop(false)
    , _lastTime(0)
    , _opacity(1.0)
    , _flipX(false)
    , _vertexCount(0)
    , _indexCount(0)
{
    setFlag(QGraphicsItem::ItemHasNoContents, false);
    // Fullscreen by default (will be set by fitToScene or setRenderRect)
    _renderRect = QRectF(0, 0, 1920, 1080);

    // 60fps update timer
    _timer.setInterval(16); // ~60 FPS
    connect(&_timer, &QTimer::timeout, this, &SpineGlItem::onTimer);
}

SpineGlItem::~SpineGlItem() {
    stop();
    cleanupGL();
}

// ─── Loading ────────────────────────────────────────────────────────────────

bool SpineGlItem::loadSpine(const QString &basePath, const QString &animationName) {
    QString atlasPath = basePath + ".atlas";
    QString skelPath  = basePath + ".skel";
    return loadSpineFiles(atlasPath, skelPath, animationName);
}

bool SpineGlItem::loadSpineFiles(const QString &atlasPath, const QString &skelPath,
                                  const QString &animationName) {
    // Clean up previous
    _animState.reset();
    _animStateData.reset();
    _skeleton.reset();
    _skeletonData = nullptr;
    _atlas.reset();
    _textureLoader.reset();
    _clipper.reset();

    // Create texture loader (needs GL context – will be created on first paint)
    _textureLoader = std::make_unique<QtSpineTextureLoader>();

    // Find absolute paths
    QString resolvedAtlas = atlasPath;
    QString resolvedSkel  = skelPath;
    QString appDir = QCoreApplication::applicationDirPath();

    if (!QFile::exists(resolvedAtlas))
        resolvedAtlas = appDir + "/" + atlasPath;
    if (!QFile::exists(resolvedSkel))
        resolvedSkel = appDir + "/" + skelPath;

    if (!QFile::exists(resolvedAtlas)) {
        emit loadError(QString("Atlas file not found: %1").arg(atlasPath));
        return false;
    }
    if (!QFile::exists(resolvedSkel)) {
        emit loadError(QString("Skel file not found: %1").arg(skelPath));
        return false;
    }

    // Load atlas
    _atlas = std::make_unique<spine::Atlas>(
        spine::String(resolvedAtlas.toUtf8().constData()),
        _textureLoader.get(),
        true // createTexture – requires GL context!
    );

    if (_atlas->getPages().isEmpty()) {
        emit loadError(QString("Failed to parse atlas: %1").arg(atlasPath));
        _atlas.reset();
        return false;
    }

    qDebug("[SpineGlItem] Atlas: pages=%d, parsing skeleton '%s'...",
            (int)_atlas->getPages().size(), qPrintable(resolvedSkel));

    // Load skeleton binary with multi-version routing (reference: multi-runtime manager idea).
    // If explicit runtime hint exists, try only that hint. Otherwise, auto-probe all supported
    // runtime variants and pick the best parse result by quality score.
    struct ParseCandidate {
        spine::SkeletonData *data;
        QString hintName;
        int score;
        int animCount;
        bool hasDefaultSkin;
    };

    auto scoreData = [](spine::SkeletonData *data) -> int {
        if (!data) return -1000000;
        int score = 0;
        if (data->getDefaultSkin()) score += 1000;
        score += static_cast<int>(data->getAnimations().size()) * 10;
        if (data->getAnimations().size() == 0) score -= 100;
        return score;
    };

    QList<QPair<spine::SkeletonBinary::RuntimeVersion, QString>> candidates;
    // Always try all runtime versions. If a hint is provided, try it first for speed,
    // then fall back to all others. This makes runtimeVersion an optional optimization
    // hint rather than a hard requirement — auto-detection always works.
    if (!_runtimeVersionHint.isEmpty()) {
        QString v = _runtimeVersionHint.trimmed();
        candidates.append(qMakePair(spine::SkeletonBinary::RuntimeAuto, v));
    }
    // Add all versions as fallback (duplicates with hint are harmless — same version
    // won't produce a better score, so the first successful parse wins)
    candidates.append(qMakePair(spine::SkeletonBinary::RuntimeAuto, QString("auto")));
    candidates.append(qMakePair(spine::SkeletonBinary::Runtime3_5_35, QString("3.5.35")));
    candidates.append(qMakePair(spine::SkeletonBinary::Runtime3_7, QString("3.7")));
    candidates.append(qMakePair(spine::SkeletonBinary::Runtime3_8, QString("3.8")));
    candidates.append(qMakePair(spine::SkeletonBinary::Runtime4_0, QString("4.0")));
    candidates.append(qMakePair(spine::SkeletonBinary::Runtime4_1, QString("4.1")));

    QVector<ParseCandidate> parsed;
    QStringList errors;
    for (int ci = 0; ci < candidates.size(); ++ci) {
        spine::SkeletonBinary binary(_atlas.get());
        binary.setScale(_spineScale);

        const QString hintName = candidates[ci].second;
        if (!hintName.isEmpty() && QString::compare(hintName, "auto", Qt::CaseInsensitive) != 0) {
            binary.setRuntimeVersionHint(spine::String(hintName.toUtf8().constData()));
        }

        spine::SkeletonData *sd = nullptr;
        sd = binary.readSkeletonDataFile(
            spine::String(resolvedSkel.toUtf8().constData()));

        if (!sd) {
            errors << QString("[%1] %2")
                         .arg(hintName, QString::fromUtf8(binary.getError().buffer()));
            continue;
        }

        ParseCandidate cand;
        cand.data = sd;
        cand.hintName = hintName;
        cand.animCount = static_cast<int>(sd->getAnimations().size());
        cand.hasDefaultSkin = (sd->getDefaultSkin() != nullptr);
        cand.score = scoreData(sd);
        parsed.append(cand);
    }

    if (parsed.isEmpty()) {
        emit loadError(QString("Failed to load skeleton: %1 — %2")
                       .arg(skelPath, errors.join(" | ")));
        _atlas.reset();
        return false;
    }

    int bestIdx = 0;
    for (int i = 1; i < parsed.size(); ++i) {
        if (parsed[i].score > parsed[bestIdx].score)
            bestIdx = i;
    }

    _skeletonData = parsed[bestIdx].data;
    qDebug("[SpineGlItem] Selected runtime=%s anims=%d",
           qPrintable(parsed[bestIdx].hintName), parsed[bestIdx].animCount);

    for (int i = 0; i < parsed.size(); ++i) {
        if (i != bestIdx && parsed[i].data)
            delete parsed[i].data;
    }

    // Create skeleton instance
    _skeleton = std::make_unique<spine::Skeleton>(_skeletonData);

    // Create animation state
    _animStateData = std::make_unique<spine::AnimationStateData>(_skeletonData);
    _animStateData->setDefaultMix(0.2f);
    _animState = std::make_unique<spine::AnimationState>(_animStateData.get());

    // Set up completion listener
    _animState->listener = [this](spine::EventType type, spine::TrackEntry &entry, spine::Event *event) {
        if (type == spine::EventType_Complete && !entry.getLoop()) {
            QMetaObject::invokeMethod(this, [this]() {
                emit animationFinished();
            }, Qt::QueuedConnection);
        }
        if (type == spine::EventType_Event && event) {
            QString name = QString::fromUtf8(event->getData().getName().buffer());
            QMetaObject::invokeMethod(this, [this, name]() {
                emit spineEvent(name);
            }, Qt::QueuedConnection);
        }
    };

    // Set initial animation.
    // For character dynamic skins, prefer common idle/default action names.
    // If none found, fall back to longest-duration animation.
    if (!animationName.isEmpty()) {
        _pendingAnim = animationName;
    } else {
        auto &anims = _skeletonData->getAnimations();

        QStringList preferredNames;
        preferredNames
            << "DaiJi" << "daiji"
            << "ChuChang" << "chuchang"
            << "Idle" << "idle"
            << "Stand" << "stand"
            << "Default" << "default";

        for (int pi = 0; pi < preferredNames.size() && _pendingAnim.isEmpty(); ++pi) {
            const QString want = preferredNames.at(pi);
            for (size_t ai = 0; ai < anims.size(); ++ai) {
                QString got = QString::fromUtf8(anims[ai]->getName().buffer());
                if (QString::compare(got, want, Qt::CaseInsensitive) == 0) {
                    _pendingAnim = got;
                    break;
                }
            }
        }

        int bestIdx = -1;
        float bestDur = 0;
        for (size_t ai = 0; ai < anims.size(); ++ai) {
            float dur = anims[ai]->getDuration();
            if (dur > bestDur) { bestDur = dur; bestIdx = (int)ai; }
        }

        if (_pendingAnim.isEmpty() && bestIdx >= 0) {
            _pendingAnim = QString::fromUtf8(anims[bestIdx]->getName().buffer());
        } else if (_pendingAnim.isEmpty() && anims.size() > 0) {
            _pendingAnim = QString::fromUtf8(anims[0]->getName().buffer());
        }
    }

    // Create clipper
    _clipper = std::make_unique<spine::SkeletonClipping>();

    // Apply negative Y scale for screen coordinates (Spine Y-up → screen Y-down)
    _skeleton->setScaleY(-_spineScale);

    // Set to setup pose
    _skeleton->setToSetupPose();
    _skeleton->updateWorldTransform();

    // Build animation cache for fast lookups
    buildAnimationCache();

    qDebug("[SpineGlItem] Loaded: bones=%d anims=%d anim='%s'",
           (int)_skeletonData->getBones().size(),
           (int)_skeletonData->getAnimations().size(),
           qPrintable(_pendingAnim));

    update();
    return true;
}

// ─── Playback ───────────────────────────────────────────────────────────────

void SpineGlItem::play(bool loop) {
    _loop = loop;
    _playing = true;

    if (!_pendingAnim.isEmpty() && _animState) {
        spine::TrackEntry *entry = _animState->setAnimation(
            0, spine::String(_pendingAnim.toUtf8().constData()), loop);
        if (!entry) {
            qWarning("SpineGlItem: Animation not found: %s", qPrintable(_pendingAnim));
        }
        _pendingAnim.clear();
    }

    // Apply the initial animation frame (t=0) and update the skeleton's
    // world transforms so the very first paint uses the correct position
    // and the animation's starting pose rather than the raw setup pose at (0,0).
    if (_animState && _skeleton) {
        _animState->update(0);
        _animState->apply(*_skeleton);
        _skeleton->updateWorldTransform();
    }

    _elapsed.start();
    _lastTime = 0;
    _timer.start();
}

void SpineGlItem::pause() {
    _playing = false;
    _timer.stop();
}

void SpineGlItem::resume() {
    if (_animState && _skeleton) {
        _playing = true;
        _elapsed.restart();
        _lastTime = 0;
        _timer.start();
    }
}

void SpineGlItem::stop() {
    _playing = false;
    _timer.stop();
    if (_animState)
        _animState->clearTracks();
    if (_skeleton)
        _skeleton->setToSetupPose();
    update();
}

void SpineGlItem::setAnimation(int trackIndex, const QString &name, bool loop) {
    if (_animState) {
        _animState->setAnimation(trackIndex,
            spine::String(name.toUtf8().constData()), loop);
    }
}

void SpineGlItem::addAnimation(int trackIndex, const QString &name, bool loop, float delay) {
    if (_animState) {
        _animState->addAnimation(trackIndex,
            spine::String(name.toUtf8().constData()), loop, delay);
    }
}

// ─── Transform ──────────────────────────────────────────────────────────────

void SpineGlItem::setRenderRect(const QRectF &rect) {
    prepareGeometryChange();
    _renderRect = rect;
    setPos(rect.topLeft());
}

void SpineGlItem::setSpineScale(float scale) {
    _spineScale = scale;
    if (_skeleton) {
        _skeleton->setScaleX(scale);
        _skeleton->setScaleY(-scale); // negative Y to flip from Spine Y-up to screen Y-down
    }
}

void SpineGlItem::setSpinePosition(const QPointF &pos) {
    _spinePos = pos;
    if (_skeleton) {
        _skeleton->setX(static_cast<float>(pos.x()));
        _skeleton->setY(static_cast<float>(pos.y()));
        _skeleton->updateWorldTransform();
    }
}

void SpineGlItem::fitToScene() {
    if (!scene()) return;
    QRectF sr = scene()->sceneRect();
    setRenderRect(sr);

    // Center the skeleton in the scene
    float cx = static_cast<float>(sr.width() / 2.0);
    float cy = static_cast<float>(sr.height() / 2.0);
    setSpinePosition(QPointF(cx, cy));
}

float SpineGlItem::animationDuration() const {
    if (!_animState) return 0;
    spine::TrackEntry *entry = _animState->getCurrent(0);
    if (!entry || !entry->getAnimation()) return 0;
    return entry->getAnimation()->getDuration();
}

// ─── QGraphicsItem overrides ────────────────────────────────────────────────

QRectF SpineGlItem::boundingRect() const {
    return QRectF(QPointF(0, 0), _renderRect.size());
}

void SpineGlItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (!_skeleton || !_animState)
        return;

    // We need to render via OpenGL directly
    painter->beginNativePainting();
    renderSpine(painter);
    painter->endNativePainting();
}

// ─── Private ────────────────────────────────────────────────────────────────

void SpineGlItem::onTimer() {
    if (!_playing || !_skeleton || !_animState) return;

    float currentTime = _elapsed.elapsed() / 1000.0f;
    float delta = currentTime - _lastTime;
    _lastTime = currentTime;

    // Clamp delta to avoid large jumps
    if (delta > 0.1f) delta = 0.1f;

    updateTweens(delta);
    updateSkeleton(delta);
    update(); // Trigger repaint
}

void SpineGlItem::updateSkeleton(float deltaSeconds) {
    if (!_animState || !_skeleton) return;

    _animState->update(deltaSeconds);
    _animState->apply(*_skeleton);
    _skeleton->updateWorldTransform();
}

void SpineGlItem::initGL() {
    if (_glInitialized) return;

    QOpenGLFunctions *gl = QOpenGLContext::currentContext()->functions();
    if (!gl) return;

    // Create shader program
    _shader = new QOpenGLShaderProgram();

    const char *vertexShader =
        "#version 120\n"
        "attribute vec2 aPos;\n"
        "attribute vec2 aUV;\n"
        "attribute vec4 aColor;\n"
        "varying vec2 vUV;\n"
        "varying vec4 vColor;\n"
        "uniform mat4 uMVP;\n"
        "void main() {\n"
        "    vUV = aUV;\n"
        "    vColor = aColor;\n"
        "    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    const char *fragmentShader =
        "#version 120\n"
        "varying vec2 vUV;\n"
        "varying vec4 vColor;\n"
        "uniform sampler2D uTexture;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(uTexture, vUV) * vColor;\n"
        "}\n";

    _shader->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader);
    _shader->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader);
    _shader->link();

    _vbo.create();
    _vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    _ibo.create();
    _ibo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    _vboCapacity = 0;
    _iboCapacity = 0;
    _glInitialized = true;
}

void SpineGlItem::cleanupGL() {
    if (_shader) {
        delete _shader;
        _shader = nullptr;
    }
    if (_vbo.isCreated())
        _vbo.destroy();
    if (_ibo.isCreated())
        _ibo.destroy();
    _vboCapacity = 0;
    _iboCapacity = 0;
    _glInitialized = false;
}

void SpineGlItem::renderSpine(QPainter *painter) {
    QOpenGLFunctions *gl = QOpenGLContext::currentContext()->functions();
    if (!gl) { qWarning("[SpineGlItem] renderSpine: no GL context!"); return; }

    // Ensure we're drawing to the currently active surface's default FBO
    // (critical for QOpenGLWidget-backed painting).
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (ctx) {
        GLuint fbo = static_cast<GLuint>(ctx->defaultFramebufferObject());
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }

    if (!_glInitialized)
        initGL();

    if (!_shader || !_skeleton) {
        qWarning("[SpineGlItem] renderSpine: shader=%p skeleton=%p", _shader, _skeleton.get());
        return;
    }

    static int renderLogCount = 0;
    bool doLog = (++renderLogCount <= 10);

    // Build vertex data from skeleton draw order
    // ── Reuse vectors instead of clear()+append() every frame ──
    // We write directly into pre-allocated storage using _vertexCount/_indexCount
    // as running cursors, then only upload the used portion to the GPU via VBO/IBO.
    _vertexCount = 0;
    _indexCount  = 0;

    spine::Vector<spine::Slot *> &drawOrder = _skeleton->getDrawOrder();
    GLuint baseVertex = 0;

    if (doLog)
        qWarning("[SpineGlItem] renderSpine: drawOrder.size=%d", (int)drawOrder.size());

    // ── Estimate capacity and pre-reserve once ──
    // Typical Spine skeleton: ~4 verts per region, ~20–80 per mesh.
    // Reserve generously on first frame; subsequent frames reuse the allocation.
    {
        int estimatedVerts = static_cast<int>(drawOrder.size()) * 20;
        int estimatedIdx   = static_cast<int>(drawOrder.size()) * 60;
        if (_vertices.capacity() < estimatedVerts)
            _vertices.reserve(estimatedVerts);
        if (_indices.capacity() < estimatedIdx)
            _indices.reserve(estimatedIdx);
        // Ensure size covers the maximum we may write (resize is no-op if already large enough)
        if (_vertices.size() < estimatedVerts)
            _vertices.resize(estimatedVerts);
        if (_indices.size() < estimatedIdx)
            _indices.resize(estimatedIdx);
    }

    struct BatchCmd {
        QOpenGLTexture *texture;
        int blendMode;      // 0=normal, 1=additive, 2=multiply, 3=screen
        int indexStart;
        int indexCount;
    };
    QVector<BatchCmd> batches;

    for (size_t i = 0; i < drawOrder.size(); ++i) {
        spine::Slot *slot = drawOrder[i];
        if (!slot) { if (doLog) qWarning("[SpineGlItem]   slot[%d] NULL", (int)i); continue; }

        spine::Attachment *attachment = slot->getAttachment();
        if (!attachment) { if (doLog) qWarning("[SpineGlItem]   slot[%d] '%s' NO attachment", (int)i, slot->getData().getName().buffer()); continue; }

        if (doLog) qWarning("[SpineGlItem]   slot[%d] '%s' attach='%s' type=%d",
                            (int)i, slot->getData().getName().buffer(),
                            attachment->getName().buffer(), (int)attachment->getType());

        QOpenGLTexture *texture = nullptr;
        float *uvs = nullptr;
        int vertexCount = 0;
        unsigned short *triangles = nullptr;
        int triangleCount = 0;
        QVector<float> worldVertices;
        float *worldPtr = nullptr;

        spine::Color skeletonColor = _skeleton->getColor();
        spine::Color slotColor = slot->getColor();
        spine::Color attachColor;

        if (attachment->getType() == spine::AttachmentType_Region) {
            spine::RegionAttachment *region = static_cast<spine::RegionAttachment *>(attachment);
            attachColor = region->getColor();

            if (doLog) {
                qWarning("[SpineGlItem]   region: size=(%.1f,%.1f) color=(%.2f,%.2f,%.2f,%.2f)",
                         region->getWidth(), region->getHeight(),
                         attachColor.r, attachColor.g, attachColor.b, attachColor.a);
                qWarning("[SpineGlItem]   atlasRegion=%p texHandle=%p",
                         region->getRegion(),
                         region->getRegion() ? region->getRegion()->page->texHandle : nullptr);
                float *off = region->getOffset();
                qWarning("[SpineGlItem]   offset: [%.1f,%.1f  %.1f,%.1f  %.1f,%.1f  %.1f,%.1f]",
                         off[0], off[1], off[2], off[3], off[4], off[5], off[6], off[7]);
                spine::Bone &bone = slot->getBone();
                qWarning("[SpineGlItem]   bone '%s': worldX=%.1f worldY=%.1f a=%.3f b=%.3f c=%.3f d=%.3f",
                         bone.getData().getName().buffer(),
                         bone.getWorldX(), bone.getWorldY(),
                         bone.getA(), bone.getB(), bone.getC(), bone.getD());
            }

            worldVertices.resize(8);
            worldPtr = worldVertices.data();
            region->computeWorldVertices(slot->getBone(), worldPtr, 0, 2);
            vertexCount = 4;
            uvs = region->getUVs();

            // Region always uses 2 triangles (6 indices)
            static unsigned short quadIndices[] = {0, 1, 2, 2, 3, 0};
            triangles = quadIndices;
            triangleCount = 6;

            if (region->getRegion())
                texture = QtSpineTextureLoader::getTexture(region->getRegion()->page->texHandle);

            if (doLog) {
                qWarning("[SpineGlItem]   worldVerts: (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f)",
                         worldPtr[0], worldPtr[1], worldPtr[2], worldPtr[3],
                         worldPtr[4], worldPtr[5], worldPtr[6], worldPtr[7]);
                qWarning("[SpineGlItem]   texture=%p vertCount=%d", texture, vertexCount);
            }

        } else if (attachment->getType() == spine::AttachmentType_Mesh) {
            spine::MeshAttachment *mesh = static_cast<spine::MeshAttachment *>(attachment);
            attachColor = mesh->getColor();

            int wvLen = mesh->getWorldVerticesLength();
            worldVertices.resize(wvLen);
            worldPtr = worldVertices.data();
            mesh->computeWorldVertices(*slot, 0, wvLen, worldPtr, 0, 2);
            vertexCount = wvLen / 2;
            uvs = mesh->getUVs().buffer();
            triangles = mesh->getTriangles().buffer();
            triangleCount = static_cast<int>(mesh->getTriangles().size());

            if (doLog)
                qWarning("[SpineGlItem]   mesh: wvLen=%d verts=%d tris=%d", wvLen, vertexCount, triangleCount);

            if (mesh->getRegion())
                texture = QtSpineTextureLoader::getTexture(mesh->getRegion()->page->texHandle);
        } else {
            if (doLog)
                qWarning("[SpineGlItem]   SKIP: unsupported attachment type=%d name='%s' slot='%s'",
                         (int)attachment->getType(),
                         attachment->getName().buffer(),
                         slot->getData().getName().buffer());
            continue;
        }

        if (!texture || vertexCount == 0) {
            if (doLog) qWarning("[SpineGlItem]   SKIP: texture=%p vertCount=%d", texture, vertexCount);
            continue;
        }

        // Compute final color
        float r = skeletonColor.r * slotColor.r * attachColor.r;
        float g = skeletonColor.g * slotColor.g * attachColor.g;
        float b = skeletonColor.b * slotColor.b * attachColor.b;
        float a = skeletonColor.a * slotColor.a * attachColor.a;

        if (doLog) qWarning("[SpineGlItem]   final color: (%.2f,%.2f,%.2f,%.2f)", r, g, b, a);

        if (a <= 0) { if (doLog) qWarning("[SpineGlItem]   SKIP: alpha <= 0"); continue; }

        // Append vertices — direct indexed write instead of append()
        int indexStart = _indexCount;

        // Grow vectors if needed (rare after first frame)
        int requiredVerts = _vertexCount + vertexCount;
        int requiredIdx   = _indexCount + triangleCount;
        if (_vertices.size() < requiredVerts)
            _vertices.resize(requiredVerts * 2);
        if (_indices.size() < requiredIdx)
            _indices.resize(requiredIdx * 2);

        Vertex *vDst = _vertices.data() + _vertexCount;
        for (int vi = 0; vi < vertexCount; ++vi) {
            vDst->x = worldPtr[vi * 2];
            vDst->y = worldPtr[vi * 2 + 1];
            vDst->u = uvs[vi * 2];
            vDst->v = uvs[vi * 2 + 1];
            vDst->r = r; vDst->g = g; vDst->b = b; vDst->a = a;
            ++vDst;
        }

        GLuint *iDst = _indices.data() + _indexCount;
        for (int ti = 0; ti < triangleCount; ++ti) {
            iDst[ti] = baseVertex + triangles[ti];
        }
        _indexCount += triangleCount;

        int blendMode = static_cast<int>(slot->getData().getBlendMode());

        // Try to batch with previous command
        if (!batches.isEmpty() && batches.last().texture == texture
            && batches.last().blendMode == blendMode) {
            batches.last().indexCount += triangleCount;
        } else {
            BatchCmd cmd;
            cmd.texture = texture;
            cmd.blendMode = blendMode;
            cmd.indexStart = indexStart;
            cmd.indexCount = triangleCount;
            batches.append(cmd);
        }

        _vertexCount += vertexCount;
        baseVertex += vertexCount;
    }

    if (_vertexCount == 0) {
        if (doLog) qWarning("[SpineGlItem] renderSpine: NO vertices (drawOrder=%d)", (int)drawOrder.size());
        return;
    }

    if (doLog) qWarning("[SpineGlItem] renderSpine: %d vertices, %d indices, %d batches",
                        _vertexCount, _indexCount, batches.size());

    // ─── OpenGL rendering ───────────────────────────────────────────────

    // Ensure clean GL state — beginNativePainting() does NOT guarantee this
    GLint vp[4];
    gl->glGetIntegerv(GL_VIEWPORT, vp);
    if (doLog) qWarning("[SpineGlItem] GL viewport before: %d %d %d %d", vp[0], vp[1], vp[2], vp[3]);

    // Use the current viewport dimensions for our projection (matches actual framebuffer area)
    float vpW = static_cast<float>(vp[2]);
    float vpH = static_cast<float>(vp[3]);

    gl->glDisable(GL_DEPTH_TEST);
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glDisable(GL_STENCIL_TEST);
    gl->glDisable(GL_CULL_FACE);
    gl->glDepthMask(GL_FALSE);
    gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Unbind any VAO/VBO that Qt's paint engine may have bound
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Build MVP in device-pixel space:
    // item coords --(optional fit)--> item coords --(painter transform)--> viewport pixels --(ortho)--> NDC
    QTransform t = painter ? painter->combinedTransform() : QTransform();

    // Optional normalization for extreme authored coordinates:
    // if the generated world vertices are huge or fully outside viewport,
    // fit them into the viewport center so the effect remains visible.
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    for (int vi = 0; vi < _vertexCount; ++vi) {
        const Vertex &vv = _vertices.at(vi);
        minX = qMin(minX, vv.x);
        minY = qMin(minY, vv.y);
        maxX = qMax(maxX, vv.x);
        maxY = qMax(maxY, vv.y);
    }

    float boundsW = qMax(1.0f, maxX - minX);
    float boundsH = qMax(1.0f, maxY - minY);
    float boundsCx = (minX + maxX) * 0.5f;
    float boundsCy = (minY + maxY) * 0.5f;

    bool fullyOutside = (maxX < 0.0f || minX > vpW || maxY < 0.0f || minY > vpH);
    bool extremelyLarge = (boundsW > vpW * 2.0f || boundsH > vpH * 2.0f);

    QMatrix4x4 fitModel;
    fitModel.setToIdentity();
    if (fullyOutside || extremelyLarge) {
        float fitScale = qMin((vpW * 0.92f) / boundsW, (vpH * 0.92f) / boundsH);
        fitScale = qBound(0.01f, fitScale, 1.0f);

        fitModel.translate(vpW * 0.5f, vpH * 0.5f);
        fitModel.scale(fitScale, fitScale);
        fitModel.translate(-boundsCx, -boundsCy);

        if (doLog) {
            qWarning("[SpineGlItem] auto-fit applied: outside=%d huge=%d bounds=[%.1f,%.1f..%.1f,%.1f] size=(%.1f,%.1f) scale=%.4f",
                     (int)fullyOutside, (int)extremelyLarge,
                     minX, minY, maxX, maxY, boundsW, boundsH, fitScale);
        }
    }

    QMatrix4x4 itemToDevice;
    itemToDevice.setToIdentity();
    itemToDevice(0, 0) = static_cast<float>(t.m11());
    itemToDevice(0, 1) = static_cast<float>(t.m21());
    itemToDevice(0, 3) = static_cast<float>(t.dx());
    itemToDevice(1, 0) = static_cast<float>(t.m12());
    itemToDevice(1, 1) = static_cast<float>(t.m22());
    itemToDevice(1, 3) = static_cast<float>(t.dy());

    QMatrix4x4 ortho;
    ortho.ortho(0, vpW, vpH, 0, -1, 1);
    QMatrix4x4 mvp = ortho * itemToDevice * fitModel;

    if (doLog) qWarning("[SpineGlItem] projection: ortho(0, %.0f, %.0f, 0)  vpSize=(%.0f x %.0f)  xform=[%.3f %.3f %.3f %.3f %.1f %.1f]",
                        vpW, vpH, vpW, vpH,
                        t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy());

    gl->glEnable(GL_BLEND);

    _shader->bind();
    _shader->setUniformValue("uMVP", mvp);
    _shader->setUniformValue("uTexture", 0);

    // Upload vertex data to VBO (GPU buffer)
    int posLoc   = _shader->attributeLocation("aPos");
    int uvLoc    = _shader->attributeLocation("aUV");
    int colorLoc = _shader->attributeLocation("aColor");

    gl->glEnableVertexAttribArray(posLoc);
    gl->glEnableVertexAttribArray(uvLoc);
    gl->glEnableVertexAttribArray(colorLoc);

    int stride = sizeof(Vertex);
    int vboBytes = _vertexCount * stride;
    int iboBytes = _indexCount * static_cast<int>(sizeof(GLuint));

    // ── VBO: upload vertices to GPU ──
    _vbo.bind();
    if (_vertexCount > _vboCapacity) {
        // Grow with 1.5× headroom to avoid frequent re-alloc
        _vboCapacity = _vertexCount + (_vertexCount >> 1);
        _vbo.allocate(_vboCapacity * stride);
    }
    _vbo.write(0, _vertices.constData(), vboBytes);

    // Set attribute pointers as offsets into the bound VBO (not CPU addresses)
    gl->glVertexAttribPointer(posLoc,   2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void *>(offsetof(Vertex, x)));
    gl->glVertexAttribPointer(uvLoc,    2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void *>(offsetof(Vertex, u)));
    gl->glVertexAttribPointer(colorLoc, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void *>(offsetof(Vertex, r)));

    // ── IBO: upload indices to GPU ──
    _ibo.bind();
    if (_indexCount > _iboCapacity) {
        _iboCapacity = _indexCount + (_indexCount >> 1);
        _ibo.allocate(_iboCapacity * static_cast<int>(sizeof(GLuint)));
    }
    _ibo.write(0, _indices.constData(), iboBytes);

    // Draw batches
    for (const BatchCmd &cmd : batches) {
        // Set blend mode
        switch (cmd.blendMode) {
        case 0: // Normal
            gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case 1: // Additive
            gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case 2: // Multiply
            gl->glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case 3: // Screen
            gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
            break;
        }

        // Bind texture
        if (cmd.texture) {
            cmd.texture->bind(0);
        }

        // Draw elements (indices come from bound IBO, so pass byte offset)
        gl->glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT,
                           reinterpret_cast<const void *>(cmd.indexStart * sizeof(GLuint)));

        if (doLog) {
            GLenum err = gl->glGetError();
            if (err != GL_NO_ERROR)
                qWarning("[SpineGlItem] GL ERROR after draw: 0x%x", err);
            else
                qWarning("[SpineGlItem] glDrawElements OK: %d indices, texture=%p (glId=%u)",
                         cmd.indexCount, cmd.texture, cmd.texture ? cmd.texture->textureId() : 0);
        }
    }

    gl->glDisableVertexAttribArray(posLoc);
    gl->glDisableVertexAttribArray(uvLoc);
    gl->glDisableVertexAttribArray(colorLoc);

    _ibo.release();
    _vbo.release();

    _shader->release();
    gl->glDepthMask(GL_TRUE); // restore
}
// ═══════════════════════════════════════════════════════════════════════════
//  Tween / Motion
// ═══════════════════════════════════════════════════════════════════════════

static float easeInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * 0.5f;
}

void SpineGlItem::moveTo(const QPointF &target, int durationMs) {
    _tweenMove.active = true;
    _tweenMove.elapsed = 0;
    _tweenMove.duration = durationMs / 1000.0f;
    _tweenMove.startPos = _spinePos;
    _tweenMove.endPos = target;

    if (!_timer.isActive())
        _timer.start();
}

void SpineGlItem::scaleTo(float targetScale, int durationMs) {
    _tweenScale.active = true;
    _tweenScale.elapsed = 0;
    _tweenScale.duration = durationMs / 1000.0f;
    _tweenScale.startScale = _spineScale;
    _tweenScale.endScale = targetScale;

    if (!_timer.isActive())
        _timer.start();
}

void SpineGlItem::fadeTo(qreal targetOpacity, int durationMs) {
    _tweenFade.active = true;
    _tweenFade.elapsed = 0;
    _tweenFade.duration = durationMs / 1000.0f;
    _tweenFade.startOpacity = _opacity;
    _tweenFade.endOpacity = targetOpacity;

    if (!_timer.isActive())
        _timer.start();
}

void SpineGlItem::setSpineOpacity(qreal opacity) {
    _opacity = qBound(0.0, opacity, 1.0);
    if (_skeleton) {
        spine::Color &c = _skeleton->getColor();
        c.a = static_cast<float>(_opacity);
    }
}

bool SpineGlItem::isTweening() const {
    return _tweenMove.active || _tweenScale.active || _tweenFade.active;
}

void SpineGlItem::cancelTweens() {
    _tweenMove.active = false;
    _tweenScale.active = false;
    _tweenFade.active = false;
}

void SpineGlItem::setFlipX(bool flip) {
    _flipX = flip;
    if (_skeleton) {
        float absScale = qAbs(_spineScale);
        _skeleton->setScaleX(flip ? -absScale : absScale);
    }
}

void SpineGlItem::updateTweens(float deltaSeconds) {
    bool anyActive = false;

    // Move tween
    if (_tweenMove.active) {
        _tweenMove.elapsed += deltaSeconds;
        float t = qBound(0.0f, _tweenMove.elapsed / qMax(0.001f, _tweenMove.duration), 1.0f);
        float e = easeInOutQuad(t);
        QPointF p = _tweenMove.startPos + e * (_tweenMove.endPos - _tweenMove.startPos);
        setSpinePosition(p);
        if (t >= 1.0f)
            _tweenMove.active = false;
        else
            anyActive = true;
    }

    // Scale tween
    if (_tweenScale.active) {
        _tweenScale.elapsed += deltaSeconds;
        float t = qBound(0.0f, _tweenScale.elapsed / qMax(0.001f, _tweenScale.duration), 1.0f);
        float e = easeInOutQuad(t);
        float s = _tweenScale.startScale + e * (_tweenScale.endScale - _tweenScale.startScale);
        setSpineScale(s);
        if (t >= 1.0f)
            _tweenScale.active = false;
        else
            anyActive = true;
    }

    // Fade tween
    if (_tweenFade.active) {
        _tweenFade.elapsed += deltaSeconds;
        float t = qBound(0.0f, _tweenFade.elapsed / qMax(0.001f, _tweenFade.duration), 1.0f);
        float e = easeInOutQuad(t);
        qreal o = _tweenFade.startOpacity + e * (_tweenFade.endOpacity - _tweenFade.startOpacity);
        setSpineOpacity(o);
        if (t >= 1.0f)
            _tweenFade.active = false;
        else
            anyActive = true;
    }

    if (!anyActive && (_tweenMove.elapsed > 0 || _tweenScale.elapsed > 0 || _tweenFade.elapsed > 0)) {
        emit tweenFinished();
        _tweenMove.elapsed = 0;
        _tweenScale.elapsed = 0;
        _tweenFade.elapsed = 0;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Animation cache
// ═══════════════════════════════════════════════════════════════════════════

void SpineGlItem::buildAnimationCache() {
    _cachedAnimations.clear();
    _animDurationMap.clear();
    if (!_skeletonData) return;

    auto &anims = _skeletonData->getAnimations();
    for (size_t i = 0; i < anims.size(); ++i) {
        AnimationInfo info;
        info.name = QString::fromUtf8(anims[i]->getName().buffer());
        info.duration = anims[i]->getDuration();
        _cachedAnimations.append(info);
        _animDurationMap.insert(info.name, info.duration);
    }
    qWarning("[SpineGlItem] animation cache built: %d entries", _cachedAnimations.size());
}

bool SpineGlItem::hasAnimation(const QString &name) const {
    return _animDurationMap.contains(name);
}

float SpineGlItem::animationDurationByName(const QString &name) const {
    auto it = _animDurationMap.find(name);
    if (it != _animDurationMap.end())
        return it.value();
    return -1.0f;
}