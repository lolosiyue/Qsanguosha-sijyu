#ifndef AI_STRATEGY_NEWGENERALS_H
#define AI_STRATEGY_NEWGENERALS_H

#include "ai-strategy/owner.h"
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>

struct AIEventView;
struct AIRequest;
struct AIWorldView;
class Card;
struct lua_State;
class QVariant;
class Room;
class ServerPlayer;
struct SkillInstanceRef;

class AINewgeneralsFrame {
public:
 AINewgeneralsFrame(Room &,int,ServerPlayer *,const QVariant &);
 ~AINewgeneralsFrame();
 AINewgeneralsFrame(const AINewgeneralsFrame &)=delete;
 AINewgeneralsFrame &operator=(const AINewgeneralsFrame &)=delete;
private: Room *room_=nullptr; quint64 serial_=0;
};
void clearAINewgeneralsFrames(Room &);
void projectAINewgenerals(AIRequest &,Room &,ServerPlayer *,const std::function<QJsonObject(const Card *)> &);
void projectAINewgeneralsEvent(AIWorldView &,Room &,ServerPlayer *,const AIEventView &);
QString aiNewgeneralsNotifiedSkill(const AIRequest &);
bool authorizeAINewgeneralsNotifiedEvaluation(const AIRequest &,Room &,ServerPlayer *,int,QJsonObject *);
bool authorizeAINewgeneralsNotifiedPayment(const AIRequest &,Room &,ServerPlayer *,const SkillInstanceRef &,const SkillInstanceRef &,const QList<int> &,QList<int> *foreign=nullptr);
int aiNewgeneralsPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool authorizeAINewgeneralsNotifiedPhysical(const AIRequest &,Room &,ServerPlayer *,const SkillInstanceRef &,const SkillInstanceRef &,int);
const Card *aiMoujiangEvaluation(const AIRequest &,Room &,ServerPlayer *);
QJsonObject aiMoujiangTargetFilter(Room &,ServerPlayer *,const AIRequest &,const Card *,const QJsonObject &);
bool aiKeolcihuangConstruction(const AIRequest &,Room &,ServerPlayer *,const QJsonObject &);

bool aiMouluanjiConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

// Readonly source recipes only. Neither function authorizes selection/payment.
bool aiNewgeneralsPlanningConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
bool aiNewgeneralsPlanningProduct(const QJsonObject&,const Card&);
QJsonValue aiNewgeneralsPlanningHandPile(const AIWorldView&,Room&,const ServerPlayer*);
bool aiNewgeneralsPlanningRepeatedMaterials(const QJsonObject&);
void projectAINewgeneralsPlanningFacts(AIWorldView&,Room&,const ServerPlayer*);

bool aiFangchanYantaoluanConstruction(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &);
QJsonObject aiYantaoluanSlash(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &, const Card *);

bool aiKeolshuangxiongConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

bool aiKeolsujiConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

bool aiKeolxiejuConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);

// Three fixed own game-VM scratch slots. The retained Card is observation only.
// Result contains current native serialization/ordered public target names;
// it never contains an action receipt or a replacement Card allocation.
QJsonObject queryAINewgeneralsPlanningSlot(const AIRequest&,ServerPlayer*,const QString&slot,
 const QString&operation,const QJsonObject&payload,AIReadonlyCardLease lease={});
void clearAINewgeneralsPlanningSlots(Room&);
// Exact attached view-as provider; paid/declaration bits do not grant payment.
int aiNewgeneralsXdyifuPrototype(const AIRequest&,ServerPlayer*,const QString&,const QString&);

const Card*aiNewgeneralsGodshenjiMaterial(const AIRequest&,ServerPlayer*,int id);

bool registerAINewgeneralsPlanningSlotSources(Room&,lua_State*,QString*error=nullptr);

bool aiNewgeneralsMouConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
int aiMouxiejiExtraTarget(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
bool aiMouxiejiSource(const AIRequest&,Room&,const ServerPlayer*,const Card*,const QString&);
QJsonObject aiMouxiejiTargetFilter(Room&,ServerPlayer*,const AIRequest&,const Card*,const QString&,const QJsonObject&);
int aiMouxiejiPrototype(const AIRequest&,const ServerPlayer*,const QString&,const QString&);

#endif
