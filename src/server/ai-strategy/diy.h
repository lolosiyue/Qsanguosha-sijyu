#ifndef AI_STRATEGY_DIY_H
#define AI_STRATEGY_DIY_H

#include "ai.h"
#include <QJsonObject>
#include <QString>
#include <functional>
#include <QVariant>
#include <QList>
#include <QtGlobal>
#include <memory>

struct AICardConversionView;
struct AIEventView;
struct AIRequest;
struct AIResult;
struct AIWorldView;
class AIZhaobingPhysicalUseProof;
class Card;
struct CardUseStruct;
struct JudgeStruct;
class QJsonObject;
class QVariant;
class Room;
class ServerPlayer;
struct SkillContext;
class TriggerSkillV2;

int aiMeowLiuliChoicePrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
void projectAIMeowLiuliChoiceContext(AIRequest &,Room &,ServerPlayer *,QJsonObject &,
    const std::function<QJsonObject(const Card *)> &);

// Prototype catalogue admission only. Real scope, ordered materials and final
// provider feasibility remain checked by the existing sealed native binder.
int aiMeowGuoseChoicePrototype(const AIRequest &, const ServerPlayer *,
                             const QString &skill, const QString &prototype);
bool aiMeowGuoseChoiceConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);
void projectAIMeowLierenChoiceContext(AIRequest &, Room &, ServerPlayer *, const QVariant &,
    QJsonObject &, const std::function<QJsonObject(const Card *)> &);

// Fixed-source constructor vocabulary only. -1: absent; bit0: ordered payment
// materials; bit1: declared userString. Neither this mask nor a readonly clone
// grants action authority: current instance, selection and factory stay native.
int aiMeowTianxiangChoicePrototype(const AIRequest &request, const ServerPlayer *viewer,
                             const QString &skill, const QString &prototype);
// Source sgs.Analeptic_IsAvailable(player) is the static predicate with a null
// card, not Analeptic::isAvailable (which additionally checks BasicCard rules).
// Caller supplies the active Room/Engine context and a live owner viewer.
bool aiSourceAnalepticAvailable(const ServerPlayer *viewer);
void projectAIMeowTianxiangChoiceContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    QJsonObject &context, const std::function<QJsonObject(const Card *)> &publicCard);

int aiOlddiyKeguibaoChoicePrototype(const AIRequest &,const ServerPlayer *,const QString &skill,const QString &prototype);
bool aiOlddiyKeguibaoChoiceConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &spec);
void projectAIOlddiyKeguibaoChoiceContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &context,
                                  const std::function<QJsonObject(const Card *)> &publicCard);

int aiSRkillChoicePrototype(const AIRequest &,const ServerPlayer *,const QString &skill,const QString &prototype);
bool aiSRkillChoiceConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &spec);
void projectAISRkillChoiceContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &context,
 const std::function<QJsonObject(const Card *)> &publicCard);
bool stageAISRkillChoiceFlags(Room &,const AIRequest &,const AIResult &,bool commit,QString *error);
// Invoke only after the actual public flag broadcast.
void observeAISRkillChoiceFlag(Room &,ServerPlayer *,const QString &flag);

// Only the exact sr_yinmeng CardShow question. Requires a face already lawfully
// present in the viewer's snapshot; the source tag alone grants no visibility.
void projectAIStrategyYinmengResponse(AIRequest &, Room &, ServerPlayer *, QJsonObject &context);

// Audited readonly source constructors; never grants payment or a receipt.
bool aiLoongResponseConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);
// Zhangwu's native non-recast branches select no cards and generate materials.
// The second function requires exact original order, not merely a set match.
bool aiZhangwuGeneratedBranch(const AIRequest &,const ServerPlayer *,const QString &skill);
bool aiZhangwuGeneratedPayment(const AIRequest &,const ServerPlayer *,const QString &skill,const QList<int> &costs);

// True isolated-runtime bootstrap only: immutable absence metadata, never a
// mutable gameplay choice or another viewer's strategy memory.
void captureAIStrategyXuheResponse(Room &);
void clearAIStrategyLoongXuheResponse(Room &);
void projectAIStrategyXuheResponse(AIRequest &, Room &, ServerPlayer *);
void projectAIStrategyFeilongResponseEvent(AIWorldView &, Room &, ServerPlayer *, const AIEventView &);

// Call only after an actual all-player flags broadcast. Finite audited literal.
void recordAIStrategyFeilongPublicFlag(Room &, ServerPlayer *, const QString &operation);
QJsonObject readAIStrategyFeilongPublicFlag(Room &, ServerPlayer *viewer, ServerPlayer *target, const QString &flag);

// The source key must come from the runtime-owned readonly recipe store.
bool aiFeiDIYSource(const AIRequest &, const ServerPlayer *, const Card *,
    const QString &sourceKey, QString *error = nullptr);
// Exact constructor tuple and request admission; no payment rights.
int aiFeiDIYExtraTarget(const AIRequest &, const ServerPlayer *, const QJsonObject &spec);
QJsonObject aiFeiDIYTargetFilter(Room &, ServerPlayer *, const AIRequest &,
    const Card *, const QString &sourceKey, const QJsonObject &query);
