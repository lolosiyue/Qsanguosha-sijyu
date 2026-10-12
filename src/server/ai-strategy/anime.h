#ifndef AI_STRATEGY_ANIME_H
#define AI_STRATEGY_ANIME_H

#include <memory>
#include <QtGlobal>
#include <QString>
#include <QJsonObject>
#include <functional>
#include <QSet>

struct ActiveSkillRequest;
struct AICardConversionView;
struct AIEventView;
class AIFateInquiryState;
class AIFateUseProof;
struct AIRequest;
struct AIWorldView;
class Card;
struct CardUseStruct;
struct lua_State;
class LuaTriggerSkillV2;
class QJsonObject;
class QString;
class QVariant;
class Room;
class ServerPlayer;
struct SkillContext;
struct SkillInstanceRef;
class TriggerSkillV2;
class ViewAsSkillV2;

// Native-only source callback frame. Construct at actual LuaAI::filterEvent entry.
class AIFocusFrame {
public:
    AIFocusFrame(Room &,ServerPlayer *,int,const QVariant &);
    ~AIFocusFrame();
    AIFocusFrame(const AIFocusFrame &)=delete;
    AIFocusFrame &operator=(const AIFocusFrame &)=delete;
private:
    struct Data; std::unique_ptr<Data> d;
    friend struct AIFocusAccess;
};
// Once after normal game SmartAI autoload, before any native filterEvent call.
void installAIFocusCallbacks(Room &);
// Native-only metadata; does not read marks or invoke callbacks.
bool hasAIFocusCallbacks(Room &);
// Native integration evidence, never exported to either policy VM.
quint64 aiFocusExecutionSerial(Room &);
// Metadata-only finite leaf proof for a genuine current filterEvent frame.
// Requires exact installed C closures/Room upvalues and a non-Focus event.
// It does not certify the selected callback, arbitrary C functions, or effects.
bool aiFocusContinuityNoop(Room &,lua_State *,int argumentCount);
// Metadata-only preallocation proof of the two fixed C closures and Room upvalue.
bool aiFocusBindings(Room&,lua_State*) noexcept;

// Finite public policy counters; no generic mark/tag or legacy-AI lookup.
void projectAIImagineWorld(AIWorldView &, Room &, ServerPlayer *viewer);
// Invoke only from the real native event dispatch, after building that viewer's
// world. The validator matches this witness before accepting a counter delta.
void projectAIImagineEvent(AIWorldView &, Room &, ServerPlayer *viewer,
                                 const AIEventView &);

// Diagnostic-only finite Imagine source sinks. Never reads gameplay facts.
bool emitAIImagineDiagnostic(Room &, const AIRequest &, ServerPlayer *,
                                    const QString &site, const QString &message);

// Stack-only exact producer context. Every nested V2 callback shadows its parent.
class AIGoddessScope final {
public:
 AIGoddessScope(Room&,const LuaTriggerSkillV2*,ServerPlayer*,int,const SkillContext&,bool cost);
 ~AIGoddessScope();
 AIGoddessScope(const AIGoddessScope&)=delete;
 AIGoddessScope&operator=(const AIGoddessScope&)=delete;
private:struct Data;std::unique_ptr<Data>d;
};
void projectAIGoddessContext(AIRequest&,Room&,ServerPlayer*,const QVariant&,QJsonObject&,
 const std::function<QJsonObject(const Card*)>&);

// Finite source-timed reference/parse observations. No ticket or hidden lookup.
QJsonObject queryAIGodYouhua(const AIRequest&,Room&,ServerPlayer*,const QJsonObject&);
bool aiGodYouhuaConstruction(const AIRequest&,Room&,ServerPlayer*,const QJsonObject&);

bool registerAIYuriSource(Room&,lua_State*);
// Every selected native V2 entry shadows the outer entry, including unrelated skills.
// This lexical capability holds no cross-event saved history and grants no payment.
class AIYuriScope {
public:
 AIYuriScope(Room &,const TriggerSkillV2 *,SkillContext *,int originalEvent);
 ~AIYuriScope();
 AIYuriScope(const AIYuriScope&)=delete;
 AIYuriScope&operator=(const AIYuriScope&)=delete;
 struct State;
 const State *state()const{return state_;}
private:
 State *state_=nullptr;AIYuriScope *previous_=nullptr;
};
void projectAIYuriQuestion(AIRequest &,Room &,ServerPlayer *,const std::function<QJsonObject(const Card *)>&);
void projectAIYuriEvent(AIWorldView &,Room &,ServerPlayer *viewer,ServerPlayer *actor,const AIEventView &,const QVariant &,const std::function<QJsonObject(const Card *)>&);

