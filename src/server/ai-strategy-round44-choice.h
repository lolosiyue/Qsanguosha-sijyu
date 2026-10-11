#ifndef AI_STRATEGY_ROUND44_CHOICE_H
#define AI_STRATEGY_ROUND44_CHOICE_H
#include <QString>
struct AIRequest;
struct SkillInstanceRef;
class ServerPlayer;
class Card;
int aiRound44SkinPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiRound44SkinProduct(const AIRequest &,const ServerPlayer *,const SkillInstanceRef &,
                         const SkillInstanceRef &,const Card *,const Card *);
#endif
