/******************************************************************************
 * SpineEffectWidget.cpp
 * QOpenGLWidget fullscreen overlay for Spine dynamic effects.
 *****************************************************************************/

#include "SpineEffectWidget.h"
#include "SpineGlItem.h"  // for QtSpineTextureLoader

#include <QCoreApplication>
#include <QFile>
#include <QSurfaceFormat>
#include <QMatrix4x4>
#include <QPainter>
#include <cstring>

#include <spine/spine.h>

// ═══════════════════════════════════════════════════════════════════════════
//  SpineEffectWidget
// ═══════════════════════════════════════════════════════════════════════════

SpineEffectWidget::SpineEffectWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , _skeletonData(nullptr)
    , _shader(nullptr)
    , _lastTime(0)
    , _clickThrough(true)
    , _autoClose(true)
    , _playing(false)
    , _spineScale(1.0f)
    , _spineOffsetX(0)
    , _spineOffsetY(0)
{
    // Transparent background
    QSurfaceFormat fmt = format();
    fmt.setAlphaBufferSize(8);
    setFormat(fmt);

    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_AlwaysStackOnTop, true);
    setAttribute(Qt::WA_DeleteOnClose, false);

    // 60fps timer
    _frameTimer.setInterval(16);
    connect(&_frameTimer, &QTimer::timeout, this, &SpineEffectWidget::onFrameTimer);
}

SpineEffectWidget::~SpineEffectWidget() {
    stopEffect();
    makeCurrent();
    cleanupSpine();
    delete _shader;
    doneCurrent();
}

// ─── Public API ─────────────────────────────────────────────────────────────

bool SpineEffectWidget::playEffect(const QString &basePath,
                                    const QString &animationName,
                                    bool loop, bool autoClose) {
    return playEffectFiles(basePath + ".atlas", basePath + ".skel",
                           animationName, loop, autoClose);
}

bool SpineEffectWidget::playEffectFiles(const QString &atlasPath, const QString &skelPath,
                                         const QString &animationName,
                                         bool loop, bool autoClose) {
    qWarning("[SpineEW] playEffectFiles: atlas='%s' skel='%s' anim='%s'",
             qPrintable(atlasPath), qPrintable(skelPath), qPrintable(animationName));
    _autoClose = autoClose;

    // Make sure GL context is current
    makeCurrent();

    if (!loadSpineData(atlasPath, skelPath)) {
        qWarning("[SpineEW] loadSpineData FAILED");
        doneCurrent();
        return false;
    }
    qWarning("[SpineEW] loadSpineData succeeded");

    // Set skeleton position to center of widget
    float cx = width() / 2.0f + _spineOffsetX;
    float cy = height() / 2.0f + _spineOffsetY;
    _skeleton->setX(cx);
    _skeleton->setY(cy);
    _skeleton->setScaleX(_spineScale);
    _skeleton->setScaleY(-_spineScale); // Flip Y for screen coordinates

    // Set animation
    QString animName = animationName;
    if (animName.isEmpty()) {
        auto &anims = _skeletonData->getAnimations();
        qWarning("[SpineEW] No anim name given, available anims: %d", (int)anims.size());
        // Prefer an animation with non-zero duration (actual visual animation)
        int bestIdx = -1;
        float bestDur = 0;
        for (size_t ai = 0; ai < anims.size(); ++ai) {
            float dur = anims[ai]->getDuration();
            QString nm = QString::fromUtf8(anims[ai]->getName().buffer());
            qWarning("[SpineEW]   anim[%d] '%s' dur=%.3f", (int)ai, qPrintable(nm), dur);
            if (dur > bestDur) { bestDur = dur; bestIdx = (int)ai; }
        }
        if (bestIdx >= 0) {
            animName = QString::fromUtf8(anims[bestIdx]->getName().buffer());
            qWarning("[SpineEW] Using longest-duration animation: '%s' (%.3f sec)", qPrintable(animName), bestDur);
        } else if (anims.size() > 0) {
            animName = QString::fromUtf8(anims[0]->getName().buffer());
            qWarning("[SpineEW] Using first animation: '%s'", qPrintable(animName));
        }
    }

    if (!animName.isEmpty()) {
        qWarning("[SpineEW] setAnimation: '%s' loop=%d", qPrintable(animName), (int)loop);
        _animState->setAnimation(0,
            spine::String(animName.toUtf8().constData()), loop);
    } else {
        qWarning("[SpineEW] WARNING: No animation to play!");
    }

    // Set up completion listener
    _animState->listener = [this](spine::EventType type, spine::TrackEntry &entry, spine::Event *event) {
        if (type == spine::EventType_Complete && !entry.getLoop()) {
            QMetaObject::invokeMethod(this, &SpineEffectWidget::onAutoClose,
                                       Qt::QueuedConnection);
        }
        if (type == spine::EventType_Event && event) {
            QString name = QString::fromUtf8(event->getData().getName().buffer());
            QMetaObject::invokeMethod(this, [this, name]() {
                emit spineEvent(name);
            }, Qt::QueuedConnection);
        }
    };

    _skeleton->setToSetupPose();
    _skeleton->updateWorldTransform();

    doneCurrent();

    // Start playing
    _playing = true;
    _elapsed.start();
    _lastTime = 0;
    _frameTimer.start();

    show();
    raise();

    qWarning("[SpineEW] Widget shown: size=%dx%d visible=%d skeleton pos=(%.1f,%.1f)",
             width(), height(), isVisible(),
             _skeleton->getX(), _skeleton->getY());

    return true;
}

