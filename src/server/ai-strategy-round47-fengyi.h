#ifndef AI_STRATEGY_ROUND47_FENGYI_H
#define AI_STRATEGY_ROUND47_FENGYI_H
#include <memory>
#include <QString>
class Room; class ServerPlayer; class Card; class QVariant; class QJsonObject; struct AIRequest;
// Only a real askForCard(isRetrial=true) may issue this finite physical answer.
// Every nested askForCard pushes a mask; other decision kinds fail the request gate.
class AIRound47FengyiQuestion final {
public:
 AIRound47FengyiQuestion(Room &,ServerPlayer *,const QString &,const QString &,const QVariant &,int,bool);
 ~AIRound47FengyiQuestion();
 AIRound47FengyiQuestion(const AIRound47FengyiQuestion&)=delete;
 AIRound47FengyiQuestion&operator=(const AIRound47FengyiQuestion&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
 friend struct AIRound47FengyiAccess;
};
QJsonObject selectAIRound47FengyiPhysical(Room &,const AIRequest &,ServerPlayer *,int);
const Card *consumeAIRound47FengyiPhysical(Room &,const AIRequest &,ServerPlayer *,int);
#endif
