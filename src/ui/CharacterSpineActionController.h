/******************************************************************************
 * CharacterSpineActionController.h
 *
 * Central controller for character Spine "pop-out" (break-frame) actions.
 *
 * Replaces the JS-based chukuangWorker logic with a native C++ state machine
 * that manages:
 *   - Per-player action state (idle / attacking / special / entrance)
 *   - Primary & deputy skin resolution
 *   - Cooldown and mutual-exclusion between concurrent actions
 *   - Pop-out → hold → return-to-frame tween choreography
 *   - Indicator-line sub-effects spawned per attack target
 *   - Pre-cached animation durations to avoid runtime skeleton queries
 *
 * Owned by RoomScene; one instance per game room.
 *****************************************************************************/

#ifndef CHARACTER_SPINE_ACTION_CONTROLLER_H
#define CHARACTER_SPINE_ACTION_CONTROLLER_H

#include <QObject>
#include <QHash>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QList>
#include <QJsonObject>
#include <functional>

class SpineGlItem;
class SpineIndicatorLine;
class PlayerCardContainer;
class Photo;
class Dashboard;

// ═══════════════════════════════════════════════════════════════════════════
//  Data types
// ═══════════════════════════════════════════════════════════════════════════

/// Identifies a character action type.
enum class ActionType {
    Attack,      // 攻击  (was "GongJi")
    Special,     // 特殊  (was "TeShu")
    Entrance,    // 出场  (was "chuchang" / "ChuChang")
    Idle         // 待机  (was "DaiJi")
};

/// Required by QHash<ActionType, ...> — scoped enums need an explicit qHash.
inline uint qHash(ActionType key, uint seed = 0) {
    return ::qHash(static_cast<int>(key), seed);
}

/// Cached metadata for a single action of a skin.
struct ActionMetadata {
    QString animationName;    ///< Spine animation track name (e.g. "GongJi", "idle")
    float   duration  = 0;   ///< Raw animation duration (seconds)
    float   showTime  = 0;   ///< How long the pop-out stays on screen (seconds)
    float   scale     = 1.0f;
    QPointF position;         ///< Scene position for pop-out (if specified)
    bool    autoPosition = true;  ///< Let controller calculate pop-out position
    bool    available = false;    ///< Whether this action was resolved successfully
    bool    flipX     = false;
    QString skelBasePath;     ///< Skeleton base path (may differ from idle skin)
    QString runtimeVersion;   ///< Spine runtime version hint
};

/// Per-skin configuration: holds idle + action metadata.
struct SkinConfig {
    QString basePath;              ///< Spine skeleton base path (used for idle)
    QString runtimeVersion;
    float   idleScale    = 1.0f;
    float   idleAlpha    = 1.0f;
    QPointF idlePosition;
    QString background;            ///< Optional background image (relative to asset prefix)

    QHash<ActionType, ActionMetadata> actions;

    /// Indicator-line config (optional).
    struct IndicatorConfig {
        bool    enabled    = false;
        QString skelName;          ///< Skeleton for the indicator beam
        QString effectName;        ///< Skeleton for the hit-burst effect
        float   delay      = 0;   ///< Delay before beam fires (fraction of action time)
        float   speed      = 1.0f;
        float   effectDelay= 0.5f;
        QString runtimeVersion;
    } indicator;
};

/// Tracks the live state of one player seat (may have primary + deputy skins).
struct PlayerActionState {
    /// Which action is currently playing (Idle = nothing special happening).
    ActionType currentAction  = ActionType::Idle;

    /// Timestamp of last action trigger (for cooldown).
    qint64     lastTriggerMs  = 0;

    /// The SpineGlItem currently performing the pop-out (nullptr if none).
    SpineGlItem *activePopOut = nullptr;

    /// Skin IDs
    QString primarySkinId;
    QString deputySkinId;

    /// Seat position in the scene (for return-to-frame target).
    QPointF seatPosition;
    QSizeF  seatSize;

    /// Whether the player is alive.
    bool alive = true;
};

// ═══════════════════════════════════════════════════════════════════════════
//  Controller
// ═══════════════════════════════════════════════════════════════════════════

class CharacterSpineActionController : public QObject
{
    Q_OBJECT

public:
    explicit CharacterSpineActionController(QGraphicsScene *scene, QObject *parent = nullptr);
    ~CharacterSpineActionController() override;

    // ─── Configuration ──────────────────────────────────────────

    /// Set the path prefix where Spine assets live (e.g. "assets/dynamic").
    void setAssetPathPrefix(const QString &prefix);

    /// Register a skin for a player seat.
    /// @param playerId  Unique seat id (e.g. "player_0").
    /// @param skinId    Unique skin identifier.
    /// @param config    Full skin configuration.
    /// @param isPrimary Whether this is the primary (true) or deputy (false) skin.
    void registerSkin(const QString &playerId, const QString &skinId,
                      const SkinConfig &config, bool isPrimary);

    /// Remove all skins for a player (e.g. on disconnect).
    void unregisterPlayer(const QString &playerId);

    /// Update seat geometry (call when layout changes).
    void updateSeatGeometry(const QString &playerId,
                            const QPointF &position, const QSizeF &size);

    // ─── Pre-loading ────────────────────────────────────────────

