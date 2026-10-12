#ifndef AI_STRATEGY_ROUND56_PROJECTION_H
#define AI_STRATEGY_ROUND56_PROJECTION_H
#include <memory>
#include <QString>
class Room;class Player;class ServerPlayer;class Card;struct lua_State;
// Ordinary PlayerChosen/CardChosen full world projection only. Activate's
// buildSkillActionContext and arbitrary view-as callbacks require another proof.
class AIRound56ProjectionScope final {
public:
 AIRound56ProjectionScope(Room&,ServerPlayer*,lua_State*,QString*why=nullptr);
 ~AIRound56ProjectionScope();bool accepted()const noexcept;
private:
 struct Data;std::unique_ptr<Data>d;
 friend bool inAIRound56ProjectionScope(const Room&,lua_State*)noexcept;
 friend bool acceptAIRound56ProjectionGetterEntry(Room&,lua_State*,int)noexcept;
 friend bool preserveAIRound56ProjectionNativeQuery(Room&,const Card*,bool,bool)noexcept;
 friend void retireAIRound56Projection(Room&,lua_State*)noexcept;
 friend void recordAIRound56ProjectionSourceCard(Room&,const Card*,const QString&)noexcept;
 friend void recordAIRound56ProjectionOwnerSourceCard(Room&,const Player*,const Card*,const QString&)noexcept;
};
bool inAIRound56ProjectionScope(const Room&,lua_State*) noexcept;
// Shared dispatcher tries every complete exact source certifier before default
// retirement; a nonmatching sibling thunk is not immediately an unknown entry.
bool acceptAIRound56ProjectionGetterEntry(Room&,lua_State*,int) noexcept;
void retireAIRound56Projection(Room&,lua_State*) noexcept;
bool preserveAIRound56ProjectionNativeQuery(Room&,const Card*,bool hasCard,bool scalarCardOnly=false) noexcept;
// Source-owned callsites only: fixed named projection/record constructor or the
// intrinsic Player::canSlash new Slash source. Never called from a query result.
void recordAIRound56ProjectionSourceCard(Room&,const Card*,const QString&literalName) noexcept;
// Immediate native constructor receipts with the active projection viewer as
// source owner. Only fixed producer callsites may use this; never parsed tags
// or a later selected card/unchanged pointer observation.
void recordAIRound56ProjectionOwnerSourceCard(Room&,const Player*,const Card*,const QString&literalName) noexcept;
// Source type membership only: not custody or an execution grant.
bool aiRound56OrdinaryCardPolicyType(const Card *) noexcept;
#endif
