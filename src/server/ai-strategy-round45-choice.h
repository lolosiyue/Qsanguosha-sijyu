#ifndef AI_STRATEGY_ROUND45_CHOICE_H
#define AI_STRATEGY_ROUND45_CHOICE_H
#include <QJsonObject>
class Room;class ServerPlayer;class Card;struct AIRequest;
// Fixed RushB's eight reached Card::setSkillName statements. Returns the same
// native physical object after the write, never a clone or payment capability.
const Card *writeAIRound45PhysicalSkill(Room &,const AIRequest &,ServerPlayer *,const QJsonObject &,QString *);
bool authorizeAIRound45DanlvePhysical(Room&,const AIRequest&,ServerPlayer*,int);
const Card* attachAIRound45DanlvePhysical(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&,const QString&,const QString&,int,QString*);
#endif
