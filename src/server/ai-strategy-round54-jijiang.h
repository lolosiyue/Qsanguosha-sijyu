#pragma once
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;class Card;class QVariant;struct AIRequest;struct SkillContext;struct lua_State;
// Native OL constructors only; the reader is used by declared PreCardUsed
// projection, with no Lua lookup or permission to disclose other skill names.
void recordAIRound54FreshOLJijiangSkills(Room&,const Card*);
QJsonObject readAIRound54OLJijiangConstructedSkills(Room&,ServerPlayer*,const Card*);
// Operation identity is issued only by the compiled OL pay/effect call sites.
// It conveys the actual response source, never a plan or payment authority.
class AIRound54JijiangScope {
public:
 AIRound54JijiangScope(Room&,const SkillContext&); // OLJijiangVS::pay
 AIRound54JijiangScope(Room&,const SkillContext&,int event,bool effect);
 ~AIRound54JijiangScope();
 AIRound54JijiangScope(const AIRound54JijiangScope&)=delete;
 AIRound54JijiangScope&operator=(const AIRound54JijiangScope&)=delete;
 struct Data;Data*d;
};
class AIRound54JijiangLiege {
public:AIRound54JijiangLiege(Room&,ServerPlayer*);~AIRound54JijiangLiege();
 AIRound54JijiangLiege(const AIRound54JijiangLiege&)=delete;
 AIRound54JijiangLiege&operator=(const AIRound54JijiangLiege&)=delete;
 struct Data;Data*d;
};
class AIRound54JijiangEvent {
public:AIRound54JijiangEvent(Room&,int,ServerPlayer*,const QVariant&);~AIRound54JijiangEvent();
 AIRound54JijiangEvent(const AIRound54JijiangEvent&)=delete;
 AIRound54JijiangEvent&operator=(const AIRound54JijiangEvent&)=delete;
 struct Data;Data*d;
};
void enterAIRound54JijiangEvent(Room&,int,ServerPlayer*);
void enterAIRound54JijiangFilter(Room&,int,ServerPlayer*,const QVariant&);
void clearAIRound54Jijiang(Room&);
bool initializeAIRound54Jijiang(Room&,lua_State*);
QJsonObject queryAIRound54Jijiang(Room&,const AIRequest&,ServerPlayer*,const QString&,const QString&);

// Compatibility with existing GameVM observers. OL no longer executes a
// GameVM thunk or exports shared Lua tables, so these issue no exemptions.
void observeAIRound54JijiangEntry(Room&,lua_State*,int) noexcept;
struct lua_TableObservation;
bool acceptAIRound54JijiangEntry(Room&,lua_State*,int) noexcept;
bool allowAIRound54JijiangWrite(Room&,lua_State*,const lua_TableObservation&) noexcept;
