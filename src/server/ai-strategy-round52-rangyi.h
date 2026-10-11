#ifndef AI_STRATEGY_ROUND52_RANGYI_H
#define AI_STRATEGY_ROUND52_RANGYI_H
#include <QString>
#include <QList>
#include <memory>
class Room;class ServerPlayer;class Card;struct CardUseStruct;struct AIRequest;struct AICardConversionView;struct lua_State;
void registerAIRound52RangyiSource(Room&,lua_State*);
// Native accepted Room use only, immediately around the existing onUse call.
class AIRound52RangyiUse final {
public:AIRound52RangyiUse(Room&,const CardUseStruct&);~AIRound52RangyiUse();
private:struct Data;std::unique_ptr<Data>d;friend struct AIRound52RangyiAccess;friend class AIRound52RangyiCallback;
};
// Inside the real LuaSkillCard::use bridge, after on_use.push and before pcall.
class AIRound52RangyiCallback final {
public:AIRound52RangyiCallback(Room&,lua_State*,int,const Card*,ServerPlayer*,const QList<ServerPlayer*>&);~AIRound52RangyiCallback();
private:struct Data;std::unique_ptr<Data>d;friend struct AIRound52RangyiAccess;friend class AIRound52RangyiQuestion;
};
// Every UseCard question masks its parent. A nested question permanently
// revokes an already-bound Rangyi question; no restoration of old authority.
class AIRound52RangyiQuestion final {
public:AIRound52RangyiQuestion(Room&,ServerPlayer*,const QString&,const QString&,int);~AIRound52RangyiQuestion();
private:struct Data;std::unique_ptr<Data>d;friend struct AIRound52RangyiAccess;
};
void projectAIRound52Rangyi(AIRequest&,Room&,ServerPlayer*);
bool aiRound52RangyiCurrent(const AIRequest&,Room&,ServerPlayer*,int);
// Exact raw LuaSkillCard product, never a forged V2 activation instance.
// Caller owns virtual return. selected adds native physical availability gate.
Card*createAIRound52RangyiProduct(const AIRequest&,Room&,ServerPlayer*,int,bool selected,QString* =nullptr);
Card*rebuildAIRound52RangyiProduct(const AIRequest&,Room&,ServerPlayer*,const AICardConversionView&);
#endif
