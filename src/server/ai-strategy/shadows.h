#ifndef AI_STRATEGY_SHADOWS_H
#define AI_STRATEGY_SHADOWS_H

#include <QJsonObject>
#include <QString>
#include <QList>
#include <QJsonValue>
#include <functional>

struct AIEventView;
struct AIRequest;
struct AIResult;
struct AIWorldView;
class Card;
class Player;
class QVariant;
class Room;
class ServerPlayer;

QJsonObject readAIGuanxingShadow(Room&,const AIRequest&,const ServerPlayer*);
QJsonObject writeAIGuanxingShadow(Room&,const AIRequest&,const ServerPlayer*,bool);
void invalidateAIGuanxingShadow(Room&);

void initializeAIKurouShadow(Room&);
void invalidateAIKurouShadow(Room&);
QJsonObject readAIKurouShadow(Room&,const AIRequest&,const ServerPlayer*);
QJsonObject writeAIKurouShadow(Room&,const AIRequest&,const ServerPlayer*,bool);
int aiHegKurouPrototype(const AIRequest&,const ServerPlayer*,const QString&,const QString&);

void initializeAICrossbowShadow(Room&);
void invalidateAICrossbowShadow(Room&);
QJsonObject readAICrossbowShadow(Room&,const AIRequest&,const ServerPlayer*);
QJsonObject writeAICrossbowShadow(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&source);

// Once per Room, before policies. Deliberately retained across AI VM rebuilds.
void initializeAIManbing(Room &);
// Existing legacy/fallback/external-policy entry invalidates tracked continuity.
void invalidateAIManbing(Room &);
// Optional exact native flag mutation hook, before its possible unchanged-native early return.
// Only manbingTo_<current-roster-name>; invalidates, never publishes a value.
void observeAIManbingFlag(Room &, const ServerPlayer *, const QString &);
// Host supplies the actual active request/viewer; optional write only sets true.
// No Player flags, gameplay revision, payment receipts or card authority change.
QJsonObject queryAIManbing(Room &, const AIRequest &, const ServerPlayer *,
                                 const QString &targetName, bool write);

// Host must supply its current active decision/event request, never a Lua-built
// request. Only the answering owner's fixed valuation recursion bit is exposed.
bool readAIUseValueFlag(Room &, const ServerPlayer *, const AIRequest &, bool &value, QString *error = nullptr);
bool writeAIUseValueFlag(Room &, const ServerPlayer *, const AIRequest &, bool value, QString *error = nullptr);
// Call for exact +/-stack_overflow_UseValue commands before the service's
// absent-removal early return. This records the completed intent even when only
// an isolated shadow exists. Generic '.'/bury resets are tracked by clear epoch.
void synchronizeAIUseValueFlag(Room &, const ServerPlayer *, const QString &command);

// Call once after all configured donor scripts/Original overrides and before any
// policy invocation. Never recapture on an isolated VM rebuild.
void captureAIOverseasPriorities(Room &);
// Every policy entry, including events, must pass this public viewer boundary.
// Legacy/fallback or uninstrumented execution invalidates all continuity.
void beginAIOverseasPriorityPolicy(Room &,const QString &viewer);
void invalidateAIOverseasPriorities(Room &,bool invalidateChoices=true);
QJsonObject queryAIOverseasPriority(Room &,const AIRequest &,ServerPlayer *,const QJsonObject &);
QJsonObject writeAIXiafengFlag(Room &,const AIRequest &,ServerPlayer *,int cardId,bool enabled);

void captureAIOriginalSD(Room &);
void beginAIOriginalSD(Room &,const QString &viewer);
void invalidateAIOriginalSD(Room &);
void invalidateAIOriginalSDCard(Room &,int cardId);
QJsonObject queryAIOriginalSD(Room &,const AIRequest &,ServerPlayer *,const QJsonObject &);
bool resolveAIOriginalSD(Room &,ServerPlayer *,const AIRequest &,AIResult &);

// Only already-disclosed general identities (or the viewer's own identities)
// authorize looking up the exact General description used by donor policy.
void projectAITargetRecommend(AIWorldView &, Room &, const ServerPlayer *);
// Capture only after configured scripts and Original overrides, before policy.
void captureAITargetCache(Room &);
// Exact source filterEvent/invalidate_qlist_cache boundary, not every recordEvent.
void resetAITargetCache(Room &);
// Legacy/external policy can mutate a VM-global cache without lawful provenance.
void invalidateAITargetCache(Room &);
QJsonObject queryAITargetCache(Room &, const AIRequest &, ServerPlayer *, const QJsonObject &);

// Call only after the corresponding actual public fixed-distance notification.
void observeAIFixedDistance(Room &, Player *, const Player *, int distance,
                                  bool set, const QList<ServerPlayer *> &notifiedPlayers);
void projectAIFixedDistanceWitness(AIWorldView &, Room &, const ServerPlayer *);

// Source smart-ai.lua:7212 inspects only these two visibility flags. A lawful
// remembered/canSeeHandcard face alone does not satisfy that policy predicate.
void projectAIVisibleHandFlags(AIWorldView &, Room &, const ServerPlayer *);

// Actual ServerPlayer construction only, before any flag setter. Not a reset API.
void initializeAIPublicSkillFlags(Room &, ServerPlayer *);
// Call after the real all-client flags-property broadcast, never direct setFlags.
void observeAIPublicSkillFlag(Room &, ServerPlayer *, const QString &);
void projectAIPublicSkillFlagQuestion(AIRequest &, Room &, ServerPlayer *);
void projectAIPublicSkillFlagEvent(AIWorldView &, Room &, ServerPlayer *,
    const AIEventView &);

// Invoke only after a successful broadcastProperty. `value` is the exact
// optional override passed to that broadcast (empty means native property).
void recordAIPublicGeneralSlot(Room &room, ServerPlayer *player,
                                     const char *property, const QString &value);
// Undefined means no lawful proof. Never reads a foreign actual_general2.
QJsonValue aiShownAllGenerals(Room &room, ServerPlayer *viewer,
                                   ServerPlayer *player);

void projectAIIntentionWorld(AIWorldView &, Room &, ServerPlayer *);
void projectAIIntentionEvent(AIEventView &, const AIWorldView &, Room &,
    ServerPlayer *observer, ServerPlayer *eventPlayer, const QVariant &,
    const std::function<QJsonObject(const Card *)> &publicCard);

// Invoke once after normal legacy AI autoload, before any strategy executes.
void captureAIMidaoNaMetadata(Room &room);
void projectAIMidaoNaMetadata(AIWorldView &world, Room &room);

#endif
