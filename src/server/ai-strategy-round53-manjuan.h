#pragma once
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
QJsonObject queryAIRound53ManjuanPrinted(Room&,const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&);

#include <QStringList>
#include <functional>
class Card;struct AICardSpec;struct CardUseStruct;
const Card*constructAIRound53ManjuanRaw(const AIRequest&,ServerPlayer*,const QString&,int*selected);
bool hasAIRound53ManjuanRaw(const AIRequest&,int conversionId);
bool applyAIRound53ManjuanRaw(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,
 const std::function<bool(CardUseStruct&)>&,CardUseStruct&);
