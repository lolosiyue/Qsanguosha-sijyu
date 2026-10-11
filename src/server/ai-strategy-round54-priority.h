#pragma once
#include <QJsonObject>
#include <QString>
#include <memory>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
typedef int (*AIR54PriorityCFunction)(lua_State*);
// Native-only lexical seal. The fixed callers validate operation/card/key;
// nothing here executes a setter or accepts a Lua-provided function/token.
class AIRound54PriorityOperation {
public:
 enum Mode { Preserve, LiteralTen, CopyCell, Arithmetic };
 AIRound54PriorityOperation(Room&,ServerPlayer*,const AIRequest&,lua_State*,AIR54PriorityCFunction,void*,Mode,const QString&target={},const QString&source={});
 ~AIRound54PriorityOperation();
 AIRound54PriorityOperation(const AIRound54PriorityOperation&)=delete;
 AIRound54PriorityOperation&operator=(const AIRound54PriorityOperation&)=delete;
 // Called after allocations, immediately before the actual RHS/read or write.
 void beforeWrite();
 // Called immediately after the actual plain-table write; never on a skip.
 void afterWrite();
 struct Data;
private:
 std::unique_ptr<Data>d;
};
void observeAIRound54PriorityEntry(Room&,lua_State*,int argumentCount)noexcept;
void clearAIRound54Priority(Room&);
QJsonObject readAIRound54Priority(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);
