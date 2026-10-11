#ifndef AI_STRATEGY_ROUND53_XUANZHU_RESPONSE_H
#define AI_STRATEGY_ROUND53_XUANZHU_RESPONSE_H
#include "ai-strategy-readonly-card-store.h"
#include <QJsonObject>
#include <QStringList>
#include <QList>
#include <functional>
class Room;class ServerPlayer;class Card;struct AIRequest;struct AICardSpec;struct CardUseStruct;
// Constructed ONLY at the exact compiled Xuanzhu2Card::onUse askForUseCard callsite.
class AIRound53XuanzhuQuestionScope final {
public:
 AIRound53XuanzhuQuestionScope(Room&,ServerPlayer*,const Card*,const QString&);
 ~AIRound53XuanzhuQuestionScope();
 AIRound53XuanzhuQuestionScope(const AIRound53XuanzhuQuestionScope&)=delete;
 AIRound53XuanzhuQuestionScope& operator=(const AIRound53XuanzhuQuestionScope&)=delete;
private:struct State;std::unique_ptr<State>d;
};
// Installed at EVERY native askForUseCardStruct entry, including unrelated/nested calls.
class AIRound53XuanzhuInquiryScope final {
public:
 AIRound53XuanzhuInquiryScope(Room&,ServerPlayer*,const QString&,const QString&,int);
 ~AIRound53XuanzhuInquiryScope();
 AIRound53XuanzhuInquiryScope(const AIRound53XuanzhuInquiryScope&)=delete;
 AIRound53XuanzhuInquiryScope& operator=(const AIRound53XuanzhuInquiryScope&)=delete;
private:struct State;std::unique_ptr<State>d;
};
// Called only after native makeRequest has completed its actual world projection.
void bindAIRound53XuanzhuQuestion(const AIRequest&,ServerPlayer*);
bool aiRound53XuanzhuQuestion(const AIRequest&,ServerPlayer*);
std::unique_ptr<Card> constructAIRound53XuanzhuFirst(const AIRequest&,ServerPlayer*,QString*);
QJsonObject issueAIRound53XuanzhuFirst(const AIRequest&,ServerPlayer*,AIReadonlyCardLease,
 const std::function<bool(CardUseStruct&)>&);
QJsonObject issueAIRound53XuanzhuSaved(const AIRequest&,ServerPlayer*,int,const QString&,const QStringList&,
 const std::function<bool(CardUseStruct&)>&);
bool hasAIRound53XuanzhuReceipt(const AIRequest&,int);
bool applyAIRound53XuanzhuReceipt(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,
 const std::function<bool(CardUseStruct&)>&,CardUseStruct&);
#endif
