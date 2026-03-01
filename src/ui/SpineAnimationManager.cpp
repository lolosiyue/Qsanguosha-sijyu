/******************************************************************************
 * SpineAnimationManager.cpp
 * Convenience manager for triggering Spine fullscreen effects.
 *****************************************************************************/

#include "SpineAnimationManager.h"
#include "SpineEffectWidget.h"

SpineAnimationManager *SpineAnimationManager::s_instance = nullptr;

SpineAnimationManager::SpineAnimationManager(QObject *parent)
    : QObject(parent)
{
}

SpineAnimationManager::~SpineAnimationManager() {
    stopAll();
}

SpineAnimationManager *SpineAnimationManager::instance() {
    if (!s_instance)
        s_instance = new SpineAnimationManager();
    return s_instance;
}

bool SpineAnimationManager::playFullscreen(QWidget *parentWidget,
                                            const QString &basePath,
                                            const QString &animationName,
                                            bool loop, bool clickThrough,
                                            float scale, float offsetX, float offsetY) {
    qWarning("[SpineAM] playFullscreen called: basePath='%s' anim='%s' parent=%p",
             qPrintable(basePath), qPrintable(animationName), parentWidget);
    return playFullscreenFiles(parentWidget,
                               basePath + ".atlas",
                               basePath + ".skel",
                               animationName, loop, clickThrough,
                               scale, offsetX, offsetY);
}

bool SpineAnimationManager::playFullscreenFiles(QWidget *parentWidget,
                                                 const QString &atlasPath,
                                                 const QString &skelPath,
                                                 const QString &animationName,
                                                 bool loop, bool clickThrough,
                                                 float scale, float offsetX, float offsetY) {
    if (!parentWidget) return false;

    SpineEffectWidget *effect = new SpineEffectWidget(parentWidget);
    effect->setSpineScale(scale);
    effect->setSpineOffset(offsetX, offsetY);
    effect->setClickThrough(clickThrough);

    // Size to cover the parent
    effect->setGeometry(0, 0, parentWidget->width(), parentWidget->height());

    // Track active effects
    _activeEffects.append(effect);

    QString basePath = atlasPath;
    if (basePath.endsWith(".atlas"))
        basePath.chop(6); // remove .atlas

    connect(effect, &SpineEffectWidget::effectFinished, this,
            [this, effect, basePath]() {
                _activeEffects.removeOne(effect);
                emit effectFinished(basePath);
            });

    connect(effect, &SpineEffectWidget::effectError, this,
            [this, effect](const QString &error) {
                qWarning("SpineAnimationManager: %s", qPrintable(error));
                _activeEffects.removeOne(effect);
                effect->deleteLater();
            });

    emit effectStarted(basePath);

    qWarning("[SpineAM] Calling playEffectFiles: atlas='%s' skel='%s'", qPrintable(atlasPath), qPrintable(skelPath));
    bool ok = effect->playEffectFiles(atlasPath, skelPath, animationName, loop, true);
    if (!ok) {
        qWarning("[SpineAM] playEffectFiles FAILED");
        _activeEffects.removeOne(effect);
        effect->deleteLater();
        return false;
    }

    qWarning("[SpineAM] playEffectFiles succeeded, effect widget shown");
    return true;
}

void SpineAnimationManager::stopAll(QWidget *parentWidget) {
    QList<SpineEffectWidget *> toStop;
    for (auto *w : _activeEffects) {
        if (!parentWidget || w->parentWidget() == parentWidget)
            toStop.append(w);
    }
    for (auto *w : toStop) {
        w->stopEffect();
        _activeEffects.removeOne(w);
        w->deleteLater();
    }
}
