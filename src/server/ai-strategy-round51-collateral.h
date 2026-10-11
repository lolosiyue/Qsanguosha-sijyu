#ifndef AI_STRATEGY_ROUND51_COLLATERAL_H
#define AI_STRATEGY_ROUND51_COLLATERAL_H
#include <QString>
#include <memory>
class Room; class ServerPlayer; class Card; struct AIRequest; struct CardUseStruct;
// Native observation only. This constructor is inserted at the one compiled
// TenyearBenxi producer; it does not set a property, choose, or pay anything.
class AIRound51CollateralSource final {
public:
 AIRound51CollateralSource(Room&,ServerPlayer*,const CardUseStruct&);
 ~AIRound51CollateralSource();
 AIRound51CollateralSource(const AIRound51CollateralSource&)=delete;
 AIRound51CollateralSource&operator=(const AIRound51CollateralSource&)=delete;
private: struct Data;std::unique_ptr<Data>d;friend struct AIRound51CollateralAccess;friend class AIRound51CollateralQuestion;
};
// Every actual use-card inquiry masks an outer inquiry, including same-pattern
// nested asks. A source scope can bind only its first exact inquiry.
class AIRound51CollateralQuestion final {
public:
 AIRound51CollateralQuestion(Room&,ServerPlayer*,const QString&,const QString&,int);
 ~AIRound51CollateralQuestion();
 AIRound51CollateralQuestion(const AIRound51CollateralQuestion&)=delete;
 AIRound51CollateralQuestion&operator=(const AIRound51CollateralQuestion&)=delete;
private:struct Data;std::unique_ptr<Data>d;friend struct AIRound51CollateralAccess;
};
void projectAIRound51Collateral(AIRequest&,Room&,ServerPlayer*);
bool aiRound51CollateralCurrent(const AIRequest&,Room&,ServerPlayer*,const QString&wire);
// Parse at the reached source call, never during projection. A non-null virtual
// product transfers to the existing readonly store; a physical product is borrowed.
const Card *parseAIRound51Collateral(const AIRequest&,Room&,ServerPlayer*,const QString&wire,bool *authorized);
#endif
