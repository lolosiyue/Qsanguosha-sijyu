#ifndef AI_STRATEGY_ROUND53_COLLATERAL_H
#define AI_STRATEGY_ROUND53_COLLATERAL_H
#include <QJsonObject>
#include <QString>
#include <memory>
class Room;class ServerPlayer;class Card;struct AIRequest;
QJsonObject queryAIRound53Collateral(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
class AIRound53CollateralEffect final {
public:
 AIRound53CollateralEffect(Room&,ServerPlayer*killer,ServerPlayer*victim,ServerPlayer*user,const Card*,const QString&prompt);
 ~AIRound53CollateralEffect();
 AIRound53CollateralEffect(const AIRound53CollateralEffect&)=delete;
 AIRound53CollateralEffect&operator=(const AIRound53CollateralEffect&)=delete;
private:struct Data;std::unique_ptr<Data>d;friend struct AIRound53CollateralAccess;friend class AIRound53CollateralQuestion;
};
class AIRound53CollateralQuestion final {
public:
 AIRound53CollateralQuestion(Room&,ServerPlayer*,const QString&,const QString&,int);
 ~AIRound53CollateralQuestion();
 AIRound53CollateralQuestion(const AIRound53CollateralQuestion&)=delete;
 AIRound53CollateralQuestion&operator=(const AIRound53CollateralQuestion&)=delete;
private:struct Data;std::unique_ptr<Data>d;friend struct AIRound53CollateralAccess;
};
void projectAIRound53Collateral(AIRequest&,Room&,ServerPlayer*);
#endif
