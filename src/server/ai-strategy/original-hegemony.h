#ifndef AI_STRATEGY_ORIGINAL_HEGEMONY_H
#define AI_STRATEGY_ORIGINAL_HEGEMONY_H

#include <QJsonObject>
#include <QString>
#include <functional>
#include <QVariant>

struct AIEventView;
struct AIRequest;
struct AIResult;
struct AIWorldView;
class Card;
struct JudgeStruct;
class QVariant;
class Room;
class ServerPlayer;

bool aiOriginalHegemonyQuestion(const AIRequest &request);
bool aiOriginalGuanxingConstruction(const AIRequest &request, const ServerPlayer *viewer,
                                   const QJsonObject &spec);
bool emitAIOriginalSourceWarning(Room &room, const AIRequest &request,
                                const ServerPlayer *viewer, const QString &source,
                                const QString &message);

void projectAIOriginalFormation(AIWorldView&,Room&,const ServerPlayer*);

// Finite Original HMiewu Play hypotheses only. Does not authorize payment.
int aiFormationOriginalPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiMiewuOriginalConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
void projectAIFormationOriginalContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,const std::function<QJsonObject(const Card *)> &);

int aiTransformationOriginalPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiTransformationOriginalConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
void projectAITransformationOriginalContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,const std::function<QJsonObject(const Card *)> &);
void projectAIGeneralDefinitionsOriginalWorld(AIWorldView &,Room &,ServerPlayer *);

int aiLordExOriginalPrototype(const AIRequest &, const ServerPlayer *, const QString &skill, const QString &prototype);
bool aiLordExOriginalConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &spec);
void projectAILordExOriginalContext(AIRequest &, Room &, ServerPlayer *, const QVariant &, const std::function<QJsonObject(const Card *)> &publicCard);
void projectAILordExOriginalWorld(AIWorldView &, Room &, ServerPlayer *);

int aiBasaraOriginalPrototype(const AIRequest &, const ServerPlayer *, const QString &skill, const QString &prototype);
void projectAIBasaraOriginalWorld(AIWorldView &, Room &, ServerPlayer *);
void recordAIViewCardsOriginalEvent(const AIWorldView &, Room &, ServerPlayer *viewer, ServerPlayer *eventPlayer, const AIEventView &, const QVariant &);
void clearAIViewCardsOriginalViews(Room &);

// Finite source constructor/global existence, never conversion authority.
void projectAILegacyPrototypeExtraWorld(AIWorldView &, Room &, ServerPlayer *);

// Caller supplies only its authorized current JudgeStruct. Readonly printed
// candidates are limited to faces already disclosed in this request.
void projectAIOriginalRetrialCandidates(const AIRequest &, Room &, ServerPlayer *,
    const JudgeStruct *, QJsonObject &,
    const std::function<QJsonObject(const Card *)> &publicCard);

void projectAIStrategyKnownBothOriginalWorld(AIWorldView &, Room &, ServerPlayer *);
void projectAIStrategyKnownBothOriginalRequest(AIRequest &, Room &, ServerPlayer *, const QVariant &, QJsonObject &);
void projectAIStrategyKnownBothOriginalEvent(AIWorldView &, Room &, ServerPlayer *, const AIEventView &);

// Public engine definitions and viewer-visible hand/Ox counts only. No action
// receipt, draw-pile ordering, foreign hidden face, or foreign observer rights.
void projectAIOriginalCardCounts(AIWorldView &, Room &, const ServerPlayer *);

void projectAITrustCardDefault(AIRequest &request, Room &room, ServerPlayer *viewer);

// This result is final for the finite source-wire path, including a native
// rejection after the source tag was committed. Never run legacy fallback then.
struct AIOriginalResponseWireOutcome {
    bool applicable = false;
    bool committed = false;
    bool accepted = false;
    const Card *card = nullptr;
    QString error;
};
bool aiOriginalResponseWireSyntax(const QString &wire, int *physicalId = nullptr);
bool aiOriginalResponseWireRequest(const AIRequest &request);
AIOriginalResponseWireOutcome commitAIOriginalResponseWire(
    Room &room, ServerPlayer *viewer, const AIRequest &request, const AIResult &result,
    const std::function<bool()> &commitPriorStrategyEffects);

// Native call site only: invoke inside decideNullification after the actual
// trick/from/to context is assembled. No Lua card/tag query is exposed.
void projectAIAmazingGrace(AIRequest &request, Room &room,
                                 ServerPlayer *viewer, const Card *trick,
                                 ServerPlayer *from, ServerPlayer *to);

#endif
