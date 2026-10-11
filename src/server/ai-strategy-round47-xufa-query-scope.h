#ifndef AI_STRATEGY_ROUND47_XUFA_QUERY_SCOPE_H
#define AI_STRATEGY_ROUND47_XUFA_QUERY_SCOPE_H
#include <memory>
#include <QString>
class Room;class Card;class Player;class ServerPlayer;struct lua_State;struct AIRequest;
// Native call-tree custody, never a Lua capability or a result oracle.
// The scope must begin BEFORE ownership/invalidity/holder filtering.
class AIRound47XufaQueryScope {
public:
 explicit AIRound47XufaQueryScope(Room*,const Card*card=nullptr,bool cardRequired=false,bool preserveAiNoTo=false,bool preservePriority=false,bool scalarCardOnly=false);
 ~AIRound47XufaQueryScope();
 AIRound47XufaQueryScope(const AIRound47XufaQueryScope&)=delete;
 AIRound47XufaQueryScope&operator=(const AIRound47XufaQueryScope&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;friend struct AIRound47XufaQueryAccess;
};
// Only the fresh native Slash allocated by Player::canSlash(nullptr) is
// admitted here. Not arbitrary caller-provided zero-cost/virtual cards.
class AIRound47XufaCanSlashCard {
public:
 AIRound47XufaCanSlashCard(Room*,const Card*);
 ~AIRound47XufaCanSlashCard();
private:Room*room;const Card*card;quint64 stamp=0;bool entered=false;
};
// Three exact coordinator sites: validated Xufa seed/product/rebuild only.
// Observation custody, no constructor/payment/target permission.
class AIRound47XufaSourceCard {
public:
 AIRound47XufaSourceCard(Room&,const AIRequest&,ServerPlayer*,const Card*,const QString&skill);
 ~AIRound47XufaSourceCard();
 AIRound47XufaSourceCard(const AIRound47XufaSourceCard&)=delete;
 AIRound47XufaSourceCard&operator=(const AIRound47XufaSourceCard&)=delete;
private:struct Data;std::unique_ptr<Data>d;friend struct AIRound47XufaSourceAccess;
};
// Exact five native snapshot constructors only; no Lua capability or receipt.
class AIRound47XufaProjectionCard {
public:
 AIRound47XufaProjectionCard(Room&,ServerPlayer*,const Card*,const QString&);
 ~AIRound47XufaProjectionCard();
 AIRound47XufaProjectionCard(const AIRound47XufaProjectionCard&)=delete;
 AIRound47XufaProjectionCard&operator=(const AIRound47XufaProjectionCard&)=delete;
private:Room*room;const Card*card;quint64 stamp=0;bool entered=false;
};
bool certifyAIRound47XufaQueryEntry(Room&,lua_State*,int nargs,QString*reason=nullptr);
#endif
