/******************************************************************************
 * CharacterSpineActionController.cpp
 *
 * Native C++ implementation of the character pop-out action state machine,
 * replacing the JS-based chukuangWorker.
 *****************************************************************************/

#include "CharacterSpineActionController.h"
#include "SpineGlItem.h"
#include "SpineIndicatorLine.h"

#include <QGraphicsScene>
#include <QDateTime>
#include <QtMath>
#include <QTimer>
#include <QPointer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

// ═══════════════════════════════════════════════════════════════════════════
//  Construction / destruction
// ═══════════════════════════════════════════════════════════════════════════

CharacterSpineActionController::CharacterSpineActionController(QGraphicsScene *scene, QObject *parent)
    : QObject(parent)
    , _scene(scene)
{
}

CharacterSpineActionController::~CharacterSpineActionController()
{
    // Clean up any remaining pop-outs and indicators.
    // Detach from parent first so Qt won't double-delete us.
    setParent(nullptr);

    for (auto it = _playerStates.begin(); it != _playerStates.end(); ++it) {
        cleanupPopOut(it.value());
    }
    _playerStates.clear();

    qDeleteAll(_activeIndicators);
    _activeIndicators.clear();

    // Prevent stale scene pointer from being used by any queued events
    _scene = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Configuration
// ═══════════════════════════════════════════════════════════════════════════

void CharacterSpineActionController::setAssetPathPrefix(const QString &prefix)
{
    _assetPrefix = prefix;
    if (!_assetPrefix.isEmpty() && !_assetPrefix.endsWith('/'))
        _assetPrefix += '/';
}

void CharacterSpineActionController::registerSkin(const QString &playerId,
                                                   const QString &skinId,
                                                   const SkinConfig &config,
                                                   bool isPrimary)
{
    _skinConfigs[playerId][skinId] = config;

    PlayerActionState &state = _playerStates[playerId];
    if (isPrimary)
        state.primarySkinId = skinId;
    else
        state.deputySkinId = skinId;

    // Notify listeners about background image change
    if (!config.background.isEmpty()) {
        QString fullBgPath = _assetPrefix + config.background;
        emit skinBackgroundChanged(playerId, fullBgPath);
    }
}

void CharacterSpineActionController::unregisterPlayer(const QString &playerId)
{
    if (_playerStates.contains(playerId)) {
        cleanupPopOut(_playerStates[playerId]);
        _playerStates.remove(playerId);
    }
    _skinConfigs.remove(playerId);
}

void CharacterSpineActionController::updateSeatGeometry(const QString &playerId,
                                                         const QPointF &position,
                                                         const QSizeF &size)
{
    PlayerActionState &state = _playerStates[playerId];
    state.seatPosition = position;
    state.seatSize = size;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Pre-loading
// ═══════════════════════════════════════════════════════════════════════════

static const char *actionTypeName(ActionType a)
{
    switch (a) {
    case ActionType::Attack:   return "Attack";
    case ActionType::Special:  return "Special";
    case ActionType::Entrance: return "Entrance";
    case ActionType::Idle:     return "Idle";
    }
    return "Unknown";
}

void CharacterSpineActionController::preloadPlayer(const QString &playerId)
{
    if (!_skinConfigs.contains(playerId)) return;

    QHash<QString, SkinConfig> &skins = _skinConfigs[playerId];
    for (auto it = skins.begin(); it != skins.end(); ++it) {
        SkinConfig &skin = it.value();
        QString fullBase = _assetPrefix + skin.basePath;

        qDebug("[SpineAction] preloadPlayer '%s' skin '%s': basePath='%s'",
               qPrintable(playerId), qPrintable(it.key()), qPrintable(fullBase));

        // Preload idle skeleton
        SpineGlItem *probe = new SpineGlItem();
        if (!skin.runtimeVersion.isEmpty())
            probe->setRuntimeVersionHint(skin.runtimeVersion);
        if (probe->loadSpine(fullBase)) {
            // Auto-discovery: if no actions explicitly configured, scan for known names
            bool hadExplicitActions = !skin.actions.isEmpty();
            if (!hadExplicitActions)
                autoDiscoverActions(probe, skin);

            // Cache durations for each action from animation list
            for (auto ait = skin.actions.begin(); ait != skin.actions.end(); ++ait) {
                ActionMetadata &meta = ait.value();
                if (meta.skelBasePath.isEmpty() || meta.skelBasePath == skin.basePath) {
                    float dur = probe->animationDurationByName(meta.animationName);
                    if (dur > 0) {
                        meta.duration = dur;
                        if (meta.showTime <= 0)
                            meta.showTime = dur;
                        meta.available = true;
                    } else {
                        qWarning("[SpineAction] action '%s' anim '%s' NOT found in idle skeleton",
                                 actionTypeName(ait.key()), qPrintable(meta.animationName));
                    }
                }
            }
        } else {
            qWarning("[SpineAction]   FAILED to load idle skeleton '%s'", qPrintable(fullBase));
        }
        delete probe;

        // Preload separate action skeletons (different from idle)
        for (auto ait = skin.actions.begin(); ait != skin.actions.end(); ++ait) {
            ActionMetadata &meta = ait.value();
            if (!meta.skelBasePath.isEmpty() && meta.skelBasePath != skin.basePath) {
                SpineGlItem *actionProbe = new SpineGlItem();
                if (!meta.runtimeVersion.isEmpty())
                    actionProbe->setRuntimeVersionHint(meta.runtimeVersion);
                QString actionPath = _assetPrefix + meta.skelBasePath;
                qDebug("[SpineAction] loading separate action skeleton '%s' for '%s'",
                       qPrintable(actionPath), actionTypeName(ait.key()));
                if (actionProbe->loadSpine(actionPath)) {
                    float dur = actionProbe->animationDurationByName(meta.animationName);
                    if (dur > 0) {
                        meta.duration = dur;
                        if (meta.showTime <= 0)
                            meta.showTime = dur;
                        meta.available = true;
                    } else {
                        qWarning("[SpineAction] action '%s' anim '%s' NOT found in action skeleton",
                                 actionTypeName(ait.key()), qPrintable(meta.animationName));
                    }
                } else {
                    qWarning("[SpineAction] FAILED to load action skeleton '%s'",
                             qPrintable(actionPath));
                }
                delete actionProbe;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Auto-discovery of animations from a single skeleton
// ═══════════════════════════════════════════════════════════════════════════

/// Well-known animation name aliases per ActionType.
/// The first match found in the skeleton wins.
static const QHash<ActionType, QStringList> &knownAnimationAliases()
{
    static const QHash<ActionType, QStringList> aliases = {
        { ActionType::Attack,   { "GongJi", "gongji", "Attack", "attack" } },
        { ActionType::Special,  { "TeShu", "teshu", "JiNeng", "jineng", "JiNeng01", "JiNeng02",
                                  "Special", "special", "Skill", "skill" } },
        { ActionType::Entrance, { "ChuChang", "chuchang", "Entrance", "entrance",
                                  "Appear", "appear" } },
        { ActionType::Idle,     { "DaiJi", "daiji", "Idle", "idle",
                                  "Stand", "stand", "Default", "default" } }
    };
    return aliases;
}

void CharacterSpineActionController::autoDiscoverActions(SpineGlItem *probe, SkinConfig &skin)
{
    if (!probe) return;

    const QList<SpineGlItem::AnimationInfo> &anims = probe->availableAnimations();
    if (anims.isEmpty()) {
        qWarning("[SpineAction] autoDiscoverActions: skeleton has 0 animations");
        return;
    }

    // Build a quick lookup: animName → duration
    QHash<QString, float> animMap;
    for (const auto &ai : anims)
        animMap[ai.name] = ai.duration;

    const QHash<ActionType, QStringList> &aliases = knownAnimationAliases();

    for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
        ActionType actionType = it.key();

        // Skip Idle — it's not a pop-out action
        if (actionType == ActionType::Idle) continue;

        // Skip if already explicitly configured
        if (skin.actions.contains(actionType)) continue;

        // Try each alias until one matches
        for (const QString &alias : it.value()) {
            if (animMap.contains(alias)) {
                ActionMetadata meta;
                meta.animationName = alias;
                meta.skelBasePath  = QString();  // reuse basePath (single skeleton)
                meta.scale         = skin.idleScale;
                meta.flipX         = false;
                float dur = animMap[alias];
                meta.duration      = dur;
                meta.showTime      = dur;
                meta.available     = true;
                meta.autoPosition  = true;
                meta.runtimeVersion = skin.runtimeVersion;

                skin.actions[actionType] = meta;
                break;  // first match wins for this action type
            }
        }
    }

    // ── Fallback: if no known aliases matched, use the sole/longest animation ──
    // Many skins have a single animation named "play", "animation", etc.
    // Map it to all unassigned action types so the skin still works.
    if (skin.actions.isEmpty() && !anims.isEmpty()) {
        // Pick the longest-duration animation (or the only one)
        int bestIdx = 0;
        for (int i = 1; i < anims.size(); ++i) {
            if (anims[i].duration > anims[bestIdx].duration)
                bestIdx = i;
        }
        const SpineGlItem::AnimationInfo &best = anims[bestIdx];
        qDebug("[SpineAction] autoDiscoverActions: fallback animation '%s' for all action types",
               qPrintable(best.name));

        ActionType fallbackTypes[] = { ActionType::Attack, ActionType::Special, ActionType::Entrance };
        for (ActionType at : fallbackTypes) {
            if (skin.actions.contains(at)) continue;  // already set by explicit config

            ActionMetadata meta;
            meta.animationName  = best.name;
            meta.skelBasePath   = QString();
            meta.scale          = skin.idleScale;
            meta.flipX          = false;
            meta.duration       = best.duration;
            meta.showTime       = best.duration;
            meta.available      = true;
            meta.autoPosition   = true;
            meta.runtimeVersion = skin.runtimeVersion;

            skin.actions[at] = meta;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Dynamic skin registration (path-based)
// ═══════════════════════════════════════════════════════════════════════════

/// Determines the skin root path based on the resolved general name and skin index.
/// When skinIndex > 0: hero-skin/[generalName]/[skinIndex]/dynamicSkin/
/// When skinIndex == 0: fullskin/dynamicSkin/[generalName]/dynamicSkin/
static QString buildDynamicSkinRoot(const QString &resolvedGeneral, int skinIndex)
{
    if (skinIndex > 0) {
        return QString("hero-skin/%1/%2/dynamicSkin").arg(resolvedGeneral).arg(skinIndex);
    }
    return QString("fullskin/dynamicSkin/%1/dynamicSkin").arg(resolvedGeneral);
}

static void parseSkinConfigFromJson(const QJsonObject &obj, SkinConfig &out)
{
    out.runtimeVersion = obj.value("runtimeVersion").toString();
    out.idleScale = static_cast<float>(obj.value("scale").toDouble(0.35));
    out.idleAlpha = static_cast<float>(obj.value("idleAlpha").toDouble(1.0));
    out.background = obj.value("background").toString();

    auto parseAction = [&](const QString &key, ActionType type) {
        if (obj.contains(key) && obj.value(key).isObject()) {
            QJsonObject a = obj.value(key).toObject();
            ActionMetadata meta;
            meta.animationName = a.value("animationName").toString();
            meta.skelBasePath = a.value("skelBasePath").toString();
            meta.scale = static_cast<float>(a.value("scale").toDouble(out.idleScale));
            meta.flipX = a.value("flipX").toBool(false);
            meta.showTime = static_cast<float>(a.value("showTime").toDouble(0));
            meta.runtimeVersion = a.value("runtimeVersion").toString(out.runtimeVersion);
            meta.available = false;
            out.actions[type] = meta;
        }
    };

    parseAction("attack", ActionType::Attack);
    parseAction("special", ActionType::Special);
    parseAction("entrance", ActionType::Entrance);

    if (obj.contains("indicator") && obj.value("indicator").isObject()) {
        QJsonObject ind = obj.value("indicator").toObject();
        out.indicator.enabled = ind.value("enabled").toBool(false);
        out.indicator.skelName = ind.value("skelName").toString();
        out.indicator.effectName = ind.value("effectName").toString();
        out.indicator.delay = static_cast<float>(ind.value("delay").toDouble(0.3));
        out.indicator.speed = static_cast<float>(ind.value("speed").toDouble(1.0));
        out.indicator.effectDelay = static_cast<float>(ind.value("effectDelay").toDouble(0.5));
        out.indicator.runtimeVersion = ind.value("runtimeVersion").toString(out.runtimeVersion);
    }
}

void CharacterSpineActionController::registerDynamicSkin(const QString &playerId,
                                                          const QString &resolvedGeneral,
                                                          int skinIndex,
                                                          bool isPrimary)
{
    QString rootPath = buildDynamicSkinRoot(resolvedGeneral, skinIndex);
    QString fullBasePath = rootPath; // relative, combined with asset prefix later

    qDebug("[SpineAction] registerDynamicSkin player='%s' general='%s' root='%s'",
           qPrintable(playerId), qPrintable(resolvedGeneral), qPrintable(rootPath));

    // Probe skeleton to discover animations
    SpineGlItem *probe = new SpineGlItem();
    if (!probe->loadSpine(fullBasePath)) {
        qDebug("[SpineAction]   no dynamic skin at '%s'", qPrintable(fullBasePath));
        delete probe;
        return;
    }

    // Build SkinConfig: start with defaults
    SkinConfig config;
    config.basePath = fullBasePath;
    config.idleScale = 0.35f;
    config.idleAlpha = 1.0f;

    // Load config.json if present (override defaults)
    QString configPath = _assetPrefix + rootPath + "/config.json";
    QFile configFile(configPath);
    if (configFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(configFile.readAll());
        if (!doc.isNull() && doc.isObject()) {
            parseSkinConfigFromJson(doc.object(), config);
            qDebug("[SpineAction]   loaded config.json");
        }
    }

    // Auto-discover actions from skeleton (fills in missing actions)
    autoDiscoverActions(probe, config);

    // Cache durations
    for (auto it = config.actions.begin(); it != config.actions.end(); ++it) {
        ActionMetadata &meta = it.value();
        float dur = probe->animationDurationByName(meta.animationName);
        if (dur > 0) {
            meta.duration = dur;
            if (meta.showTime <= 0)
                meta.showTime = dur;
            meta.available = true;
        } else {
            qWarning("[SpineAction] action '%s' anim '%s' NOT found in skeleton",
                     actionTypeName(it.key()), qPrintable(meta.animationName));
        }
    }
    delete probe;

    if (config.actions.isEmpty()) {
        qDebug("[SpineAction]   no actions discovered, skipping");
        return;
    }

    QString skinId = QString("%1_%2").arg(resolvedGeneral).arg(skinIndex);
    registerSkin(playerId, skinId, config, isPrimary);
}

bool CharacterSpineActionController::hasDynamicSkin(const QString &resolvedGeneral,
                                                    int skinIndex) const
{
    QString rootPath = buildDynamicSkinRoot(resolvedGeneral, skinIndex);
    QString fullPath = _assetPrefix + rootPath;
    return QDir(fullPath).exists();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Action triggering
// ═══════════════════════════════════════════════════════════════════════════

bool CharacterSpineActionController::triggerAction(const QString &playerId,
                                                    ActionType action,
                                                    const QList<QPointF> &targetPositions,
                                                    bool isLocalPlayer,
                                                    const QPointF &attackDirection)
{
    if (!_playerStates.contains(playerId))
        return false;

    PlayerActionState &state = _playerStates[playerId];

    // Dead players cannot perform actions
    if (!state.alive)
        return false;

    // ── Cooldown check ──
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (state.lastTriggerMs > 0 && (now - state.lastTriggerMs) < _cooldownMs)
        return false;

    // ── Mutual exclusion: if an action is already running, check compatibility ──
    if (_mutualExclusion && state.currentAction != ActionType::Idle) {
        if (state.currentAction == ActionType::Entrance && action != ActionType::Entrance)
            return false;
        // Consecutive same-type attack is allowed (chain hits)
        if (state.currentAction == action && action == ActionType::Attack) {
            // Chain hit: reset the hold timer but keep the pop-out
            // (equivalent to lianxuChuKuang in JS)
        } else if (state.currentAction != action)
            return false;
    }

    QString skinId;
    SkinConfig *skin = findSkin(playerId, action, skinId);
    if (!skin) {
        emit actionUnavailable(playerId, action);
        return false;
    }

    ActionMetadata *meta = resolveAction(skin, action);
    if (!meta || !meta->available) {
        emit actionUnavailable(playerId, action);
        return false;
    }

    // ── If already popped out for same action (chain hit), just reset timer ──
    if (state.activePopOut && state.currentAction == action) {
        // Reset animation to start
        state.activePopOut->setAnimation(0, meta->animationName, false);
        state.lastTriggerMs = now;

        // Re-start the hold → return sequence
        float holdTime = meta->showTime / _playbackSpeed;
        QPointer<CharacterSpineActionController> guard(this);
        QTimer::singleShot(static_cast<int>(holdTime * 1000), this, [guard, playerId]() {
            if (guard.isNull()) return;
            guard->returnToIdle(playerId);
        });
        return true;
    }

    // ── Normal case: clean up previous pop-out (if any) and start fresh ──
    cleanupPopOut(state);

    state.lastTriggerMs = now;
    state.currentAction = action;

    startPopOutSequence(playerId, skin, meta, targetPositions,
                        isLocalPlayer, attackDirection);
    return true;
}

void CharacterSpineActionController::cancelAction(const QString &playerId)
{
    if (!_playerStates.contains(playerId)) return;
    PlayerActionState &state = _playerStates[playerId];
    cleanupPopOut(state);
    state.currentAction = ActionType::Idle;
    emit actionFinished(playerId, state.currentAction);
}

void CharacterSpineActionController::cancelActionWithFade(const QString &playerId, int fadeMs)
{
    if (!_playerStates.contains(playerId)) return;
    PlayerActionState &state = _playerStates[playerId];
    if (!state.activePopOut) {
        // No pop-out running — just reset state
        state.currentAction = ActionType::Idle;
        emit actionFinished(playerId, ActionType::Idle);
        return;
    }

    // Fade out the pop-out gracefully, then clean up
    state.activePopOut->fadeTo(0.0, fadeMs);
    QPointer<CharacterSpineActionController> guard(this);
    QTimer::singleShot(fadeMs + 50, this, [guard, playerId]() {
        if (guard.isNull()) return;
        CharacterSpineActionController *self = guard.data();
        if (!self->_playerStates.contains(playerId)) return;
        PlayerActionState &st = self->_playerStates[playerId];
        ActionType finished = st.currentAction;
        self->cleanupPopOut(st);
        st.currentAction = ActionType::Idle;
        emit self->actionFinished(playerId, finished);
    });
}

void CharacterSpineActionController::setPlayerAlive(const QString &playerId, bool alive)
{
    if (!_playerStates.contains(playerId)) return;
    _playerStates[playerId].alive = alive;
    if (!alive) {
        // Death: graceful fade-out instead of instant removal
        cancelActionWithFade(playerId, 300);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Internal helpers
// ═══════════════════════════════════════════════════════════════════════════

SkinConfig *CharacterSpineActionController::findSkin(const QString &playerId,
                                                       ActionType action,
                                                       QString &outSkinId)
{
    if (!_skinConfigs.contains(playerId))
        return nullptr;
    QHash<QString, SkinConfig> &skins = _skinConfigs[playerId];
    const PlayerActionState &state = _playerStates[playerId];

    // Try primary first
    if (!state.primarySkinId.isEmpty() && skins.contains(state.primarySkinId)) {
        SkinConfig &s = skins[state.primarySkinId];
        if (s.actions.contains(action)) {
            if (s.actions[action].available) {
                outSkinId = state.primarySkinId;
                return &s;
            }
        }
    }

    // Fall back to deputy
    if (!state.deputySkinId.isEmpty() && skins.contains(state.deputySkinId)) {
        SkinConfig &s = skins[state.deputySkinId];
        if (s.actions.contains(action)) {
            if (s.actions[action].available) {
                outSkinId = state.deputySkinId;
                return &s;
            }
        }
    }
    return nullptr;
}

ActionMetadata *CharacterSpineActionController::resolveAction(SkinConfig *skin, ActionType action)
{
    if (!skin || !skin->actions.contains(action)) return nullptr;
    return &skin->actions[action];
}

SpineGlItem *CharacterSpineActionController::createPopOutItem(const SkinConfig &skin,
                                                                const ActionMetadata &meta)
{
    SpineGlItem *item = new SpineGlItem();
    item->setZValue(10000); // Above normal scene items

    // Determine which skeleton to load
    QString basePath = meta.skelBasePath.isEmpty() ? skin.basePath : meta.skelBasePath;
    QString fullPath = _assetPrefix + basePath;

    QString rtVersion = meta.runtimeVersion.isEmpty() ? skin.runtimeVersion : meta.runtimeVersion;
    if (!rtVersion.isEmpty())
        item->setRuntimeVersionHint(rtVersion);

    if (!item->loadSpine(fullPath, meta.animationName)) {
        qWarning("[SpineAction] createPopOutItem FAILED: '%s'", qPrintable(fullPath));
        delete item;
        return nullptr;
    }

    item->setSpineScale(meta.scale);
    item->setFlipX(meta.flipX);

    return item;
}

QPointF CharacterSpineActionController::computePopOutPosition(const PlayerActionState &state,
                                                                const ActionMetadata &meta,
                                                                bool isLocal,
                                                                ActionType action) const
{
    Q_UNUSED(state);
    Q_UNUSED(isLocal);
    Q_UNUSED(action);
    if (!meta.autoPosition)
        return meta.position;

    // All actions pop out to scene center
    QRectF sceneRect = _scene ? _scene->sceneRect() : QRectF(0, 0, 1920, 1080);
    return sceneRect.center();
}

void CharacterSpineActionController::startPopOutSequence(const QString &playerId,
                                                          SkinConfig *skin,
                                                          ActionMetadata *meta,
                                                          const QList<QPointF> &targets,
                                                          bool isLocal,
                                                          const QPointF &direction)
{
    PlayerActionState &state = _playerStates[playerId];

    SpineGlItem *popOut = createPopOutItem(*skin, *meta);
    if (!popOut) {
        state.currentAction = ActionType::Idle;
        emit actionUnavailable(playerId, state.currentAction);
        return;
    }

    state.activePopOut = popOut;
    _scene->addItem(popOut);

    // Flip based on direction
    if (!isLocal && !direction.isNull()) {
        bool dirLeft = direction.x() < 0;
        if (dirLeft)
            popOut->setFlipX(!meta->flipX);
    }

    // ── Origin: seat avatar center ──
    QPointF seatCenter(
        state.seatPosition.x() + state.seatSize.width() / 2.0,
        state.seatPosition.y() + state.seatSize.height() / 2.0
    );

    // ── Destination: scene center ──
    QPointF sceneCenter = computePopOutPosition(state, *meta, isLocal, state.currentAction);

    // Start at the seat position, transparent and small
    popOut->setSpinePosition(seatCenter);
    popOut->setSpineOpacity(0.0);

    // Phase 1: fly from seat → scene center while fading in (200ms)
    int flyInMs = 200;
    popOut->moveTo(sceneCenter, flyInMs);
    popOut->fadeTo(1.0, flyInMs);

    // Start playing the action animation immediately
    popOut->play(false);

    emit actionStarted(playerId, state.currentAction);

    // ── Schedule hold → return-to-frame ──
    float adjustedShowTime = meta->showTime / _playbackSpeed;
    int returnDelay = _returnDelayMs;

    // Shorter animations get shorter return delay
    if (adjustedShowTime * 1000 <= 800) returnDelay = 200;
    else if (adjustedShowTime * 1000 <= 1200) returnDelay = 300;

    float holdMs = adjustedShowTime * 1000.0f - returnDelay + _showTimeBeforeMs;
    if (holdMs < flyInMs + 50) holdMs = flyInMs + 50;  // ensure fly-in completes first

    // ── Indicator lines ──
    if (state.currentAction == ActionType::Attack && skin->indicator.enabled && !targets.isEmpty()) {
        float indicatorDelay = skin->indicator.delay * adjustedShowTime * 1000.0f;
        spawnIndicatorLines(*skin, popOut, targets, state, indicatorDelay);
    }

    // Phase 3: after hold, fly back from scene center → seat and fade out.
    // Use QPointer guard so the lambda becomes a no-op if the controller is
    // destroyed (e.g. game ending) before the timer fires.
    QPointer<CharacterSpineActionController> guard(this);
    QTimer::singleShot(static_cast<int>(holdMs), this, [guard, playerId, returnDelay, seatCenter]() {
        if (guard.isNull()) return;
        CharacterSpineActionController *self = guard.data();
        if (!self->_playerStates.contains(playerId)) return;
        PlayerActionState &st = self->_playerStates[playerId];
        if (!st.activePopOut) return;

        st.activePopOut->moveTo(seatCenter, returnDelay);
        st.activePopOut->fadeTo(0.0, returnDelay);

        QPointer<CharacterSpineActionController> guard2(self);
        QTimer::singleShot(returnDelay, self, [guard2, playerId]() {
            if (guard2.isNull()) return;
            guard2->returnToIdle(playerId);
        });
    });
}

void CharacterSpineActionController::returnToIdle(const QString &playerId)
{
    if (!_playerStates.contains(playerId)) return;
    PlayerActionState &state = _playerStates[playerId];
    ActionType finishedAction = state.currentAction;
    cleanupPopOut(state);
    state.currentAction = ActionType::Idle;
    emit actionFinished(playerId, finishedAction);
}

void CharacterSpineActionController::cleanupPopOut(PlayerActionState &state)
{
    if (state.activePopOut) {
        state.activePopOut->cancelTweens();
        state.activePopOut->stop();
        // Guard: only remove from scene if the scene is still alive and the
        // item is actually in it.  During RoomScene destruction the scene may
        // already be tearing down its child items.
        if (_scene && state.activePopOut->scene() == _scene)
            _scene->removeItem(state.activePopOut);
        delete state.activePopOut;
        state.activePopOut = nullptr;
    }
}

void CharacterSpineActionController::spawnIndicatorLines(const SkinConfig &skin,
                                                          SpineGlItem *popOut,
                                                          const QList<QPointF> &targets,
                                                          const PlayerActionState &state,
                                                          float delayMs)
{
    if (!_scene || !skin.indicator.enabled) return;

    const SkinConfig::IndicatorConfig &cfg = skin.indicator;

    QPointF startPos = popOut->spinePosition();

    QPointer<CharacterSpineActionController> indicatorGuard(this);
    QTimer::singleShot(static_cast<int>(delayMs), this, [indicatorGuard, cfg, startPos, targets, state]() {
        if (indicatorGuard.isNull()) return;
        CharacterSpineActionController *self = indicatorGuard.data();
        for (const QPointF &targetPos : targets) {
            SpineIndicatorLine *line = new SpineIndicatorLine(self->_scene);
            line->setAssetPrefix(self->_assetPrefix);

            // Calculate rotation angle
            double dx = startPos.x() - targetPos.x();
            double dy = startPos.y() - targetPos.y();
            double angle = qRadiansToDegrees(qAtan2(dy, dx));
            double rotationAngle = 180.0 - angle;

            line->launch(cfg.skelName, cfg.runtimeVersion,
                         startPos, targetPos, rotationAngle,
                         cfg.speed);

            self->_activeIndicators.append(line);

            // Spawn hit-burst effect at target after beam arrives
            if (!cfg.effectName.isEmpty()) {
                float beamDuration = line->estimatedDuration();
                int burstDelay = static_cast<int>(beamDuration * cfg.effectDelay * 1000);
                QPointer<CharacterSpineActionController> burstGuard(self);
                QTimer::singleShot(burstDelay, self, [burstGuard, cfg, targetPos, line]() {
                    if (burstGuard.isNull()) return;
                    line->spawnBurstEffect(cfg.effectName, cfg.runtimeVersion, targetPos);
                });
            }

            // Auto-cleanup after animation finishes
            QPointer<CharacterSpineActionController> cleanGuard(self);
            QObject::connect(line, &SpineIndicatorLine::finished, self, [cleanGuard, line]() {
                if (cleanGuard.isNull()) return;
                cleanGuard->_activeIndicators.removeAll(line);
                line->deleteLater();
            });
        }
    });
}