    /// Pre-load all Spine skeletons needed for a player's skins.
    /// Loads happen via SpineGlItem::loadSpine; actions' duration/showTime
    /// are cached from the resulting animation list.
    void preloadPlayer(const QString &playerId);


    // ─── JSON config loading ────────────────────────────────────

    /// Load the master dynamic-skin configuration JSON.
    /// @param jsonPath  Path to dynamicSkinConfig.json.
    /// @return true if parsed successfully.
    bool loadConfigFromJson(const QString &jsonPath);

    /// Look up a SkinConfig for a general + skin name from the loaded JSON.
    /// Returns true and fills `out` if found.
    bool buildSkinConfigForGeneral(const QString &generalName,
                                   const QString &skinName,
                                   SkinConfig &out) const;

    /// Check whether any dynamic skin entry exists for a given general.
    bool hasDynamicSkin(const QString &generalName) const;

    /// Return the first available skin name for a general (or empty).
    QString defaultSkinNameForGeneral(const QString &generalName) const;

    // ─── Action triggering ──────────────────────────────────────

    /// Attempt to trigger an action for a player.
    /// @param playerId    Seat id.
    /// @param action      Which action to play.
    /// @param targetPositions  Scene positions of attack targets (for indicator lines).
    /// @param isLocalPlayer    True if this is the dashboard (self) player.
    /// @param attackDirection  Normalized direction vector for flip logic.
    /// @return true if the action was accepted and will play.
    bool triggerAction(const QString &playerId,
                       ActionType action,
                       const QList<QPointF> &targetPositions = {},
                       bool isLocalPlayer = false,
                       const QPointF &attackDirection = QPointF());

    /// Force-cancel any running action for a player and return to idle.
    void cancelAction(const QString &playerId);

    /// Cancel with a visual fade-out (used when player dies).
    void cancelActionWithFade(const QString &playerId, int fadeMs = 300);

    /// Mark player as dead (blocks future actions, fades out pop-outs).
    void setPlayerAlive(const QString &playerId, bool alive);

    // ─── Cooldown settings ──────────────────────────────────────

    /// Minimum milliseconds between two actions on the same seat.
    void setCooldownMs(int ms) { _cooldownMs = ms; }
    int  cooldownMs() const    { return _cooldownMs; }

    /// Whether Attack + Special are mutually exclusive (default: true).
    void setMutualExclusion(bool on) { _mutualExclusion = on; }

    /// Playback speed multiplier (default 1.2 like the JS version).
    void setPlaybackSpeed(float speed) { _playbackSpeed = speed; }

signals:
    /// Emitted when a player action starts (UI may hide idle animation).
    void actionStarted(const QString &playerId, ActionType action);

    /// Emitted when the player returns to idle (UI may show idle animation).
    void actionFinished(const QString &playerId, ActionType action);

    /// The action was requested but no matching animation exists.
    void actionUnavailable(const QString &playerId, ActionType action);

    /// Emitted when a dynamic skin with a background image is registered.
    void skinBackgroundChanged(const QString &playerId, const QString &backgroundPath);

private:
    // ─── Helpers ────────────────────────────────────────────────
    SkinConfig *findSkin(const QString &playerId, ActionType action, QString &outSkinId);
    ActionMetadata *resolveAction(SkinConfig *skin, ActionType action);
    SpineGlItem *createPopOutItem(const SkinConfig &skin, const ActionMetadata &meta);

    /// Auto-discover action animations from a loaded skeleton probe.
    /// For each ActionType NOT already present in skin.actions, checks
    /// well-known animation name aliases in the skeleton and adds
    /// ActionMetadata entries with available=true if found.
    void autoDiscoverActions(SpineGlItem *probe, SkinConfig &skin);
    void startPopOutSequence(const QString &playerId, SkinConfig *skin,
                             ActionMetadata *meta, const QList<QPointF> &targets,
                             bool isLocal, const QPointF &direction);
    QPointF computePopOutPosition(const PlayerActionState &state, const ActionMetadata &meta,
                                   bool isLocal, ActionType action) const;
    void spawnIndicatorLines(const SkinConfig &skin, SpineGlItem *popOut,
                             const QList<QPointF> &targets,
                             const PlayerActionState &state, float delay);
    void returnToIdle(const QString &playerId);
    void cleanupPopOut(PlayerActionState &state);

    // ─── Data ───────────────────────────────────────────────────
    QGraphicsScene *_scene;
    QString         _assetPrefix;

    /// playerId → per-seat state
    QHash<QString, PlayerActionState> _playerStates;

    /// (playerId, skinId) → skin config
    QHash<QString, QHash<QString, SkinConfig>> _skinConfigs;

    /// Active indicator line items (auto-cleaned).
    QList<SpineIndicatorLine *> _activeIndicators;

    /// Loaded JSON config: generalName → { skinName → QJsonObject }
    QHash<QString, QHash<QString, QJsonObject>> _jsonSkinDatabase;

    // ─── Tuning ─────────────────────────────────────────────────
    int   _cooldownMs      = 40;
    bool  _mutualExclusion = true;
    float _playbackSpeed   = 1.2f;
    int   _returnDelayMs   = 400;   ///< Delay for "return to frame" motion
    int   _showTimeBeforeMs= 100;   ///< Extra showTime padding
};

#endif // CHARACTER_SPINE_ACTION_CONTROLLER_H
