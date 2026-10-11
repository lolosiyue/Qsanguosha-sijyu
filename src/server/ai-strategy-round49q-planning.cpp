#include "ai-strategy-round49q-planning.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
namespace {
bool ownerPlay(const AIRequest&r,const ServerPlayer*p){
 if(!p||!p->getRoom()||!p->getRoom()->roomRuntime()||p->getRoom()->roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRound44RangeRequestCurrent(*p->getRoom(),r,p))return false;
 // Source presence only. Native activation/invalidity/selected targets remain
 // the existing skill-scope/factory/final checks, not this readonly predicate.
 for(const auto&i:p->getSkillInstances())if(i.skillName=="renchou")return true;
 return false;
}
}
bool aiRound49qRenchouEvaluation(const AIRequest&r,const ServerPlayer*p,const QJsonObject&s){
 return ownerPlay(r,p)&&s.size()==4&&s.value("kind")=="evaluate_skill_card"
  &&(s.value("skill")=="renchou"||s.value("skill")=="renchou_limit")
  &&s.value("name")=="slash"&&s.value("subcards").isArray()&&s.value("subcards").toArray().isEmpty();
}
int aiRound49qRenchouPrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&name){
 // Source #renchou:.:: actual LuaSkillCard, empty cost/declaration. This admits
 // only its finite prototype. Existing native receipt construction still owns
 // source/instance refs and independently checks zero cost and target legality.
 return skill=="renchou"&&name=="#renchou"&&ownerPlay(r,p)?0:-1;
}