// Only the two previously absent response prototypes. Existing seven Play
// prototypes remain in the audited construction catalogue.
int aiFeiDIYPrototype(const AIRequest &, const ServerPlayer *, const QString &skill,
    const QString &prototype);
void projectAIFeiDIYContext(AIRequest &, Room &, const ServerPlayer *,
    const std::function<QJsonObject(const Card *)> &publicCard);

// Native dispatch lifetime only. No copied Room tag may create this scope.
class AIFCUseFrame {
public:
 AIFCUseFrame(Room &, int event, ServerPlayer *eventTarget, const QVariant &);
 ~AIFCUseFrame();
 AIFCUseFrame(const AIFCUseFrame &) = delete;
 AIFCUseFrame &operator=(const AIFCUseFrame &) = delete;
private:
 Room *m_room = nullptr;
 quint64 m_serial = 0;
};
void clearAIFCUseFrames(Room &);
void bindAIFCUseFrame(AIRequest &, Room &, const ServerPlayer *);
// 0: old unscoped request; +1: exact current owner frame; -1: scoped rejection.
int aiFCUseFrameWitness(const AIRequest &, Room &, const ServerPlayer *, const CardUseStruct &);

// All profiles borrow the actual current-question card. No cloned identity or payment.
using AIFcDIYPlanningProfile = std::function<QJsonObject(const Card *, int, const QString &)>;
void projectAIFcDIYPlanningContext(AIRequest &, Room &, const ServerPlayer *, const QVariant &,
    QJsonObject &, const std::function<QJsonObject(const Card *)> &, const AIFcDIYPlanningProfile & = {});
bool aiMoutaozeiPlanningConstruction(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &);
const Card *aiFcDIYPlanningEvaluation(const AIRequest &, Room &, const ServerPlayer *, const QString &);
int aiFcDIYPlanningExtraTarget(const AIRequest &, Room &, const ServerPlayer *, const QString &);
QJsonObject aiFcDIYPlanningTargetFilter(Room &, ServerPlayer *, const AIRequest &, const Card *,
    const QString &, const QJsonObject &);

// Readonly fixed fcDIY4788 constructor only. No activation/material authority.
bool aiTyshouliConstruction(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &);
// Same source's later #tyshouli:onePublicHorse:slash parse. Actual issued scope
// identity and enabled provider are validated by the existing seed endpoint.
bool aiTyshouliSeed(const AIRequest &, Room &, const ServerPlayer *,
    const QString &skill, const QString &prototype, const QJsonObject &proposal);

// Fixed FC DIY constructor hypotheses. Never called by payment admission.
bool aiFcDIYConstruction(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &);
// Additional lawful own ShenJian materials, after full constructor validation.
bool aiFcDIYPileMaterial(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &, int);

// Fixed fcDIY15975-16010: only this owner's invocation or this recipient's
// currently offered Huanshi cards. Results classify; they never authorize cost.
void projectAIFcHuanshiPlanningContext(AIRequest &, Room &, const ServerPlayer *,
    const QVariant &, QJsonObject &, const std::function<QJsonObject(const Card *)> &,
    const std::function<QJsonObject(const Card *)> &judgeProfile = {});

// Observer only. Every real use-card question (including nested other questions)
// pushes a boundary; only the finite native Jieyingh trigger can authorize it.
class AIJieyinghQuestion {
public:
    AIJieyinghQuestion(Room &, ServerPlayer *, const QString &pattern,
                             const QString &prompt, int method);
    ~AIJieyinghQuestion();
    AIJieyinghQuestion(const AIJieyinghQuestion &) = delete;
    AIJieyinghQuestion &operator=(const AIJieyinghQuestion &) = delete;
private:
    struct Data;
    std::unique_ptr<Data> d;
    friend struct AIJieyinghAccess;
};
void projectAIJieyinghQuestion(AIRequest &, Room &, ServerPlayer *);
// Fresh exact native V1 product. Caller owns the returned virtual card.
Card *createAIJieyinghProduct(const AIRequest &, Room &, ServerPlayer *,
                                    quint64 serial, QString *error = nullptr);
Card *rebuildAIJieyinghProduct(const AIRequest &, Room &, ServerPlayer *,
                                     const AICardConversionView &);

void projectAIMountainrumourPlanningContext(AIRequest &,Room &,const ServerPlayer *,const QVariant &,QJsonObject &,
 const std::function<QJsonObject(const Card *)> &);
// Actual public-use event witness; never a Lua-provided observation.
void clearAIPlanningPublicUses(Room &);
void recordAIPlanningPublicUse(Room &,int triggerEvent,const QVariant &);
// A current native use, including its actor and ordered targets, must match a
// live public event. A disclosed card face alone is not proof of a CardUse.
bool aiPlanningPublicUseWitness(const AIRequest &,Room &,const ServerPlayer *,const CardUseStruct &);
// An observer can read the card face only after the public use stage; this
// grants neither a foreign actor's private rules nor payment authority.
bool aiPlanningPublicCardWitness(const AIRequest &,Room &,const ServerPlayer *,const Card *);
// Borrowed actual publicly disclosed card; never cloned, owned, or payment authority.
const Card *aiCaokongPlanningEvaluation(const AIRequest &,Room &,const ServerPlayer *,const QString &sourceKey);