void SpineEffectWidget::setClickThrough(bool enabled) {
    _clickThrough = enabled;
    setAttribute(Qt::WA_TransparentForMouseEvents, enabled);
}

void SpineEffectWidget::stopEffect() {
    _playing = false;
    _frameTimer.stop();
    if (_animState)
        _animState->clearTracks();
    hide();
    emit effectFinished();
}

void SpineEffectWidget::setSpineScale(float scale) {
    _spineScale = scale;
    if (_skeleton) {
        _skeleton->setScaleX(scale);
        _skeleton->setScaleY(-scale);
    }
}

void SpineEffectWidget::setSpineOffset(float x, float y) {
    _spineOffsetX = x;
    _spineOffsetY = y;
}

// ─── OpenGL overrides ───────────────────────────────────────────────────────

void SpineEffectWidget::initializeGL() {
    initializeOpenGLFunctions();

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // transparent

    // Create shader
    _shader = new QOpenGLShaderProgram(this);

    const char *vs =
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

    const char *fs =
        "#version 120\n"
        "varying vec2 vUV;\n"
        "varying vec4 vColor;\n"
        "uniform sampler2D uTexture;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(uTexture, vUV) * vColor;\n"
        "}\n";

    _shader->addShaderFromSourceCode(QOpenGLShader::Vertex, vs);
    _shader->addShaderFromSourceCode(QOpenGLShader::Fragment, fs);
    _shader->link();
}

void SpineEffectWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    // Update skeleton position
    if (_skeleton) {
        _skeleton->setX(w / 2.0f + _spineOffsetX);
        _skeleton->setY(h / 2.0f + _spineOffsetY);
    }
}

void SpineEffectWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    if (!_playing || !_skeleton || !_animState || !_shader) {
        static int paintSkipCount = 0;
        if (++paintSkipCount <= 3)
            qWarning("[SpineEW] paintGL: skipping (playing=%d skel=%p animState=%p shader=%p)",
                     _playing, _skeleton.get(), _animState.get(), _shader);
        return;
    }

    renderSkeleton();
}

// ─── Private slots ──────────────────────────────────────────────────────────

void SpineEffectWidget::onFrameTimer() {
    if (!_playing || !_skeleton || !_animState) return;

    float currentTime = _elapsed.elapsed() / 1000.0f;
    float delta = currentTime - _lastTime;
    _lastTime = currentTime;
    if (delta > 0.1f) delta = 0.1f;

    _animState->update(delta);
    _animState->apply(*_skeleton);
    _skeleton->updateWorldTransform();

    update(); // Request repaint
}

void SpineEffectWidget::onAutoClose() {
    if (_autoClose) {
        stopEffect();
        deleteLater();
    } else {
        emit effectFinished();
    }
}

// ─── Private ────────────────────────────────────────────────────────────────

