#pragma once
#include <QJsonObject>
#include <QVariant>
#include <QList>
#include <functional>
class Room;class ServerPlayer;class Card;class LuaRuntime;struct lua_State;struct AIRequest;struct CardUseStruct;
void projectAIRound53Lianzhan(AIRequest &,Room &,ServerPlayer *,const QVariant &);
QJsonObject queryAIRound53LianzhanSlot(const AIRequest &,ServerPlayer *,const QString &,const QJsonObject &,const std::function<QJsonObject(const Card *)>&);
bool registerAIRound53LianzhanSources(Room &,lua_State *,QString *reason=nullptr);
void clearAIRound53LianzhanSlots(Room &);
// Native-only exact source QVariant::toCardUse value bridge; no SWIG export.
bool pushAIRound53LianzhanUse(lua_State *,const CardUseStruct &);
bool readAIRound53LianzhanUse(lua_State *,int,const Card *&,ServerPlayer *&,QList<ServerPlayer *> &);
void clearAIRound53LianzhanBindings(LuaRuntime &);
bool installAIRound53LianzhanBinding(lua_State *);

// Metadata-only exact pending installer entry. Caller must independently prove
// the complete original bootstrap source/allocation union and lexical phase.
// This cannot authorize a standalone/later protected call to the same pointer.
bool matchAIRound56LianzhanBootstrapEntry(Room&,lua_State*,int arguments) noexcept;
// Exact accepted installer while actually executing at protected depth + 1.
bool inAIRound56LianzhanBootstrapInstaller(Room&,lua_State*) noexcept;