// Only a real askForCard(isRetrial=true) may issue this finite physical answer.
// Every nested askForCard pushes a mask; other decision kinds fail the request gate.
class AIFengyiQuestion final {
public:
 AIFengyiQuestion(Room &,ServerPlayer *,const QString &,const QString &,const QVariant &,int,bool);
 ~AIFengyiQuestion();
 AIFengyiQuestion(const AIFengyiQuestion&)=delete;
 AIFengyiQuestion&operator=(const AIFengyiQuestion&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
 friend struct AIFengyiAccess;
};
QJsonObject selectAIFengyiPhysical(Room &,const AIRequest &,ServerPlayer *,int);
const Card *consumeAIFengyiPhysical(Room &,const AIRequest &,ServerPlayer *,int);

int aiSijyuXuanmengPrototype(const AIRequest &, const ServerPlayer *, const QString &, const QString &);

// AI-only, fixed-source sfofl_zhaobing Play/own-pile Equip product. This does
// not add a name to the global native/human physical-product classifier.
bool aiZhaobingPhysicalScope(const AIRequest &, const QString &skill);
bool authorizeAIZhaobingPhysicalProduct(const AIRequest &, Room &,
    ServerPlayer *, const SkillInstanceRef &activation, const SkillInstanceRef &source,
    const QList<int> &selected, const Card *produced, QString *error = nullptr);

// Opaque native-only continuation; declaration permits an incomplete private
// shared_ptr member in CardUseStruct, with no SWIG or serialization access.
class AIZhaobingPhysicalUseAccess {
public:
    static bool attach(const AIRequest &, Room &, ServerPlayer *,
        const AICardConversionView &, CardUseStruct &);
    // -1 means no AI proof: native human/legacy behavior stays untouched.
    // 0 is an invalid/stale AI continuation; 1 is exact live acceptance.
    static int validate(Room &, CardUseStruct &);
};

// Finite actual sfofl_haiyou question only: the viewer's own source Jink
// requirement, or the ordinary source error at its reached missing slot.
void projectAIStrategyHaiyouResponse(AIRequest &, Room &, ServerPlayer *,
                                    const QVariant &, QJsonObject &context);

void projectAIBozhi(AIRequest&,Room&,ServerPlayer*);
bool aiBozhiConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

int aiMtjieliPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiMtjieliDeclaration(const AIRequest &,const ServerPlayer *,const QJsonObject &);
bool aiMtjieliConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);

// Exact current-owner toInt at the named PlayerChosen inquiry. No history cache.
void projectAIQingsi(AIRequest &, Room &, ServerPlayer *);

void projectAIMaotuYinhu(AIRequest&,Room&,ServerPlayer*);
bool aiMaotuYinhuConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
bool aiMaotuYinhuPublicMaterial(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

// Native call-site capability, scoped around the admitted V2 cost/effect only.
// No gameplay mutations and no stored Room/tag/foreign-card lookup interface.
class AIKeqiwendaoScope {
public:
    AIKeqiwendaoScope(Room &, const TriggerSkillV2 *, SkillContext *);
    ~AIKeqiwendaoScope();
    AIKeqiwendaoScope(const AIKeqiwendaoScope &) = delete;
    AIKeqiwendaoScope &operator=(const AIKeqiwendaoScope &) = delete;
private:
    friend void projectAIKeqiwendao(AIRequest &, Room &, ServerPlayer *);
    AIKeqiwendaoScope *previous_;
    Room *room_ = nullptr;
    SkillContext *context_ = nullptr;
    JudgeStruct *judge_ = nullptr;
};
void projectAIKeqiwendao(AIRequest &, Room &, ServerPlayer *);

// Call only inside actual LuaAI::filterEvent after its callback==0 guard,
// before invoking the fixed CloneAI thunk. TrustAI/direct recordEvent is not a writer.
void observeAITargetHistoryFilterEvent(Room &, int event, ServerPlayer *, const QVariant &);
// Full game-Lua initialization/reload, not isolated AI VM recreation.
void clearAITargetHistory(Room &);
void projectAITargetHistory(AIRequest &, Room &, ServerPlayer *);
void projectAIAzirshenjiEvent(AIWorldView &, Room &, ServerPlayer *viewer,
    ServerPlayer *eventPlayer, const AIEventView &, const QVariant &);

// Fixed olddiy-ai.lua819: same disclosed physical policy identity, no receipt.
std::unique_ptr<Card> aiBuStwoGuicaiEvaluation(const AIRequest &,Room &,ServerPlayer *,int,
    QJsonObject *disclosedFace,QString *error);

#endif
