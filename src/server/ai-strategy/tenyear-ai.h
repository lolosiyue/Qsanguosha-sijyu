#ifndef AI_STRATEGY_TENYEAR_AI_H
#define AI_STRATEGY_TENYEAR_AI_H

#include "structs.h"
#include "ai-strategy/owner.h"
#include <QJsonObject>
#include <QVariant>
#include <functional>
#include <QString>
#include <QStringList>
#include <QList>
#include <QtGlobal>
#include <memory>

struct AICardConversionView;
struct AICardSpec;
struct AIEventView;
struct AIRequest;
class Card;
struct CardUseStruct;
struct lua_State;
class LuaRuntime;
class QVariant;
class Room;
class ServerPlayer;
struct SkillInstanceRef;

void projectAIZunweiBools(AIRequest&,Room&,ServerPlayer*);

void projectAIQuxiShawuFields(AIRequest&,Room&,ServerPlayer*);

// Fixed tenyear_hc source hypotheses, including the reached mutation prefixes.
// No native provider selection, payment receipt, hidden card or new rule grant.
bool aiTenyearHcConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
void projectAICuixinContext(AIRequest&,const QVariant&);

// Exact three source sgs fields; write-only, no saved value or reader grant.
QJsonObject writeAITenyearHCGlobal(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);

void projectAIGongjian(AIRequest&,Room&,ServerPlayer*);

void projectAIRecordedOwnerNames(AIRequest&,Room&,ServerPlayer*);
bool aiJilieConstruction(const AIRequest&,Room&,ServerPlayer*,const QJsonObject&);

bool aiRecordedNameConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

void projectAINotifiedPileContexts(AIRequest&,Room&,ServerPlayer*);

// Exact compiled Sushou question callsite. No Lua exposure; the lexical scope
// authenticates recipient/current foreign owner, order and ambient game depth.
const Card* askAISushou(Room&,ServerPlayer*,ServerPlayer*,const QList<int>&,const QString&);
QJsonObject issueAISushouNotifiedCosts(const AIRequest&,ServerPlayer*,const QString&,const std::function<bool(CardUseStruct&)>&);
bool hasAISushouNotifiedCosts(const AIRequest&,int);
bool applyAISushouNotifiedCosts(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,const std::function<bool(CardUseStruct&)>&,CardUseStruct&);

QString aiNotifiedPileSkill(const AIRequest&);
void projectAINotifiedPile(AIRequest&,Room&,ServerPlayer*);
bool authorizeAINotifiedPileEvaluation(const AIRequest&,Room&,ServerPlayer*,int,QJsonObject* = nullptr);
bool authorizeAINotifiedPilePhysical(const AIRequest&,Room&,ServerPlayer*,const SkillInstanceRef&,const SkillInstanceRef&,int);

// Current owner Play inquiry only. No retained history, payment, or hidden faces.
void projectAIJijiao(AIRequest &, Room &, ServerPlayer *);
// Zero-cost, no-declaration fixed V1 prototype. Final native validation remains required.
int aiJijiaoPrototype(const AIRequest &, const ServerPlayer *, const QString &, const QString &);

// Finite current-owner tag read only. No persistent tag/recipient/card authority.
void projectAILiehou(AIRequest &, Room &, ServerPlayer *);

bool aiDiouConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAIDiouSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAIDiouSlots(Room&);
bool registerAIDiouSources(Room&,lua_State*,QString*error=nullptr);

QJsonObject issueAIDiouReceipt(const AIRequest&,ServerPlayer*,const QJsonObject&,const std::function<bool(CardUseStruct&)>&);
bool hasAIDiouReceipt(const AIRequest&,int);
bool applyAIDiouReceipt(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,const std::function<bool(CardUseStruct&)>&,CardUseStruct&);

bool aiGengduConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAIGengduSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAIGengduSlots(Room&);
bool registerAIGengduSources(Room&,lua_State*,QString*error=nullptr);

bool aiZhuningConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAIZhuningSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAIZhuningSlots(Room&);
bool registerAIZhuningSources(Room&,lua_State*,QString*error=nullptr);

