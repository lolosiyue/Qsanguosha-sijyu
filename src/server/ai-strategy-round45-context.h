#ifndef AI_STRATEGY_ROUND45_CONTEXT_H
#define AI_STRATEGY_ROUND45_CONTEXT_H
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>
class Room; class ServerPlayer; class Card; class QVariant;
struct SkillInstanceRef; struct AIRequest; struct AIWorldView; struct AIEventView;
class AIRound45NewgeneralsFrame {
public:
 AIRound45NewgeneralsFrame(Room &,int,ServerPlayer *,const QVariant &);
 ~AIRound45NewgeneralsFrame();
 AIRound45NewgeneralsFrame(const AIRound45NewgeneralsFrame &)=delete;
 AIRound45NewgeneralsFrame &operator=(const AIRound45NewgeneralsFrame &)=delete;
private: Room *room_=nullptr; quint64 serial_=0;
};
void clearAIRound45NewgeneralsFrames(Room &);
void projectAIRound45Newgenerals(AIRequest &,Room &,ServerPlayer *,const std::function<QJsonObject(const Card *)> &);
void projectAIRound45NewgeneralsEvent(AIWorldView &,Room &,ServerPlayer *,const AIEventView &);
QString aiRound45NotifiedSkill(const AIRequest &);
bool authorizeAIRound45NotifiedEvaluation(const AIRequest &,Room &,ServerPlayer *,int,QJsonObject *);
bool authorizeAIRound45NotifiedPayment(const AIRequest &,Room &,ServerPlayer *,const SkillInstanceRef &,const SkillInstanceRef &,const QList<int> &,QList<int> *foreign=nullptr);
int aiRound45NewgeneralsPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool authorizeAIRound45NotifiedPhysical(const AIRequest &,Room &,ServerPlayer *,const SkillInstanceRef &,const SkillInstanceRef &,int);
const Card *aiRound45MoujiangEvaluation(const AIRequest &,Room &,ServerPlayer *);
QJsonObject aiRound45MoujiangTargetFilter(Room &,ServerPlayer *,const AIRequest &,const Card *,const QJsonObject &);
bool aiRound45KeolcihuangConstruction(const AIRequest &,Room &,ServerPlayer *,const QJsonObject &);
#endif
