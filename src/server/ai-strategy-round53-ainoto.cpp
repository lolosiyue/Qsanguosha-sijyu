#include "ai-strategy-round53-ainoto.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QDynamicPropertyChangeEvent>
#include <QHash>
#include <QPointer>
#include <QSet>
namespace {
// A witness is provenance only. No bool value is cached or substituted.
struct Witness {bool initialAbsent=false;QString viewer;quint64 transport=0;};
class State final:public QObject {
public:
 explicit State(Room&r):QObject(&r){}
 bool initialized=false;QString viewer;quint64 generation=0,initialReads=0,choulveWrites=0,choulveAccepted=0;
 QHash<const ServerPlayer*,Witness>witnesses;QSet<const ServerPlayer*>watched;
 QPointer<ServerPlayer>writing;
 void retire(){++generation;witnesses.clear();}
 bool eventFilter(QObject*object,QEvent*e)override{
  if(e->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent*>(e)->propertyName()=="aiNoTo"){
   // The same exact certified setter is checked again after all synchronous
   // observers. Every other setter loses permission; never imports its value.
   if(object!=writing.data())retire();
  }
  return false;
 }
};
State*state(Room&r){for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s;return new State(r);}
QJsonObject error(const char*s){return{{"error",QString::fromLatin1(s)},{"key","targetRevises.aiNoTo"}};}
}
void createdAIRound53AiNoTo(Room&r,ServerPlayer*p){
 if(!p||p->getRoom()!=&r)return;auto*s=state(r);if(s->watched.contains(p))return;
 s->watched.insert(p);p->installEventFilter(s);
 QObject::connect(p,&QObject::destroyed,s,[s,p](){s->witnesses.remove(p);s->watched.remove(p);});
 // Called only from ServerPlayer construction. A later runtime reset cannot
 // sample an already-running native property and turn it into a baseline.
 const auto transport=r.roomRuntime()?r.roomRuntime()->xufaTransportEpoch():0;
 if(transport&&!p->property("aiNoTo").isValid())s->witnesses.insert(p,{true,{},transport});
}
bool hasAIRound56InitialAiNoToWitness(const Room&r,const ServerPlayer*p) noexcept {
 auto*rt=const_cast<Room&>(r).roomRuntime();
 if(!p||p->getRoom()!=&r||!rt)return false;
 for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c)){
  const auto i=s->witnesses.constFind(p);
  return i!=s->witnesses.cend()&&i->initialAbsent&&i->transport
   &&i->transport==rt->xufaTransportEpoch();
 }
 return false;
}
quint64 observedAIRound56InitialAiNoToReads(const Room&r) noexcept {
 for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s->initialReads;
 return 0;
}
void recordAIRound56ChoulveTargetWrite(Room&r) noexcept {for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c)){++s->choulveWrites;return;}}
void recordAIRound56ChoulveNativeAcceptance(Room&r) noexcept {for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c)){++s->choulveAccepted;return;}}
quint64 observedAIRound56ChoulveTargetWrites(const Room&r) noexcept {for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s->choulveWrites;return 0;}
quint64 observedAIRound56ChoulveNativeAcceptances(const Room&r) noexcept {for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c))return s->choulveAccepted;return 0;}
void initializeAIRound53AiNoTo(Room&r){state(r)->initialized=true;}
void beginAIRound53AiNoTo(Room&r,const QString&viewer){
 auto*s=state(r);
 // This happens before the next viewer's policy private gates, irrespective of
 // whether that policy actually reaches a write. It exports no write occurrence.
 if(viewer.isEmpty()||(!s->viewer.isEmpty()&&s->viewer!=viewer))s->retire();
 s->viewer=viewer;
}
void retireAIRound53AiNoTo(Room&r) noexcept {
 // A preselection/async barrier only revokes existing provenance. It must not
 // allocate a Room child or establish a new baseline during observation.
 for(auto*c:r.children())if(auto*s=dynamic_cast<State*>(c)){s->retire();return;}
}
QJsonObject queryAIRound53AiNoTo(Room&r,const AIRequest&q,const ServerPlayer*viewer,const QJsonObject&spec){
 if(!viewer||viewer->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()
  ||q.viewerObjectName!=viewer->objectName()||q.worldView.self.objectName!=viewer->objectName()
  ||!aiRound44RangeRequestCurrent(r,q,viewer))return error("target-revise property request/viewer is stale");
 auto*s=state(r);if(!s->initialized||s->viewer!=viewer->objectName())return error("target-revise source entry unavailable");
 if(spec.value("kind")!="target_revise_property"||!spec.value("target").isString()||!spec.value("op").isString())return error("target-revise operation malformed");
 auto*target=r.findPlayerByObjectName(spec.value("target").toString(),true);bool projected=q.worldView.self.objectName==spec.value("target").toString();
 for(const auto&p:q.worldView.players)projected=projected||p.objectName==spec.value("target").toString();
 if(!target||!projected||!s->watched.contains(target))return error("target-revise target lacks constructor/current roster identity");
 const auto op=spec.value("op").toString();
 if(op=="write"){
  if(spec.size()!=4||!spec.value("value").isBool()||s->writing)return error("target-revise writer shape/reentry invalid");
  const bool value=spec.value("value").toBool();const auto generation=s->generation;
  const auto transport=r.roomRuntime()->xufaTransportEpoch();QString why;
  AIRound53AiNoToPermit permit(&r,&q,viewer,target,value);s->writing=target;
  bool ok=false;try{ok=writeAIRound53AiNoToProperty(r,q,const_cast<ServerPlayer*>(viewer),target,value,permit,&why);}catch(...){s->writing.clear();s->retire();throw;}
  s->writing.clear();
  if(!ok||s->generation!=generation||!transport||transport!=r.roomRuntime()->xufaTransportEpoch()){s->retire();return error("target-revise prefix committed but native continuation is uncertified");}
  s->witnesses.insert(target,{false,viewer->objectName(),transport});
 }else if(op!="read"||spec.size()!=3)return error("target-revise reader shape invalid");
 const auto it=s->witnesses.constFind(target);const QVariant actual=target->property("aiNoTo");
 const bool known=it!=s->witnesses.cend()&&it->transport&&it->transport==r.roomRuntime()->xufaTransportEpoch()&&(it->initialAbsent?!actual.isValid():(it->viewer==viewer->objectName()&&actual.userType()==QMetaType::Bool));
 if(op=="read"&&known&&it->initialAbsent)++s->initialReads;
 QJsonObject row{{"kind","target_revise_property"},{"target",target->objectName()},{"known",known},{"viewer",viewer->objectName()},{"decision_id",q.getDecisionId()},{"state_revision",q.getStateRevision()}};
 if(known)row.insert("value",actual.toBool());else row.insert("reason","target-revise actual property has no lawful source witness for this viewer");
 return row;
}