void projectAILianzhan(AIRequest &,Room &,ServerPlayer *,const QVariant &);
QJsonObject queryAILianzhanSlot(const AIRequest &,ServerPlayer *,const QString &,const QJsonObject &,const std::function<QJsonObject(const Card *)>&);
bool registerAILianzhanSources(Room &,lua_State *,QString *reason=nullptr);
void clearAILianzhanSlots(Room &);
// Native-only exact source QVariant::toCardUse value bridge; no SWIG export.
bool pushAILianzhanUse(lua_State *,const CardUseStruct &);
bool readAILianzhanUse(lua_State *,int,const Card *&,ServerPlayer *&,QList<ServerPlayer *> &);
void clearAILianzhanBindings(LuaRuntime &);
bool installAILianzhanBinding(lua_State *);

// Metadata-only exact pending installer entry. Caller must independently prove
// the complete original bootstrap source/allocation union and lexical phase.
// This cannot authorize a standalone/later protected call to the same pointer.
bool matchAILianzhanBootstrapEntry(Room&,lua_State*,int arguments) noexcept;
// Exact accepted installer while actually executing at protected depth + 1.
bool inAILianzhanBootstrapInstaller(Room&,lua_State*) noexcept;

void projectAIMingfa(AIRequest&,const QVariant&);
bool aiMingfaConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAIMingfaSlot(const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&,AIReadonlyCardLease lease={});
bool registerAIMingfaSources(Room&,lua_State*,QString*error=nullptr);
void clearAIMingfaSlots(Room&);

// event must be the runtime's native-only currently delivered event, never Lua data.
QJsonObject queryAIPeiqiRecord(Room&, const AIRequest&, ServerPlayer*,
 const AIEventView* event,const QString& op,const QString& field,const QJsonObject&);

bool aiTychendeConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAITychendeSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAITychendeSlots(Room&);
bool registerAITychendeSources(Room&,lua_State*,QString*error=nullptr);

bool aiTyfenwuConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAITyfenwuSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAITyfenwuSlots(Room&);
bool registerAITyfenwuSources(Room&,lua_State*,QString*error=nullptr);

bool aiTyfenwuRawProduct(const AIRequest&,ServerPlayer*,const QString&,const Card*);

bool aiTyqingkouConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
QJsonObject queryAITyqingkouSlot(const AIRequest&,ServerPlayer*,const QString&operation,const QJsonObject&,AIReadonlyCardLease lease={});
void clearAITyqingkouSlots(Room&);
bool registerAITyqingkouSources(Room&,lua_State*,QString*error=nullptr);

// Fixed ol_qifu1111-1124. This is a policy-class admission only; the existing
// V2 issuer and final reconstruction remain the sole payment authority.
int aiJiqiaoPrototype(const AIRequest &,const ServerPlayer *,
                             const QString &skill,const QString &prototype);

// Fixed ol_qifu Cihuai/Chixin source hypotheses; never issues payment authority.
bool aiOlQifuResponseConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);
bool aiTenyearXdResponseConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);

// Every actual use-card question masks an outer question. Only the native
// ThWeijing EventPhaseChanging frame can authorize this finite recipient action.
class AIZhifengQuestion {
public:
    AIZhifengQuestion(Room &, ServerPlayer *, const QString &, const QString &, int method);
    ~AIZhifengQuestion();
    AIZhifengQuestion(const AIZhifengQuestion &) = delete;
    AIZhifengQuestion &operator=(const AIZhifengQuestion &) = delete;
private:
    struct Data;
    std::unique_ptr<Data> d;
    friend struct AIZhifengAccess;
};
void projectAIZhifengQuestion(AIRequest &, Room &, ServerPlayer *);
Card *createAIZhifengProduct(const AIRequest &, Room &, ServerPlayer *, quint64 serial,
                                   const QList<int> &costs, QString *error = nullptr);
Card *rebuildAIZhifengProduct(const AIRequest &, Room &, ServerPlayer *, const AICardConversionView &);
// Exact Godlongnu source hypotheses, not provider admission or receipts.
bool aiGodlongnuConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);

// Only nyarz_huanshi Discard/AG and kanyu Invoke/AG. No receipt/card custody.
void projectAIStrategyHuanshiKanyuResponse(AIRequest &, Room &, ServerPlayer *,
                                     const QVariant &, QJsonObject &context);
void clearAIStrategyKanyuResponse(Room &);

#endif
