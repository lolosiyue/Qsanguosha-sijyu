#ifndef AI_STRATEGY_ROUND45_XUFA_FILTER_H
#define AI_STRATEGY_ROUND45_XUFA_FILTER_H
#include <memory>
#include <QList>
#include <QString>
class Room;class ServerPlayer;class Card;struct lua_State;struct CardsMoveStruct;
struct AIRound45NaturalFilterProof;
// Install only around the two compiled XufaCard::use move statements. Native
// custody of owner-selected IDs, not a serialized reason or a Lua capability.
class AIRound45XufaMoveBodyScope {
public:
 AIRound45XufaMoveBodyScope(Room&,ServerPlayer*,const Card*,bool discard);
 ~AIRound45XufaMoveBodyScope();
 AIRound45XufaMoveBodyScope(const AIRound45XufaMoveBodyScope&)=delete;
 AIRound45XufaMoveBodyScope&operator=(const AIRound45XufaMoveBodyScope&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
 friend struct AIRound45XufaBodyAccess;
};
// Installed at EVERY moveCardsSub entry, before normalization or callbacks.
// One pending compiled Xufa call is consumed once; nested moves push a denied
// frame and cannot inherit the parent's custody.
class AIRound45XufaMoveEntry {
public:
 AIRound45XufaMoveEntry(Room&,const QList<CardsMoveStruct>&,bool visible,bool guanxing);
 ~AIRound45XufaMoveEntry();
 AIRound45XufaMoveEntry(const AIRound45XufaMoveEntry&)=delete;
 AIRound45XufaMoveEntry&operator=(const AIRound45XufaMoveEntry&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
 friend struct AIRound45XufaBodyAccess;
};
// Trusted bootstrap only; hashes are compiled constants, never file-derived.
bool registerAIRound45XufaBodySources(Room&,lua_State*,QString*reason=nullptr);
// This certifies ONLY this filterEvent body/callback union. The caller must
// separately certify the common prefix, actual self, outer native graph,
// asynchronous hooks/finalizers, and saved-plan lifetime. No continuity lease.
bool certifyAIRound45XufaFilterBody(Room&,lua_State*,int selfIndex,
 const AIRound45NaturalFilterProof&,QString*reason);
class QVariant;
bool matchesAIRound47XufaMoveEvent(Room&,int,ServerPlayer*,const QVariant&);
#endif