// Every V2 cost/effect, including unrecognised callbacks, shadows the outer scope.
class AIHTMSFrame final {
public:
 AIHTMSFrame(Room&,const LuaTriggerSkillV2*,ServerPlayer*,int,const SkillContext&,bool);
 ~AIHTMSFrame();
 AIHTMSFrame(const AIHTMSFrame&)=delete;
private:struct Data;std::unique_ptr<Data>d;
};
bool registerAIHTMSSource(Room&,lua_State*);
void clearAIHTMSWitnesses(Room&);
void projectAIHTMSEvents(AIRequest&,Room&,ServerPlayer*,const std::function<QJsonObject(const Card*)>&);
void projectAIHTMSIntention(AIWorldView&,Room&,ServerPlayer*,const AIEventView&,const std::function<QJsonObject(const Card*)>&);
// Exact native resolveDamage writes, with all other tag writes/removals revoking
// the witness. This is a synchronous current-damage stack, not event history.
class AIHTMSDamageScope final {
public:explicit AIHTMSDamageScope(Room&);~AIHTMSDamageScope();
private:struct Data;std::unique_ptr<Data>d;
};
class AIHTMSDamageTagWrite final {
public:AIHTMSDamageTagWrite(Room&,const QVariant&,int writeKind);~AIHTMSDamageTagWrite();
private:struct Data;std::unique_ptr<Data>d;
};
void observeAIHTMSDamageTag(Room&,const QString&,const QVariant*value);

void projectAIHTMSProperties(AIRequest &,Room &,ServerPlayer *);
bool aiHTMSConstruction(const AIRequest &,Room &,const ServerPlayer *,const QJsonObject &);

// Fixed htms-ai.lua6346: same disclosed physical policy identity, no receipt.
std::unique_ptr<Card> aiZhongerhxEvaluation(const AIRequest &,Room &,ServerPlayer *,int,
    QJsonObject *disclosedFace,QString *error);

// Five source hypotheses only: no payment, playable conversion or new disclosure.
bool aiScarletYuriConstruction(const AIRequest &,Room &,const ServerPlayer *,const QJsonObject &);
bool aiGenjutsuPileMaterial(const AIRequest &,Room &,const ServerPlayer *,const QJsonObject &,int);

bool aiScarletTenyearZeroConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

void projectAIZhuangpoPlanning(AIRequest&,Room&,const ServerPlayer*);

void projectAIXiaoyanPlanning(AIRequest&,Room&,const ServerPlayer*);

bool aiRenchouEvaluation(const AIRequest&,const ServerPlayer*,const QJsonObject&);
int aiRenchouPrototype(const AIRequest&,const ServerPlayer*,const QString&,const QString&);

bool aiHfgushiConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