bool SpineEffectWidget::loadSpineData(const QString &atlasPath, const QString &skelPath) {
    cleanupSpine();

    _textureLoader = std::make_unique<QtSpineTextureLoader>();

    // Resolve paths
    QString resolvedAtlas = atlasPath;
    QString resolvedSkel  = skelPath;
    QString appDir = QCoreApplication::applicationDirPath();

    qWarning("[SpineEW] loadSpineData: atlas='%s' skel='%s' appDir='%s'",
             qPrintable(atlasPath), qPrintable(skelPath), qPrintable(appDir));

    if (!QFile::exists(resolvedAtlas)) {
        resolvedAtlas = appDir + "/" + atlasPath;
        qWarning("[SpineEW] atlas not found at relative path, trying: '%s'", qPrintable(resolvedAtlas));
    }
    if (!QFile::exists(resolvedSkel)) {
        resolvedSkel = appDir + "/" + skelPath;
        qWarning("[SpineEW] skel not found at relative path, trying: '%s'", qPrintable(resolvedSkel));
    }

    if (!QFile::exists(resolvedAtlas)) {
        qWarning("[SpineEW] FATAL: Atlas file not found: '%s'", qPrintable(resolvedAtlas));
        emit effectError(QString("Atlas file not found: %1").arg(atlasPath));
        return false;
    }
    if (!QFile::exists(resolvedSkel)) {
        qWarning("[SpineEW] FATAL: Skel file not found: '%s'", qPrintable(resolvedSkel));
        emit effectError(QString("Skel file not found: %1").arg(skelPath));
        return false;
    }
    qWarning("[SpineEW] Resolved: atlas='%s' skel='%s'", qPrintable(resolvedAtlas), qPrintable(resolvedSkel));

    // Load atlas (textures need current GL context)
    qWarning("[SpineEW] Loading atlas...");
    _atlas = std::make_unique<spine::Atlas>(
        spine::String(resolvedAtlas.toUtf8().constData()),
        _textureLoader.get(), true);

    qWarning("[SpineEW] Atlas pages: %d, regions: %d",
             (int)_atlas->getPages().size(), (int)_atlas->getRegions().size());

    if (_atlas->getPages().isEmpty()) {
        qWarning("[SpineEW] FATAL: Atlas has no pages!");
        emit effectError(QString("Failed to parse atlas: %1").arg(atlasPath));
        _atlas.reset();
        return false;
    }

    // Load skeleton
    qWarning("[SpineEW] Loading skeleton binary...");
    spine::SkeletonBinary binary(_atlas.get());
    binary.setScale(_spineScale);

    _skeletonData = binary.readSkeletonDataFile(
        spine::String(resolvedSkel.toUtf8().constData()));

    if (!_skeletonData) {
        qWarning("[SpineEW] FATAL: Failed to load skeleton! Error: '%s'",
                 binary.getError().buffer());
        emit effectError(QString("Failed to load skeleton: %1").arg(skelPath));
        _atlas.reset();
        return false;
    }
    qWarning("[SpineEW] Skeleton loaded: bones=%d slots=%d anims=%d",
             (int)_skeletonData->getBones().size(),
             (int)_skeletonData->getSlots().size(),
             (int)_skeletonData->getAnimations().size());

    _skeleton = std::make_unique<spine::Skeleton>(_skeletonData);
    _animStateData = std::make_unique<spine::AnimationStateData>(_skeletonData);
    _animStateData->setDefaultMix(0.2f);
    _animState = std::make_unique<spine::AnimationState>(_animStateData.get());
    _clipper = std::make_unique<spine::SkeletonClipping>();

    return true;
}

void SpineEffectWidget::cleanupSpine() {
    _animState.reset();
    _animStateData.reset();
    _skeleton.reset();
    if (_skeletonData) {
        delete _skeletonData;
        _skeletonData = nullptr;
    }
    _atlas.reset();
    _clipper.reset();
    _textureLoader.reset();
}

