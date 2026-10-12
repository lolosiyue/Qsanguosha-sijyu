#ifndef AI_STRATEGY_ROUND53_XUFA_RESPONSE_H
#define AI_STRATEGY_ROUND53_XUFA_RESPONSE_H
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
#include <QStringList>
#include <QList>
#include <functional>
class Room;class ServerPlayer;class Card;struct AIRequest;struct AICardSpec;struct CardUseStruct;
// Constructed ONLY at the exact compiled XufaCard::use askForUseCard callsite.
class AIRound53XufaQuestionScope final {
public:
 AIRound53XufaQuestionScope(Room&,ServerPlayer*,const Card*,const QString&);
 ~AIRound53XufaQuestionScope();
 AIRound53XufaQuestionScope(const AIRound53XufaQuestionScope&)=delete;
 AIRound53XufaQuestionScope& operator=(const AIRound53XufaQuestionScope&)=delete;
private:struct State;std::unique_ptr<State>d;
};
// Installed at EVERY native askForUseCardStruct entry, including unrelated/nested calls.
class AIRound53XufaInquiryScope final {
public:
 AIRound53XufaInquiryScope(Room&,ServerPlayer*,const QString&,const QString&,int);
 ~AIRound53XufaInquiryScope();
 AIRound53XufaInquiryScope(const AIRound53XufaInquiryScope&)=delete;
 AIRound53XufaInquiryScope& operator=(const AIRound53XufaInquiryScope&)=delete;
private:struct State;std::unique_ptr<State>d;
};
// Called only after native makeRequest has completed its actual world projection.
void bindAIRound53XufaQuestion(const AIRequest&,ServerPlayer*);
bool aiRound53XufaQuestion(const AIRequest&,ServerPlayer*);
std::unique_ptr<Card> constructAIRound53XufaFirst(const AIRequest&,ServerPlayer*,const QList<int>&,QString*);
QJsonObject issueAIRound53XufaFirst(const AIRequest&,ServerPlayer*,AIReadonlyCardLease,
 const std::function<bool(CardUseStruct&)>&);
QJsonObject issueAIRound53XufaSaved(const AIRequest&,ServerPlayer*,AIReadonlyCardLease,const QStringList&,
 const std::function<bool(CardUseStruct&)>&);
// Pure remembered values are revalidated and receive NEW current-request authority.
QJsonObject issueAIRound53XufaValue(const AIRequest&,ServerPlayer*,const QJsonObject&,const QStringList&,
 const std::function<bool(CardUseStruct&)>&);
// Used only by the existing declared PreCardUsed viewer projection. Returns
// the fixed constructor list only while this exact live object's list matches.
QJsonObject readAIRound53XufaConstructedSkills(Room&,ServerPlayer*,const Card*);
bool hasAIRound53XufaReceipt(const AIRequest&,int);
bool applyAIRound53XufaReceipt(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,
 const std::function<bool(CardUseStruct&)>&,CardUseStruct&);
#endif