bool aiGfengyingJxyangjieEvaluation(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
bool aiGfengyingJxyangjieRecipe(const AIRequest&,Room&,const ServerPlayer*,const QString&,const Card*);
int aiGfengyingPrototype(const AIRequest&,const ServerPlayer*,const QString&,const QString&);
bool registerAISgs10thSources(Room&,lua_State*,QString*reason=nullptr);
void projectAIYuxinChenfusuiPlanning(AIRequest&,Room&,const ServerPlayer*);
class AIYuxinScope final{
public:AIYuxinScope(Room&,const LuaTriggerSkillV2*,ServerPlayer*,int,const SkillContext&,bool);~AIYuxinScope();
 AIYuxinScope(const AIYuxinScope&)=delete;AIYuxinScope&operator=(const AIYuxinScope&)=delete;
private:struct Data;std::unique_ptr<Data>d;
};

void projectAIYijiaPlanning(AIRequest&,Room&,const ServerPlayer*);

void projectAIQhstandardChoiceProperties(AIRequest &, Room &, ServerPlayer *);

// Fixed qhstandard qhfireqinyin PlayerChosen reads this answering player's
// current public property. This is a snapshot fact, not retained history.
void projectAIQhfireQinyin(AIRequest &, Room &, ServerPlayer *);

void projectAIQhwindGuhuo(AIRequest&,Room&,ServerPlayer*);
bool aiQhwindGuhuoConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

bool aiQhfireLianhuanConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

bool aiQhTurnConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

bool aiQhstandardQixiConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

bool aiQhfireShuangxiongConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

void projectAIQhfireXinhuo(AIRequest &,Room &,ServerPlayer *);

// Compiled real askForUseCardStruct entry only. Nested questions mask the outer frame.
class AIFateInquiry final {
public:
    AIFateInquiry(Room &, ServerPlayer *, const QString &pattern);
    ~AIFateInquiry();
    AIFateInquiry(const AIFateInquiry &)=delete;
    AIFateInquiry &operator=(const AIFateInquiry &)=delete;
private:
    friend class AIFateUseAccess;
    std::shared_ptr<AIFateInquiryState> m_state, m_previous;
    static thread_local std::shared_ptr<AIFateInquiryState> s_current;
};
class AIFateUseAccess final {
public:
    static bool attach(const AIRequest &, Room &, ServerPlayer *, const AICardConversionView &, CardUseStruct &);
    static bool bind(Room &, const CardUseStruct &, ActiveSkillRequest &);
    static bool extend(const Room &, const ServerPlayer *, const ViewAsSkillV2 *, const ActiveSkillRequest &, QSet<int> &);
    static bool finish(const Room &, const ActiveSkillRequest &, const Card *);
private:
    static bool current(const Room &, const std::shared_ptr<const AIFateUseProof> &);
};

void projectAIChendancuiPlanning(AIRequest&,Room&,const ServerPlayer*);

void projectAIKesxchunlaoPlanning(AIRequest&,Room&,const ServerPlayer*);

void projectAILuncePlanning(AIRequest&,Room&,const ServerPlayer*);

void projectAIBackpackShizongPlanning(AIRequest&,Room&,const ServerPlayer*);

void projectAIQiangwuPlanning(AIRequest&,Room&,const ServerPlayer*);

void projectAIYikuxinChenlanjiaoPlanning(AIRequest&,Room&,const ServerPlayer*);

void projectAIYitianChoices(AIRequest &,Room &,ServerPlayer *);

// Fixed RushB's eight reached Card::setSkillName statements. Returns the same
// native physical object after the write, never a clone or payment capability.
const Card *writeAIRushBPhysicalSkill(Room &,const AIRequest &,ServerPlayer *,const QJsonObject &,QString *);
bool authorizeAIDanlvePhysical(Room&,const AIRequest&,ServerPlayer*,int);
const Card* attachAIDanlvePhysical(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&,const QString&,const QString&,int,QString*);

int aiSkinPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiSkinProduct(const AIRequest &,const ServerPlayer *,const SkillInstanceRef &,
                         const SkillInstanceRef &,const Card *,const Card *);

bool aiDmpkancollePlanningConstruction(const AIRequest&,const ServerPlayer*,const QJsonObject&,QString*error=nullptr);
void projectAIDmpkancollePlanningContext(AIRequest&,Room&,const ServerPlayer*,const QVariant&,const std::function<QJsonObject(const Card*)>&);
void captureAIHayateEnemyMetadata(Room&);
void projectAIHayateEnemyMetadata(AIWorldView&,Room&);

// Record only the target accepted by the actual reijyuu PlayerChosen inquiry.
void witnessAIStrategyReijyuuChoice(Room &room, const ServerPlayer *viewer,
                                  const ServerPlayer *target);
// Project only that viewer's witnessed target for the following Choice prompt.
// Missing/stale witness stays absent (unknown), never known-empty.
void projectAIStrategyReijyuuChoice(Room &room, const ServerPlayer *viewer,
                                  AIRequest &request);
void projectAIStrategyAnimecardCatalog(AIWorldView &world, Room &room,
                                      const ServerPlayer *viewer);

#endif