void SpineEffectWidget::renderSkeleton() {
    if (!_skeleton || !_shader) return;

    _vertices.clear();
    _indices.clear();

    spine::Vector<spine::Slot *> &drawOrder = _skeleton->getDrawOrder();
    GLuint baseVertex = 0;

    static int renderLogCount = 0;
    bool doLog = (++renderLogCount <= 10);
    if (doLog)
        qWarning("[SpineEW] renderSkeleton: drawOrder.size=%d", (int)drawOrder.size());

    struct BatchCmd {
        QOpenGLTexture *texture;
        int blendMode;
        int indexStart;
        int indexCount;
    };
    QVector<BatchCmd> batches;

    for (size_t i = 0; i < drawOrder.size(); ++i) {
        spine::Slot *slot = drawOrder[i];
        if (!slot) { if (doLog) qWarning("[SpineEW]   slot[%d] NULL", (int)i); continue; }

        spine::Attachment *attachment = slot->getAttachment();
        if (!attachment) { if (doLog) qWarning("[SpineEW]   slot[%d] '%s' NO attachment", (int)i, slot->getData().getName().buffer()); continue; }

        if (doLog) qWarning("[SpineEW]   slot[%d] '%s' attach='%s' type=%d",
                            (int)i, slot->getData().getName().buffer(),
                            attachment->getName().buffer(), (int)attachment->getType());

        QOpenGLTexture *texture = nullptr;
        float *uvs = nullptr;
        int vertexCount = 0;
        unsigned short *triangles = nullptr;
        int triangleCount = 0;
        float worldVertices[1024];

        spine::Color skeletonColor = _skeleton->getColor();
        spine::Color slotColor = slot->getColor();
        spine::Color attachColor;

        if (attachment->getType() == spine::AttachmentType_Region) {
            spine::RegionAttachment *region = static_cast<spine::RegionAttachment *>(attachment);
            attachColor = region->getColor();

            if (doLog) {
                qWarning("[SpineEW]   region: pos=(%.1f,%.1f) size=(%.1f,%.1f) rot=%.1f scale=(%.2f,%.2f)",
                         region->getX(), region->getY(), region->getWidth(), region->getHeight(),
                         region->getRotation(), region->getScaleX(), region->getScaleY());
                qWarning("[SpineEW]   region color: (%.2f,%.2f,%.2f,%.2f)",
                         attachColor.r, attachColor.g, attachColor.b, attachColor.a);
                qWarning("[SpineEW]   atlasRegion=%p", region->getRegion());
                if (region->getRegion()) {
                    qWarning("[SpineEW]   atlasRegion: name='%s' %dx%d orig=%dx%d page=%p texHandle=%p",
                             region->getRegion()->name.buffer(),
                             region->getRegion()->width, region->getRegion()->height,
                             region->getRegion()->originalWidth, region->getRegion()->originalHeight,
                             region->getRegion()->page,
                             region->getRegion()->page ? region->getRegion()->page->texHandle : nullptr);
                }
                float *off = region->getOffset();
                qWarning("[SpineEW]   offset: [%.1f,%.1f  %.1f,%.1f  %.1f,%.1f  %.1f,%.1f]",
                         off[0], off[1], off[2], off[3], off[4], off[5], off[6], off[7]);
                float *uv = region->getUVs();
                qWarning("[SpineEW]   uvs: [%.4f,%.4f  %.4f,%.4f  %.4f,%.4f  %.4f,%.4f]",
                         uv[0], uv[1], uv[2], uv[3], uv[4], uv[5], uv[6], uv[7]);
                spine::Bone &bone = slot->getBone();
                qWarning("[SpineEW]   bone '%s': worldX=%.1f worldY=%.1f a=%.3f b=%.3f c=%.3f d=%.3f",
                         bone.getData().getName().buffer(),
                         bone.getWorldX(), bone.getWorldY(),
                         bone.getA(), bone.getB(), bone.getC(), bone.getD());
            }

            region->computeWorldVertices(slot->getBone(), worldVertices, 0, 2);
            vertexCount = 4;
            uvs = region->getUVs();
            static unsigned short quadIdx[] = {0, 1, 2, 2, 3, 0};
            triangles = quadIdx;
            triangleCount = 6;
            if (region->getRegion())
                texture = QtSpineTextureLoader::getTexture(region->getRegion()->page->texHandle);

            if (doLog) {
                qWarning("[SpineEW]   worldVerts: (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f)",
                         worldVertices[0], worldVertices[1], worldVertices[2], worldVertices[3],
                         worldVertices[4], worldVertices[5], worldVertices[6], worldVertices[7]);
                qWarning("[SpineEW]   texture=%p vertCount=%d", texture, vertexCount);
            }

        } else if (attachment->getType() == spine::AttachmentType_Mesh) {
            spine::MeshAttachment *mesh = static_cast<spine::MeshAttachment *>(attachment);
            attachColor = mesh->getColor();
            int wvLen = mesh->getWorldVerticesLength();
            mesh->computeWorldVertices(*slot, 0, wvLen, worldVertices, 0, 2);
            vertexCount = wvLen / 2;
            uvs = mesh->getUVs().buffer();
            triangles = mesh->getTriangles().buffer();
            triangleCount = static_cast<int>(mesh->getTriangles().size());
            if (mesh->getRegion())
                texture = QtSpineTextureLoader::getTexture(mesh->getRegion()->page->texHandle);
        } else {
            if (doLog) qWarning("[SpineEW]   SKIP: unknown attachment type %d", (int)attachment->getType());
            continue;
        }

        if (!texture || vertexCount == 0) {
            if (doLog) qWarning("[SpineEW]   SKIP: texture=%p vertCount=%d", texture, vertexCount);
            continue;
        }

        float r = skeletonColor.r * slotColor.r * attachColor.r;
        float g = skeletonColor.g * slotColor.g * attachColor.g;
        float b = skeletonColor.b * slotColor.b * attachColor.b;
        float a = skeletonColor.a * slotColor.a * attachColor.a;
        if (doLog) qWarning("[SpineEW]   color: skel(%.2f,%.2f,%.2f,%.2f) slot(%.2f,%.2f,%.2f,%.2f) attach(%.2f,%.2f,%.2f,%.2f) => a=%.2f",
                            skeletonColor.r, skeletonColor.g, skeletonColor.b, skeletonColor.a,
                            slotColor.r, slotColor.g, slotColor.b, slotColor.a,
                            attachColor.r, attachColor.g, attachColor.b, attachColor.a, a);
        if (a <= 0) { if (doLog) qWarning("[SpineEW]   SKIP: alpha <= 0"); continue; }

        int indexStart = _indices.size();
        for (int vi = 0; vi < vertexCount; ++vi) {
            Vertex v;
            v.x = worldVertices[vi * 2];
            v.y = worldVertices[vi * 2 + 1];
            v.u = uvs[vi * 2];
            v.v = uvs[vi * 2 + 1];
            v.r = r; v.g = g; v.b = b; v.a = a;
            _vertices.append(v);
        }

        for (int ti = 0; ti < triangleCount; ++ti)
            _indices.append(baseVertex + triangles[ti]);

        int bm = static_cast<int>(slot->getData().getBlendMode());
        if (!batches.isEmpty() && batches.last().texture == texture
            && batches.last().blendMode == bm) {
            batches.last().indexCount += triangleCount;
        } else {
            BatchCmd cmd;
            cmd.texture = texture;
            cmd.blendMode = bm;
            cmd.indexStart = indexStart;
            cmd.indexCount = triangleCount;
            batches.append(cmd);
        }
        baseVertex += vertexCount;
    }

    if (_vertices.isEmpty()) {
        static int emptyLogCount = 0;
        if (++emptyLogCount <= 10)
            qWarning("[SpineEW] renderSkeleton: NO vertices generated! (batches=%d drawOrder=%d)",
                     batches.size(), (int)drawOrder.size());
        return;
    }

    qWarning("[SpineEW] renderSkeleton: %d vertices, %d indices, %d batches (log#%d)",
             _vertices.size(), _indices.size(), batches.size(), renderLogCount);

    // Only log render success for first 3 frames
    bool doRenderLog = (renderLogCount <= 3);
    if (doRenderLog)
        qWarning("[SpineEW] Rendering frame with GL");

    // Orthographic projection
    QMatrix4x4 mvp;
    mvp.ortho(0, static_cast<float>(width()),
              static_cast<float>(height()), 0, -1, 1);

    glEnable(GL_BLEND);
    _shader->bind();
    _shader->setUniformValue("uMVP", mvp);
    _shader->setUniformValue("uTexture", 0);

    int posLoc   = _shader->attributeLocation("aPos");
    int uvLoc    = _shader->attributeLocation("aUV");
    int colorLoc = _shader->attributeLocation("aColor");

    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(uvLoc);
    glEnableVertexAttribArray(colorLoc);

    const Vertex *vData = _vertices.constData();
    int stride = sizeof(Vertex);

    glVertexAttribPointer(posLoc,   2, GL_FLOAT, GL_FALSE, stride, &vData->x);
    glVertexAttribPointer(uvLoc,    2, GL_FLOAT, GL_FALSE, stride, &vData->u);
    glVertexAttribPointer(colorLoc, 4, GL_FLOAT, GL_FALSE, stride, &vData->r);

    for (const BatchCmd &cmd : batches) {
        switch (cmd.blendMode) {
        case 0: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
        case 1: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
        case 2: glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); break;
        case 3: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
        }
        if (cmd.texture) cmd.texture->bind(0);
        glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT,
                       _indices.constData() + cmd.indexStart);
    }

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(uvLoc);
    glDisableVertexAttribArray(colorLoc);

    _shader->release();
}
